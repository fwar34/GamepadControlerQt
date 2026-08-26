// =====================================================================
// main_window.rs —— 主窗口（Dioxus 版）
//
// 布局（深色主题，与 Qt 版一致）：
//   ┌──────────────────────────────────────────────┐
//   │ 标题行：Gamepad 键鼠映射 · 连接状态 · 映射状态 │
//   ├──────────────┬───────────────────────────────┤
//   │ 操作集 chip  │  当前操作集/当前层             │
//   │ [+添加][复制]│  全局设置（死区/灵敏度/平滑/加速）│
//   │ 公共层 按钮  │  [开始映射][保存][重置默认]     │
//   │ Layer1..10   │  [显示悬浮窗][使用说明][退出]   │
//   └──────────────┴───────────────────────────────┘
//
// 刷新机制：原生线程每 50ms 轮询共享 core 的快照，更新 Signal 触发重绘。
// 视图路由：主界面 / 层编辑 / 使用说明（单窗口内切换）。
// =====================================================================

use crate::core::input_types::mouse_button_display_name;
use crate::ui::help::HelpView;
use crate::ui::layer_edit::LayerEditView;
use crate::ui::shared::AppShared;
use crate::ui::theme::*;
use dioxus::desktop::use_window;
use dioxus::prelude::*;
use futures_util::stream::StreamExt;
use std::sync::atomic::Ordering;
use std::sync::Arc;
use std::time::Duration;

/// 主窗口内切换的视图
#[derive(Clone)]
enum View {
    Main,
    LayerEdit(String),
    Help,
}

#[derive(Clone, Copy, PartialEq)]
enum SettingKey {
    Deadzone,
    LookSensitivity,
    LookSmoothing,
    LookAcceleration,
}

/// 输入框用途：重命名操作集 / 复制操作集（新名字）
#[derive(Clone)]
enum InputMode {
    Rename(String),
    Copy(String),
}

/// 显示缓存（由 50ms 轮询更新）
#[derive(Clone, Default)]
struct MainUi {
    connected: bool,
    layer_name: String,
    set_name: String,
    mouse_toggle: Option<String>,
    profile_rev: u64,
    running: bool,
}

/// 访问全局共享状态（见 main.rs）
fn shared() -> Arc<AppShared> {
    crate::SHARED.get().expect("SHARED not set").clone()
}

// ---------------------------------------------------------------------
// 核心动作（直接改共享 core）
// ---------------------------------------------------------------------

fn adjust_setting(s: &Arc<AppShared>, key: SettingKey, delta: f32) {
    let mut core = s.core.lock().unwrap();
    let mut gs = core.steam.profile.global_settings.clone();
    match key {
        SettingKey::Deadzone => gs.deadzone = (gs.deadzone + delta).clamp(0.0, 0.5),
        SettingKey::LookSensitivity => {
            gs.look_sensitivity = (gs.look_sensitivity + delta).clamp(0.05, 2.0)
        }
        SettingKey::LookSmoothing => {
            gs.look_smoothing = (gs.look_smoothing + delta).clamp(0.0, 0.95)
        }
        SettingKey::LookAcceleration => {
            gs.look_acceleration = (gs.look_acceleration + delta).clamp(0.5, 3.0)
        }
    }
    core.update_global_settings(gs);
}

fn commit_input(
    s: &Arc<AppShared>,
    mut input_mode: Signal<Option<InputMode>>,
    input_text: Signal<String>,
) {
    let name = input_text.read().trim().to_string();
    let mode = input_mode.read().clone();
    if !name.is_empty() {
        if let Some(m) = mode {
            let mut core = s.core.lock().unwrap();
            match m {
                InputMode::Rename(id) => {
                    core.rename_operation_set(&id, &name);
                }
                InputMode::Copy(id) => {
                    core.copy_operation_set(&id, &name);
                }
            }
        }
    }
    input_mode.set(None);
}

// ---------------------------------------------------------------------
// 主窗口根组件
// ---------------------------------------------------------------------

