// =====================================================================
// main_window.rs —— 主窗口（egui）
//
// 立即模式 UI：每 50ms 请求重绘，update() 里直接读取共享 core 快照
// 渲染主 / 层编辑 / 帮助 三视图。动作方法与 gpui/Slint 版一致。
// =====================================================================

use crate::core::config_manager;
use crate::core::input_types::*;
use crate::core::mapping_types::{
    ActionType, ControllerProfile, KeyMapping, MappedAction, OperationLayer,
};
use crate::ui::shared::AppShared;
use crate::ui::theme::*;
use eframe::egui;
use std::sync::atomic::{AtomicBool, Ordering};
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

const HELP_SECTIONS: [(&str, &str); 5] = [
    (
        "一、快速上手",
        "1. 连接手柄，点击「开始映射」。\n2. 默认配置已含一个「默认操作集」，公共层绑定基础按键（A=空格、B=右键、X=左键、Y=I、菜单键=Esc、视图键=M）。\n3. 右摇杆 = 视角控制，左摇杆 = WASD 移动。",
    ),
    (
        "二、操作集与层映射",
        "操作集是最高层容器，每个操作集内包含 1 个公共层 + 最多 10 个操作层。\n切换操作集时，其下所有层整体切换（适合不同游戏/场景一键切换整套配置）。\n• 添加：新建空操作集（默认名可再改）。\n• 复制：把当前操作集整体复制为新操作集，可直接改名。\n• 重命名：修改当前操作集的自定义名字。\n• 删除：至少保留一个操作集。\n悬浮窗始终显示当前操作集名称。",
    ),
    (
        "三、层切换机制",
        "操作层由公共层的「切层」(SwitchLayer) 映射驱动：按住切层键激活目标层，松开自动回退。\n按键查询顺序：最后激活的操作层 → 较早的操作层 → 公共层（兜底）。",
    ),
    (
        "四、悬浮窗",
        "「显示悬浮窗」打开置顶透明信息窗，实时显示：当前操作集、当前层、连接状态、按下的手柄按键。\n当鼠标长按锁存（MouseToggle）激活时，悬浮窗边框变橙色并显示警示。",
    ),
    (
        "五、配置文件",
        "配置文件 steamlike_config.json 位于程序同目录，绿色便携。与安卓版格式兼容（version=2）。",
    ),
];

// ---------------------------------------------------------------------
// 层查找辅助
// ---------------------------------------------------------------------
fn find_layer_mut<'a>(profile: &'a mut ControllerProfile, id: &str) -> Option<&'a mut OperationLayer> {
    if id == "Common" {
        profile.common_layer_mut()
    } else {
        profile.layers_mut().into_iter().find(|l| l.id == id)
    }
}

fn find_layer_ref<'a>(profile: &'a ControllerProfile, id: &str) -> Option<&'a OperationLayer> {
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

/// 深色圆角 chip 按钮
fn chip(ui: &mut egui::Ui, text: &str, active: bool) -> egui::Response {
    let (bg, fg, stroke) = if active {
        (rgb(ACCENT), rgb(BG_DEEP), rgb(ACCENT))
    } else {
        (rgb(BG_INSET), rgb(TEXT), rgb(BORDER))
    };
    ui.add(
        egui::Button::new(egui::RichText::new(text).color(fg).size(13.0))
            .fill(bg)
            .stroke(egui::Stroke::new(1.0, stroke))
            .corner_radius(6.0),
    )
}

// ---------------------------------------------------------------------
// 视图 / 状态
// ---------------------------------------------------------------------
#[derive(Clone, Copy, PartialEq)]
enum RenameMode {
    Rename,
    Copy,
}

enum View {
    Main,
    Edit,
    Help,
}

struct Snapshot {
    connected: bool,
    running: bool,
    status: String,
    set_name: String,
    layer_name: String,
    sets: Vec<(String, String, bool)>,
    layers: Vec<(String, String, bool)>,
    mouse_toggle: bool,
    mouse_toggle_text: String,
    deadzone: f32,
    sensitivity: f32,
    smoothing: f32,
    acceleration: f32,
}

pub struct MainApp {
    shared: Arc<AppShared>,
    overlay_visible: Arc<AtomicBool>,
    view: View,
    selected_button: String,
    edit_layer_id: String,
    edit_kind: String,
    rename_mode: Option<RenameMode>,
    rename_value: String,
}

