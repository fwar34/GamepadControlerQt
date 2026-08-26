// =====================================================================
// main_window.rs —— 主窗口逻辑（Slint）
//
// 连接 .slint 组件回调、维护列表模型（VecModel）、每 50ms 轮询共享
// core 刷新界面。数据无变化时跳过重建避免闪烁。
// =====================================================================

use crate::core::config_manager;
use crate::core::input_types::*;
use crate::core::mapping_types::{ActionType, ControllerProfile, KeyMapping, MappedAction, OperationLayer};
use crate::ui::shared::AppShared;
use crate::{ButtonItem, KeyItem, LayerItem, MainWindow, OverlayWindow, SetItem};
use slint::{ComponentHandle, ModelRc, VecModel, Weak};
use std::cell::RefCell;
use std::rc::Rc;
use std::sync::atomic::Ordering;
use std::sync::Arc;
use std::time::Duration;

// ---- 常用键盘键（与 gpui 版 COMMON_KEYS 一致）----
const COMMON_KEYS: [(i32, &str); 28] = [
    (android_key::W, "W"),
    (android_key::A, "A"),
    (android_key::S, "S"),
    (android_key::D, "D"),
    (android_key::Q, "Q"),
    (android_key::E, "E"),
    (android_key::SPACE, "Space"),
    (android_key::ENTER, "Enter"),
    (android_key::TAB, "Tab"),
    (android_key::ESCAPE, "Esc"),
    (android_key::SHIFT_LEFT, "Shift"),
    (android_key::CTRL_LEFT, "Ctrl"),
    (android_key::ALT_LEFT, "Alt"),
    (android_key::N1, "1"),
    (android_key::N2, "2"),
    (android_key::N3, "3"),
    (android_key::N4, "4"),
    (android_key::N5, "5"),
    (android_key::F1, "F1"),
    (android_key::F2, "F2"),
    (android_key::F3, "F3"),
    (android_key::F4, "F4"),
    (android_key::F5, "F5"),
    (android_key::F6, "F6"),
    (android_key::F7, "F7"),
    (android_key::F8, "F8"),
    (android_key::F9, "F9"),
    (android_key::F10, "F10"),
];

// ---- 动作类型（key = kind 字符串）----
const KIND_DEFS: [(&str, &str); 8] = [
    ("keyboard", "键盘"),
    ("mouse", "鼠标点击"),
    ("mousetoggle", "鼠标长按"),
    ("wheelup", "滚轮上"),
    ("wheeldown", "滚轮下"),
    ("switchlayer", "切层"),
    ("lookaround", "视角控制"),
    ("mousemove", "鼠标移动"),
];

const MOUSE_BUTTONS: [(&str, &str); 5] = [
    ("LEFT", "鼠标左键"),
    ("RIGHT", "鼠标右键"),
    ("MIDDLE", "鼠标中键"),
    ("FORWARD", "鼠标前进键"),
    ("BACK", "鼠标后退键"),
];

// ---------------------------------------------------------------------
// 层查找辅助
// ---------------------------------------------------------------------
fn find_layer_mut<'a>(
    profile: &'a mut ControllerProfile,
    id: &str,
) -> Option<&'a mut OperationLayer> {
    if id == "Common" {
        profile.common_layer_mut()
    } else {
        profile.layers_mut().into_iter().find(|l| l.id == id)
    }
}

fn find_layer_ref<'a>(
    profile: &'a ControllerProfile,
    id: &str,
) -> Option<&'a OperationLayer> {
    if id == "Common" {
        profile.common_layer()
    } else {
        profile.layers().into_iter().find(|l| l.id == id)
    }
}

// ---- 动作描述 / 类型串 ----
fn describe_action(a: &MappedAction) -> String {
    match a.r#type {
        ActionType::KeyboardKey => format!("键盘: {}", key_code_to_name(a.key_code)),
        ActionType::MouseClick => format!("鼠标: {}", mouse_button_display_name(a.mouse_button)),
        ActionType::MouseToggle => format!("鼠标长按: {}", mouse_button_display_name(a.mouse_button)),
        ActionType::WheelUp => "滚轮上".to_string(),
        ActionType::WheelDown => "滚轮下".to_string(),
        ActionType::SwitchLayer => format!("切换到层: {}", a.layer_name.clone().unwrap_or_default()),
        ActionType::LookAround => "视角控制（右摇杆）".to_string(),
        ActionType::MouseMove => "鼠标移动（左摇杆）".to_string(),
        _ => "（未知）".to_string(),
    }
}

