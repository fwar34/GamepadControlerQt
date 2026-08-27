// =====================================================================
// app.rs —— Iced Application（主窗口）
// =====================================================================

use crate::core::input_types::{
    all_controller_buttons, controller_button_display_name, layer_display_name,
    mouse_button_display_name, key_code_to_name, MouseButton,
};
use crate::core::mapping_types::{ActionType, KeyMapping, MappedAction};
use crate::ui::shared::AppShared;
use crate::ui::theme::*;
use crate::ui::theme::rgb;
use iced::widget::{
    button, column, container, mouse_area, row, scrollable, slider, text, text_input, Space,
};
use iced::window;
use iced::{Background, Element, Length, Subscription, Task, Theme};
use std::sync::Arc;

// ---- 输入框用途 ----
#[derive(Clone, Debug)]
pub enum InputMode {
    Rename(String),
    Copy(String),
}

// ---- 层编辑模式 ----
#[derive(Clone, Debug)]
pub enum EditTarget {
    None,
    Layer(String),
}

// ---- UI 消息 ----
#[derive(Clone, Debug)]
pub enum Message {
    SwitchSet(String),
    AddSet,
    StartRename(String),
    StartCopy(String),
    DeleteSet(String),
    InputChanged(String),
    InputConfirm,
    InputCancel,

    OpenLayerEdit(String),
    CloseLayerEdit,
    LayerEditSelectButton(usize),
    LayerEditSetActionKind(usize),
    LayerEditSetKeyTarget(i32),
    LayerEditSetMouseTarget(usize),
    LayerEditSetLayerTarget(String),
    LayerEditToggleSub(i32),
    LayerEditClearMapping,

    AdjustSetting(SettingKey, f32),

    ToggleMapping,
    SaveConfig,
    ResetConfig,
    ToggleOverlay,
    ToggleHelp,
    Quit,

    PollTick,
    /// 空操作：用于弹窗面板捕获点击，避免误触遮罩关闭
    Noop,

