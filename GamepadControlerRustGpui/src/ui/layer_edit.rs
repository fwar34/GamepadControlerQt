// =====================================================================
// layer_edit.rs —— 层编辑窗口
//
// 编辑公共层或操作层中每个手柄按钮的映射：
//   - 左侧手柄按钮网格，点击选中；按下的按钮实时高亮
//   - 右侧设置动作类型（键盘/鼠标/长按/滚轮/切层/视角/移动）
//     与目标（键/鼠标键/层），以及最多 3 个子命令（组合键）
//
// 编辑直接写入共享 profile（AppCore.profile_rev++ 触发主界面刷新）。
// =====================================================================

use crate::core::input_types::*;
use crate::core::input_types::MouseButton; // 显式导入：消除与 gpui::MouseButton 的 glob 歧义
use crate::core::mapping_types::{ActionType, ControllerProfile, KeyMapping, MappedAction, OperationLayer};
use crate::ui::shared::AppShared;
use crate::ui::theme::*;
use crate::ui::theme::rgb; // 显式导入：消除与 gpui::rgb 的 glob 歧义
use gpui::*;
use std::sync::Arc;

/// UI 编辑用动作类型（比 ActionType 少 Toggle 系列）
#[derive(Debug, Clone, Copy, PartialEq)]
enum EditActionKind {
    Keyboard,
    Mouse,
    MouseToggle,
    WheelUp,
    WheelDown,
    SwitchLayer,
    LookAround,
    MouseMove,
}

fn kind_of(a: &MappedAction) -> EditActionKind {
    match a.r#type {
        ActionType::KeyboardKey => EditActionKind::Keyboard,
        ActionType::MouseClick => EditActionKind::Mouse,
        ActionType::MouseToggle => EditActionKind::MouseToggle,
        ActionType::WheelUp => EditActionKind::WheelUp,
        ActionType::WheelDown => EditActionKind::WheelDown,
        ActionType::SwitchLayer => EditActionKind::SwitchLayer,
        ActionType::LookAround => EditActionKind::LookAround,
        ActionType::MouseMove => EditActionKind::MouseMove,
        _ => EditActionKind::Keyboard,
    }
}

/// 常用键盘键（显示名, Android KeyCode）
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

fn describe_action(a: &MappedAction) -> String {
    match a.r#type {
        ActionType::KeyboardKey => format!("键盘: {}", key_code_to_name(a.key_code)),
        ActionType::MouseClick => format!("鼠标: {}", mouse_button_display_name(a.mouse_button)),
        ActionType::MouseToggle => format!("鼠标长按: {}", mouse_button_display_name(a.mouse_button)),
        ActionType::WheelUp => "滚轮上".to_string(),
        ActionType::WheelDown => "滚轮下".to_string(),
        ActionType::SwitchLayer => format!(
            "切换到层: {}",
            a.layer_name.clone().unwrap_or_default()
        ),
        ActionType::LookAround => "视角控制（右摇杆）".to_string(),
        ActionType::MouseMove => "鼠标移动（左摇杆）".to_string(),
        _ => "（未知）".to_string(),
    }
}

pub struct LayerEditView {
    shared: Arc<AppShared>,
    layer_id: String,
    selected: ControllerButton,
    action_type: EditActionKind,
    /// 按下高亮的按钮
    held: Vec<ControllerButton>,
    profile_rev: u64,
}

// ---- 层查找辅助 ----
fn find_layer<'a>(profile: &'a mut ControllerProfile, id: &str) -> Option<&'a mut OperationLayer> {
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

impl LayerEditView {
    pub fn new(shared: Arc<AppShared>, layer_id: String, _window: &mut Window, cx: &mut Context<Self>) -> Self {
        let mut this = Self {
            shared,
            layer_id,
            selected: ControllerButton::A,
            action_type: EditActionKind::Keyboard,
            held: Vec::new(),
            profile_rev: u64::MAX,
        };
        this.load_current_mapping();
        this.start_polling(cx);
        this
    }

    fn start_polling(&mut self, cx: &mut Context<Self>) {
        let shared = Arc::clone(&self.shared);
        cx.spawn(async move |this, mut cx| {
            loop {
                Timer::after(std::time::Duration::from_millis(50)).await;
                let _ = this.update(cx, |this, cx| {
                    let core = shared.core.lock().unwrap();
                    let held = core.steam.held_buttons().iter().copied().collect();
                    let rev = core.profile_rev;
                    drop(core);
                    if this.held != held || this.profile_rev != rev {
                        this.held = held;
                        this.profile_rev = rev;
                        cx.notify();
                    }
                });
            }
        })
        .detach();
    }

