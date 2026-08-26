// =====================================================================
// overlay.rs —— 悬浮窗逻辑（Slint）
//
// 每 50ms 轮询共享 core 更新显示；点击标题/按钮切换展开/收起并调整
// 窗口大小；标题行拖动移动窗口。
// =====================================================================

use crate::core::input_types::*;
use crate::ui::shared::AppShared;
use crate::{OverlayMapItem, OverlayWindow};
use slint::{ComponentHandle, LogicalPosition, LogicalSize, ModelRc, VecModel, Weak};
use std::cell::RefCell;
use std::rc::Rc;
use std::sync::Arc;
use std::time::Duration;

const W: f32 = 320.0;
const COLLAPSED_H: f32 = 220.0;
const EXPANDED_H: f32 = 480.0;

fn model<T: Clone + 'static>(items: Vec<T>) -> ModelRc<T> {
    Rc::new(VecModel::from(items)).into()
}

pub struct Logic {
    shared: Arc<AppShared>,
    ui: Weak<OverlayWindow>,
    expanded: bool,
    timer: slint::Timer,
    prev: Option<String>,
}

impl Logic {
    pub fn new(ui: Weak<OverlayWindow>, shared: Arc<AppShared>) -> Self {
        Self {
            shared,
            ui,
            expanded: false,
            timer: slint::Timer::default(),
            prev: None,
        }
    }

    fn toggle_expand(&mut self) {
        self.expanded = !self.expanded;
        if let Some(ui) = self.ui.upgrade() {
            ui.set_expanded(self.expanded);
            ui.set_toggle_text(if self.expanded { "收起" } else { "展开" }.into());
            let h = if self.expanded { EXPANDED_H } else { COLLAPSED_H };
            let _ = ui.window().set_size(LogicalSize::new(W, h));
        }
    }

    fn drag_by(&mut self, dx: f32, dy: f32) {
        if let Some(ui) = self.ui.upgrade() {
            let win = ui.window();
            let pos = win.position();
            let _ = win.set_position(LogicalPosition::new(pos.x as f32 + dx, pos.y as f32 + dy));
        }
    }

    fn poll(&mut self, ui: &OverlayWindow) {
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
        // 当前层映射列表：最后激活的操作层，否则公共层
        let active_layers = core.steam.get_active_layers();
        let layer_ref = if active_layers.is_empty() {
            core.steam.profile.common_layer()
        } else {
            active_layers.last().copied()
        };
        let held = core.steam.held_buttons();
        let mut mappings: Vec<OverlayMapItem> = Vec::new();
        if let Some(lr) = layer_ref {
            for b in all_controller_buttons() {
                if let Some(m) = lr.get_mapping(b) {
                    mappings.push(OverlayMapItem {
                        button: controller_button_display_name(b).into(),
                        desc: m.describe().into(),
                        held: held.contains(&b),
                    });
                }
            }
        }
        drop(core);

        let key = format!(
            "{:?}|{:?}|{}|{}|{}|{}",
            pressed, mappings, set_name, layer_name, connected, mouse_toggle,
        );
        if self.prev.as_deref() == Some(&key) {
            return;
        }
        self.prev = Some(key);

        ui.set_set_name(format!("操作集: {}", set_name).into());
        ui.set_layer_name(format!("当前层: {}", layer_name).into());
        ui.set_connected(connected);
        ui.set_mouse_toggle(mouse_toggle);
        // 按下按键拆成 4 组（每组最多 3 个）填充 4 行
        let mut p1: Vec<slint::SharedString> = Vec::new();
        let mut p2: Vec<slint::SharedString> = Vec::new();
        let mut p3: Vec<slint::SharedString> = Vec::new();
        let mut p4: Vec<slint::SharedString> = Vec::new();
        for (i, p) in pressed.into_iter().enumerate() {
            let target = match i / 3 {
                0 => &mut p1,
                1 => &mut p2,
                2 => &mut p3,
                _ => &mut p4,
            };
            target.push(p.into());
        }
        ui.set_pressed1(model(p1));
        ui.set_pressed2(model(p2));
        ui.set_pressed3(model(p3));
        ui.set_pressed4(model(p4));
        ui.set_mappings(model(mappings));
    }
}

pub fn setup(ui: &OverlayWindow, shared: Arc<AppShared>) {
    let logic = Rc::new(RefCell::new(Logic::new(ui.as_weak(), shared)));

    let l = logic.clone();
    ui.on_toggle_expand(move || l.borrow_mut().toggle_expand());
    let l = logic.clone();
    ui.on_drag_by(move |dx, dy| l.borrow_mut().drag_by(dx, dy));

    // 启动轮询
    let l = logic.clone();
    let timer = slint::Timer::default();
    timer.start(slint::TimerMode::Repeated, Duration::from_millis(50), move || {
        if let Ok(mut g) = l.try_borrow_mut() {
            if let Some(u) = g.ui.upgrade() {
                g.poll(&u);
            }
        }
    });
    logic.borrow_mut().timer = timer;

    // 首次渲染
    if let Some(u) = ui.as_weak().upgrade() {
        let mut g = logic.borrow_mut();
        g.poll(&u);
    }
}
