// =====================================================================
// overlay.rs —— 悬浮窗（信息悬浮层）
//
// 等效 Qt 版 OverlayWidget：始终置顶的透明小窗，显示
//   操作集名 / 当前层 / 连接状态 / 按下按键 / L3 锁存警示
// 展开时可查看当前操作集下所有层的映射概览（简化：显示按下按键）。
//
// 窗口属性：PopUp（置顶）+ 透明背景 + 隐藏系统标题栏。
// =====================================================================

use crate::core::input_types::controller_button_display_name;
use crate::ui::shared::AppShared;
use crate::ui::theme::*;
use crate::ui::theme::rgb; // 显式导入：消除与 gpui::rgb 的 glob 歧义
use gpui::*;
use std::sync::Arc;

pub struct OverlayView {
    shared: Arc<AppShared>,
    // ---- 显示缓存（50ms 轮询）----
    set_name: SharedString,
    layer_name: SharedString,
    connected: bool,
    pressed: Vec<SharedString>,
    mouse_toggle: bool,
    expanded: bool,
}

impl OverlayView {
    pub fn new(shared: Arc<AppShared>, _window: &mut Window, cx: &mut Context<Self>) -> Self {
        let mut this = Self {
            shared,
            set_name: "默认操作集".into(),
            layer_name: "Common".into(),
            connected: false,
            pressed: Vec::new(),
            mouse_toggle: false,
            expanded: false,
        };
        this.start_polling(cx);
        this
    }

    fn start_polling(&mut self, cx: &mut Context<Self>) {
        let shared = Arc::clone(&self.shared);
        cx.spawn(async move |this, mut cx| {
            loop {
                Timer::after(std::time::Duration::from_millis(50)).await;
                let mut changed = false;
                let _ = this.update(cx, |this, cx| {
                    let core = shared.core.lock().unwrap();
                    let set = core.steam.profile.active_operation_set_name();
                    let layer = core.steam.active_layer_name().to_string();
                    let connected = core.connected;
                    let mut pressed: Vec<SharedString> = core
                        .steam
                        .held_buttons()
                        .iter()
                        .map(|b| SharedString::from(controller_button_display_name(*b).to_string()))
                        .collect();
                    pressed.sort();
                    let toggle = !core.mapper.toggled_mouse_buttons.is_empty();
                    drop(core);
                    if this.set_name.as_ref() != set {
                        this.set_name = set.into();
                        changed = true;
                    }
                    if this.layer_name.as_ref() != layer {
                        this.layer_name = layer.into();
                        changed = true;
                    }
                    if this.connected != connected {
                        this.connected = connected;
                        changed = true;
                    }
                    if this.pressed != pressed {
                        this.pressed = pressed;
                        changed = true;
                    }
                    if this.mouse_toggle != toggle {
                        this.mouse_toggle = toggle;
                        changed = true;
                    }
                    if changed {
                        cx.notify();
                    }
                });
            }
        })
        .detach();
    }

    fn toggle_expanded(&mut self) {
        self.expanded = !self.expanded;
    }
}

impl Render for OverlayView {
    fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        // 标题栏：点击切换展开/收起
        let title = div()
            .id("overlay-title")
            .flex()
            .flex_row()
            .items_center()
            .gap_2()
            .cursor_pointer()
            .on_mouse_down(
                MouseButton::Left,
                cx.listener(|this, _: &MouseDownEvent, _w, cx| {
                    this.toggle_expanded();
                    cx.notify();
                }),
            )
            .child(
                div()
                    .text_size(px(16.0))
                    .text_color(rgb(ACCENT))
                    .child("操作集: ".to_string() + self.set_name.as_ref()),
            )
            .child(
                div()
                    .text_size(px(16.0))
                    .text_color(rgb(TEXT))
                    .child("当前层: ".to_string() + self.layer_name.as_ref()),
            );

        // 连接状态点
        let conn = div()
            .size(px(10.0))
            .rounded_full()
            .bg(rgb(if self.connected { OK } else { TEXT_FAINT }));

        // 按下按键
        let mut press_row = div().flex().flex_row().flex_wrap().gap_2();
        if self.pressed.is_empty() {
            press_row = press_row.child(
                div()
                    .text_sm()
                    .text_color(rgb(TEXT_FAINT))
                    .child("无按键按下"),
            );
        } else {
            for name in &self.pressed {
                press_row = press_row.child(
                    div()
                        .px(px(8.0))
                        .py(px(2.0))
                        .rounded_sm()
                        .bg(rgb(ACCENT))
                        .text_color(rgb(BG_DEEP))
                        .text_sm()
                        .child(name.clone()),
                );
            }
        }

        let mut content = div()
            .flex()
            .flex_col()
            .gap_2()
            .child(title)
            .child(
                div()
                    .flex()
                    .flex_row()
                    .items_center()
                    .gap_2()
                    .child(conn)
                    .child(
                        div()
                            .text_sm()
                            .text_color(rgb(TEXT_DIM))
                            .child(if self.connected { "手柄已连接" } else { "手柄未连接" }),
                    ),
            )
            .child(press_row);

        // L3 锁存警示（橙色高亮）
        if self.mouse_toggle {
            content = content.child(
                div()
                    .px(px(8.0))
                    .py(px(3.0))
                    .rounded_sm()
                    .bg(rgb(WARN))
                    .text_color(rgb(BG_DEEP))
                    .text_sm()
                    .child("⚠ 鼠标长按锁存中，再按一次解除"),
            );
        }

        // 展开：显示按下按键明细（已在上方）+ 提示
        if self.expanded {
            content = content.child(
                div()
                    .text_sm()
                    .text_color(rgb(TEXT_FAINT))
                    .child("（点击标题收起）"),
            );
        }

        div()
            .flex()
            .flex_col()
            .p_3()
            .gap_2()
            .bg(argb(0xd02b2d31)) // 半透明深色背景
            .border_1()
            .border_color(rgb(if self.mouse_toggle { WARN } else { ACCENT }))
            .rounded_lg()
            .child(content)
    }
}
