// =====================================================================
// overlay.rs —— 悬浮窗（egui 多 viewport）
//
// 透明置顶无边框小窗：显示当前操作集 / 当前层 / 连接状态 / 按下按键 /
// L3 锁存警示；点击标题或展开按钮切换展开并调整窗口高度；标题行拖动窗口。
//
// 悬浮窗作为主窗口的第二个 viewport 存在（同一事件循环，更可靠）：
// 主窗口按钮翻转 OverlayState::visible，show() 每帧通过 with_visible
// 驱动显隐，无需独立线程。
// =====================================================================

use crate::core::input_types::*;
use crate::ui::shared::AppShared;
use crate::ui::theme::*;
use eframe::egui;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

const W: f32 = 320.0;
const COLLAPSED_H: f32 = 220.0;
const EXPANDED_H: f32 = 480.0;

/// 悬浮窗共享状态（主窗口持有，viewport 渲染闭包访问）
pub struct OverlayState {
    pub visible: AtomicBool,
    pub expanded: bool,
}

impl OverlayState {
    pub fn new() -> Self {
        Self {
            // 临时：默认显示，用于验证显示链路（验证后改回 false）
            visible: AtomicBool::new(true),
            expanded: false,
        }
    }

    pub fn toggle_visible(&self) {
        let new = !self.visible.load(Ordering::SeqCst);
        self.visible.store(new, Ordering::SeqCst);
    }
}