    fn load_current_mapping(&mut self) {
        let core = self.shared.core.lock().unwrap();
        let layer = find_layer_ref(&core.steam.profile, &self.layer_id);
        if let Some(layer) = layer {
            if let Some(m) = layer.button_mappings.get(&self.selected) {
                self.action_type = kind_of(&m.action);
            }
        }
    }

    fn select_button(&mut self, b: ControllerButton) {
        self.selected = b;
        self.load_current_mapping();
    }

    /// 把当前动作写入选中按钮的映射（保留已有子命令）
    fn write_action(&mut self, action: MappedAction) {
        let mut core = self.shared.core.lock().unwrap();
        if let Some(layer) = find_layer(&mut core.steam.profile, &self.layer_id) {
            let subs = layer
                .button_mappings
                .get(&self.selected)
                .map(|m| m.sub_commands.clone())
                .unwrap_or_default();
            layer
                .button_mappings
                .insert(self.selected, KeyMapping { action, sub_commands: subs });
            core.profile_rev += 1;
        }
    }

    fn clear_mapping(&mut self) {
        let mut core = self.shared.core.lock().unwrap();
        if let Some(layer) = find_layer(&mut core.steam.profile, &self.layer_id) {
            layer.button_mappings.remove(&self.selected);
            core.profile_rev += 1;
        }
    }

    fn set_action_kind(&mut self, kind: EditActionKind) {
        self.action_type = kind;
        // 无需目标的动作类型立即写入
        let action = match kind {
            EditActionKind::WheelUp => Some(MappedAction::wheel_up()),
            EditActionKind::WheelDown => Some(MappedAction::wheel_down()),
            EditActionKind::LookAround => Some(MappedAction::look_around()),
            EditActionKind::MouseMove => Some(MappedAction::mouse_move()),
            _ => None,
        };
        if let Some(a) = action {
            self.write_action(a);
        }
    }

    fn set_key_target(&mut self, key: i32) {
        self.write_action(MappedAction::keyboard_key(key));
    }

    fn set_mouse_target(&mut self, mb: MouseButton) {
        let action = match self.action_type {
            EditActionKind::Mouse => MappedAction::mouse_click(mb),
            EditActionKind::MouseToggle => MappedAction::mouse_toggle(mb),
            _ => MappedAction::mouse_click(mb),
        };
        self.write_action(action);
    }

    fn set_layer_target(&mut self, layer_name: &str) {
        self.write_action(MappedAction::switch_layer(layer_name));
    }

    fn toggle_sub(&mut self, key: i32) {
        let mut core = self.shared.core.lock().unwrap();
        if let Some(layer) = find_layer(&mut core.steam.profile, &self.layer_id) {
            if let Some(m) = layer.button_mappings.get_mut(&self.selected) {
                if let Some(pos) = m.sub_commands.iter().position(|&k| k == key) {
                    m.sub_commands.remove(pos);
                } else if m.sub_commands.len() < KeyMapping::MAX_SUB_COMMANDS {
                    m.sub_commands.push(key);
                }
            }
            core.profile_rev += 1;
        }
    }

    /// 读取当前选中按钮的映射信息（渲染用）
    fn read_mapping(&self) -> (Option<MappedAction>, Vec<i32>) {
        let core = self.shared.core.lock().unwrap();
        let layer = find_layer_ref(&core.steam.profile, &self.layer_id);
        match layer.and_then(|l| l.button_mappings.get(&self.selected)) {
            Some(m) => (Some(m.action.clone()), m.sub_commands.clone()),
            None => (None, Vec::new()),
        }
    }

    /// 读取层名与可选层列表（切层目标）
    fn read_layer_context(&self) -> (String, Vec<(String, String)>) {
        let core = self.shared.core.lock().unwrap();
        let name = if self.layer_id == "Common" {
            "公共层".to_string()
        } else {
            find_layer_ref(&core.steam.profile, &self.layer_id)
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
        (name, layers)
    }
}

impl Render for LayerEditView {
    fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let (layer_name, switch_targets) = self.read_layer_context();
        let (current, subs) = self.read_mapping();