    // ---- 窗口 / 悬浮窗 ----
    MainOpened(window::Id),
    OverlayOpened(window::Id),
    /// 某个窗口已被关闭（daemon 模式下据此判断是否整体退出）
    WindowClosed(window::Id),
    /// 拖动悬浮窗（在悬浮窗标题栏触发）
    OverlayDrag,
    OverlayOpacityChanged(f32),
    OverlayExpanded(bool),
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum SettingKey {
    Deadzone,
    LookSensitivity,
    LookSmoothing,
    LookAcceleration,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum EditActionKind {
    Keyboard,
    Mouse,
    MouseToggle,
    WheelUp,
    WheelDown,
    SwitchLayer,
    LookAround,
    MouseMove,
}

const EDIT_ACTION_KINDS: [EditActionKind; 8] = [
    EditActionKind::Keyboard,
    EditActionKind::Mouse,
    EditActionKind::MouseToggle,
    EditActionKind::WheelUp,
    EditActionKind::WheelDown,
    EditActionKind::SwitchLayer,
    EditActionKind::LookAround,
    EditActionKind::MouseMove,
];

const EDIT_ACTION_LABELS: [&str; 8] = [
    "键盘", "鼠标点击", "鼠标长按", "滚轮上", "滚轮下", "切层", "视角控制", "鼠标移动",
];

const COMMON_KEYS: [(i32, &str); 28] = [
    (51, "W"), (29, "A"), (47, "S"), (32, "D"), (45, "Q"), (33, "E"),
    (62, "Space"), (66, "Enter"), (61, "Tab"), (111, "Esc"),
    (59, "Shift"), (113, "Ctrl"), (57, "Alt"),
    (8, "1"), (9, "2"), (10, "3"), (11, "4"), (12, "5"),
    (131, "F1"), (132, "F2"), (133, "F3"), (134, "F4"),
    (135, "F5"), (136, "F6"), (137, "F7"), (138, "F8"),
    (139, "F9"), (140, "F10"),
];

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

// ---- 通用按钮样式 ----
fn styled_btn(_label: &str, bg_color: u32, fg_color: u32) -> button::Style {
    button::Style {
        background: Some(Background::Color(rgb(bg_color))),
        text_color: rgb(fg_color),
        border: iced::Border::default().rounded(4),
        ..Default::default()
    }
}

// =====================================================================
// App 状态
// =====================================================================
pub struct App {
    pub shared: Arc<AppShared>,
    connected: bool,
    layer_name: String,
    set_name: String,
    mouse_toggle: Option<String>,
    profile_rev: u64,
    input_text: String,
    input_mode: Option<InputMode>,
    edit_target: EditTarget,
    edit_selected: usize,
    edit_action_kind: EditActionKind,
    edit_held: Vec<usize>,
    main_window: Option<window::Id>,
    overlay_window: Option<window::Id>,
    overlay_opacity: f32,
    overlay_expanded: bool,
    show_help: bool,
}

impl App {
    /// boot 逻辑：daemon 不自动开窗，启动时打开主窗口
    pub fn boot_with_shared(shared: Arc<AppShared>) -> impl Fn() -> (Self, Task<Message>) {
        move || {
            let app = App {
                shared: Arc::clone(&shared),
                connected: false,
                layer_name: "Common".to_string(),
                set_name: "默认操作集".to_string(),
                mouse_toggle: None,
                profile_rev: u64::MAX,
                input_text: String::new(),
                input_mode: None,
                edit_target: EditTarget::None,
                edit_selected: 0,
                edit_action_kind: EditActionKind::Keyboard,
                edit_held: Vec::new(),
                main_window: None,
                overlay_window: None,
                overlay_opacity: 0.85,
                overlay_expanded: false,
                show_help: false,
            };
            let (_id, open) = iced::window::open(main_window_settings());
            (app, open.map(Message::MainOpened))
        }
    }

    pub fn theme(_app: &App, _window: window::Id) -> Option<Theme> {
        Some(Theme::TokyoNight)
    }

    pub fn subscription(_app: &App) -> Subscription<Message> {
        // 按键/连接事件通知：手柄线程有变化时即时刷新 UI，
        // 避免固定 200ms 轮询跟不上快速按键（悬浮窗按键显示要跟手）。
        let button_stream = Subscription::run(|| {
            iced::stream::channel(100, async |mut output| {
                let mut rx = crate::ui::shared::notif_receiver();
                while let Some(rx) = rx.as_mut() {
                    use iced::futures::sink::SinkExt;
                    use iced::futures::StreamExt;
                    while rx.next().await.is_some() {
                        let _ = output.send(Message::PollTick).await;
                    }
                }
            })
        });

        Subscription::batch([
            // 兜底轮询：连接/层状态等低频变化，200ms 刷新一次即可
            iced::time::every(std::time::Duration::from_millis(200))
                .map(|_| Message::PollTick),
            // 监听窗口关闭：主窗口关闭则整体退出，悬浮窗关闭仅清理状态
            iced::window::close_events().map(Message::WindowClosed),
            button_stream,
        ])
    }

    fn poll_update(&mut self) {
        if let Ok(core) = self.shared.core.lock() {
            self.connected = core.connected;
            self.layer_name = core.steam.active_layer_name().to_string();
            self.set_name = core.steam.profile.active_operation_set_name();
            self.profile_rev = core.profile_rev;
            self.mouse_toggle = core
                .mapper
                .toggled_mouse_buttons
                .values()
                .next()
                .map(|mb| format!("长按锁存: {}", mouse_button_display_name(*mb)));
            self.edit_held = core
                .steam
                .held_buttons()
                .iter()
                .filter_map(|b| all_controller_buttons().iter().position(|&x| x == *b))
                .collect();
        }
    }

    pub fn update(&mut self, message: Message) -> Task<Message> {
        match message {
            Message::PollTick => self.poll_update(),
            Message::SwitchSet(id) => {
                if let Ok(mut core) = self.shared.core.lock() {
                    core.switch_operation_set(&id);
                }
            }
            Message::AddSet => {
                if let Ok(mut core) = self.shared.core.lock() {
                    core.add_operation_set();
                }
            }
            Message::StartRename(id) => {
                self.input_text.clear();
                self.input_mode = Some(InputMode::Rename(id));
            }
            Message::StartCopy(id) => {
                self.input_text = format!("{} - 副本", self.set_name);
                self.input_mode = Some(InputMode::Copy(id));
            }
            Message::DeleteSet(id) => {
                if let Ok(mut core) = self.shared.core.lock() {
                    core.delete_operation_set(&id);
                }
            }
            Message::InputChanged(s) => self.input_text = s,
            Message::InputConfirm => {
                let name = self.input_text.trim().to_string();
                let mode = self.input_mode.take();
                if !name.is_empty() {
                    if let Ok(mut core) = self.shared.core.lock() {
                        match mode {
                            Some(InputMode::Rename(id)) => { core.rename_operation_set(&id, &name); }
                            Some(InputMode::Copy(id)) => { core.copy_operation_set(&id, &name); }
                            None => {}
                        }
                    }
                }
            }
            Message::InputCancel => self.input_mode = None,

            Message::OpenLayerEdit(id) => {
                self.edit_target = EditTarget::Layer(id);
                self.edit_selected = 0;
                self.edit_action_kind = EditActionKind::Keyboard;
            }
            Message::CloseLayerEdit => self.edit_target = EditTarget::None,
            Message::LayerEditSelectButton(idx) => {
                self.edit_selected = idx;
                if let Some(a) = self.edit_read_mapping().0 {
                    self.edit_action_kind = kind_of(&a);
                }
            }
            Message::LayerEditSetActionKind(idx) => {
                if idx < EDIT_ACTION_KINDS.len() {
                    self.edit_set_kind(EDIT_ACTION_KINDS[idx]);
                }
            }
            Message::LayerEditSetKeyTarget(code) => {
                self.edit_write_action(MappedAction::keyboard_key(code));
            }
            Message::LayerEditSetMouseTarget(idx) => {
                let mb = [MouseButton::Left, MouseButton::Right, MouseButton::Middle, MouseButton::Forward, MouseButton::Back][idx];
                let action = match self.edit_action_kind {
                    EditActionKind::MouseToggle => MappedAction::mouse_toggle(mb),
                    _ => MappedAction::mouse_click(mb),
                };
                self.edit_write_action(action);
            }
            Message::LayerEditSetLayerTarget(name) => {
                self.edit_write_action(MappedAction::switch_layer(&name));
            }
            Message::LayerEditToggleSub(code) => self.edit_toggle_sub(code),
            Message::LayerEditClearMapping => self.edit_clear_mapping(),

            Message::AdjustSetting(key, delta) => {
                if let Ok(mut core) = self.shared.core.lock() {
                    let mut gs = core.steam.profile.global_settings.clone();
                    match key {
                        SettingKey::Deadzone => gs.deadzone = (gs.deadzone + delta).clamp(0.0, 0.5),
                        SettingKey::LookSensitivity => gs.look_sensitivity = (gs.look_sensitivity + delta).clamp(0.05, 2.0),
                        SettingKey::LookSmoothing => gs.look_smoothing = (gs.look_smoothing + delta).clamp(0.0, 0.95),
                        SettingKey::LookAcceleration => gs.look_acceleration = (gs.look_acceleration + delta).clamp(0.5, 3.0),
                    }
                    core.update_global_settings(gs);
                }
            }

            Message::ToggleMapping => {
                if self.shared.running.load(std::sync::atomic::Ordering::SeqCst) {
                    self.shared.stop_mapping();
                } else {
                    self.shared.start_mapping();
                }
            }
            Message::SaveConfig => {
                if let Ok(core) = self.shared.core.lock() {
                    crate::core::config_manager::save(&core.steam.profile);
                }
            }
            Message::ResetConfig => {
                crate::core::config_manager::reset_to_default();
                let def = crate::core::config_manager::load();
                if let Ok(mut core) = self.shared.core.lock() {
                    core.load_profile(def);
                }
            }
            Message::ToggleOverlay => return self.toggle_overlay(),
            Message::ToggleHelp => self.show_help = !self.show_help,
            Message::Noop => {}
            Message::MainOpened(id) => self.main_window = Some(id),
            Message::OverlayOpened(id) => self.overlay_window = Some(id),
            Message::WindowClosed(id) => {
                if self.overlay_window == Some(id) {
                    // 悬浮窗被单独关闭：仅清理状态，程序继续运行
                    self.overlay_window = None;
                } else if self.main_window == Some(id) {
                    // 主窗口被关闭（点 X / 右键任务栏关闭）→ 整体退出程序
                    self.main_window = None;
                    return iced::exit();
                }
            }
            Message::OverlayDrag => {
                // 在悬浮窗标题栏按下左键 → 拖动整个窗口
                if let Some(id) = self.overlay_window {
                    return iced::window::drag(id);
                }
            }
            Message::OverlayOpacityChanged(v) => self.overlay_opacity = v.clamp(0.2, 1.0),
            Message::OverlayExpanded(b) => self.overlay_expanded = b,
            Message::Quit => {
                // daemon 模式：仅关单个窗口不会退出，需显式退出整个应用
                return iced::exit();
            }
        }
        Task::none()
    }

    /// 开关独立悬浮窗：未开则创建新窗口，已开则关闭
    fn toggle_overlay(&mut self) -> Task<Message> {
        if let Some(id) = self.overlay_window.take() {
            iced::window::close(id)
        } else {
            let (_id, open) = iced::window::open(overlay_window_settings());
            open.map(Message::OverlayOpened)
        }
    }

    pub fn view(&self, window_id: window::Id) -> Element<Message> {
        // 按窗口分发：悬浮窗窗口显示独立悬浮窗，其余显示主界面
        if self.overlay_window == Some(window_id) {
            return self.overlay_window_view(window_id);
        }
        let running = self.shared.running.load(std::sync::atomic::Ordering::SeqCst);

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

        let status_text = if self.connected {
            if running { "● 已连接 · 映射运行中" } else { "● 已连接 · 已停止" }
        } else {
            "○ 手柄未连接"
        };
        let status_color = if self.connected { OK } else { TEXT_FAINT };

        let header = row![
            text("Gamepad 键鼠映射").size(20).color(rgb(TEXT)),
            Space::new().width(Length::Fill),
            text(status_text).size(15).color(rgb(status_color)),
        ];

        let mut chips = row![text("操作集").size(13).color(rgb(TEXT_DIM))].spacing(4);
        for (id, name) in &sets {
            let active = *id == active_set_id;
            let chip_id = id.clone();
            let (b, f) = if active { (ACCENT, BG_DEEP) } else { (BG_INSET, TEXT) };
            let chip = button(text(name.clone()).size(13))
                .style(move |_, _| styled_btn("", b, f))
                .on_press(Message::SwitchSet(chip_id));
            chips = chips.push(chip);
        }

        let active_id2 = active_set_id.clone();
        let active_id3 = active_set_id.clone();
        let active_id4 = active_set_id.clone();
        let action_row = row![
            small_button("添加", Message::AddSet),
            small_button("复制", Message::StartCopy(active_id2)),
            small_button("重命名", Message::StartRename(active_id3)),
            small_button("删除", Message::DeleteSet(active_id4)),
        ]
        .spacing(4);

        let mut layer_col = column![
            small_button("公共层 (Common)", Message::OpenLayerEdit("Common".to_string())),
        ]
        .spacing(4);
        for (id, name, active) in layer_info.iter() {
            let lid = id.clone();
            let display = if *active {
                format!("▶ {}", layer_display_name(name))
            } else {
                layer_display_name(name).to_string()
            };
            let (b, f) = if *active { (ACCENT, BG_DEEP) } else { (BG_INSET, TEXT) };
            let btn = button(text(display).size(13))
                .width(Length::Fill)
                .style(move |_, _| styled_btn("", b, f))
                .on_press(Message::OpenLayerEdit(lid));
            layer_col = layer_col.push(btn);
        }

        let (deadzone, sens, smoothing, accel) = {
            let core = self.shared.core.lock().unwrap();
            let gs = &core.steam.profile.global_settings;
            (gs.deadzone, gs.look_sensitivity, gs.look_smoothing, gs.look_acceleration)
        };

        let settings_col = column![
            setting_row("死区", deadzone, SettingKey::Deadzone),
            setting_row("视角灵敏度", sens, SettingKey::LookSensitivity),
            setting_row("视角平滑", smoothing, SettingKey::LookSmoothing),
            setting_row("视角加速", accel, SettingKey::LookAcceleration),
        ]
        .spacing(4);

        // 悬浮窗透明度滑杆
        let opacity_row = row![
            text("悬浮窗透明度").size(13).color(rgb(TEXT_DIM)).width(96),
            slider(0.2..=1.0, self.overlay_opacity, Message::OverlayOpacityChanged)
                .width(Length::Fill),
            text(format!("{:.0}%", self.overlay_opacity * 100.0))
                .size(13)
                .color(rgb(TEXT))
                .width(48),
        ]
        .spacing(4);

        let toggle_label = if running { "停止映射" } else { "开始映射" };
        let toggle_bg = if running { DANGER } else { ACCENT };
        let btn_toggle = button(text(toggle_label).size(17))
            .padding([8, 20])
            .style(move |_, _| styled_btn("", toggle_bg, BG_DEEP))
            .on_press(Message::ToggleMapping);

        let overlay_label = if self.overlay_window.is_some() { "关闭悬浮窗" } else { "显示悬浮窗" };
        let btn_row = row![
            btn_toggle,
            small_button("保存配置", Message::SaveConfig),
            small_button("重置默认", Message::ResetConfig),
            small_button(overlay_label, Message::ToggleOverlay),
            small_button("使用说明", Message::ToggleHelp),
            small_button("退出", Message::Quit),
        ]
        .spacing(8);

        let info_col = column![
            text(format!("当前操作集: {}", self.set_name)).size(17).color(rgb(TEXT)),
            text(format!("当前层: {}", self.layer_name)).size(17).color(rgb(TEXT)),
            text(self.mouse_toggle.clone().unwrap_or_else(|| "无长按锁存".into()))
                .size(13)
                .color(rgb(if self.mouse_toggle.is_some() { WARN } else { TEXT_FAINT })),
        ]
        .spacing(4);

        let mut input_row = row![].spacing(8);
        if let Some(ref mode) = self.input_mode {
            let hint = match mode {
                InputMode::Rename(_) => "重命名操作集",
                InputMode::Copy(_) => "复制为新操作集",
            };
            input_row = input_row
                .push(text(hint).size(13).color(rgb(TEXT_DIM)))
                .push(
                    text_input("输入名称", &self.input_text)
                        .width(Length::Fill)
                        .on_input(Message::InputChanged)
                        .on_submit(Message::InputConfirm),
                )
                .push(small_button("确定", Message::InputConfirm))
                .push(small_button("取消", Message::InputCancel));
        }

        let left_panel = column![
            text("操作集管理").size(13).color(rgb(TEXT_DIM)),
            chips,
            action_row,
            Space::new().height(8),
            layer_col,
        ]
        .spacing(6)
        .width(240);

        let right_panel = column![
            info_col,
            text("全局设置").size(13).color(rgb(TEXT_DIM)),
            settings_col,
            text("悬浮窗").size(13).color(rgb(TEXT_DIM)),
            opacity_row,
            btn_row,
        ]
        .spacing(8)
        .width(Length::Fill);

        let main_layout = row![left_panel, Space::new().width(12), right_panel];

        // 基础页面：主界面。层编辑/帮助/悬浮窗改为模态弹窗，不再内联挤占页面。
        let page: Element<Message> = container(
            column![header, input_row, Space::new().height(8), main_layout].spacing(8),
        )
        .width(Length::Fill)
        .height(Length::Fill)
        .padding(16)
        .style(|_| iced::widget::container::Style {
            background: Some(Background::Color(rgb(BG))),
            ..Default::default()
        })
        .into();

        // 弹窗优先级：层编辑 > 使用说明
        if matches!(self.edit_target, EditTarget::Layer(_)) {
            iced::widget::stack([page, self.modal_layer_edit()]).into()
        } else if self.show_help {
            iced::widget::stack([page, self.modal_help()]).into()
        } else {
            page
        }
    }

    // =================================================================
    // 模态弹窗：半透明遮罩 + 居中面板（iced 0.14 移除了 modal，用
    // Stack + MouseArea 组合实现；点击遮罩关闭，点击面板不穿透）
    // =================================================================

    fn modal_panel<'a>(
        &self,
        content: Element<'a, Message>,
        close: Message,
    ) -> Element<'a, Message> {
        // 遮罩层：全屏半透明，点击任意处关闭弹窗
        let backdrop = mouse_area(
            container(Space::new())
                .width(Length::Fill)
                .height(Length::Fill)
                .style(|_| container::Style {
                    background: Some(Background::Color(argb(0x99000000))),
                    ..Default::default()
                }),
        )
        .on_press(close);

        // 面板层：居中显示，捕获点击避免误触遮罩关闭
        let panel = mouse_area(
            container(content)
                .padding(16)
                .style(|_| container::Style {
                    background: Some(Background::Color(rgb(BG_PANEL))),
                    border: iced::Border::default()
                        .width(1.0)
                        .color(rgb(BORDER))
                        .rounded(10),
                    ..Default::default()
                }),
        )
        .on_press(Message::Noop);

        iced::widget::stack([
            backdrop.into(),
            container(panel)
                .center_x(Length::Fill)
                .center_y(Length::Fill)
                .into(),
        ])
        .into()
    }