fn kind_str(a: &MappedAction) -> String {
    match a.r#type {
        ActionType::KeyboardKey => "keyboard",
        ActionType::MouseClick => "mouse",
        ActionType::MouseToggle => "mousetoggle",
        ActionType::WheelUp => "wheelup",
        ActionType::WheelDown => "wheeldown",
        ActionType::SwitchLayer => "switchlayer",
        ActionType::LookAround => "lookaround",
        ActionType::MouseMove => "mousemove",
        _ => "keyboard",
    }
    .to_string()
}

/// Vec -> Slint ModelRc
fn model<T: Clone + 'static>(items: Vec<T>) -> ModelRc<T> {
    Rc::new(VecModel::from(items)).into()
}

// ---------------------------------------------------------------------
// 主窗口逻辑
// ---------------------------------------------------------------------
#[derive(Clone, Copy, PartialEq)]
enum RenameMode {
    Rename,
    Copy,
}

pub struct Logic {
    shared: Arc<AppShared>,
    ui: Weak<MainWindow>,
    overlay: Option<Weak<OverlayWindow>>,
    timer: slint::Timer,
    // 轮询缓存
    prev_main: Option<String>,
    prev_edit: Option<String>,
    // 编辑状态
    selected_button: String,
    edit_layer_id: String,
    edit_kind: String,
    // 悬浮窗 / 重命名
    overlay_visible: bool,
    rename_mode: Option<RenameMode>,
    rename_value: String,
}

impl Logic {
    pub fn new(ui: Weak<MainWindow>, shared: Arc<AppShared>) -> Self {
        Self {
            shared,
            ui,
            overlay: None,
            timer: slint::Timer::default(),
            prev_main: None,
            prev_edit: None,
            selected_button: "A".to_string(),
            edit_layer_id: "Common".to_string(),
            edit_kind: "keyboard".to_string(),
            overlay_visible: false,
            rename_mode: None,
            rename_value: String::new(),
        }
    }

    // ------------------------- 动作 -------------------------
    fn switch_set(&mut self, id: &str) {
        if let Ok(mut core) = self.shared.core.lock() {
            core.switch_operation_set(id);
        }
    }

    fn add_set(&mut self) {
        if let Ok(mut core) = self.shared.core.lock() {
            core.add_operation_set();
        }
    }

    fn start_rename(&mut self) {
        self.rename_value.clear();
        self.rename_mode = Some(RenameMode::Rename);
    }

    fn start_copy(&mut self) {
        let base = {
            let core = self.shared.core.lock().unwrap();
            core.steam.profile.active_operation_set_name()
        };
        self.rename_value = format!("{} - 副本", base);
        self.rename_mode = Some(RenameMode::Copy);
    }

    fn commit_rename(&mut self) {
        let name = self.rename_value.trim().to_string();
        if name.is_empty() {
            return;
        }
        let mode = self.rename_mode.take();
        let set_id = {
            let core = self.shared.core.lock().unwrap();
            core.steam.profile.active_operation_set_id.clone()
        };
        let mut core = self.shared.core.lock().unwrap();
        match mode {
            Some(RenameMode::Rename) => {
                core.rename_operation_set(&set_id, &name);
            }
            Some(RenameMode::Copy) => {
                core.copy_operation_set(&set_id, &name);
            }
            None => {}
        }
    }

    fn delete_set(&mut self) {
        let set_id = {
            let core = self.shared.core.lock().unwrap();
            core.steam.profile.active_operation_set_id.clone()
        };
        if let Ok(mut core) = self.shared.core.lock() {
            core.delete_operation_set(&set_id);
        }
    }

    fn toggle_mapping(&mut self) {
        if self.shared.running.load(Ordering::SeqCst) {
            self.shared.stop_mapping();
        } else {
            self.shared.start_mapping();
        }
    }

    fn save_config(&mut self) {
        let profile = {
            let core = self.shared.core.lock().unwrap();
            core.steam.profile.clone()
        };
        config_manager::save(&profile);
    }

    fn reset_config(&mut self) {
        config_manager::reset_to_default();
        let def = config_manager::load();
        if let Ok(mut core) = self.shared.core.lock() {
            core.load_profile(def);
        }
    }