        // ---- 左侧：手柄按钮网格 ----
        let mut grid = div().flex().flex_col().gap_1().w(px(180.0));
        for b in all_controller_buttons() {
            let is_pressed = self.held.contains(&b);
            let is_selected = self.selected == b;
            let bg = if is_selected {
                ACCENT
            } else if is_pressed {
                WARN
            } else {
                BG_INSET
            };
            let fg = if is_selected || is_pressed { BG_DEEP } else { TEXT };
            let b_copy = b;
            let btn = div()
                .id(SharedString::from(controller_button_name(b)))
                .px(px(8.0))
                .py(px(4.0))
                .rounded_md()
                .cursor_pointer()
                .bg(rgb(bg))
                .text_color(rgb(fg))
                .text_sm()
                .on_mouse_down(
                    gpui::MouseButton::Left,
                    cx.listener(move |this, _: &MouseDownEvent, _w, cx| {
                        this.select_button(b_copy);
                        cx.notify();
                    }),
                )
                .child(controller_button_display_name(b));
            grid = grid.child(btn);
        }

        // ---- 右侧：编辑区 ----
        let desc = match &current {
            Some(a) => describe_action(a),
            None => "（无映射）".to_string(),
        };

        // 动作类型 chips
        let kind_defs: [(EditActionKind, &str); 8] = [
            (EditActionKind::Keyboard, "键盘"),
            (EditActionKind::Mouse, "鼠标点击"),
            (EditActionKind::MouseToggle, "鼠标长按"),
            (EditActionKind::WheelUp, "滚轮上"),
            (EditActionKind::WheelDown, "滚轮下"),
            (EditActionKind::SwitchLayer, "切层"),
            (EditActionKind::LookAround, "视角控制"),
            (EditActionKind::MouseMove, "鼠标移动"),
        ];
        let mut kind_row = div().flex().flex_row().flex_wrap().gap_1();
        for (kind, label) in kind_defs {
            let active = self.action_type == kind;
            let btn = div()
                .id(SharedString::from(format!("kind-{:?}", kind)))
                .px(px(8.0))
                .py(px(3.0))
                .rounded_md()
                .cursor_pointer()
                .bg(rgb(if active { ACCENT } else { BG_INSET }))
                .text_color(rgb(if active { BG_DEEP } else { TEXT }))
                .text_sm()
                .on_mouse_down(
                    gpui::MouseButton::Left,
                    cx.listener(move |this, _: &MouseDownEvent, _w, cx| {
                        this.set_action_kind(kind);
                        cx.notify();
                    }),
                )
                .child(label);
            kind_row = kind_row.child(btn);
        }

        // 目标选择区（上下文相关）
        let mut target = div().flex().flex_col().gap_1();
        match self.action_type {
            EditActionKind::Keyboard => {
                target = target.child(
                    div()
                        .text_sm()
                        .text_color(rgb(TEXT_DIM))
                        .child("选择按键"),
                );
                let mut keys = div().flex().flex_row().flex_wrap().gap_1();
                for (code, label) in COMMON_KEYS {
                    let btn = div()
                        .id(SharedString::from(format!("key-{}", code)))
                        .px(px(8.0))
                        .py(px(3.0))
                        .rounded_md()
                        .cursor_pointer()
                        .bg(rgb(BG_INSET))
                        .text_color(rgb(TEXT))
                        .text_sm()
                        .on_mouse_down(
                            gpui::MouseButton::Left,
                            cx.listener(move |this, _: &MouseDownEvent, _w, cx| {
                                this.set_key_target(code);
                                cx.notify();
                            }),
                        )
                        .child(label);
                    keys = keys.child(btn);
                }
                target = target.child(keys);
            }
            EditActionKind::Mouse | EditActionKind::MouseToggle => {
                target = target.child(
                    div()
                        .text_sm()
                        .text_color(rgb(TEXT_DIM))
                        .child("选择鼠标键"),
                );
                let mut keys = div().flex().flex_row().flex_wrap().gap_1();
                for mb in [
                    MouseButton::Left,
                    MouseButton::Right,
                    MouseButton::Middle,
                    MouseButton::Forward,
                    MouseButton::Back,
                ] {
                    let btn = div()
                        .id(SharedString::from(mouse_button_name(mb)))
                        .px(px(8.0))
                        .py(px(3.0))
                        .rounded_md()
                        .cursor_pointer()
                        .bg(rgb(BG_INSET))
                        .text_color(rgb(TEXT))
                        .text_sm()
                        .on_mouse_down(
                            gpui::MouseButton::Left,
                            cx.listener(move |this, _: &MouseDownEvent, _w, cx| {
                                this.set_mouse_target(mb);
                                cx.notify();
                            }),
                        )
                        .child(mouse_button_display_name(mb));
                    keys = keys.child(btn);
                }
                target = target.child(keys);
            }
            EditActionKind::SwitchLayer => {
                target = target.child(
                    div()
                        .text_sm()
                        .text_color(rgb(TEXT_DIM))
                        .child("选择目标层"),
                );
                let mut keys = div().flex().flex_row().flex_wrap().gap_1();
                for (lid, lname) in switch_targets.iter() {
                    let lname = lname.clone();
                    let lname_disp = layer_display_name(&lname);
                    let btn = div()
                        .id(SharedString::from(format!("switch-{}", lid)))
                        .px(px(8.0))
                        .py(px(3.0))
                        .rounded_md()
                        .cursor_pointer()
                        .bg(rgb(BG_INSET))
                        .text_color(rgb(TEXT))
                        .text_sm()
                        .on_mouse_down(
                            gpui::MouseButton::Left,
                            cx.listener(move |this, _: &MouseDownEvent, _w, cx| {
                                this.set_layer_target(&lname);
                                cx.notify();
                            }),
                        )
                        .child(lname_disp);
                    keys = keys.child(btn);
                }
                target = target.child(keys);
            }
            _ => {
                target = target.child(
                    div()
                        .text_sm()
                        .text_color(rgb(TEXT_FAINT))
                        .child("该动作无需额外目标，点击上方类型即生效"),
                );
            }
        }