    /// 层编辑弹窗（内容可滚动）
    fn modal_layer_edit(&self) -> Element<Message> {
        let content = scrollable(self.view_layer_edit()).width(700).height(480);
        self.modal_panel(content.into(), Message::CloseLayerEdit)
    }

    /// 使用说明弹窗（内容可滚动）
    fn modal_help(&self) -> Element<Message> {
        let content = scrollable(crate::ui::help::help_view())
            .width(700)
            .height(480);
        self.modal_panel(content.into(), Message::ToggleHelp)
    }

    // =================================================================
    // 独立悬浮窗窗口（可拖动、置顶、透明、透明度可调）
    // =================================================================

    fn overlay_window_view(&self, _id: window::Id) -> Element<'_, Message> {
        // 标题栏：按下左键即可拖动整个窗口
        let title_bar = mouse_area(
            row![
                text("⚡ 悬浮窗").size(14).color(rgb(ACCENT)),
                Space::new().width(Length::Fill),
                text("按住拖动").size(11).color(rgb(TEXT_FAINT)),
            ]
            .padding([4, 8]),
        )
        .on_press(Message::OverlayDrag);

        let expand_label = if self.overlay_expanded { "收起映射" } else { "展开映射" };
        let btn_row = row![
            small_button(expand_label, Message::OverlayExpanded(!self.overlay_expanded)),
            small_button("关闭", Message::ToggleOverlay),
        ]
        .spacing(4);

