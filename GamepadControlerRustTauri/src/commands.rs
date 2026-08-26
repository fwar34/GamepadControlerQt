// =====================================================================
// commands.rs —— Tauri IPC 命令（前端 WebView <-> 后端）
//
// 前端通过 window.__TAURI__.core.invoke("命令名", 参数) 调用；
// 所有命令运行在 Tauri 主线程，内部短时 lock 共享 core。
// =====================================================================

use crate::core::config_manager;
use crate::core::input_types::*;
use crate::core::mapping_types::{ActionType, KeyMapping, MappedAction};
use crate::AppState;
use serde::{Deserialize, Serialize};
use std::sync::atomic::Ordering;
use tauri::{AppHandle, Manager, State};

// ---------------------------------------------------------------------
// 主窗口快照
// ---------------------------------------------------------------------

#[derive(Serialize)]
pub struct SetInfo {
    id: String,
    name: String,
}

#[derive(Serialize)]
pub struct LayerInfo {
    id: String,
    name: String,
    active: bool,
}

#[derive(Serialize)]
pub struct Snapshot {
    connected: bool,
    running: bool,
    active_set_id: String,
    active_set_name: String,
    layer_name: String,
    sets: Vec<SetInfo>,
    layers: Vec<LayerInfo>,
    mouse_toggle: Option<String>,
    deadzone: f32,
    look_sensitivity: f32,
    look_smoothing: f32,
    look_acceleration: f32,
}

// ---------------------------------------------------------------------
// 悬浮窗快照
// ---------------------------------------------------------------------

#[derive(Serialize)]
pub struct MappingRow {
    button: String,
    desc: String,
    held: bool,
}

#[derive(Serialize)]
pub struct OverlaySnapshot {
    set_name: String,
    layer_name: String,
    connected: bool,
    pressed: Vec<String>,
    mouse_toggle: bool,
    mappings: Vec<MappingRow>,
}

// ---------------------------------------------------------------------
// 层编辑快照
// ---------------------------------------------------------------------

#[derive(Serialize)]
pub struct SwitchTarget {
    id: String,
    name: String,
    display: String,
}

#[derive(Serialize)]
pub struct ButtonGridItem {
    name: String,
    display: String,
    pressed: bool,
}

#[derive(Serialize)]
pub struct LayerEditSnapshot {
    layer_name: String,
    switch_targets: Vec<SwitchTarget>,
    buttons: Vec<ButtonGridItem>,
}

#[derive(Serialize)]
pub struct MappingView {
    kind: String,
    desc: String,
    subs: Vec<String>,
    has_mapping: bool,
}

impl MappingView {
    fn none() -> Self {
        Self {
            kind: "keyboard".into(),
            desc: "（无映射）".into(),
            subs: Vec::new(),
            has_mapping: false,
        }
    }
}

// ---------------------------------------------------------------------
// 层查找辅助（与 gpui 版 layer_edit.rs 一致）
// ---------------------------------------------------------------------
fn find_layer_ref<'a>(
    profile: &'a crate::core::mapping_types::ControllerProfile,
    id: &str,
) -> Option<&'a crate::core::mapping_types::OperationLayer> {
    if id == "Common" {
        profile.common_layer()
    } else {
        profile.layers().into_iter().find(|l| l.id == id)
    }
}

fn find_layer_mut<'a>(
    profile: &'a mut crate::core::mapping_types::ControllerProfile,
    id: &str,
) -> Option<&'a mut crate::core::mapping_types::OperationLayer> {
    if id == "Common" {
        profile.common_layer_mut()
    } else {
        profile.layers_mut().into_iter().find(|l| l.id == id)
    }
}

// ---------------------------------------------------------------------
// 动作描述 / 类型串（与 gpui 版 layer_edit.rs 一致）
// ---------------------------------------------------------------------
fn describe_action(a: &MappedAction) -> String {
    match a.r#type {
        ActionType::KeyboardKey => format!("键盘: {}", key_code_to_name(a.key_code)),
        ActionType::MouseClick => format!("鼠标: {}", mouse_button_display_name(a.mouse_button)),
        ActionType::MouseToggle => {
            format!("鼠标长按: {}", mouse_button_display_name(a.mouse_button))
        }
        ActionType::WheelUp => "滚轮上".to_string(),
        ActionType::WheelDown => "滚轮下".to_string(),
        ActionType::SwitchLayer => {
            format!("切换到层: {}", a.layer_name.clone().unwrap_or_default())
        }
        ActionType::LookAround => "视角控制（右摇杆）".to_string(),
        ActionType::MouseMove => "鼠标移动（左摇杆）".to_string(),
        _ => "（未知）".to_string(),
    }
}

fn kind_str(a: &MappedAction) -> String {
    match a.r#type {
        ActionType::KeyboardKey => "keyboard",
        ActionType::MouseClick => "mouse",
        ActionType::MouseToggle => "mousetoggle",
        ActionType::WheelUp => "wheelup",
        ActionType::WheelDown => "wheeldown",
        ActionType::SwitchLayer => "switchlayer",
        ActionType::LookAround => "lookaround",
        ActionType::MouseMove => "mousemove",
        _ => "keyboard",
    }
    .to_string()
}