        // 子命令区
        let mut sub_row = div().flex().flex_row().flex_wrap().gap_1();
        for (code, label) in COMMON_KEYS {
            let active = subs.contains(&code);
            let btn = div()
                .id(SharedString::from(format!("sub-{}", code)))
                .px(px(8.0))
                .py(px(3.0))
                .rounded_md()
                .cursor_pointer()
                .bg(rgb(if active { ACCENT_DIM } else { BG_INSET }))
                .text_color(rgb(if active { BG_DEEP } else { TEXT }))
                .text_sm()
                .on_mouse_down(
                    gpui::MouseButton::Left,
                    cx.listener(move |this, _: &MouseDownEvent, _w, cx| {
                        this.toggle_sub(code);
                        cx.notify();
                    }),
                )
                .child(label);
            sub_row = sub_row.child(btn);
        }

        let clear_btn = div()
            .id("clear")
            .px(px(12.0))
            .py(px(6.0))
            .rounded_md()
            .cursor_pointer()
            .bg(rgb(DANGER))
            .text_color(rgb(BG_DEEP))
            .text_sm()
            .on_mouse_down(
                gpui::MouseButton::Left,
                cx.listener(|this, _: &MouseDownEvent, _w, cx| {
                    this.clear_mapping();
                    cx.notify();
                }),
            )
            .child("清除映射");

        let subs_text = if subs.is_empty() {
            "无子命令".to_string()
        } else {
            subs.iter().map(|&k| key_code_to_name(k)).collect::<Vec<_>>().join(" + ")
        };

        let right_col = div()
            .flex()
            .flex_col()
            .gap_2()
            .flex_grow()
            .child(
                div()
                    .flex()
                    .flex_row()
                    .items_center()
                    .justify_between()
                    .child(
                        div()
                            .text_lg()
                            .text_color(rgb(TEXT))
                            .child(format!("编辑层: {}", layer_display_name(&layer_name))),
                    ),
            )
            .child(
                div()
                    .flex()
                    .flex_row()
                    .items_center()
                    .gap_2()
                    .child(
                        div()
                            .text_sm()
                            .text_color(rgb(TEXT_DIM))
                            .child(format!(
                                "当前: {} ({})",
                                controller_button_display_name(self.selected),
                                desc
                            )),
                    )
                    .child(clear_btn),
            )
            .child(
                div()
                    .text_sm()
                    .text_color(rgb(TEXT_DIM))
                    .child("动作类型"),
            )
            .child(kind_row)
            .child(target)
            .child(
                div()
                    .text_sm()
                    .text_color(rgb(TEXT_DIM))
                    .child(format!("子命令（最多{}个，当前: {}）", KeyMapping::MAX_SUB_COMMANDS, subs_text)),
            )
            .child(sub_row)
            .child(
                div()
                    .text_sm()
                    .text_color(rgb(TEXT_FAINT))
                    .child("说明：切层动作由公共层的 SwitchLayer 映射驱动；操作层子命令用于组合键（如 Ctrl+1）。"),
            );

        div()
            .flex()
            .flex_row()
            .size_full()
            .bg(rgb(BG))
            .p_3()
            .gap_3()
            .child(grid)
            .child(
                div()
                    .w_px()
                    .h_full()
                    .bg(rgb(BORDER)),
            )
            .child(right_col)
    }
}