    fn adjust(&mut self, key: &str, delta: f32) {
        let mut core = self.shared.core.lock().unwrap();
        let mut gs = core.steam.profile.global_settings.clone();
        match key {
            "deadzone" => gs.deadzone = (gs.deadzone + delta).clamp(0.0, 0.5),
            "look_sensitivity" => gs.look_sensitivity = (gs.look_sensitivity + delta).clamp(0.05, 2.0),
            "look_smoothing" => gs.look_smoothing = (gs.look_smoothing + delta).clamp(0.0, 0.95),
            "look_acceleration" => gs.look_acceleration = (gs.look_acceleration + delta).clamp(0.5, 3.0),
            _ => {}
        }
        core.update_global_settings(gs);
    }

    fn toggle_overlay(&mut self) {
        let Some(ov) = self.overlay.as_ref().and_then(|w| w.upgrade()) else {
            return;
        };
        if self.overlay_visible {
            let _ = ov.hide();
        } else {
            let _ = ov.show();
        }
        self.overlay_visible = !self.overlay_visible;
    }

    fn quit(&mut self) {
        self.shared.stop_mapping();
        let _ = slint::quit_event_loop();
    }

    // ------------------------- 层编辑动作 -------------------------
    fn read_kind_for(&self, button_name: &str) -> String {
        let Some(b) = controller_button_from_name(button_name) else {
            return "keyboard".to_string();
        };
        let core = self.shared.core.lock().unwrap();
        let Some(layer) = find_layer_ref(&core.steam.profile, &self.edit_layer_id) else {
            return "keyboard".to_string();
        };
        match layer.button_mappings.get(&b) {
            Some(m) => kind_str(&m.action),
            None => "keyboard".to_string(),
        }
    }

    fn write_action(&mut self, action: MappedAction) {
        let Some(b) = controller_button_from_name(&self.selected_button) else {
            return;
        };
        let mut core = self.shared.core.lock().unwrap();
        if let Some(layer) = find_layer_mut(&mut core.steam.profile, &self.edit_layer_id) {
            let subs = layer
                .button_mappings
                .get(&b)
                .map(|m| m.sub_commands.clone())
                .unwrap_or_default();
            layer.button_mappings.insert(b, KeyMapping { action, sub_commands: subs });
            core.profile_rev += 1;
        }
    }

    fn open_edit(&mut self, layer_id: &str) {
        self.edit_layer_id = layer_id.to_string();
        self.selected_button = "A".to_string();
        self.edit_kind = self.read_kind_for("A");
        self.prev_edit = None;
        if let Some(ui) = self.ui.upgrade() {
            ui.set_current_view(1);
        }
    }

    fn edit_back(&mut self) {
        if let Some(ui) = self.ui.upgrade() {
            ui.set_current_view(0);
        }
    }

    fn select_button(&mut self, name: &str) {
        self.selected_button = name.to_string();
        self.edit_kind = self.read_kind_for(name);
        self.prev_edit = None;
    }

    fn set_kind(&mut self, kind: &str) {
        self.edit_kind = kind.to_string();
        let action = match kind {
            "wheelup" => Some(MappedAction::wheel_up()),
            "wheeldown" => Some(MappedAction::wheel_down()),
            "lookaround" => Some(MappedAction::look_around()),
            "mousemove" => Some(MappedAction::mouse_move()),
            _ => None,
        };
        if let Some(a) = action {
            self.write_action(a);
        }
        self.prev_edit = None;
    }

    fn set_key(&mut self, code: &str) {
        if let Ok(c) = code.parse::<i32>() {
            self.write_action(MappedAction::keyboard_key(c));
        }
    }

    fn set_mouse(&mut self, name: &str) {
        let Some(mb) = mouse_button_from_name(name) else {
            return;
        };
        let action = match self.edit_kind.as_str() {
            "mousetoggle" => MappedAction::mouse_toggle(mb),
            _ => MappedAction::mouse_click(mb),
        };
        self.write_action(action);
    }

    fn set_layer(&mut self, name: &str) {
        self.write_action(MappedAction::switch_layer(name));
    }

