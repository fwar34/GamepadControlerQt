// =====================================================================
// main_window.rs —— 主窗口
//
// 布局（深色主题）：
//   ┌────────────────────────────────────────────────┐
//   │ 标题行：Gamepad 键鼠映射 · 连接状态 · 映射状态  │
//   ├──────────────┬─────────────────────────────────┤
//   │ 操作集 chip  │  当前操作集/当前层               │
//   │ [+复制][重名]│  全局设置（死区/灵敏度/平滑/加速）│
//   │ 公共层 按钮  │  [开始映射]  [保存] [重置] [说明] │
//   │ Layer1..10   │  [显示悬浮窗]  [退出]            │
//   └──────────────┴─────────────────────────────────┘
//
// 刷新机制：UI 线程每 50ms 轮询共享 core 的快照（连接/当前层/操作集/
// 按下按键/配置修订号），有变化才 notify 重绘。
// =====================================================================

use crate::ui::layer_edit::LayerEditView;
use crate::ui::overlay::OverlayView;
use crate::ui::shared::AppShared;
use crate::ui::theme::*;
use crate::ui::theme::rgb; // 显式导入：消除与 gpui::rgb 的 glob 歧义（显式优先于 glob）
use gpui::*;
use gpui_component::input::{Input, InputEvent, InputState};
use std::sync::Arc;

#[derive(Clone, Copy)]
enum SettingKey {
    Deadzone,
    LookSensitivity,
    LookSmoothing,
    LookAcceleration,
}

/// 输入框用途：重命名操作集 / 复制操作集（新名字）
#[derive(Clone)]
enum InputMode {
    Rename(String), // 操作集 id
    Copy(String),   // 操作集 id
}

pub struct MainWindowView {
    shared: Arc<AppShared>,
    // ---- 显示缓存（由 50ms 轮询更新）----
    connected: bool,
    layer_name: SharedString,
    set_name: SharedString,
    mouse_toggle: Option<SharedString>,
    profile_rev: u64,
    // ---- 输入（重命名/复制）----
    input_state: Entity<InputState>,
    input_text: SharedString,
    input_mode: Option<InputMode>,
    // ---- 悬浮窗句柄 ----
    overlay: Option<WindowHandle<OverlayView>>,
    // ---- 长生命周期订阅（drop 即取消订阅，必须持有）----
    _subscriptions: Vec<Subscription>,
}

impl MainWindowView {
    pub fn new(shared: Arc<AppShared>, window: &mut Window, cx: &mut Context<Self>) -> Self {
        let input_state = cx.new(|cx| InputState::new(window, cx).placeholder("输入名称"));
        let mut this = Self {
            shared,
            connected: false,
            layer_name: "Common".into(),
            set_name: "默认操作集".into(),
            mouse_toggle: None,
            profile_rev: u64::MAX, // 首次强制刷新
            input_state,
            input_text: SharedString::default(),
            input_mode: None,
            overlay: None,
            _subscriptions: Vec::new(),
        };
        this.subscribe_input(window, cx);
        this.start_polling(cx);
        this
    }

    /// 订阅输入框变化，同步到 input_text
    fn subscribe_input(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let input_state = self.input_state.clone();
        let input_state_cb = self.input_state.clone(); // 闭包内读取用（避免与借用冲突）
        let sub = cx.subscribe_in(&input_state, window, {
            move |this, _, ev: &InputEvent, _window, _cx| {
                if let InputEvent::Change = ev {
                    this.input_text = input_state_cb.read(&*_cx).value().into();
                }
            }
        });
        self._subscriptions.push(sub); // 持有订阅，否则事件不生效
    }