impl MainApp {
    pub fn new(shared: Arc<AppShared>, overlay_visible: Arc<AtomicBool>) -> Self {
        Self {
            shared,
            overlay_visible,
            view: View::Main,
            selected_button: "A".to_string(),
            edit_layer_id: "Common".to_string(),
            edit_kind: "keyboard".to_string(),
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
        let new = !self.overlay_visible.load(Ordering::SeqCst);
        self.overlay_visible.store(new, Ordering::SeqCst);
    }

    fn quit(&mut self, ctx: &egui::Context) {
        self.shared.stop_mapping();
        ctx.send_viewport_cmd(egui::ViewportCommand::Close);
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
            layer
                .button_mappings
                .insert(b, KeyMapping { action, sub_commands: subs });
            core.profile_rev += 1;
        }
    }

    fn open_edit(&mut self, layer_id: &str) {
        self.edit_layer_id = layer_id.to_string();
        self.selected_button = "A".to_string();
        self.edit_kind = self.read_kind_for("A");
        self.view = View::Edit;
    }

    fn select_button(&mut self, name: &str) {
        self.selected_button = name.to_string();
        self.edit_kind = self.read_kind_for(name);
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

    // ------------------------- 快照 -------------------------
    fn read_snapshot(&self) -> Snapshot {
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
        let mouse_toggle = !core.mapper.toggled_mouse_buttons.is_empty();
        let mouse_toggle_text = core
            .mapper
            .toggled_mouse_buttons
            .values()
            .next()
            .map(|mb| format!("长按锁存: {}", mouse_button_display_name(*mb)))
            .unwrap_or_default();
        let gs = &core.steam.profile.global_settings;
        let status = if connected {
            if running {
                "● 已连接 · 映射运行中".to_string()
            } else {
                "● 已连接 · 已停止".to_string()
            }
        } else {
            "○ 手柄未连接".to_string()
        };
        Snapshot {
            connected,
            running,
            status,
            set_name,
            layer_name,
            sets,
            layers,
            mouse_toggle,
            mouse_toggle_text,
            deadzone: gs.deadzone,
            sensitivity: gs.look_sensitivity,
            smoothing: gs.look_smoothing,
            acceleration: gs.look_acceleration,
        }
    }

    // ------------------------- 主视图 -------------------------
    fn show_main(&mut self, ui: &mut egui::Ui) {
        let snap = self.read_snapshot();
        egui::Panel::left("side_left")
            .resizable(false)
            .default_size(240.0)
            .frame(egui::Frame::NONE.fill(rgb(BG_PANEL)).inner_margin(egui::Margin::same(14)))
            .show(ui, |ui| {
                self.show_left(ui, &snap);
            });
        egui::CentralPanel::default_margins()
            .frame(egui::Frame::NONE.fill(rgb(BG)).inner_margin(egui::Margin::same(16)))
            .show(ui, |ui| {
                self.show_right(ui, &snap);
            });
    }

    fn show_left(&mut self, ui: &mut egui::Ui, snap: &Snapshot) {
        ui.label(
            egui::RichText::new("Gamepad 键鼠映射")
                .color(rgb(ACCENT))
                .size(18.0)
                .strong(),
        );
        ui.label(egui::RichText::new("操作集 · 层管理").color(rgb(TEXT_FAINT)).size(12.0));
        ui.add_space(10.0);

        card(ui, |ui| {
            ui.label(
                egui::RichText::new("操作集").color(rgb(TEXT_DIM)).size(13.0).strong(),
            );
            ui.add_space(4.0);
            ui.horizontal_wrapped(|ui| {
                for (id, name, active) in &snap.sets {
                    if chip(ui, name, *active).clicked() {
                        self.switch_set(id);
                    }
                }
            });
            ui.add_space(6.0);
            ui.horizontal_wrapped(|ui| {
                if btn_accent(ui, "＋ 添加").clicked() {
                    self.add_set();
                }
                if btn_ghost(ui, "复制").clicked() {
                    self.start_copy();
                }
                if btn_ghost(ui, "重命名").clicked() {
                    self.start_rename();
                }
                if btn_ghost(ui, "删除").clicked() {
                    self.delete_set();
                }
            });
        });
        ui.add_space(10.0);

        card(ui, |ui| {
            ui.label(
                egui::RichText::new("层列表").color(rgb(TEXT_DIM)).size(13.0).strong(),
            );
            ui.add_space(4.0);
            egui::ScrollArea::vertical()
                .id_salt("layer_list")
                .max_height(320.0)
                .show(ui, |ui| {
                    for (id, name, active) in &snap.layers {
                        let display = layer_display_name(name);
                        if chip(ui, &display, *active).clicked() {
                            self.open_edit(id);
                        }
                    }
                });
        });

        ui.with_layout(egui::Layout::bottom_up(egui::Align::Min), |ui| {
            ui.add_space(4.0);
            ui.label(egui::RichText::new("egui 版 · v0.0.1").color(rgb(TEXT_FAINT)).size(11.0));
        });
    }

    fn show_right(&mut self, ui: &mut egui::Ui, snap: &Snapshot) {
        // 顶部状态标题栏
        ui.horizontal(|ui| {
            let dot = if snap.connected { rgb(OK) } else { rgb(TEXT_FAINT) };
            ui.label(egui::RichText::new("●").color(dot).size(18.0));
            ui.label(egui::RichText::new(&snap.status).color(rgb(TEXT)).size(15.0).strong());
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                ui.label(
                    egui::RichText::new(format!("当前层: {}", snap.layer_name))
                        .color(rgb(TEXT_DIM))
                        .size(13.0),
                );
                ui.add_space(10.0);
                ui.label(
                    egui::RichText::new(format!("当前操作集: {}", snap.set_name))
                        .color(rgb(ACCENT))
                        .size(14.0)
                        .strong(),
                );
            });
        });
        if snap.mouse_toggle {
            ui.add_space(4.0);
            ui.label(
                egui::RichText::new(&snap.mouse_toggle_text)
                    .color(rgb(WARN))
                    .size(13.0),
            );
        }
        ui.add_space(8.0);
        ui.separator();
        ui.add_space(10.0);