#[component]
pub fn MainApp() -> Element {
    let s = shared();

    let mut ui = use_signal(MainUi::default);
    let mut view = use_signal(|| View::Main);
    let mut input_mode = use_signal(|| None::<InputMode>);
    let mut input_text = use_signal(String::new);
    let window = use_window();

    // 50ms 轮询：后台线程读共享 core 快照 → UnboundedSender；
    // use_coroutine 在 UI 线程（dioxus runtime）接收快照并写 Signal 触发重绘。
    // Signal 非 Send，只能在 runtime 线程写；UnboundedSender 可跨线程。
    let poll = use_coroutine(move |mut rx: UnboundedReceiver<MainUi>| async move {
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
            let (connected, layer_name, set_name, profile_rev, mouse_toggle, running) = {
                let core = s.core.lock().unwrap();
                let mt = core
                    .mapper
                    .toggled_mouse_buttons
                    .values()
                    .next()
                    .map(|mb| format!("长按锁存: {}", mouse_button_display_name(*mb)));
                (
                    core.connected,
                    core.steam.active_layer_name().to_string(),
                    core.steam.profile.active_operation_set_name(),
                    core.profile_rev,
                    mt,
                    s.running.load(Ordering::SeqCst),
                )
            };
            let _ = poll_tx.unbounded_send(MainUi {
                connected,
                layer_name,
                set_name,
                profile_rev,
                mouse_toggle,
                running,
            });
        });
    });

    // ---- 视图路由：使用说明 / 层编辑 ----
    let v = view.read().clone();
    match v {
        View::Help => {
            return rsx! {
                HelpView { on_back: move |_| view.set(View::Main) }
            };
        }
        View::LayerEdit(id) => {
            return rsx! {
                LayerEditView {
                    layer_id: id,
                    on_back: move |_| view.set(View::Main),
                }
            };
        }
        View::Main => {}
    }

    // ---- 从 core 读取配置快照（render 时读取；配置变化由 profile_rev 驱动重渲染）----
    let (sets, active_set_id, layers, gs) = {
        let core = s.core.lock().unwrap();
        let sets = core
            .steam
            .profile
            .operation_sets
            .iter()
            .map(|o| (o.id.clone(), o.name.clone()))
            .collect::<Vec<_>>();
        let active_set_id = core.steam.profile.active_operation_set_id.clone();
        let layers = core
            .steam
            .profile
            .layers()
            .iter()
            .map(|l| (l.id.clone(), l.name.clone(), core.steam.is_layer_active(&l.id)))
            .collect::<Vec<_>>();
        let gs = core.steam.profile.global_settings.clone();
        (sets, active_set_id, layers, gs)
    };

    // 提取状态值（读守卫不能跨进闭包，先拷贝出来再释放）
    let state = ui.read();
    let state_connected = state.connected;
    let state_running = state.running;
    let state_set_name = state.set_name.clone();
    let state_layer_name = state.layer_name.clone();
    let state_mouse_toggle = state.mouse_toggle.clone();
    drop(state);

    // ---- 标题行状态 ----
    let status_text = if state_connected {
        if state_running {
            "● 已连接 · 映射运行中".to_string()
        } else {
            "● 已连接 · 已停止".to_string()
        }
    } else {
        "○ 手柄未连接".to_string()
    };
    let status_color = if state_connected { hex(OK) } else { hex(TEXT_FAINT) };

    let header = rsx! {
        div {
            style: "display:flex;flex-direction:row;align-items:center;gap:12px;padding:4px 0;",
            div { style: format!("font-size:17px;color:{};font-weight:600;", hex(TEXT)), "Gamepad 键鼠映射" }
            div { style: "flex:1;" }
            div { style: format!("font-size:13px;color:{};", status_color), "{status_text}" }
        }
    };

    // ---- 输入行（重命名/复制时显示）----
    let input_row = if let Some(mode) = input_mode.read().clone() {
        let hint = match &mode {
            InputMode::Rename(_) => "重命名操作集",
            InputMode::Copy(_) => "复制为新操作集",
        }
        .to_string();
        let s = s.clone();
        rsx! {
            div {
                style: format!(
                    "display:flex;flex-direction:row;align-items:center;gap:8px;padding:6px;\
                     border-radius:8px;border:1px solid {};background:{};",
                    hex(BORDER), hex(BG_INSET)
                ),
                div { style: format!("color:{};font-size:13px;", hex(TEXT_DIM)), "{hint}" }
                input {
                    style: format!(
                        "flex:1;background:{};border:1px solid {};border-radius:6px;color:{};\
                         padding:5px 8px;font-size:13px;font-family:{};",
                        hex(BG_DEEP), hex(BORDER_STRONG), hex(TEXT), FONT
                    ),
                    value: "{input_text}",
                    oninput: move |e| input_text.set(e.value()),
                }
                button { style: btn_css(true), onclick: move |_| commit_input(&s, input_mode, input_text), "确定" }
                button {
                    style: btn_css(false),
                    onclick: move |_| input_mode.set(None),
                    "取消",
                }
            }
        }
    } else {
        rsx! {}
    };

    // ---- 操作集管理按钮 ----
    let s_add = s.clone();
    let btn_add = rsx! {
        button {
            style: btn_css(false),
            onclick: move |_| {
                if let Ok(mut core) = s_add.core.lock() {
                    core.add_operation_set();
                }
            },
            "添加",
        }
    };
    let aid_copy = active_set_id.clone();
    let sn_copy = state_set_name.clone();
    let btn_copy = rsx! {
        button {
            style: btn_css(false),
            onclick: move |_| {
                input_text.set(format!("{} - 副本", sn_copy));
                input_mode.set(Some(InputMode::Copy(aid_copy.clone())));
            },
            "复制",
        }
    };
    let aid_rename = active_set_id.clone();
    let btn_rename = rsx! {
        button {
            style: btn_css(false),
            onclick: move |_| {
                input_text.set(String::new());
                input_mode.set(Some(InputMode::Rename(aid_rename.clone())));
            },
            "重命名",
        }
    };
    let s_delete = s.clone();
    let aid_delete = active_set_id.clone();
    let btn_delete = rsx! {
        button {
            style: btn_css(false),
            onclick: move |_| {
                if let Ok(mut core) = s_delete.core.lock() {
                    core.delete_operation_set(&aid_delete);
                }
            },
            "删除",
        }
    };

    // ---- 设置行 ----
    let slider_row = |label: &str, value: f32, key: SettingKey| {
        let l = label.to_string();
        let s_minus = s.clone();
        let s_plus = s.clone();
        rsx! {
            div {
                style: "display:flex;flex-direction:row;align-items:center;gap:8px;",
                div { style: format!("width:92px;color:{};font-size:13px;", hex(TEXT_DIM)), "{l}" }
                button {
                    style: btn_css(false),
                    onclick: move |_| adjust_setting(&s_minus, key, -0.01),
                    "-",
                }
                div {
                    style: format!("width:52px;color:{};font-size:13px;text-align:center;", hex(TEXT)),
                    {format!("{:.2}", value)},
                }
                button {
                    style: btn_css(false),
                    onclick: move |_| adjust_setting(&s_plus, key, 0.01),
                    "+",
                }
            }
        }
    };
    let settings_col = rsx! {
        div { style: "display:flex;flex-direction:column;gap:8px;",
            {slider_row("死区", gs.deadzone, SettingKey::Deadzone)}
            {slider_row("视角灵敏度", gs.look_sensitivity, SettingKey::LookSensitivity)}
            {slider_row("视角平滑", gs.look_smoothing, SettingKey::LookSmoothing)}
            {slider_row("视角加速", gs.look_acceleration, SettingKey::LookAcceleration)}
        }
    };

    // ---- 当前信息 ----
    let mt_text = state_mouse_toggle
        .clone()
        .unwrap_or_else(|| "无长按锁存".to_string());
    let mt_color = if state_mouse_toggle.is_some() {
        hex(WARN)
    } else {
        hex(TEXT_FAINT)
    };
    let info_col = rsx! {
        div { style: "display:flex;flex-direction:column;gap:2px;",
            div {
                style: format!("font-size:16px;color:{};", hex(TEXT)),
                {format!("当前操作集: {}", state_set_name)},
            }
            div {
                style: format!("font-size:16px;color:{};", hex(TEXT)),
                {format!("当前层: {}", state_layer_name)},
            }
            div { style: format!("font-size:13px;color:{};", mt_color), "{mt_text}" }
        }
    };

    // ---- 操作按钮 ----
    let toggle_text = if state_running { "停止映射" } else { "开始映射" };
    let toggle_color = if state_running { hex(DANGER) } else { hex(ACCENT) };
    let s_toggle = s.clone();
    let btn_toggle = rsx! {
        button {
            style: format!(
                "background:{};border:1px solid {};color:{};border-radius:6px;padding:8px 20px;\
                 cursor:pointer;font-size:16px;font-family:{};",
                toggle_color, toggle_color, hex(BG_DEEP), FONT
            ),
            onclick: move |_| {
                if s_toggle.running.load(Ordering::SeqCst) {
                    s_toggle.stop_mapping();
                } else {
                    s_toggle.start_mapping();
                }
            },
            "{toggle_text}",
        }
    };
    let s_save = s.clone();
    let btn_save = rsx! {
        button {
            style: btn_css(false),
            onclick: move |_| {
                let profile = { let core = s_save.core.lock().unwrap(); core.steam.profile.clone() };
                crate::core::config_manager::save(&profile);
            },
            "保存配置",
        }
    };
    let s_reset = s.clone();
    let btn_reset = rsx! {
        button {
            style: btn_css(false),
            onclick: move |_| {
                crate::core::config_manager::reset_to_default();
                let def = crate::core::config_manager::load();
                if let Ok(mut core) = s_reset.core.lock() {
                    core.load_profile(def);
                }
            },
            "重置默认",
        }
    };
    let overlay_visible = s.overlay_visible.load(Ordering::SeqCst);
    let s_overlay = s.clone();
    let btn_overlay = rsx! {
        button {
            style: btn_css(false),
            onclick: move |_| {
                let cur = s_overlay.overlay_visible.load(Ordering::SeqCst);
                s_overlay.overlay_visible.store(!cur, Ordering::SeqCst);
            },
            if overlay_visible { "关闭悬浮窗" } else { "显示悬浮窗" },
        }
    };
    let btn_help = rsx! {
        button { style: btn_css(false), onclick: move |_| view.set(View::Help), "使用说明" }
    };
    let s_quit = s.clone();
    let btn_quit = rsx! {
        button {
            style: btn_css(false),
            onclick: move |_| {
                s_quit.stop_mapping();
                window.close();
            },
            "退出",
        }
    };

    let right_col = rsx! {
        div { style: "display:flex;flex-direction:column;gap:12px;flex:1;",
            {info_col}
            div { style: format!("font-size:13px;color:{};", hex(TEXT_DIM)), "全局设置" }
            {settings_col}
            div { style: "display:flex;flex-direction:row;gap:8px;align-items:center;",
                {btn_toggle} {btn_save} {btn_reset}
            }
            div { style: "display:flex;flex-direction:row;gap:8px;align-items:center;",
                {btn_overlay} {btn_help} {btn_quit}
            }
        }
    };

    let left_col = rsx! {
        div { style: "display:flex;flex-direction:column;gap:8px;width:260px;flex-shrink:0;",
            div { style: format!("font-size:13px;color:{};", hex(TEXT_DIM)), "操作集管理" }
            div { style: "display:flex;flex-direction:row;flex-wrap:wrap;gap:6px;align-items:center;",
                div { style: format!("color:{};font-size:13px;flex-shrink:0;", hex(TEXT_DIM)), "操作集" }
                {sets.iter().map(|(id, name)| {
                    let idc = id.clone();
                    let s_map = s.clone();
                    let active = *id == active_set_id;
                    rsx! {
                        div {
                            key: "{id}",
                            style: chip_css(active),
                            onclick: move |_| {
                                if let Ok(mut core) = s_map.core.lock() {
                                    core.switch_operation_set(&idc);
                                }
                            },
                            "{name}",
                        }
                    }
                })}
            }
            div { style: "display:flex;flex-direction:row;flex-wrap:wrap;gap:6px;align-items:center;",
                {btn_add} {btn_copy} {btn_rename} {btn_delete}
            }
            div {
                style: "display:flex;flex-direction:column;gap:6px;flex:1;overflow-y:auto;padding-top:6px;",
                div {
                    style: layer_btn_css(false),
                    onclick: move |_| view.set(View::LayerEdit("Common".to_string())),
                    "公共层",
                }
                {layers.iter().map(|(id, name, active)| {
                    let idc = id.clone();
                    let activec = *active;
                    rsx! {
                        div {
                            key: "{id}",
                            style: layer_btn_css(activec),
                            onclick: move |_| view.set(View::LayerEdit(idc.clone())),
                            "{name}",
                        }
                    }
                })}
            }
        }
    };

    rsx! {
        div {
            style: format!("{}display:flex;flex-direction:column;padding:16px;gap:8px;", root_css()),
            {header}
            {input_row}
            div { style: "display:flex;flex-direction:row;gap:12px;flex:1;min-height:0;",
                div { style: format!("{}display:flex;flex-direction:column;", panel_css()), {left_col} }
                div { style: format!("{}display:flex;flex-direction:column;flex:1;", panel_css()), {right_col} }
            }
        }
    }
}
