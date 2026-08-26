// =====================================================================
// overlay.rs —— 悬浮窗（信息悬浮层）Dioxus 版
//
// 独立线程 + 独立窗口，始终置顶。显示：
//   操作集名 / 当前层 / 连接状态 / 按下按键 / L3 锁存警示
// 点击标题栏展开/收起：展开时显示当前层映射列表 + 摇杆说明。
//
// 主窗口通过 AppShared.overlay_visible 原子标志控制内容显隐
// （隐藏时仅渲染占位文本）。
// =====================================================================

use crate::core::input_types::{all_controller_buttons, controller_button_display_name};
use crate::ui::shared::AppShared;
use crate::ui::theme::*;
use dioxus::desktop::{use_window, LogicalSize};
use dioxus::prelude::*;
use futures_util::stream::StreamExt;
use std::sync::atomic::Ordering;
use std::sync::Arc;
use std::time::Duration;

/// 显示缓存（50ms 轮询）
#[derive(Clone, Default)]
struct OverlayUi {
    set_name: String,
    layer_name: String,
    connected: bool,
    pressed: Vec<String>,
    mouse_toggle: bool,
    /// 展开时显示的当前层映射：(按钮名, 描述, 是否按下)
    mappings: Vec<(String, String, bool)>,
}

fn shared() -> Arc<AppShared> {
    crate::SHARED.get().expect("SHARED not set").clone()
}

