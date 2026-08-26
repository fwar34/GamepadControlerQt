// =====================================================================
// layer_edit.rs —— 层编辑视图（Dioxus 版）
//
// 编辑公共层或操作层中每个手柄按钮的映射：
//   - 左侧手柄按钮网格，点击选中；按下的按钮实时高亮
//   - 右侧设置动作类型（键盘/鼠标/长按/滚轮/切层/视角/移动）
//     与目标（键/鼠标键/层），以及最多 3 个子命令（组合键）
//
// 编辑直接写入共享 profile（AppCore.profile_rev++ 触发刷新）。
// =====================================================================

use crate::core::input_types::{
    all_controller_buttons, android_key, controller_button_display_name, controller_button_name,
    key_code_to_name, layer_display_name, mouse_button_display_name, ControllerButton, MouseButton,
};
use crate::core::mapping_types::{
    ActionType, ControllerProfile, KeyMapping, MappedAction, OperationLayer,
};
use crate::ui::theme::*;
use dioxus::prelude::*;
use futures_util::stream::StreamExt;
use std::time::Duration;

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
        ActionType::MouseToggle => {
            format!("鼠标长按: {}", mouse_button_display_name(a.mouse_button))
        }
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

/// 访问全局共享状态（见 main.rs）
fn shared() -> std::sync::Arc<crate::ui::shared::AppShared> {
    crate::SHARED.get().expect("SHARED not set").clone()
}

// ---- 编辑动作（直接写共享 core；rev 信号驱动即时重绘）----

fn select_button(
    layer_id: &str,
    b: ControllerButton,
    mut selected: Signal<ControllerButton>,
    mut action_type: Signal<EditActionKind>,
) {
    selected.set(b);
    let s = shared();
    let core = s.core.lock().unwrap();
    if let Some(layer) = find_layer_ref(&core.steam.profile, layer_id) {
        if let Some(m) = layer.button_mappings.get(&b) {
            action_type.set(kind_of(&m.action));
        }
    }
}