/// 渲染悬浮窗内容（透明圆角卡片）
fn overlay_ui(shared: &Arc<AppShared>, state: &mut OverlayState, ui: &mut egui::Ui) {
    let ctx = ui.ctx().clone();
    let (set_name, layer_name, connected, pressed, mouse_toggle, mappings) = {
        let core = shared.core.lock().unwrap();
        let set_name = core.steam.profile.active_operation_set_name();
        let layer_name = core.steam.active_layer_name().to_string();
        let connected = core.connected;
        let mut pressed: Vec<String> = core
            .steam
            .held_buttons()
            .iter()
            .map(|b| controller_button_display_name(*b).to_string())
            .collect();
        pressed.sort();
        let mouse_toggle = !core.mapper.toggled_mouse_buttons.is_empty();
        let active_layers = core.steam.get_active_layers();
        let layer_ref = if active_layers.is_empty() {
            core.steam.profile.common_layer()
        } else {
            active_layers.last().copied()
        };
        let held = core.steam.held_buttons();
        let mut mappings: Vec<(String, String, bool)> = Vec::new();
        if let Some(lr) = layer_ref {
            for b in all_controller_buttons() {
                if let Some(m) = lr.get_mapping(b) {
                    mappings.push((
                        controller_button_display_name(b).to_string(),
                        m.describe().to_string(),
                        held.contains(&b),
                    ));
                }
            }
        }
        (set_name, layer_name, connected, pressed, mouse_toggle, mappings)
    };

    egui::CentralPanel::no_frame().show(ui, |ui| {
        let rect = ui.max_rect();
        let card = egui::Rect::from_min_size(
            rect.min + egui::vec2(4.0, 4.0),
            egui::vec2(rect.width() - 8.0, rect.height() - 8.0),
        );
        let border = if mouse_toggle { rgb(WARN) } else { rgb(ACCENT) };
        ui.painter().rect(
            card,
            10.0,
            argb(0x2b2d31d9),
            egui::Stroke::new(1.0, border),
            egui::StrokeKind::Inside,
        );
        ui.scope_builder(egui::UiBuilder::new().max_rect(card.shrink(10.0)), |ui| {
            // 标题行（可拖动 / 点击切换展开）
            let title_res = ui
                .horizontal(|ui| {
                    ui.label(
                        egui::RichText::new(format!("操作集: {}", set_name))
                            .color(rgb(ACCENT))
                            .size(15.0),
                    );
                    ui.label(
                        egui::RichText::new(format!("当前层: {}", layer_name))
                            .color(rgb(TEXT))
                            .size(15.0),
                    );
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        let txt = if state.expanded { "收起" } else { "展开" };
                        if btn_ghost(ui, txt).clicked() {
                            state.expanded = !state.expanded;
                            let h = if state.expanded { EXPANDED_H } else { COLLAPSED_H };
                            ctx.send_viewport_cmd(egui::ViewportCommand::InnerSize(egui::vec2(
                                W, h,
                            )));
                        }
                    });
                })
                .response
                .interact(egui::Sense::click_and_drag());
            if title_res.dragged() {
                ctx.send_viewport_cmd(egui::ViewportCommand::StartDrag);
            } else if title_res.clicked() {
                state.expanded = !state.expanded;
                let h = if state.expanded { EXPANDED_H } else { COLLAPSED_H };
                ctx.send_viewport_cmd(egui::ViewportCommand::InnerSize(egui::vec2(W, h)));
            }

            // 连接状态
            ui.horizontal(|ui| {
                let dot = if connected { rgb(OK) } else { rgb(TEXT_FAINT) };
                ui.label(egui::RichText::new("●").color(dot).size(14.0));
                ui.label(
                    egui::RichText::new(if connected {
                        "手柄已连接"
                    } else {
                        "手柄未连接"
                    })
                    .color(rgb(TEXT))
                    .size(13.0)
                    .strong(),
                );
            });

            // 按下按键
            if pressed.is_empty() {
                ui.label(
                    egui::RichText::new("无按键按下").color(rgb(TEXT_FAINT)).size(13.0),
                );
            } else {
                ui.horizontal_wrapped(|ui| {
                    for p in &pressed {
                        ui.add(
                            egui::Button::new(
                                egui::RichText::new(p).color(rgb(BG_DEEP)).size(13.0),
                            )
                            .fill(rgb(ACCENT))
                            .corner_radius(4.0),
                        );
                    }
                });
            }

            // L3 锁存警示
            if mouse_toggle {
                ui.add(
                    egui::Button::new(
                        egui::RichText::new("⚠ 鼠标长按锁存中，再按一次解除")
                            .color(rgb(BG_DEEP))
                            .size(13.0),
                    )
                    .fill(rgb(WARN))
                    .corner_radius(4.0),
                );
            }

            // 展开：当前层映射列表
            if state.expanded {
                ui.add_space(4.0);
                ui.separator();
                ui.add_space(4.0);
                ui.label(
                    egui::RichText::new(format!("当前层映射: {}", layer_name))
                        .color(rgb(TEXT_DIM))
                        .size(13.0),
                );
                if mappings.is_empty() {
                    ui.label(
                        egui::RichText::new("（无映射）")
                            .color(rgb(TEXT_FAINT))
                            .size(13.0),
                    );
                }
                egui::ScrollArea::vertical()
                    .id_salt("overlay_mappings")
                    .max_height(EXPANDED_H - 170.0)
                    .show(ui, |ui| {
                        for (button, desc, held) in &mappings {
                            ui.horizontal(|ui| {
                                let color = if *held { rgb(WARN) } else { rgb(ACCENT) };
                                ui.add_sized(
                                    [64.0, 18.0],
                                    egui::Label::new(
                                        egui::RichText::new(button).color(color).size(13.0),
                                    ),
                                );
                                ui.label(
                                    egui::RichText::new("→").color(rgb(TEXT_DIM)).size(13.0),
                                );
                                ui.label(
                                    egui::RichText::new(desc).color(rgb(TEXT)).size(13.0),
                                );
                            });
                        }
                    });
                ui.add_space(4.0);
                ui.horizontal(|ui| {
                    ui.add_sized(
                        [64.0, 18.0],
                        egui::Label::new(egui::RichText::new("左摇杆").color(rgb(ACCENT)).size(13.0)),
                    );
                    ui.label(egui::RichText::new("→").color(rgb(TEXT_DIM)).size(13.0));
                    ui.label(egui::RichText::new("WASD 移动").color(rgb(TEXT)).size(13.0));
                });
                ui.horizontal(|ui| {
                    ui.add_sized(
                        [64.0, 18.0],
                        egui::Label::new(egui::RichText::new("右摇杆").color(rgb(ACCENT)).size(13.0)),
                    );
                    ui.label(egui::RichText::new("→").color(rgb(TEXT_DIM)).size(13.0));
                    ui.label(egui::RichText::new("视角控制").color(rgb(TEXT)).size(13.0));
                });
                ui.label(
                    egui::RichText::new("（点击标题收起）")
                        .color(rgb(TEXT_FAINT))
                        .size(13.0),
                );
            }
        });
    });
}

/// 在主窗口每帧调用：创建/更新透明置顶悬浮窗 viewport
pub fn show(ctx: &egui::Context, shared: &Arc<AppShared>, state: &Arc<Mutex<OverlayState>>) {
    let want = state.lock().unwrap().visible.load(Ordering::SeqCst);
    let shared = Arc::clone(shared);
    let state = Arc::clone(state);
    ctx.show_viewport_deferred(
        egui::ViewportId::from_hash_of("gamepad_overlay"),
        egui::ViewportBuilder::default()
            .with_title("手柄悬浮窗")
            .with_inner_size([W, COLLAPSED_H])
            .with_resizable(false)
            .with_decorations(false)
            .with_transparent(true)
            .with_always_on_top()
            .with_taskbar(false)
            .with_visible(want),
        move |ui, _class| {
            let Ok(mut st) = state.lock() else {
                return;
            };
            if !st.visible.load(Ordering::SeqCst) {
                return;
            }
            overlay_ui(&shared, &mut st, ui);
        },
    );
}