#[component]
pub fn OverlayApp() -> Element {
    let s = shared();
    let mut ui = use_signal(OverlayUi::default);
    let mut expanded = use_signal(|| false);
    let window = use_window();

    // 50ms 轮询：后台线程读 core 快照 → UnboundedSender；
    // use_coroutine 在 UI 线程（dioxus runtime）接收快照并写 Signal 触发重绘
    let poll = use_coroutine(move |mut rx: UnboundedReceiver<OverlayUi>| async move {
        while let Some(snap) = rx.next().await {
            ui.set(snap);
        }
    });
    let poll_tx = poll.tx();
    let s_poll = s.clone();
    use_effect(move || {
        let s = s_poll.clone();
        let poll_tx = poll_tx.clone();
        std::thread::spawn(move || loop {
            std::thread::sleep(Duration::from_millis(50));
            let snapshot = {
                let core = s.core.lock().unwrap();
                let set = core.steam.profile.active_operation_set_name();
                let layer = core.steam.active_layer_name().to_string();
                let connected = core.connected;
                let mut pressed: Vec<String> = core
                    .steam
                    .held_buttons()
                    .iter()
                    .map(|b| controller_button_display_name(*b).to_string())
                    .collect();
                pressed.sort();
                let toggle = !core.mapper.toggled_mouse_buttons.is_empty();
                // 当前层映射列表（展开时显示）：最后激活的操作层，否则公共层
                let active_layers = core.steam.get_active_layers();
                let layer_ref = if active_layers.is_empty() {
                    core.steam.profile.common_layer()
                } else {
                    active_layers.last().copied()
                };
                let held = core.steam.held_buttons();
                let mut mappings: Vec<(String, String, bool)> = Vec::new();
                if let Some(layer_ref) = layer_ref {
                    for b in all_controller_buttons() {
                        if let Some(m) = layer_ref.get_mapping(b) {
                            mappings.push((
                                controller_button_display_name(b).to_string(),
                                m.describe(),
                                held.contains(&b),
                            ));
                        }
                    }
                }
                OverlayUi {
                    set_name: set,
                    layer_name: layer,
                    connected,
                    pressed,
                    mouse_toggle: toggle,
                    mappings,
                }
            };
            let _ = poll_tx.unbounded_send(snapshot);
        });
    });

    let is_hidden = !s.overlay_visible.load(Ordering::SeqCst);

    // 点击标题栏展开/收起（同时调整窗口高度）
    let on_toggle = move |_| {
        let next = !expanded();
        expanded.set(next);
        let h = if next { 480.0 } else { 220.0 };
        window.set_inner_size(LogicalSize::new(360.0, h));
    };

    let state = ui.read();

    // 按下按键行
    let press_row = rsx! {
        if state.pressed.is_empty() {
            div { style: format!("font-size:12px;color:{};", hex(TEXT_FAINT)), "无按键按下" }
        } else {
            for name in &state.pressed {
                div {
                    key: "{name}",
                    style: format!(
                        "background:{};color:{};border-radius:4px;padding:2px 8px;font-size:12px;white-space:nowrap;",
                        hex(ACCENT), hex(BG_DEEP)
                    ),
                    "{name}",
                }
            }
        }
    };

    let border_color = if state.mouse_toggle { hex(WARN) } else { hex(ACCENT) };

    rsx! {
        div {
            style: format!(
                "{}background:rgba(43,45,49,0.82);border:1px solid {};border-radius:10px;\
                 padding:12px;display:flex;flex-direction:column;gap:8px;overflow-y:auto;",
                root_css(), border_color
            ),
            if is_hidden {
                div { style: format!("color:{};font-size:14px;", hex(TEXT_FAINT)), "悬浮窗已隐藏" }
            } else {
                // 标题栏：点击展开/收起
                div {
                    style: "display:flex;flex-direction:row;align-items:center;gap:12px;cursor:pointer;user-select:none;",
                    onclick: on_toggle,
                    div { style: format!("font-size:15px;color:{};", hex(ACCENT)), {format!("操作集: {}", state.set_name)} }
                    div { style: format!("font-size:15px;color:{};", hex(TEXT)), {format!("当前层: {}", state.layer_name)} }
                }
                div {
                    style: "display:flex;flex-direction:row;align-items:center;gap:8px;",
                    div {
                        style: format!(
                            "width:10px;height:10px;border-radius:50%;background:{};",
                            if state.connected { hex(OK) } else { hex(TEXT_FAINT) }
                        ),
                    }
                    div {
                        style: format!("font-size:12px;color:{};", hex(TEXT_DIM)),
                        if state.connected { "手柄已连接" } else { "手柄未连接" },
                    }
                }
                div { style: "display:flex;flex-direction:row;flex-wrap:wrap;gap:6px;", {press_row} }
                if state.mouse_toggle {
                    div {
                        style: format!(
                            "background:{};color:{};border-radius:4px;padding:3px 8px;font-size:12px;",
                            hex(WARN), hex(BG_DEEP)
                        ),
                        "⚠ 鼠标长按锁存中，再按一次解除",
                    }
                }
                if *expanded.read() {
                    div { style: format!("font-size:12px;color:{};", hex(TEXT_DIM)), {format!("当前层映射: {}", state.layer_name)} }
                    if state.mappings.is_empty() {
                        div { style: format!("font-size:12px;color:{};", hex(TEXT_FAINT)), "（无映射）" }
                    } else {
                        for (btn, desc, held) in &state.mappings {
                            div {
                                key: "{btn}",
                                style: "display:flex;flex-direction:row;gap:8px;align-items:center;",
                                div {
                                    style: format!(
                                        "flex-shrink:0;font-size:12px;color:{};",
                                        if *held { hex(WARN) } else { hex(ACCENT) }
                                    ),
                                    "{btn}",
                                }
                                div { style: format!("font-size:12px;color:{};", hex(TEXT_DIM)), "→" }
                                div { style: format!("font-size:12px;color:{};", hex(TEXT)), "{desc}" }
                            }
                        }
                    }
                    // 摇杆映射
                    div {
                        style: "display:flex;flex-direction:row;gap:8px;",
                        div { style: format!("flex-shrink:0;font-size:12px;color:{};", hex(ACCENT)), "左摇杆" }
                        div { style: format!("font-size:12px;color:{};", hex(TEXT_DIM)), "→" }
                        div { style: format!("font-size:12px;color:{};", hex(TEXT)), "WASD 移动" }
                    }
                    div {
                        style: "display:flex;flex-direction:row;gap:8px;",
                        div { style: format!("flex-shrink:0;font-size:12px;color:{};", hex(ACCENT)), "右摇杆" }
                        div { style: format!("font-size:12px;color:{};", hex(TEXT_DIM)), "→" }
                        div { style: format!("font-size:12px;color:{};", hex(TEXT)), "视角控制" }
                    }
                    div { style: format!("font-size:12px;color:{};", hex(TEXT_FAINT)), "（点击标题收起）" }
                }
            }
        }
    }
}