// ---------------------------------------------------------------------
// 主窗口：整体快照
// ---------------------------------------------------------------------
#[tauri::command]
pub fn get_snapshot(state: State<'_, AppState>) -> Snapshot {
    let running = state.shared.running.load(Ordering::SeqCst);
    let core = state.shared.core.lock().unwrap();
    let connected = core.connected;
    let layer_name = core.steam.active_layer_name().to_string();
    let active_set_id = core.steam.profile.active_operation_set_id.clone();
    let active_set_name = core.steam.profile.active_operation_set_name();
    let sets: Vec<SetInfo> = core
        .steam
        .profile
        .operation_sets
        .iter()
        .map(|s| SetInfo {
            id: s.id.clone(),
            name: s.name.clone(),
        })
        .collect();
    let layers: Vec<LayerInfo> = core
        .steam
        .profile
        .layers()
        .iter()
        .map(|l| LayerInfo {
            id: l.id.clone(),
            name: l.name.clone(),
            active: core.steam.is_layer_active(&l.id),
        })
        .collect();
    let mouse_toggle = core
        .mapper
        .toggled_mouse_buttons
        .values()
        .next()
        .map(|mb| format!("长按锁存: {}", mouse_button_display_name(*mb)));
    let gs = &core.steam.profile.global_settings;
    Snapshot {
        connected,
        running,
        active_set_id,
        active_set_name,
        layer_name,
        sets,
        layers,
        mouse_toggle,
        deadzone: gs.deadzone,
        look_sensitivity: gs.look_sensitivity,
        look_smoothing: gs.look_smoothing,
        look_acceleration: gs.look_acceleration,
    }
}

// ---------------------------------------------------------------------
// 悬浮窗：整体快照（含当前层映射列表）
// ---------------------------------------------------------------------
#[tauri::command]
pub fn get_overlay_snapshot(state: State<'_, AppState>) -> OverlaySnapshot {
    let core = state.shared.core.lock().unwrap();
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
    let mut mappings: Vec<MappingRow> = Vec::new();
    if let Some(lr) = layer_ref {
        for b in all_controller_buttons() {
            if let Some(m) = lr.get_mapping(b) {
                mappings.push(MappingRow {
                    button: controller_button_display_name(b).to_string(),
                    desc: m.describe(),
                    held: held.contains(&b),
                });
            }
        }
    }
    OverlaySnapshot {
        set_name,
        layer_name,
        connected,
        pressed,
        mouse_toggle,
        mappings,
    }
}

// ---------------------------------------------------------------------
// 启停
// ---------------------------------------------------------------------
#[tauri::command]
pub fn start_mapping(state: State<'_, AppState>) {
    state.shared.start_mapping();
}

#[tauri::command]
pub fn stop_mapping(state: State<'_, AppState>) {
    state.shared.stop_mapping();
}

// ---------------------------------------------------------------------
// 操作集管理
// ---------------------------------------------------------------------
#[tauri::command]
pub fn add_operation_set(state: State<'_, AppState>) -> String {
    let mut core = state.shared.core.lock().unwrap();
    core.add_operation_set()
}

#[tauri::command]
pub fn rename_operation_set(state: State<'_, AppState>, set_id: String, name: String) -> bool {
    let mut core = state.shared.core.lock().unwrap();
    core.rename_operation_set(&set_id, &name)
}

#[tauri::command]
pub fn copy_operation_set(state: State<'_, AppState>, set_id: String, name: String) -> bool {
    let mut core = state.shared.core.lock().unwrap();
    core.copy_operation_set(&set_id, &name)
}

#[tauri::command]
pub fn delete_operation_set(state: State<'_, AppState>, set_id: String) -> bool {
    let mut core = state.shared.core.lock().unwrap();
    core.delete_operation_set(&set_id)
}

#[tauri::command]
pub fn switch_operation_set(state: State<'_, AppState>, set_id: String) -> bool {
    let mut core = state.shared.core.lock().unwrap();
    core.switch_operation_set(&set_id)
}

// ---------------------------------------------------------------------
// 全局设置
// ---------------------------------------------------------------------
#[derive(Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SettingKey {
    Deadzone,
    LookSensitivity,
    LookSmoothing,
    LookAcceleration,
}