    fn toggle_sub(&mut self, code: &str) {
        let Ok(c) = code.parse::<i32>() else {
            return;
        };
        let Some(b) = controller_button_from_name(&self.selected_button) else {
            return;
        };
        let mut core = self.shared.core.lock().unwrap();
        if let Some(layer) = find_layer_mut(&mut core.steam.profile, &self.edit_layer_id) {
            if let Some(m) = layer.button_mappings.get_mut(&b) {
                if let Some(pos) = m.sub_commands.iter().position(|&k| k == c) {
                    m.sub_commands.remove(pos);
                } else if m.sub_commands.len() < KeyMapping::MAX_SUB_COMMANDS {
                    m.sub_commands.push(c);
                }
            }
            core.profile_rev += 1;
        }
    }

    fn clear_mapping(&mut self) {
        let Some(b) = controller_button_from_name(&self.selected_button) else {
            return;
        };
        let mut core = self.shared.core.lock().unwrap();
        if let Some(layer) = find_layer_mut(&mut core.steam.profile, &self.edit_layer_id) {
            layer.button_mappings.remove(&b);
            core.profile_rev += 1;
        }
    }

    // ------------------------- 轮询 -------------------------
    fn poll_main(&mut self, ui: &MainWindow) {
        let core = self.shared.core.lock().unwrap();
        let connected = core.connected;
        let running = self.shared.running.load(Ordering::SeqCst);
        let layer_name = core.steam.active_layer_name().to_string();
        let set_name = core.steam.profile.active_operation_set_name();
        let active_set_id = core.steam.profile.active_operation_set_id.clone();
        let sets: Vec<(String, String, bool)> = core
            .steam
            .profile
            .operation_sets
            .iter()
            .map(|s| (s.id.clone(), s.name.clone(), s.id == active_set_id))
            .collect();
        let layers: Vec<(String, String, bool)> = core
            .steam
            .profile
            .layers()
            .iter()
            .map(|l| (l.id.clone(), l.name.clone(), core.steam.is_layer_active(&l.id)))
            .collect();
        let mouse_toggle = core
            .mapper
            .toggled_mouse_buttons
            .values()
            .next()
            .copied()
            .map(|mb| format!("长按锁存: {}", mouse_button_display_name(mb)));
        let gs = &core.steam.profile.global_settings;
        let (dz, sens, sm, acc) = (gs.deadzone, gs.look_sensitivity, gs.look_smoothing, gs.look_acceleration);
        drop(core);

        let status = if connected {
            if running {
                "● 已连接 · 映射运行中"
            } else {
                "● 已连接 · 已停止"
            }
        } else {
            "○ 手柄未连接"
        };
        let toggle_txt = if running { "停止映射" } else { "开始映射" };
        let ov_txt = if self.overlay_visible { "关闭悬浮窗" } else { "显示悬浮窗" };
        let mt = mouse_toggle.is_some();
        let mtt = mouse_toggle.unwrap_or_else(|| "无长按锁存".to_string());

        let key = format!(
            "{}|{}|{}|{}|{}|{}|{:.2}|{:.2}|{:.2}|{:.2}|{:?}|{:?}|{}",
            connected, running, set_name, layer_name, mtt, mt,
            dz, sens, sm, acc, sets, layers, self.rename_value,
        );
        if self.prev_main.as_deref() == Some(&key) {
            return;
        }
        self.prev_main = Some(key);

        ui.set_connected(connected);
        ui.set_running(running);
        ui.set_status_text(status.into());
        ui.set_set_name(set_name.into());
        ui.set_layer_name(layer_name.into());
        ui.set_mouse_toggle_text(mtt.into());
        ui.set_mouse_toggle(mt);
        ui.set_toggle_text(toggle_txt.into());
        ui.set_overlay_btn_text(ov_txt.into());
        ui.set_deadzone(format!("{:.2}", dz).into());
        ui.set_sensitivity(format!("{:.2}", sens).into());
        ui.set_smoothing(format!("{:.2}", sm).into());
        ui.set_acceleration(format!("{:.2}", acc).into());

        // 重命名行状态
        let (rv, rh) = match &self.rename_mode {
            Some(RenameMode::Rename) => (true, "重命名操作集".to_string()),
            Some(RenameMode::Copy) => (true, "复制为新操作集".to_string()),
            None => (false, String::new()),
        };
        ui.set_rename_visible(rv);
        ui.set_rename_hint(rh.into());
        ui.set_rename_value(self.rename_value.clone().into());

        ui.set_sets(model(
            sets
                .into_iter()
                .map(|(id, name, active)| SetItem {
                    id: id.into(),
                    name: name.into(),
                    active,
                })
                .collect(),
        ));
        ui.set_layers(model(
            layers
                .into_iter()
                .map(|(id, name, active)| LayerItem {
                    id: id.into(),
                    name: name.into(),
                    active,
                })
                .collect(),
        ));
    }