fn write_action(
    layer_id: &str,
    b: ControllerButton,
    action: MappedAction,
    mut rev: Signal<u64>,
) {
    let s = shared();
    let mut core = s.core.lock().unwrap();
    if let Some(layer) = find_layer(&mut core.steam.profile, layer_id) {
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
    rev.set(rev() + 1);
}

fn clear_mapping(layer_id: &str, selected: Signal<ControllerButton>, mut rev: Signal<u64>) {
    let s = shared();
    let mut core = s.core.lock().unwrap();
    if let Some(layer) = find_layer(&mut core.steam.profile, layer_id) {
        layer.button_mappings.remove(&selected());
        core.profile_rev += 1;
    }
    rev.set(rev() + 1);
}

fn set_action_kind(
    layer_id: &str,
    kind: EditActionKind,
    selected: Signal<ControllerButton>,
    mut action_type: Signal<EditActionKind>,
    mut rev: Signal<u64>,
) {
    action_type.set(kind);
    // 无需目标的动作类型立即写入
    let action = match kind {
        EditActionKind::WheelUp => Some(MappedAction::wheel_up()),
        EditActionKind::WheelDown => Some(MappedAction::wheel_down()),
        EditActionKind::LookAround => Some(MappedAction::look_around()),
        EditActionKind::MouseMove => Some(MappedAction::mouse_move()),
        _ => None,
    };
    if let Some(a) = action {
        write_action(layer_id, selected(), a, rev);
    }
}

fn set_key_target(
    layer_id: &str,
    key: i32,
    selected: Signal<ControllerButton>,
    rev: Signal<u64>,
) {
    write_action(layer_id, selected(), MappedAction::keyboard_key(key), rev);
}

fn set_mouse_target(
    layer_id: &str,
    mb: MouseButton,
    selected: Signal<ControllerButton>,
    action_type: Signal<EditActionKind>,
    rev: Signal<u64>,
) {
    let action = match action_type() {
        EditActionKind::Mouse => MappedAction::mouse_click(mb),
        EditActionKind::MouseToggle => MappedAction::mouse_toggle(mb),
        _ => MappedAction::mouse_click(mb),
    };
    write_action(layer_id, selected(), action, rev);
}

fn set_layer_target(
    layer_id: &str,
    target: &str,
    selected: Signal<ControllerButton>,
    rev: Signal<u64>,
) {
    write_action(layer_id, selected(), MappedAction::switch_layer(target), rev);
}

fn toggle_sub(layer_id: &str, key: i32, selected: Signal<ControllerButton>, mut rev: Signal<u64>) {
    let s = shared();
    let mut core = s.core.lock().unwrap();
    if let Some(layer) = find_layer(&mut core.steam.profile, layer_id) {
        if let Some(m) = layer.button_mappings.get_mut(&selected()) {
            if let Some(pos) = m.sub_commands.iter().position(|&k| k == key) {
                m.sub_commands.remove(pos);
            } else if m.sub_commands.len() < KeyMapping::MAX_SUB_COMMANDS {
                m.sub_commands.push(key);
            }
        }
        core.profile_rev += 1;
    }
    rev.set(rev() + 1);
}

#[component]
pub fn LayerEditView(layer_id: String, on_back: EventHandler<()>) -> Element {
    let lid = layer_id.clone();

    let selected = use_signal(|| ControllerButton::A);
    let action_type = use_signal(|| EditActionKind::Keyboard);
    let mut held = use_signal(Vec::<ControllerButton>::new);
    let mut rev = use_signal(|| 0u64);

    // 50ms 轮询：后台线程读 core → UnboundedSender；
    // use_coroutine 在 UI 线程（dioxus runtime）接收并写 Signal（按下高亮 + 修订号）
    let poll = use_coroutine(
        move |mut rx: UnboundedReceiver<(Vec<ControllerButton>, u64)>| async move {
            while let Some((h, r)) = rx.next().await {
                if h != *held.peek() {
                    held.set(h);
                }
                if r != *rev.peek() {
                    rev.set(r);
                }
            }
        },
    );
    let poll_tx = poll.tx();
    use_effect(move || {
        let poll_tx = poll_tx.clone();
        std::thread::spawn(move || loop {
            std::thread::sleep(Duration::from_millis(50));
            let (h, r) = {
                let s = shared();
                let core = s.core.lock().unwrap();
                (
                    core.steam.held_buttons().iter().copied().collect::<Vec<_>>(),
                    core.profile_rev,
                )
            };
            let _ = poll_tx.unbounded_send((h, r));
        });
    });

    // 订阅修订号：编辑写入 core 后 profile_rev 变化 → rev 更新 → 触发本组件重绘
    let _ = rev();

    // ---- 从 core 读取：层名、切层目标、当前选中按钮映射 ----
    let (layer_name, switch_targets) = {
        let s = shared();
        let core = s.core.lock().unwrap();
        let name = if lid == "Common" {
            "公共层".to_string()
        } else {
            find_layer_ref(&core.steam.profile, &lid)
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
    };
    let (current, subs) = {
        let s = shared();
        let core = s.core.lock().unwrap();
        let b = *selected.read();
        match find_layer_ref(&core.steam.profile, &lid).and_then(|l| l.button_mappings.get(&b)) {
            Some(m) => (Some(m.action.clone()), m.sub_commands.clone()),
            None => (None, Vec::new()),
        }
    };

    let desc = match &current {
        Some(a) => describe_action(a),
        None => "（无映射）".to_string(),
    };
    let subs_text = if subs.is_empty() {
        "无子命令".to_string()
    } else {
        subs.iter()
            .map(|&k| key_code_to_name(k))
            .collect::<Vec<_>>()
            .join(" + ")
    };

    // ---- 动作类型 chips ----
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

    // ---- 目标选择区（上下文相关）----
    let target_section = match *action_type.read() {
        EditActionKind::Keyboard => rsx! {
            div { style: format!("font-size:12px;color:{};", hex(TEXT_DIM)), "选择按键" }
            div { style: "display:flex;flex-direction:row;flex-wrap:wrap;gap:6px;",
                {COMMON_KEYS.iter().map(|(code, label)| {
                    let lid_c = lid.clone();
                    rsx! {
                        div {
                            key: "{label}",
                            style: chip_css(false),
                            onclick: move |_| set_key_target(&lid_c, *code, selected, rev),
                            "{label}",
                        }
                    }
                })}
            }
        },
        EditActionKind::Mouse | EditActionKind::MouseToggle => rsx! {
            div { style: format!("font-size:12px;color:{};", hex(TEXT_DIM)), "选择鼠标键" }
            div { style: "display:flex;flex-direction:row;flex-wrap:wrap;gap:6px;",
                {[
                    MouseButton::Left,
                    MouseButton::Right,
                    MouseButton::Middle,
                    MouseButton::Forward,
                    MouseButton::Back,
                ].into_iter().map(|mb| {
                    let lid_c = lid.clone();
                    rsx! {
                        div {
                            key: "{mb:?}",
                            style: chip_css(false),
                            onclick: move |_| set_mouse_target(&lid_c, mb, selected, action_type, rev),
                            {mouse_button_display_name(mb)},
                        }
                    }
                })}
            }
        },
        EditActionKind::SwitchLayer => rsx! {
            div { style: format!("font-size:12px;color:{};", hex(TEXT_DIM)), "选择目标层" }
            div { style: "display:flex;flex-direction:row;flex-wrap:wrap;gap:6px;",
                {switch_targets.iter().map(|(target_id, target_name)| {
                    let lid_c = lid.clone();
                    let name_c = target_name.clone();
                    let disp = layer_display_name(target_name);
                    rsx! {
                        div {
                            key: "{target_id}",
                            style: chip_css(false),
                            onclick: move |_| set_layer_target(&lid_c, &name_c, selected, rev),
                            "{disp}",
                        }
                    }
                })}
            }
        },
        _ => rsx! {
            div { style: format!("font-size:12px;color:{};", hex(TEXT_FAINT)), "该动作无需额外目标，点击上方类型即生效" }
        },
    };

    rsx! {
        div {
            style: format!("{}display:flex;flex-direction:column;gap:12px;padding:16px;overflow-y:auto;", root_css()),
            // 标题行
            div {
                style: "display:flex;flex-direction:row;align-items:center;gap:12px;",
                div {
                    style: format!("flex:1;font-size:17px;color:{};font-weight:600;", hex(TEXT)),
                    {format!("编辑层: {}", layer_display_name(&layer_name))},
                }
                button { style: btn_css(false), onclick: move |_| on_back.call(()), "返回" }
            }
            div { style: "display:flex;flex-direction:row;gap:16px;flex:1;min-height:0;",
                // 左侧：手柄按钮网格
                div {
                    style: "display:flex;flex-direction:column;gap:6px;width:170px;flex-shrink:0;",
                    {all_controller_buttons().into_iter().map(|b| {
                        let lid_c = lid.clone();
                        rsx! {
                            div {
                                key: "{controller_button_name(b)}",
                                style: format!(
                                    "background:{};border:1px solid {};color:{};border-radius:6px;padding:6px 4px;\
                                     cursor:pointer;font-family:{};font-size:12px;width:100%;text-align:center;\
                                     box-sizing:border-box;user-select:none;",
                                    if *selected.read() == b { hex(ACCENT) }
                                    else if held.read().contains(&b) { hex(WARN) }
                                    else { hex(BG_INSET) },
                                    hex(BORDER),
                                    if *selected.read() == b || held.read().contains(&b) { hex(BG_DEEP) }
                                    else { hex(TEXT) },
                                    FONT
                                ),
                                onclick: move |_| select_button(&lid_c, b, selected, action_type),
                                {controller_button_display_name(b)},
                            }
                        }
                    })}
                }
                // 分隔线
                div { style: "width:1px;align-self:stretch;background:#3f434a;", }
                // 右侧：编辑区
                div { style: "display:flex;flex-direction:column;gap:10px;flex:1;min-width:0;",
                    div {
                        style: "display:flex;flex-direction:row;align-items:center;gap:10px;",
                        div {
                            style: format!("flex:1;font-size:13px;color:{};", hex(TEXT_DIM)),
                            {format!(
                                "当前: {} ({})",
                                controller_button_display_name(*selected.read()),
                                desc
                            )},
                        }
                        button {
                            style: format!(
                                "background:{};border:1px solid {};color:{};border-radius:6px;\
                                 padding:5px 12px;cursor:pointer;font-family:{};font-size:13px;white-space:nowrap;",
                                hex(DANGER), hex(DANGER), hex(BG_DEEP), FONT
                            ),
                            onclick: move |_| clear_mapping(&lid, selected, rev),
                            "清除映射",
                        }
                    }
                    div { style: format!("font-size:13px;color:{};", hex(TEXT_DIM)), "动作类型" }
                    div { style: "display:flex;flex-direction:row;flex-wrap:wrap;gap:6px;",
                        {kind_defs.iter().map(|(kind, label)| {
                            let lid_c = lid.clone();
                            let k = *kind;
                            let active = *action_type.read() == k;
                            rsx! {
                                div {
                                    key: "{k:?}",
                                    style: chip_css(active),
                                    onclick: move |_| set_action_kind(&lid_c, k, selected, action_type, rev),
                                    "{label}",
                                }
                            }
                        })}
                    }
                    {target_section}
                    div {
                        style: format!("font-size:13px;color:{};", hex(TEXT_DIM)),
                        {format!(
                            "子命令（最多{}个，当前: {}）",
                            KeyMapping::MAX_SUB_COMMANDS, subs_text
                        )},
                    }
                    div { style: "display:flex;flex-direction:row;flex-wrap:wrap;gap:6px;",
                        {COMMON_KEYS.iter().map(|(code, label)| {
                            let lid_c = lid.clone();
                            let active = subs.contains(code);
                            rsx! {
                                div {
                                    key: "sub-{label}",
                                    style: chip_css(active),
                                    onclick: move |_| toggle_sub(&lid_c, *code, selected, rev),
                                    "{label}",
                                }
                            }
                        })}
                    }
                    div {
                        style: format!("font-size:12px;color:{};", hex(TEXT_FAINT)),
                        "说明：切层动作由公共层的 SwitchLayer 映射驱动；操作层子命令用于组合键（如 Ctrl+1）。",
                    }
                }
            }
        }
    }
}
