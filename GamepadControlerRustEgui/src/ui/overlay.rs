// =====================================================================
// overlay.rs —— 悬浮窗（egui，独立线程）
//
// 透明置顶无边框小窗：显示当前操作集 / 当前层 / 连接状态 / 按下按键 /
// L3 锁存警示；点击标题或展开按钮切换展开并调整窗口高度；标题行拖动窗口。
//
// 主窗口按钮翻转 overlay_visible 标志，本线程每帧轮询显隐。
// =====================================================================

use crate::core::input_types::*;
use crate::ui::shared::AppShared;
use crate::ui::theme::*;
use eframe::egui;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

const W: f32 = 320.0;
const COLLAPSED_H: f32 = 220.0;
const EXPANDED_H: f32 = 480.0;

pub struct OverlayApp {
    shared: Arc<AppShared>,
    visible: Arc<AtomicBool>,
    expanded: bool,
    was_visible: bool,
}

impl OverlayApp {
    pub fn new(shared: Arc<AppShared>, visible: Arc<AtomicBool>) -> Self {
        Self {
            shared,
            visible,
            expanded: false,
            was_visible: false,
        }
    }

    fn toggle_expand(&mut self, ctx: &egui::Context) {
        self.expanded = !self.expanded;
        let h = if self.expanded { EXPANDED_H } else { COLLAPSED_H };
        ctx.send_viewport_cmd(egui::ViewportCommand::InnerSize(egui::vec2(W, h)));
    }

    fn show(&mut self, ui: &mut egui::Ui) {
        let ctx = ui.ctx().clone();
        let (set_name, layer_name, connected, pressed, mouse_toggle, mappings) = {
            let core = self.shared.core.lock().unwrap();
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
                            ui.with_layout(
                                egui::Layout::right_to_left(egui::Align::Center),
                                |ui| {
                                    let txt = if self.expanded { "收起" } else { "展开" };
                                    if btn_ghost(ui, txt).clicked() {
                                        self.toggle_expand(&ctx);
                                    }
                                },
                            );
                        })
                        .response
                        .interact(egui::Sense::click_and_drag());
                    if title_res.dragged() {
                        ctx.send_viewport_cmd(egui::ViewportCommand::StartDrag);
                    } else if title_res.clicked() {
                        self.toggle_expand(&ctx);
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
                    if self.expanded {
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
                            .max_height(EXPANDED_H - 170.0)
                            .show(ui, |ui| {
                                for (button, desc, held) in &mappings {
                                    ui.horizontal(|ui| {
                                        let color = if *held { rgb(WARN) } else { rgb(ACCENT) };
                                        ui.add_sized(
                                            [64.0, 18.0],
                                            egui::Label::new(
                                                egui::RichText::new(button)
                                                    .color(color)
                                                    .size(13.0),
                                            ),
                                        );
                                        ui.label(
                                            egui::RichText::new("→")
                                                .color(rgb(TEXT_DIM))
                                                .size(13.0),
                                        );
                                        ui.label(
                                            egui::RichText::new(desc)
                                                .color(rgb(TEXT))
                                                .size(13.0),
                                        );
                                    });
                                }
                            });
                        ui.add_space(4.0);
                        ui.horizontal(|ui| {
                            ui.add_sized(
                                [64.0, 18.0],
                                egui::Label::new(
                                    egui::RichText::new("左摇杆").color(rgb(ACCENT)).size(13.0),
                                ),
                            );
                            ui.label(egui::RichText::new("→").color(rgb(TEXT_DIM)).size(13.0));
                            ui.label(egui::RichText::new("WASD 移动").color(rgb(TEXT)).size(13.0));
                        });
                        ui.horizontal(|ui| {
                            ui.add_sized(
                                [64.0, 18.0],
                                egui::Label::new(
                                    egui::RichText::new("右摇杆").color(rgb(ACCENT)).size(13.0),
                                ),
                            );
                            ui.label(egui::RichText::new("→").color(rgb(TEXT_DIM)).size(13.0));
                            ui.label(
                                egui::RichText::new("视角控制").color(rgb(TEXT)).size(13.0),
                            );
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
}

impl eframe::App for OverlayApp {
    fn clear_color(&self, _visuals: &egui::Visuals) -> [f32; 4] {
        [0.0, 0.0, 0.0, 0.0]
    }

    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let ctx = ui.ctx();
        let want = self.visible.load(Ordering::SeqCst);
        if want != self.was_visible {
            ctx.send_viewport_cmd(egui::ViewportCommand::Visible(want));
            self.was_visible = want;
        }
        if !want {
            ctx.request_repaint_after(Duration::from_millis(200));
            return;
        }
        ctx.request_repaint_after(Duration::from_millis(50));
        self.show(ui);
    }
}

// ---------------------------------------------------------------------
// 装配
// ---------------------------------------------------------------------
pub fn run(shared: Arc<AppShared>, visible: Arc<AtomicBool>) -> eframe::Result {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_title("手柄悬浮窗")
            .with_inner_size([W, COLLAPSED_H])
            .with_resizable(false)
            .with_decorations(false)
            .with_transparent(true)
            .with_always_on_top()
            .with_visible(false),
        ..Default::default()
    };
    eframe::run_native(
        "gamepad_controler_egui_overlay",
        options,
        Box::new(move |cc| {
            crate::ui::theme::install_fonts(&cc.egui_ctx);
            crate::ui::theme::apply(&cc.egui_ctx);
            Ok(Box::new(OverlayApp::new(shared, visible)))
        }),
    )
}