    fn poll_edit(&mut self, ui: &MainWindow) {
        let core = self.shared.core.lock().unwrap();
        let layer_name = if self.edit_layer_id == "Common" {
            "公共层".to_string()
        } else {
            find_layer_ref(&core.steam.profile, &self.edit_layer_id)
                .map(|l| l.name.clone())
                .unwrap_or_default()
        };
        let layers: Vec<(String, String)> = core
            .steam
            .profile
            .layers()
            .iter()
            .map(|l| (l.id.clone(), l.name.clone()))
            .collect();
        let held = core.steam.held_buttons();
        let buttons: Vec<ButtonItem> = all_controller_buttons()
            .into_iter()
            .map(|b| {
                let name = controller_button_name(b);
                ButtonItem {
                    name: name.into(),
                    display: controller_button_display_name(b).into(),
                    pressed: held.contains(&b),
                    selected: name == self.selected_button,
                }
            })
            .collect();
        let (desc, subs) = {
            let b = controller_button_from_name(&self.selected_button);
            let layer = find_layer_ref(&core.steam.profile, &self.edit_layer_id);
            match b.and_then(|bb| layer.and_then(|l| l.button_mappings.get(&bb))) {
                Some(m) => (describe_action(&m.action), m.sub_commands.clone()),
                None => ("（无映射）".to_string(), Vec::new()),
            }
        };
        drop(core);

        let disp = controller_button_from_name(&self.selected_button)
            .map(controller_button_display_name)
            .unwrap_or(&self.selected_button)
            .to_string();
        let current = format!("当前: {} ({})", disp, desc);
        let subs_text = if subs.is_empty() {
            "无子命令".to_string()
        } else {
            subs.iter().map(|&k| key_code_to_name(k)).collect::<Vec<_>>().join(" + ")
        };
        let subs_label = format!("子命令（最多{}个，当前: {}）", KeyMapping::MAX_SUB_COMMANDS, subs_text);

        let key = format!(
            "{:?}|{:?}|{}|{}|{}|{:?}",
            buttons, layers, self.edit_kind, current, subs_label, subs,
        );
        if self.prev_edit.as_deref() == Some(&key) {
            return;
        }
        self.prev_edit = Some(key);

        ui.set_edit_title(format!("编辑层: {}", layer_display_name(&layer_name)).into());
        ui.set_edit_current(current.into());
        ui.set_edit_kind(self.edit_kind.clone().into());
        ui.set_edit_subs_label(subs_label.into());

        ui.set_edit_buttons(model(buttons));

        ui.set_edit_kinds(model(
            KIND_DEFS
                .iter()
                .map(|(k, label)| KeyItem {
                    key: (*k).into(),
                    label: (*label).into(),
                    active: *k == self.edit_kind,
                })
                .collect(),
        ));

        // 常用按键拆成 4 组（每组 7 个）填充 4 行
        let keys: Vec<KeyItem> = COMMON_KEYS
            .iter()
            .map(|(code, label)| KeyItem {
                key: code.to_string().into(),
                label: (*label).into(),
                active: false,
            })
            .collect();
        ui.set_keys1(model(keys[0..7].to_vec()));
        ui.set_keys2(model(keys[7..14].to_vec()));
        ui.set_keys3(model(keys[14..21].to_vec()));
        ui.set_keys4(model(keys[21..28].to_vec()));

        ui.set_edit_mouse(model(
            MOUSE_BUTTONS
                .iter()
                .map(|(name, label)| KeyItem {
                    key: (*name).into(),
                    label: (*label).into(),
                    active: false,
                })
                .collect(),
        ));

        ui.set_edit_layers(model(
            layers
                .into_iter()
                .map(|(_id, name)| KeyItem {
                    key: name.clone().into(),
                    label: layer_display_name(&name).into(),
                    active: false,
                })
                .collect(),
        ));

        // 子命令按键拆成 4 组（每组 7 个）填充 4 行
        let subs_items: Vec<KeyItem> = COMMON_KEYS
            .iter()
            .map(|(code, label)| KeyItem {
                key: code.to_string().into(),
                label: (*label).into(),
                active: subs.contains(code),
            })
            .collect();
        ui.set_subs1(model(subs_items[0..7].to_vec()));
        ui.set_subs2(model(subs_items[7..14].to_vec()));
        ui.set_subs3(model(subs_items[14..21].to_vec()));
        ui.set_subs4(model(subs_items[21..28].to_vec()));
    }
}