#[tauri::command]
pub fn adjust_setting(state: State<'_, AppState>, key: SettingKey, delta: f32) {
    let mut core = state.shared.core.lock().unwrap();
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

// ---------------------------------------------------------------------
// 配置保存 / 重置
// ---------------------------------------------------------------------
#[tauri::command]
pub fn save_config(state: State<'_, AppState>) {
    let profile = {
        let core = state.shared.core.lock().unwrap();
        core.steam.profile.clone()
    };
    config_manager::save(&profile);
}

#[tauri::command]
pub fn reset_config(state: State<'_, AppState>) {
    config_manager::reset_to_default();
    let def = config_manager::load();
    let mut core = state.shared.core.lock().unwrap();
    core.load_profile(def);
}

// ---------------------------------------------------------------------
// 悬浮窗显隐 / 退出
// ---------------------------------------------------------------------
#[tauri::command]
pub fn toggle_overlay(app: AppHandle, state: State<'_, AppState>) -> bool {
    let Some(win) = app.get_webview_window("overlay") else {
        return false;
    };
    let new = !state.overlay_visible.load(Ordering::SeqCst);
    state.overlay_visible.store(new, Ordering::SeqCst);
    if new {
        let _ = win.show();
    } else {
        let _ = win.hide();
    }
    new
}

#[tauri::command]
pub fn quit_app(app: AppHandle) {
    let state = app.state::<AppState>();
    state.shared.stop_mapping(); // 停止映射并释放全部注入
    app.exit(0);
}

// ---------------------------------------------------------------------
// 层编辑
// ---------------------------------------------------------------------
#[tauri::command]
pub fn get_layer_edit_snapshot(state: State<'_, AppState>, layer_id: String) -> LayerEditSnapshot {
    let core = state.shared.core.lock().unwrap();
    let layer_name = if layer_id == "Common" {
        "公共层".to_string()
    } else {
        find_layer_ref(&core.steam.profile, &layer_id)
            .map(|l| l.name.clone())
            .unwrap_or_default()
    };
    let switch_targets: Vec<SwitchTarget> = core
        .steam
        .profile
        .layers()
        .iter()
        .map(|l| SwitchTarget {
            id: l.id.clone(),
            name: l.name.clone(),
            display: layer_display_name(&l.name),
        })
        .collect();
    let held = core.steam.held_buttons();
    let buttons: Vec<ButtonGridItem> = all_controller_buttons()
        .into_iter()
        .map(|b| ButtonGridItem {
            name: controller_button_name(b).to_string(),
            display: controller_button_display_name(b).to_string(),
            pressed: held.contains(&b),
        })
        .collect();
    LayerEditSnapshot {
        layer_name,
        switch_targets,
        buttons,
    }
}

#[tauri::command]
pub fn get_mapping(state: State<'_, AppState>, layer_id: String, button: String) -> MappingView {
    let Some(b) = controller_button_from_name(&button) else {
        return MappingView::none();
    };
    let core = state.shared.core.lock().unwrap();
    let Some(layer) = find_layer_ref(&core.steam.profile, &layer_id) else {
        return MappingView::none();
    };
    match layer.get_mapping(b) {
        Some(m) => MappingView {
            kind: kind_str(&m.action),
            desc: describe_action(&m.action),
            subs: m.sub_commands.iter().map(|&k| key_code_to_name(k)).collect(),
            has_mapping: true,
        },
        None => MappingView::none(),
    }
}

#[tauri::command]
pub fn set_mapping(
    state: State<'_, AppState>,
    layer_id: String,
    button: String,
    kind: String,
    key_code: Option<i32>,
    mouse_button: Option<String>,
    layer_name: Option<String>,
) {
    let Some(b) = controller_button_from_name(&button) else {
        return;
    };
    let action: Option<MappedAction> = match kind.as_str() {
        "keyboard" => key_code.map(MappedAction::keyboard_key),
        "mouse" => mouse_button
            .as_deref()
            .and_then(mouse_button_from_name)
            .map(MappedAction::mouse_click),
        "mousetoggle" => mouse_button
            .as_deref()
            .and_then(mouse_button_from_name)
            .map(MappedAction::mouse_toggle),
        "wheelup" => Some(MappedAction::wheel_up()),
        "wheeldown" => Some(MappedAction::wheel_down()),
        "switchlayer" => layer_name.map(|n| MappedAction::switch_layer(&n)),
        "lookaround" => Some(MappedAction::look_around()),
        "mousemove" => Some(MappedAction::mouse_move()),
        _ => None,
    };
    let Some(action) = action else {
        return;
    };
    let mut core = state.shared.core.lock().unwrap();
    if let Some(layer) = find_layer_mut(&mut core.steam.profile, &layer_id) {
        // 写入动作时保留已有子命令
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
}

#[tauri::command]
pub fn clear_mapping(state: State<'_, AppState>, layer_id: String, button: String) {
    let Some(b) = controller_button_from_name(&button) else {
        return;
    };
    let mut core = state.shared.core.lock().unwrap();
    if let Some(layer) = find_layer_mut(&mut core.steam.profile, &layer_id) {
        layer.button_mappings.remove(&b);
        core.profile_rev += 1;
    }
}

#[tauri::command]
pub fn toggle_sub(state: State<'_, AppState>, layer_id: String, button: String, key_code: i32) {
    let Some(b) = controller_button_from_name(&button) else {
        return;
    };
    let mut core = state.shared.core.lock().unwrap();
    if let Some(layer) = find_layer_mut(&mut core.steam.profile, &layer_id) {
        if let Some(m) = layer.button_mappings.get_mut(&b) {
            if let Some(pos) = m.sub_commands.iter().position(|&k| k == key_code) {
                m.sub_commands.remove(pos);
            } else if m.sub_commands.len() < KeyMapping::MAX_SUB_COMMANDS {
                m.sub_commands.push(key_code);
            }
        }
        core.profile_rev += 1;
    }
}