        // 全局设置
        card(ui, |ui| {
            ui.label(
                egui::RichText::new("全局设置").color(rgb(TEXT_DIM)).size(13.0).strong(),
            );
            ui.add_space(6.0);
            self.setting_row(ui, "死区", &format!("{:.2}", snap.deadzone), "deadzone");
            self.setting_row(ui, "视角灵敏度", &format!("{:.2}", snap.sensitivity), "look_sensitivity");
            self.setting_row(ui, "视角平滑", &format!("{:.2}", snap.smoothing), "look_smoothing");
            self.setting_row(ui, "视角加速", &format!("{:.2}", snap.acceleration), "look_acceleration");
        });
        ui.add_space(10.0);

        // 动作
        card(ui, |ui| {
            ui.label(egui::RichText::new("动作").color(rgb(TEXT_DIM)).size(13.0).strong());
            ui.add_space(6.0);
            ui.horizontal_wrapped(|ui| {
                let resp = if snap.running {
                    btn_danger(ui, "停止映射")
                } else {
                    btn_accent(ui, "开始映射")
                };
                if resp.clicked() {
                    self.toggle_mapping();
                }
                if btn_ghost(ui, "保存配置").clicked() {
                    self.save_config();
                }
                if btn_ghost(ui, "重置配置").clicked() {
                    self.reset_config();
                }
                let ov_txt = if self.overlay_visible.load(Ordering::SeqCst) {
                    "关闭悬浮窗"
                } else {
                    "显示悬浮窗"
                };
                if btn_ghost(ui, ov_txt).clicked() {
                    self.toggle_overlay();
                }
                if btn_ghost(ui, "帮助").clicked() {
                    self.view = View::Help;
                }
                if btn_ghost(ui, "退出").clicked() {
                    self.quit(ui.ctx());
                }
            });

            // 重命名 / 复制行
            if let Some(mode) = self.rename_mode {
                ui.add_space(10.0);
                ui.separator();
                ui.add_space(6.0);
                let hint = match mode {
                    RenameMode::Rename => "重命名操作集",
                    RenameMode::Copy => "复制为新操作集",
                };
                ui.label(egui::RichText::new(hint).color(rgb(ACCENT)).size(13.0));
                ui.add_space(4.0);
                ui.horizontal(|ui| {
                    ui.add(
                        egui::TextEdit::singleline(&mut self.rename_value)
                            .hint_text("输入名称")
                            .desired_width(220.0),
                    );
                    if btn_accent(ui, "确认").clicked() {
                        self.commit_rename();
                    }
                    if btn_ghost(ui, "取消").clicked() {
                        self.rename_mode = None;
                    }
                });
            }
        });
    }

    fn setting_row(&mut self, ui: &mut egui::Ui, label: &str, value: &str, key: &str) {
        ui.horizontal(|ui| {
            ui.add_space(4.0);
            ui.label(egui::RichText::new(label).color(rgb(TEXT_DIM)).size(14.0));
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                if ui.button("＋").clicked() {
                    self.adjust(key, 0.01);
                }
                ui.label(egui::RichText::new(value).color(rgb(TEXT)).size(14.0));
                if ui.button("－").clicked() {
                    self.adjust(key, -0.01);
                }
            });
        });
    }

    // ------------------------- 层编辑视图 -------------------------
    fn show_edit(&mut self, ui: &mut egui::Ui) {
        let (layer_name, layers, buttons, desc, subs) = {
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
            let buttons: Vec<(String, String, bool)> = all_controller_buttons()
                .into_iter()
                .map(|b| {
                    (
                        controller_button_name(b).to_string(),
                        controller_button_display_name(b).to_string(),
                        held.contains(&b),
                    )
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
            (layer_name, layers, buttons, desc, subs)
        };

        egui::CentralPanel::default_margins()
            .frame(egui::Frame::NONE.fill(rgb(BG)).inner_margin(egui::Margin::same(16)))
            .show(ui, |ui| {
                ui.horizontal(|ui| {
                    if btn_ghost(ui, "← 返回").clicked() {
                        self.view = View::Main;
                    }
                    ui.add_space(8.0);
                    ui.label(
                        egui::RichText::new(format!("编辑层: {}", layer_display_name(&layer_name)))
                            .color(rgb(TEXT))
                            .size(17.0)
                            .strong(),
                    );
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        if btn_danger(ui, "清空映射").clicked() {
                            self.clear_mapping();
                        }
                    });
                });
                ui.add_space(10.0);
                let sel_disp = controller_button_from_name(&self.selected_button)
                    .map(controller_button_display_name)
                    .unwrap_or(&self.selected_button);
                card(ui, |ui| {
                    ui.horizontal(|ui| {
                        ui.label(egui::RichText::new("当前选中").color(rgb(TEXT_DIM)).size(13.0));
                        ui.label(
                            egui::RichText::new(sel_disp)
                                .color(rgb(ACCENT))
                                .size(15.0)
                                .strong(),
                        );
                        ui.label(egui::RichText::new("→").color(rgb(TEXT_FAINT)).size(13.0));
                        ui.label(egui::RichText::new(desc).color(rgb(TEXT)).size(14.0));
                    });
                });
                ui.add_space(10.0);

                ui.columns(2, |cols| {
                    // 左：按钮列表
                    cols[0].set_width(200.0);
                    card(&mut cols[0], |ui| {
                        ui.label(
                            egui::RichText::new("手柄按键").color(rgb(TEXT_DIM)).size(13.0).strong(),
                        );
                        ui.add_space(4.0);
                        egui::ScrollArea::vertical()
                            .id_salt("btn_list")
                            .show(ui, |ui| {
                                for (name, display, pressed) in &buttons {
                                    let selected = *name == self.selected_button;
                                    let mut text = display.to_string();
                                    if *pressed {
                                        text.push_str("  ●");
                                    }
                                    if chip(ui, &text, selected).clicked() {
                                        self.select_button(name);
                                    }
                                }
                            });
                    });

                    // 右：编辑区
                    let ui = &mut cols[1];
                    card(ui, |ui| {
                        egui::ScrollArea::vertical()
                            .id_salt("edit_panel")
                            .show(ui, |ui| {
                                ui.label(
                                    egui::RichText::new("动作类型")
                                        .color(rgb(TEXT_DIM))
                                        .size(13.0)
                                        .strong(),
                                );
                                ui.add_space(4.0);
                                ui.horizontal_wrapped(|ui| {
                                    for (k, label) in KIND_DEFS {
                                        if chip(ui, label, *k == self.edit_kind).clicked() {
                                            self.set_kind(k);
                                        }
                                    }
                                });
                                ui.add_space(6.0);
                                ui.separator();
                                ui.add_space(6.0);

                                match self.edit_kind.as_str() {
                                    "keyboard" => {
                                        ui.label(
                                            egui::RichText::new("选择按键")
                                                .color(rgb(TEXT_DIM))
                                                .size(13.0),
                                        );
                                        ui.add_space(2.0);
                                        self.keys_grid(ui, &[]);
                                    }
                                    "mouse" | "mousetoggle" => {
                                        ui.label(
                                            egui::RichText::new("选择鼠标键")
                                                .color(rgb(TEXT_DIM))
                                                .size(13.0),
                                        );
                                        ui.add_space(2.0);
                                        ui.horizontal_wrapped(|ui| {
                                            for (name, label) in MOUSE_BUTTONS {
                                                if chip(ui, label, false).clicked() {
                                                    self.set_mouse(name);
                                                }
                                            }
                                        });
                                    }
                                    "switchlayer" => {
                                        ui.label(
                                            egui::RichText::new("选择目标层")
                                                .color(rgb(TEXT_DIM))
                                                .size(13.0),
                                        );
                                        ui.add_space(2.0);
                                        ui.horizontal_wrapped(|ui| {
                                            for (_id, name) in &layers {
                                                if chip(ui, &layer_display_name(name), false)
                                                    .clicked()
                                                {
                                                    self.set_layer(name);
                                                }
                                            }
                                        });
                                    }
                                    _ => {
                                        ui.label(
                                            egui::RichText::new(
                                                "选择该类型后已保存映射，可继续选择按键编辑。",
                                            )
                                            .color(rgb(TEXT_FAINT))
                                            .size(13.0),
                                        );
                                    }
                                }

                                ui.add_space(6.0);
                                ui.separator();
                                ui.add_space(6.0);
                                let subs_text = if subs.is_empty() {
                                    "无".to_string()
                                } else {
                                    subs.iter()
                                        .map(|&k| key_code_to_name(k))
                                        .collect::<Vec<_>>()
                                        .join(" + ")
                                };
                                ui.label(
                                    egui::RichText::new(format!(
                                        "子命令（最多{}个，当前: {}）",
                                        KeyMapping::MAX_SUB_COMMANDS,
                                        subs_text
                                    ))
                                    .color(rgb(TEXT_DIM))
                                    .size(13.0),
                                );
                                self.subs_grid(ui, &subs);
                            });
                    });
                });
            });
    }

    fn keys_grid(&mut self, ui: &mut egui::Ui, _active: &[i32]) {
        ui.horizontal_wrapped(|ui| {
            for (code, label) in COMMON_KEYS {
                if chip(ui, label, false).clicked() {
                    self.set_key(&code.to_string());
                }
            }
        });
    }

    fn subs_grid(&mut self, ui: &mut egui::Ui, active: &[i32]) {
        ui.horizontal_wrapped(|ui| {
            for (code, label) in COMMON_KEYS {
                if chip(ui, label, active.contains(&code)).clicked() {
                    self.toggle_sub(&code.to_string());
                }
            }
        });
    }

    // ------------------------- 帮助视图 -------------------------
    fn show_help(&mut self, ui: &mut egui::Ui) {
        egui::CentralPanel::default_margins()
            .frame(egui::Frame::NONE.fill(rgb(BG)).inner_margin(egui::Margin::same(16)))
            .show(ui, |ui| {
                ui.horizontal(|ui| {
                    if btn_ghost(ui, "← 返回").clicked() {
                        self.view = View::Main;
                    }
                });
                ui.add_space(10.0);
                ui.label(
                    egui::RichText::new("Gamepad 键鼠映射 · 使用说明")
                        .color(rgb(TEXT))
                        .size(20.0)
                        .strong(),
                );
                ui.label(egui::RichText::new("手柄映射到键鼠的完整说明").color(rgb(TEXT_FAINT)).size(12.0));
                ui.add_space(6.0);
                egui::ScrollArea::vertical()
                    .id_salt("help_scroll")
                    .show(ui, |ui| {
                        for (title, body) in HELP_SECTIONS {
                            ui.add_space(8.0);
                            card(ui, |ui| {
                                ui.label(
                                    egui::RichText::new(title)
                                        .color(rgb(ACCENT))
                                        .size(15.0)
                                        .strong(),
                                );
                                ui.add_space(4.0);
                                for line in body.lines() {
                                    ui.label(egui::RichText::new(line).color(rgb(TEXT)).size(13.0));
                                }
                            });
                        }
                    });
            });
    }
}

impl eframe::App for MainApp {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        ui.ctx().request_repaint_after(Duration::from_millis(50));
        match self.view {
            View::Main => self.show_main(ui),
            View::Edit => self.show_edit(ui),
            View::Help => self.show_help(ui),
        }
    }
}

// ---------------------------------------------------------------------
// 装配
// ---------------------------------------------------------------------
pub fn run(shared: Arc<AppShared>, overlay_visible: Arc<AtomicBool>) -> eframe::Result {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_title("Gamepad 键鼠映射")
            .with_inner_size([920.0, 640.0])
            .with_min_inner_size([760.0, 500.0]),
        ..Default::default()
    };
    eframe::run_native(
        "gamepad_controler_egui",
        options,
        Box::new(move |cc| {
            crate::ui::theme::install_fonts(&cc.egui_ctx);
            crate::ui::theme::apply(&cc.egui_ctx);
            Ok(Box::new(MainApp::new(shared, overlay_visible)))
        }),
    )
}