    /// 每 50ms 轮询共享 core，更新显示缓存并触发重绘
    fn start_polling(&mut self, cx: &mut Context<Self>) {
        let shared = Arc::clone(&self.shared);
        cx.spawn(async move |this, mut cx| {
            loop {
                Timer::after(std::time::Duration::from_millis(50)).await;
                let mut changed = false;
                let _ = this.update(cx, |this, cx| {
                    let core = shared.core.lock().unwrap();
                    let connected = core.connected;
                    let layer = core.steam.active_layer_name().to_string();
                    let set = core.steam.profile.active_operation_set_name();
                    let rev = core.profile_rev;
                    let toggle = core
                        .mapper
                        .toggled_mouse_buttons
                        .values()
                        .next()
                        .map(|mb| format!("长按锁存: {}", crate::core::input_types::mouse_button_display_name(*mb)));
                    drop(core);
                    if this.connected != connected {
                        this.connected = connected;
                        changed = true;
                    }
                    if this.layer_name.as_ref() != layer {
                        this.layer_name = layer.into();
                        changed = true;
                    }
                    if this.set_name.as_ref() != set {
                        this.set_name = set.clone().into();
                        changed = true;
                    }
                    if this.profile_rev != rev {
                        this.profile_rev = rev;
                        changed = true;
                    }
                    let t: Option<SharedString> = toggle.map(Into::into);
                    if this.mouse_toggle != t {
                        this.mouse_toggle = t;
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

    // -----------------------------------------------------------------
    // 动作
    // -----------------------------------------------------------------
    fn switch_set(&mut self, set_id: String) {
        if let Ok(mut core) = self.shared.core.lock() {
            core.switch_operation_set(&set_id);
        }
    }

    fn add_set(&mut self) {
        if let Ok(mut core) = self.shared.core.lock() {
            core.add_operation_set();
        }
    }

    fn start_rename(&mut self, set_id: String) {
        self.input_text = SharedString::default();
        self.input_mode = Some(InputMode::Rename(set_id));
        self.focus_input();
    }

    fn start_copy(&mut self, set_id: String) {
        // 预填"xxx - 副本"，用户可直接确认或修改
        let base = self.set_name.to_string();
        self.input_text = format!("{} - 副本", base).into();
        self.input_mode = Some(InputMode::Copy(set_id));
        self.focus_input();
    }

    fn focus_input(&mut self) {
        // 简单聚焦：输入行渲染后自动聚焦
    }

    fn commit_input(&mut self) {
        let name = self.input_text.trim().to_string();
        let mode = self.input_mode.take();
        if name.is_empty() {
            return;
        }
        if let Ok(mut core) = self.shared.core.lock() {
            match mode {
                Some(InputMode::Rename(id)) => {
                    core.rename_operation_set(&id, &name);
                }
                Some(InputMode::Copy(id)) => {
                    core.copy_operation_set(&id, &name);
                }
                None => {}
            }
        }
        self.input_mode = None;
    }

    fn cancel_input(&mut self) {
        self.input_mode = None;
    }

    fn delete_set(&mut self, set_id: String) {
        if let Ok(mut core) = self.shared.core.lock() {
            if !core.delete_operation_set(&set_id) {
                // 至少保留一个，忽略
            }
        }
    }

    fn toggle_mapping(&mut self) {
        if self.shared.running.load(std::sync::atomic::Ordering::SeqCst) {
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
        crate::core::config_manager::save(&profile);
    }

    fn reset_config(&mut self) {
        crate::core::config_manager::reset_to_default();
        let def = crate::core::config_manager::load();
        if let Ok(mut core) = self.shared.core.lock() {
            core.load_profile(def);
        }
    }

    fn adjust_setting(&mut self, key: SettingKey, delta: f32) {
        let mut core = self.shared.core.lock().unwrap();
        let mut gs = core.steam.profile.global_settings.clone();
        match key {
            SettingKey::Deadzone => gs.deadzone = (gs.deadzone + delta).clamp(0.0, 0.5),
            SettingKey::LookSensitivity => gs.look_sensitivity = (gs.look_sensitivity + delta).clamp(0.05, 2.0),
            SettingKey::LookSmoothing => gs.look_smoothing = (gs.look_smoothing + delta).clamp(0.0, 0.95),
            SettingKey::LookAcceleration => gs.look_acceleration = (gs.look_acceleration + delta).clamp(0.5, 3.0),
        }
        core.update_global_settings(gs);
    }

    fn toggle_overlay(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if let Some(handle) = self.overlay.take() {
            // 悬浮窗已打开：通过其窗口句柄关闭（不能 close 主窗口）
            let _ = handle.update(cx, |_, w, _cx| w.remove_window());
            return;
        }
        let shared = Arc::clone(&self.shared);
        let bounds = Bounds::centered(None, size(px(320.0), px(200.0)), cx);
        let handle = cx
            .open_window(
                WindowOptions {
                    kind: WindowKind::PopUp,
                    window_bounds: Some(WindowBounds::Windowed(bounds)),
                    window_background: WindowBackgroundAppearance::Transparent,
                    titlebar: Some(TitlebarOptions {
                        appears_transparent: true,
                        ..Default::default()
                    }),
                    is_resizable: false,
                    focus: false,
                    ..Default::default()
                },
                |window, cx| cx.new(|cx| OverlayView::new(shared, window, cx)),
            )
            .ok();
        self.overlay = handle;
    }

    fn open_layer_edit(&mut self, layer_id: String, window: &mut Window, cx: &mut Context<Self>) {
        let shared = Arc::clone(&self.shared);
        // 先计算窗口 bounds，避免 cx 同时被可变（open_window）与不可变（centered）借用
        let bounds = WindowBounds::centered(size(px(680.0), px(560.0)), cx);
        let _ = cx.open_window(
            WindowOptions {
                window_bounds: Some(bounds),
                ..Default::default()
            },
            |window, cx| {
                cx.new(|cx| LayerEditView::new(shared, layer_id.clone(), window, cx))
            },
        );
    }

    fn open_help(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let bounds = WindowBounds::centered(size(px(720.0), px(520.0)), cx);
        let _ = cx.open_window(
            WindowOptions {
                window_bounds: Some(bounds),
                ..Default::default()
            },
            |window, cx| cx.new(|cx| crate::ui::help::HelpView::new(window, cx)),
        );
    }

    /// 从共享 core 读取当前设置快照
    fn read_settings(&self) -> (f32, f32, f32, f32) {
        let core = self.shared.core.lock().unwrap();
        let gs = &core.steam.profile.global_settings;
        (gs.deadzone, gs.look_sensitivity, gs.look_smoothing, gs.look_acceleration)
    }
}

// ---------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------
impl Render for MainWindowView {
    fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let (deadzone, sens, smoothing, accel) = self.read_settings();
        let running = self.shared.running.load(std::sync::atomic::Ordering::SeqCst);

        // 读取操作集与层列表（锁内 clone 列表数据）
        let (sets, active_set_id, layer_info) = {
            let core = self.shared.core.lock().unwrap();
            let sets: Vec<(String, String)> = core
                .steam
                .profile
                .operation_sets
                .iter()
                .map(|s| (s.id.clone(), s.name.clone()))
                .collect();
            let active = core.steam.profile.active_operation_set_id.clone();
            let layers: Vec<(String, String, bool)> = core
                .steam
                .profile
                .layers()
                .iter()
                .map(|l| (l.id.clone(), l.name.clone(), core.steam.is_layer_active(&l.id)))
                .collect();
            (sets, active, layers)
        };

        // ---- 顶部状态行 ----
        let status_text = if self.connected {
            if running {
                format!("● 已连接 · 映射运行中")
            } else {
                format!("● 已连接 · 已停止")
            }
        } else {
            format!("○ 手柄未连接")
        };
        let status_color = if self.connected { OK } else { TEXT_FAINT };

        // ---- 操作集 chip 行 ----
        let mut set_row = div().flex().flex_row().gap_2().items_center();
        set_row = set_row.child(
            div()
                .flex_shrink_0()
                .text_sm()
                .text_color(rgb(TEXT_DIM))
                .child("操作集"),
        );
        for (id, name) in sets.iter() {
            let set_id = id.clone();
            let active = *id == active_set_id;
            let chip = div()
                .id(SharedString::from(format!("set-{}", id)))
                .px(px(10.0))
                .py(px(4.0))
                .rounded_md()
                .cursor_pointer()
                .bg(rgb(if active { ACCENT } else { BG_INSET }))
                .text_color(rgb(if active { BG_DEEP } else { TEXT }))
                .text_sm()
                .on_mouse_down(
                    MouseButton::Left,
                    cx.listener(move |this, _: &MouseDownEvent, _w, cx| {
                        this.switch_set(set_id.clone());
                        cx.notify();
                    }),
                )
                .child(name.clone());
            set_row = set_row.child(chip);
        }

        // ---- 操作集管理按钮 ----
        let btn_add = action_button(
            "添加",
            cx.listener(move |this, _: &MouseDownEvent, _w, cx| {
                this.add_set();
                cx.notify();
            }),
        );
        let active_set_id2 = active_set_id.clone();
        let btn_copy = action_button(
            "复制",
            cx.listener(move |this, _: &MouseDownEvent, _w, cx| {
                this.start_copy(active_set_id2.clone());
                cx.notify();
            }),
        );
        let active_set_id3 = active_set_id.clone();
        let btn_rename = action_button(
            "重命名",
            cx.listener(move |this, _: &MouseDownEvent, _w, cx| {
                this.start_rename(active_set_id3.clone());
                cx.notify();
            }),
        );
        let active_set_id4 = active_set_id.clone();
        let btn_delete = action_button(
            "删除",
            cx.listener(move |this, _: &MouseDownEvent, _w, cx| {
                this.delete_set(active_set_id4.clone());
                cx.notify();
            }),
        );
        set_row = set_row
            .child(btn_add)
            .child(btn_copy)
            .child(btn_rename)
            .child(btn_delete);

        // ---- 层按钮列 ----
        let mut layer_col = div().flex().flex_col().gap_2().flex_grow().pt_2();
        // 公共层（常显，点击可编辑公共层）
        layer_col = layer_col.child(layer_button(
            "Common".to_string(),
            "公共层".to_string(),
            false,
            cx.listener(|this, _: &MouseDownEvent, w, cx| {
                this.open_layer_edit("Common".to_string(), w, cx);
            }),
        ));
        for (id, name, active) in layer_info.iter() {
            let layer_id = id.clone();
            let name = name.clone();
            let active = *active;
            layer_col = layer_col.child(layer_button(
                layer_id.clone(),
                name,
                active,
                cx.listener(move |this, _: &MouseDownEvent, _w, cx| {
                    this.open_layer_edit(layer_id.clone(), _w, cx);
                }),
            ));
        }

        // ---- 设置行 ----
        let slider_row = |label: &str, value: f32, key: SettingKey, cx: &mut Context<Self>| {
            let dec = cx.listener(move |this, _: &MouseDownEvent, _w, cx| {
                this.adjust_setting(key, -0.01);
                cx.notify();
            });
            let inc = cx.listener(move |this, _: &MouseDownEvent, _w, cx| {
                this.adjust_setting(key, 0.01);
                cx.notify();
            });
            div()
                .flex()
                .flex_row()
                .items_center()
                .gap_2()
                .child(
                    div()
                        .w(px(96.0))
                        .text_sm()
                        .text_color(rgb(TEXT_DIM))
                        .child(label.to_string()),
                )
                .child(minus_button(dec))
                .child(
                    div()
                        .w(px(56.0))
                        .text_sm()
                        .text_color(rgb(TEXT))
                        .child(format!("{:.2}", value)),
                )
                .child(plus_button(inc))
        };

        let settings_col = div()
            .flex()
            .flex_col()
            .gap_2()
            .child(slider_row("死区", deadzone, SettingKey::Deadzone, cx))
            .child(slider_row("视角灵敏度", sens, SettingKey::LookSensitivity, cx))
            .child(slider_row("视角平滑", smoothing, SettingKey::LookSmoothing, cx))
            .child(slider_row("视角加速", accel, SettingKey::LookAcceleration, cx));

        // ---- 当前信息 ----
        let info_col = div()
            .flex()
            .flex_col()
            .gap_1()
            .child(
                div()
                    .text_size(px(16.0))
                    .text_color(rgb(TEXT))
                    .child(format!("当前操作集: {}", self.set_name)),
            )
            .child(
                div()
                    .text_size(px(16.0))
                    .text_color(rgb(TEXT))
                    .child(format!("当前层: {}", self.layer_name)),
            )
            .child(
                div()
                    .text_sm()
                    .text_color(rgb(if self.mouse_toggle.is_some() { WARN } else { TEXT_FAINT }))
                    .child(
                        self.mouse_toggle
                            .clone()
                            .unwrap_or_else(|| "无长按锁存".into()),
                    ),
            );

        // ---- 操作按钮 ----
        let btn_toggle = div()
            .id("toggle")
            .px(px(20.0))
            .py(px(8.0))
            .rounded_md()
            .cursor_pointer()
            .bg(rgb(if running { DANGER } else { ACCENT }))
            .text_color(rgb(BG_DEEP))
            .text_size(px(16.0))
            .on_mouse_down(
                MouseButton::Left,
                cx.listener(|this, _: &MouseDownEvent, _w, cx| {
                    this.toggle_mapping();
                    cx.notify();
                }),
            )
            .child(if running { "停止映射" } else { "开始映射" });

        let btn_save = action_button(
            "保存配置",
            cx.listener(|this, _: &MouseDownEvent, _w, cx| {
                this.save_config();
                cx.notify();
            }),
        );
        let btn_reset = action_button(
            "重置默认",
            cx.listener(|this, _: &MouseDownEvent, _w, cx| {
                this.reset_config();
                cx.notify();
            }),
        );
        let btn_overlay = action_button(
            if self.overlay.is_some() { "关闭悬浮窗" } else { "显示悬浮窗" },
            cx.listener(|this, _: &MouseDownEvent, w, cx| {
                this.toggle_overlay(w, cx);
                cx.notify();
            }),
        );
        let btn_help = action_button(
            "使用说明",
            cx.listener(|this, _: &MouseDownEvent, w, cx| {
                this.open_help(w, cx);
            }),
        );
        let btn_quit = action_button(
            "退出",
            cx.listener(|_this, _: &MouseDownEvent, _w, cx| {
                cx.quit();
            }),
        );

        let right_col = div()
            .flex()
            .flex_col()
            .gap_3()
            .flex_grow()
            .child(info_col)
            .child(
                div()
                    .text_sm()
                    .text_color(rgb(TEXT_DIM))
                    .child("全局设置"),
            )
            .child(settings_col)
            .child(
                div()
                    .flex()
                    .flex_row()
                    .gap_2()
                    .items_center()
                    .child(btn_toggle)
                    .child(btn_save)
                    .child(btn_reset),
            )
            .child(
                div()
                    .flex()
                    .flex_row()
                    .gap_2()
                    .items_center()
                    .child(btn_overlay)
                    .child(btn_help)
                    .child(btn_quit),
            );

        // ---- 输入行（重命名/复制时显示）----
        let mut input_row = div();
        if let Some(mode) = self.input_mode.clone() {
            let ok = cx.listener(move |this, _: &MouseDownEvent, _w, cx| {
                this.commit_input();
                cx.notify();
            });
            let cancel = cx.listener(move |this, _: &MouseDownEvent, _w, cx| {
                this.cancel_input();
                cx.notify();
            });
            let hint = match mode {
                InputMode::Rename(_) => "重命名操作集",
                InputMode::Copy(_) => "复制为新操作集",
            };
            input_row = div()
                .flex()
                .flex_row()
                .gap_2()
                .items_center()
                .child(
                    div()
                        .text_sm()
                        .text_color(rgb(TEXT_DIM))
                        .child(hint),
                )
                .child(
                    div()
                        .flex_grow()
                        .child(Input::new(&self.input_state)),
                )
                .child(action_button("确定", ok))
                .child(action_button("取消", cancel));
        }

        div()
            .flex()
            .flex_col()
            .size_full()
            .bg(rgb(BG))
            .p_4()
            .gap_2()
            .child(
                div()
                    .flex()
                    .flex_row()
                    .items_center()
                    .justify_between()
                    .child(
                        div()
                            .text_xl()
                            .text_color(rgb(TEXT))
                            .child("Gamepad 键鼠映射"),
                    )
                    .child(
                        div()
                            .text_sm()
                            .text_color(rgb(status_color))
                            .child(status_text),
                    ),
            )
            .child(input_row)
            .child(
                div()
                    .flex()
                    .flex_row()
                    .gap_3()
                    .flex_grow()
                    .child(
                        div()
                            .flex()
                            .flex_col()
                            .w(px(240.0))
                            .gap_2()
                            .p_3()
                            .bg(rgb(BG_PANEL))
                            .rounded_lg()
                            .child(
                                div()
                                    .text_sm()
                                    .text_color(rgb(TEXT_DIM))
                                    .child("操作集管理"),
                            )
                            .child(set_row)
                            .child(layer_col),
                    )
                    .child(
                        div()
                            .flex()
                            .flex_col()
                            .w(px(320.0))
                            .gap_3()
                            .p_3()
                            .bg(rgb(BG_PANEL))
                            .rounded_lg()
                            .child(right_col),
                    ),
            )
    }
}

// ---------------------------------------------------------------------
// 辅助元素
// ---------------------------------------------------------------------

/// 普通操作按钮
fn action_button(
    label: &str,
    listener: impl Fn(&MouseDownEvent, &mut Window, &mut App) + 'static,
) -> impl IntoElement {
    div()
        .id(SharedString::from(format!("btn-{}", label)))
        .px(px(12.0))
        .py(px(6.0))
        .rounded_md()
        .cursor_pointer()
        .bg(rgb(BG_INSET))
        .border_1()
        .border_color(rgb(BORDER))
        .text_color(rgb(TEXT))
        .text_sm()
        .hover(|s| s.bg(rgb(0x3a3e45)))
        .on_mouse_down(MouseButton::Left, listener)
        .child(label.to_string())
}

/// 层按钮（激活高亮）
fn layer_button(
    _id: String,
    name: String,
    active: bool,
    listener: impl Fn(&MouseDownEvent, &mut Window, &mut App) + 'static,
) -> impl IntoElement {
    div()
        .id(SharedString::from(format!("layer-{}", _id)))
        .px(px(10.0))
        .py(px(6.0))
        .rounded_md()
        .cursor_pointer()
        .bg(rgb(if active { ACCENT } else { BG_INSET }))
        .text_color(rgb(if active { BG_DEEP } else { TEXT }))
        .text_sm()
        .on_mouse_down(MouseButton::Left, listener)
        .child(name)
}

fn minus_button(listener: impl Fn(&MouseDownEvent, &mut Window, &mut App) + 'static) -> impl IntoElement {
    div()
        .id("minus")
        .size(px(22.0))
        .rounded_sm()
        .cursor_pointer()
        .bg(rgb(BG_INSET))
        .border_1()
        .border_color(rgb(BORDER))
        .text_color(rgb(TEXT))
        .text_sm()
        .flex()
        .items_center()
        .justify_center()
        .on_mouse_down(MouseButton::Left, listener)
        .child("−")
}

fn plus_button(listener: impl Fn(&MouseDownEvent, &mut Window, &mut App) + 'static) -> impl IntoElement {
    div()
        .id("plus")
        .size(px(22.0))
        .rounded_sm()
        .cursor_pointer()
        .bg(rgb(BG_INSET))
        .border_1()
        .border_color(rgb(BORDER))
        .text_color(rgb(TEXT))
        .text_sm()
        .flex()
        .items_center()
        .justify_center()
        .on_mouse_down(MouseButton::Left, listener)
        .child("+")
}