// ---------------------------------------------------------------------
// 装配
// ---------------------------------------------------------------------
pub fn setup(ui: &MainWindow, shared: Arc<AppShared>, overlay: Weak<OverlayWindow>) {
    let logic = Rc::new(RefCell::new(Logic::new(ui.as_weak(), shared)));
    logic.borrow_mut().overlay = Some(overlay);

    // 连接回调
    let l = logic.clone();
    ui.on_switch_set(move |id| l.borrow_mut().switch_set(id.as_str()));
    let l = logic.clone();
    ui.on_add_set(move || l.borrow_mut().add_set());
    let l = logic.clone();
    ui.on_start_copy(move || l.borrow_mut().start_copy());
    let l = logic.clone();
    ui.on_start_rename(move || l.borrow_mut().start_rename());
    let l = logic.clone();
    ui.on_commit_rename(move || l.borrow_mut().commit_rename());
    let l = logic.clone();
    ui.on_cancel_rename(move || {
        l.borrow_mut().rename_mode = None;
    });
    let l = logic.clone();
    ui.on_delete_set(move || l.borrow_mut().delete_set());
    let l = logic.clone();
    ui.on_rename_input_changed(move |t| {
        l.borrow_mut().rename_value = t.to_string();
    });
    let l = logic.clone();
    ui.on_toggle_mapping(move || l.borrow_mut().toggle_mapping());
    let l = logic.clone();
    ui.on_save_config(move || l.borrow_mut().save_config());
    let l = logic.clone();
    ui.on_reset_config(move || l.borrow_mut().reset_config());
    let l = logic.clone();
    ui.on_toggle_overlay(move || l.borrow_mut().toggle_overlay());
    let l = logic.clone();
    ui.on_open_help(move || {
        if let Some(u) = l.borrow().ui.upgrade() {
            u.set_current_view(2);
        }
    });
    let l = logic.clone();
    ui.on_quit(move || l.borrow_mut().quit());
    let l = logic.clone();
    ui.on_adjust(move |k, d| l.borrow_mut().adjust(k.as_str(), d));
    let l = logic.clone();
    ui.on_open_edit(move |id| l.borrow_mut().open_edit(id.as_str()));
    let l = logic.clone();
    ui.on_edit_back(move || l.borrow_mut().edit_back());
    let l = logic.clone();
    ui.on_edit_clear(move || l.borrow_mut().clear_mapping());
    let l = logic.clone();
    ui.on_select_button(move |name| l.borrow_mut().select_button(name.as_str()));
    let l = logic.clone();
    ui.on_set_kind(move |k| l.borrow_mut().set_kind(k.as_str()));
    let l = logic.clone();
    ui.on_set_key(move |code| l.borrow_mut().set_key(code.as_str()));
    let l = logic.clone();
    ui.on_set_mouse(move |name| l.borrow_mut().set_mouse(name.as_str()));
    let l = logic.clone();
    ui.on_set_layer(move |name| l.borrow_mut().set_layer(name.as_str()));
    let l = logic.clone();
    ui.on_toggle_sub(move |code| l.borrow_mut().toggle_sub(code.as_str()));
    let l = logic.clone();
    ui.on_help_back(move || {
        if let Some(u) = l.borrow().ui.upgrade() {
            u.set_current_view(0);
        }
    });

    // 启动轮询
    let l = logic.clone();
    let timer = slint::Timer::default();
    timer.start(slint::TimerMode::Repeated, Duration::from_millis(50), move || {
        if let Ok(mut g) = l.try_borrow_mut() {
            if let Some(u) = g.ui.upgrade() {
                match u.get_current_view() {
                    1 => g.poll_edit(&u),
                    _ => g.poll_main(&u),
                }
            }
        }
    });
    // 保活 Timer
    logic.borrow_mut().timer = timer;

    // 首次立即渲染
    if let Some(u) = ui.as_weak().upgrade() {
        let mut g = logic.borrow_mut();
        g.poll_main(&u);
    }
}