        let content =
            crate::ui::overlay::overlay_view(&self.shared, self.overlay_expanded, self.overlay_opacity);

        column![title_bar, content, btn_row]
            .spacing(4)
            .padding(4)
            .into()
    }

    fn view_layer_edit(&self) -> Element<Message> {
        let (layer_name, switch_targets) = self.edit_layer_context();
        let (current, subs) = self.edit_read_mapping();
        let all_btns = all_controller_buttons();

        let mut grid = column![].spacing(2);
        for (i, b) in all_btns.iter().enumerate() {
            let is_pressed = self.edit_held.contains(&i);
            let is_selected = self.edit_selected == i;
            let (bg_c, fg_c) = if is_selected { (ACCENT, BG_DEEP) }
                else if is_pressed { (WARN, BG_DEEP) }
                else { (BG_INSET, TEXT) };
            let btn = button(text(controller_button_display_name(*b)).size(13))
                .width(Length::Fill)
                .padding([4, 8])
                .style(move |_, _| styled_btn("", bg_c, fg_c))
                .on_press(Message::LayerEditSelectButton(i));
            grid = grid.push(btn);
        }

        let desc = match &current {
            Some(a) => describe_action(a),
            None => "（无映射）".to_string(),
        };

        let mut kind_row = row![].spacing(4);
        for (i, &kind) in EDIT_ACTION_KINDS.iter().enumerate() {
            let active = self.edit_action_kind == kind;
            let label = EDIT_ACTION_LABELS[i];
            let (b, f) = if active { (ACCENT, BG_DEEP) } else { (BG_INSET, TEXT) };
            let btn = button(text(label).size(13))
                .padding([3, 8])
                .style(move |_, _| styled_btn("", b, f))
                .on_press(Message::LayerEditSetActionKind(i));
            kind_row = kind_row.push(btn);
        }

        let target: Element<Message> = match self.edit_action_kind {
            EditActionKind::Keyboard => {
                let mut keys = row![].spacing(2);
                for (code, label) in COMMON_KEYS {
                    let btn = button(text(label).size(12))
                        .padding([2, 6])
                        .style(|_, _| styled_btn("", BG_INSET, TEXT))
                        .on_press(Message::LayerEditSetKeyTarget(code));
                    keys = keys.push(btn);
                }
                keys.into()
            }
            EditActionKind::Mouse | EditActionKind::MouseToggle => {
                let mouse_names = ["左键", "右键", "中键", "前进", "后退"];
                let mut keys = row![].spacing(2);
                for (i, label) in mouse_names.iter().enumerate() {
                    let btn = button(text(*label).size(12))
                        .padding([2, 6])
                        .style(|_, _| styled_btn("", BG_INSET, TEXT))
                        .on_press(Message::LayerEditSetMouseTarget(i));
                    keys = keys.push(btn);
                }
                keys.into()
            }
            EditActionKind::SwitchLayer => {
                let mut keys = row![].spacing(2);
                for (lid, lname) in switch_targets.iter() {
                    let display = layer_display_name(lname);
                    let lid = lid.clone();
                    let btn = button(text(display).size(12))
                        .padding([2, 6])
                        .style(|_, _| styled_btn("", BG_INSET, TEXT))
                        .on_press(Message::LayerEditSetLayerTarget(lid));
                    keys = keys.push(btn);
                }
                keys.into()
            }
            _ => text("该动作无需额外目标").size(13).color(rgb(TEXT_FAINT)).into(),
        };

        let mut sub_row = row![].spacing(2);
        for (code, label) in COMMON_KEYS {
            let active = subs.contains(&code);
            let (b, f) = if active { (ACCENT_DIM, BG_DEEP) } else { (BG_INSET, TEXT) };
            let btn = button(text(label).size(12))
                .padding([2, 6])
                .style(move |_, _| styled_btn("", b, f))
                .on_press(Message::LayerEditToggleSub(code));
            sub_row = sub_row.push(btn);
        }

        let subs_text = if subs.is_empty() {
            "无子命令".to_string()
        } else {
            subs.iter().map(|&k| key_code_to_name(k)).collect::<Vec<_>>().join(" + ")
        };

        let right_col = column![
            row![
                text(format!("编辑层: {}", layer_display_name(&layer_name))).size(17).color(rgb(TEXT)),
                Space::new().width(Length::Fill),
                text(format!("当前: {} ({})", controller_button_display_name(all_btns[self.edit_selected]), desc))
                    .size(13).color(rgb(TEXT_DIM)),
                small_button("清除映射", Message::LayerEditClearMapping),
            ],
            text("动作类型").size(13).color(rgb(TEXT_DIM)),
            kind_row,
            target,
            text(format!("子命令（最多{}个，当前: {}）", KeyMapping::MAX_SUB_COMMANDS, subs_text))
                .size(13).color(rgb(TEXT_DIM)),
            sub_row,
            small_button("关闭层编辑", Message::CloseLayerEdit),
        ]
        .spacing(6);

        row![container(grid).width(180), Space::new().width(12), right_col].into()
    }

    // ---- 辅助方法 ----

    fn edit_layer_context(&self) -> (String, Vec<(String, String)>) {
        let core = self.shared.core.lock().unwrap();
        let name = match &self.edit_target {
            EditTarget::None => return (String::new(), Vec::new()),
            EditTarget::Layer(id) => {
                if id == "Common" {
                    "公共层".to_string()
                } else {
                    core.steam.profile.find_layer(id).map(|l| l.name.clone()).unwrap_or_default()
                }
            }
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

    fn edit_read_mapping(&self) -> (Option<MappedAction>, Vec<i32>) {
        let core = self.shared.core.lock().unwrap();
        let layer = match &self.edit_target {
            EditTarget::None => None,
            EditTarget::Layer(id) => {
                if id == "Common" {
                    core.steam.profile.common_layer()
                } else {
                    core.steam.profile.find_layer(id)
                }
            }
        };
        let btn = all_controller_buttons()[self.edit_selected];
        match layer.and_then(|l| l.button_mappings.get(&btn)) {
            Some(m) => (Some(m.action.clone()), m.sub_commands.clone()),
            None => (None, Vec::new()),
        }
    }

    fn edit_write_action(&mut self, action: MappedAction) {
        if let EditTarget::Layer(ref id) = self.edit_target {
            let id = id.clone();
            let btn = all_controller_buttons()[self.edit_selected];
            if let Ok(mut core) = self.shared.core.lock() {
                let layer = if id == "Common" {
                    core.steam.profile.common_layer_mut()
                } else {
                    core.steam.profile.layers_mut().into_iter().find(|l| l.id == id)
                };
                if let Some(layer) = layer {
                    let subs = layer
                        .button_mappings
                        .get(&btn)
                        .map(|m| m.sub_commands.clone())
                        .unwrap_or_default();
                    layer.button_mappings.insert(btn, KeyMapping { action, sub_commands: subs });
                    core.profile_rev += 1;
                }
            }
        }
    }

    fn edit_clear_mapping(&mut self) {
        if let EditTarget::Layer(ref id) = self.edit_target {
            let id = id.clone();
            let btn = all_controller_buttons()[self.edit_selected];
            if let Ok(mut core) = self.shared.core.lock() {
                let layer = if id == "Common" {
                    core.steam.profile.common_layer_mut()
                } else {
                    core.steam.profile.layers_mut().into_iter().find(|l| l.id == id)
                };
                if let Some(layer) = layer {
                    layer.button_mappings.remove(&btn);
                    core.profile_rev += 1;
                }
            }
        }
    }

    fn edit_toggle_sub(&mut self, key: i32) {
        if let EditTarget::Layer(ref id) = self.edit_target {
            let id = id.clone();
            let btn = all_controller_buttons()[self.edit_selected];
            if let Ok(mut core) = self.shared.core.lock() {
                let layer = if id == "Common" {
                    core.steam.profile.common_layer_mut()
                } else {
                    core.steam.profile.layers_mut().into_iter().find(|l| l.id == id)
                };
                if let Some(layer) = layer {
                    if let Some(m) = layer.button_mappings.get_mut(&btn) {
                        if let Some(pos) = m.sub_commands.iter().position(|&k| k == key) {
                            m.sub_commands.remove(pos);
                        } else if m.sub_commands.len() < KeyMapping::MAX_SUB_COMMANDS {
                            m.sub_commands.push(key);
                        }
                    }
                    core.profile_rev += 1;
                }
            }
        }
    }

    fn edit_set_kind(&mut self, kind: EditActionKind) {
        self.edit_action_kind = kind;
        let action = match kind {
            EditActionKind::WheelUp => Some(MappedAction::wheel_up()),
            EditActionKind::WheelDown => Some(MappedAction::wheel_down()),
            EditActionKind::LookAround => Some(MappedAction::look_around()),
            EditActionKind::MouseMove => Some(MappedAction::mouse_move()),
            _ => None,
        };
        if let Some(a) = action {
            self.edit_write_action(a);
        }
    }
}

// =====================================================================
// 辅助组件
// =====================================================================

fn small_button(label: &str, msg: Message) -> Element<'static, Message> {
    button(text(label.to_owned()).size(13))
        .padding([6, 12])
        .style(|_, _| button::Style {
            background: Some(Background::Color(rgb(BG_INSET))),
            text_color: rgb(TEXT),
            border: iced::Border {
                width: 1.0,
                color: rgb(BORDER),
                ..Default::default()
            }.rounded(4),
            ..Default::default()
        })
        .on_press(msg)
        .into()
}

fn setting_row(label: &str, value: f32, key: SettingKey) -> Element<'static, Message> {
    row![
        text(label.to_owned()).size(13).color(rgb(TEXT_DIM)).width(96),
        button(text("−").size(13))
            .padding([2, 6])
            .style(|_, _| button::Style {
                background: Some(Background::Color(rgb(BG_INSET))),
                text_color: rgb(TEXT),
                border: iced::Border { width: 1.0, color: rgb(BORDER), ..Default::default() }.rounded(3),
                ..Default::default()
            })
            .on_press(Message::AdjustSetting(key, -0.01)),
        text(format!("{:.2}", value)).size(13).color(rgb(TEXT)).width(56),
        button(text("+").size(13))
            .padding([2, 6])
            .style(|_, _| button::Style {
                background: Some(Background::Color(rgb(BG_INSET))),
                text_color: rgb(TEXT),
                border: iced::Border { width: 1.0, color: rgb(BORDER), ..Default::default() }.rounded(3),
                ..Default::default()
            })
            .on_press(Message::AdjustSetting(key, 0.01)),
    ]
    .spacing(4)
    .into()
}

// =====================================================================
// 窗口配置
// =====================================================================

/// 主窗口设置：居中、可缩放
fn main_window_settings() -> iced::window::Settings {
    iced::window::Settings {
        size: iced::Size::new(960.0, 640.0),
        position: iced::window::Position::Centered,
        ..Default::default()
    }
}

/// 悬浮窗设置：无边框、透明、置顶、不可缩放
fn overlay_window_settings() -> iced::window::Settings {
    iced::window::Settings {
        size: iced::Size::new(360.0, 260.0),
        position: iced::window::Position::Default,
        transparent: true,
        decorations: false,
        resizable: false,
        level: iced::window::Level::AlwaysOnTop,
        ..Default::default()
    }
}
