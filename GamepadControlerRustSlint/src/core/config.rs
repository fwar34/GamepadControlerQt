// =====================================================================
// config.rs —— 配置序列化 / 反序列化（JSON）
//
// 等效 Qt 版 ControllerConfig.h/.cpp。
// JSON 格式与安卓版（version=2）同版本号，同一份配置文件可在
// Windows 版与安卓版之间互换使用。
//
// 顶层结构（新格式，含操作集）：
//   {
//     "version": 2,
//     "globalSettings": { ... },
//     "activeOperationSet": "Set1",
//     "operationSets": [ { "id","name","commonLayer","layers" } ]
//   }
// 兼容：旧 v2 配置（顶层直接 commonLayer/layers）加载时自动包装成
// 单个「默认操作集」，实现无缝升级。
//
// 容错策略：对无法识别的动作类型/按钮名/超限子命令，采取"跳过该条
// 映射"的宽松策略；只有 JSON 语法错误、根节点不是对象、版本号不匹配
// 才返回错误。
// =====================================================================

use crate::core::input_types::*;
use crate::core::mapping_types::*;
use serde_json::{json, Value};

pub const CONFIG_VERSION: i64 = 2;

// ---- 动作 type 的字符串常量（与配置文件/安卓版一一对应）----
const K_TYPE_KEYBOARD: &str = "keyboard";
const K_TYPE_MOUSE: &str = "mouse";
const K_TYPE_MOUSE_TOGGLE: &str = "mouseToggle";
const K_TYPE_WHEEL_UP: &str = "wheelUp";
const K_TYPE_WHEEL_DOWN: &str = "wheelDown";
const K_TYPE_SWITCH_LAYER: &str = "switchLayer";
const K_TYPE_MOUSE_MOVE: &str = "mouseMove";
const K_TYPE_LOOK_AROUND: &str = "lookAround";
const K_TYPE_TOGGLE_MAPPING: &str = "toggleMapping";
const K_TYPE_TOGGLE_ON_SCREEN_KEYBOARD: &str = "toggleOnScreenKeyboard";
const K_TYPE_TOGGLE_OVERLAY: &str = "toggleOverlay";

// =====================================================================
// 序列化：ControllerProfile -> JSON 文本
// =====================================================================
pub fn profile_to_json(profile: &ControllerProfile) -> Value {
    let mut gs = json!({
        "deadzone": profile.global_settings.deadzone,
        "lookSensitivity": profile.global_settings.look_sensitivity,
        "cursorSpeed": profile.global_settings.cursor_speed,
        "lookSmoothing": profile.global_settings.look_smoothing,
        "lookAcceleration": profile.global_settings.look_acceleration,
        "invertLookX": profile.global_settings.invert_look_x,
        "invertLookY": profile.global_settings.invert_look_y,
        "overlayX": profile.global_settings.overlay_x,
        "overlayY": profile.global_settings.overlay_y,
        "overlayScale": profile.global_settings.overlay_scale,
        "mainWindowX": profile.global_settings.main_window_x,
        "mainWindowY": profile.global_settings.main_window_y,
        "releaseOnForegroundChange": profile.global_settings.release_on_foreground_change,
        "confirmOnClose": profile.global_settings.confirm_on_close,
    });
    gs.as_object_mut().expect("object");

    let mut sets = Vec::new();
    for set in &profile.operation_sets {
        let mut layers = Vec::new();
        for layer in &set.layers {
            layers.push(layer_to_json(layer));
        }
        sets.push(json!({
            "id": set.id,
            "name": set.name,
            "commonLayer": layer_to_json(&set.common_layer),
            "layers": layers,
        }));
    }

    json!({
        "version": CONFIG_VERSION,
        "globalSettings": gs,
        "activeOperationSet": profile.active_operation_set_id,
        "operationSets": sets,
    })
}

pub fn profile_to_json_string(profile: &ControllerProfile) -> String {
    serde_json::to_string_pretty(&profile_to_json(profile)).unwrap_or_default()
}

// =====================================================================
// 反序列化：JSON 文本 -> ControllerProfile
// 失败（语法错误/非对象根/版本不匹配）返回 Err(msg)。
// =====================================================================
pub fn profile_from_json_str(text: &str) -> Result<ControllerProfile, String> {
    let root: Value =
        serde_json::from_str(text).map_err(|e| format!("Invalid JSON: {}", e))?;
    profile_from_json_value(&root)
}

pub fn profile_from_json_value(root: &Value) -> Result<ControllerProfile, String> {
    if !root.is_object() {
        return Err("Root is not an object".to_string());
    }
    let version = root.get("version").and_then(|v| v.as_i64()).unwrap_or(1);
    if version != CONFIG_VERSION {
        return Err(format!("Unsupported config version: {}", version));
    }

    let mut profile = ControllerProfile {
        operation_sets: Vec::new(),
        active_operation_set_id: String::new(),
        global_settings: GlobalSettings::default(),
    };

    // ---- 全局设置：可缺省，逐字段带默认值读取 ----
    if let Some(gs) = root.get("globalSettings").and_then(|v| v.as_object()) {
        let mut s = GlobalSettings::default();
        s.deadzone = num(gs, "deadzone", 0.15);
        s.look_sensitivity = num(gs, "lookSensitivity", 0.5);
        s.cursor_speed = num(gs, "cursorSpeed", 1.0);
        s.look_smoothing = num(gs, "lookSmoothing", 0.5);
        s.look_acceleration = num(gs, "lookAcceleration", 1.5);
        s.invert_look_x = bool_(gs, "invertLookX", false);
        s.invert_look_y = bool_(gs, "invertLookY", false);
        s.overlay_x = int_(gs, "overlayX", -1);
        s.overlay_y = int_(gs, "overlayY", -1);
        s.overlay_scale = num(gs, "overlayScale", 1.0) as f64;
        s.main_window_x = int_(gs, "mainWindowX", -1);
        s.main_window_y = int_(gs, "mainWindowY", -1);
        s.release_on_foreground_change = bool_(gs, "releaseOnForegroundChange", true);
        s.confirm_on_close = bool_(gs, "confirmOnClose", true);
        profile.global_settings = s;
    }

    // ---- 操作集：新格式 operationSets 数组；旧 v2 格式兼容包装 ----
    let sets_val = root.get("operationSets");
    let parsed_sets = match sets_val {
        Some(Value::Array(arr)) if !arr.is_empty() => {
            let mut sets = Vec::new();
            for v in arr {
                if !v.is_object() {
                    continue;
                }
                let so = v.as_object().unwrap();
                let mut set = OperationSet {
                    id: so.get("id").and_then(|x| x.as_str()).unwrap_or("").to_string(),
                    name: so.get("name").and_then(|x| x.as_str()).unwrap_or("").to_string(),
                    common_layer: OperationLayer::new("Common"),
                    layers: Vec::new(),
                };
                if set.id.is_empty() {
                    set.id = if set.name.is_empty() {
                        "Set".to_string()
                    } else {
                        set.name.clone()
                    };
                }
                // 本操作集的公共层
                set.common_layer = match so.get("commonLayer") {
                    Some(Value::Object(_)) => {
                        parse_layer(so.get("commonLayer").unwrap(), true)
                    }
                    _ => OperationLayer::new("Common"),
                };
                // 本操作集的操作层数组
                if let Some(Value::Array(larr)) = so.get("layers") {
                    for lv in larr {
                        if lv.is_object() {
                            set.layers.push(parse_layer(lv, false));
                        }
                    }
                }
                sets.push(set);
            }
            sets
        }
        _ => {
            // ---- 旧 v2 格式兼容：包装为单个「默认操作集」 ----
            let mut set = OperationSet {
                id: "Set1".to_string(),
                name: "默认操作集".to_string(),
                common_layer: OperationLayer::new("Common"),
                layers: Vec::new(),
            };
            set.common_layer = match root.get("commonLayer") {
                Some(Value::Object(_)) => parse_layer(root.get("commonLayer").unwrap(), true),
                _ => OperationLayer::new("Common"),
            };
            if let Some(Value::Array(arr)) = root.get("layers") {
                for v in arr {
                    if v.is_object() {
                        set.layers.push(parse_layer(v, false));
                    }
                }
            }
            vec![set]
        }
    };
    profile.operation_sets = parsed_sets;

    // ---- 恢复上次激活的操作集；缺失时回退到第一个 ----
    let active = root.get("activeOperationSet").and_then(|v| v.as_str()).unwrap_or("");
    if !profile.set_active_operation_set(active)
        && !profile.operation_sets.is_empty()
    {
        profile.active_operation_set_id = profile.operation_sets[0].id.clone();
    }

    // ---- 兜底保证：至少保留一个有效操作集 ----
    if profile.operation_sets.is_empty() {
        let fallback = OperationSet::create_empty("Set1", "默认操作集");
        profile.operation_sets.push(fallback);
        profile.active_operation_set_id = "Set1".to_string();
    }

    Ok(profile)
}

// =====================================================================
// 层 / 映射 / 动作的序列化与解析
// =====================================================================

fn action_to_json(action: &MappedAction) -> Value {
    match action.r#type {
        ActionType::KeyboardKey => json!({ "type": K_TYPE_KEYBOARD, "keyCode": action.key_code }),
        ActionType::MouseClick => json!({ "type": K_TYPE_MOUSE, "button": mouse_button_name(action.mouse_button) }),
        ActionType::MouseToggle => json!({ "type": K_TYPE_MOUSE_TOGGLE, "button": mouse_button_name(action.mouse_button) }),
        ActionType::WheelUp => json!({ "type": K_TYPE_WHEEL_UP }),
        ActionType::WheelDown => json!({ "type": K_TYPE_WHEEL_DOWN }),
        ActionType::SwitchLayer => json!({ "type": K_TYPE_SWITCH_LAYER, "layerName": action.layer_name.clone().unwrap_or_default() }),
        ActionType::MouseMove => json!({ "type": K_TYPE_MOUSE_MOVE }),
        ActionType::LookAround => json!({ "type": K_TYPE_LOOK_AROUND }),
        ActionType::ToggleMapping => json!({ "type": K_TYPE_TOGGLE_MAPPING }),
        ActionType::ToggleOnScreenKeyboard => json!({ "type": K_TYPE_TOGGLE_ON_SCREEN_KEYBOARD }),
        ActionType::ToggleOverlay => json!({ "type": K_TYPE_TOGGLE_OVERLAY }),
    }
}

fn parse_action(obj: &Value) -> Option<MappedAction> {
    let type_str = obj.get("type").and_then(|v| v.as_str()).unwrap_or("");
    match type_str {
        K_TYPE_KEYBOARD => Some(MappedAction::keyboard_key(obj.get("keyCode").and_then(|v| v.as_i64()).unwrap_or(0) as i32)),
        K_TYPE_MOUSE => {
            let b = mouse_button_from_name(obj.get("button").and_then(|v| v.as_str()).unwrap_or(""))?;
            Some(MappedAction::mouse_click(b))
        }
        K_TYPE_MOUSE_TOGGLE => {
            let b = mouse_button_from_name(obj.get("button").and_then(|v| v.as_str()).unwrap_or(""))?;
            Some(MappedAction::mouse_toggle(b))
        }
        K_TYPE_WHEEL_UP => Some(MappedAction::wheel_up()),
        K_TYPE_WHEEL_DOWN => Some(MappedAction::wheel_down()),
        K_TYPE_SWITCH_LAYER => {
            let name = obj.get("layerName").and_then(|v| v.as_str()).unwrap_or("");
            if name.is_empty() {
                return None;
            }
            Some(MappedAction::switch_layer(name))
        }
        K_TYPE_MOUSE_MOVE => Some(MappedAction::mouse_move()),
        K_TYPE_LOOK_AROUND => Some(MappedAction::look_around()),
        K_TYPE_TOGGLE_MAPPING => Some(MappedAction::toggle_mapping()),
        K_TYPE_TOGGLE_ON_SCREEN_KEYBOARD => Some(MappedAction::toggle_on_screen_keyboard()),
        K_TYPE_TOGGLE_OVERLAY => Some(MappedAction::toggle_overlay()),
        _ => None, // 未知类型
    }
}

fn mapping_to_json(mapping: &KeyMapping) -> Value {
    json!({
        "action": action_to_json(&mapping.action),
        "subCommands": mapping.sub_commands,
    })
}

fn parse_mapping(obj: &Value) -> Option<KeyMapping> {
    let action_val = obj.get("action")?;
    if !action_val.is_object() {
        return None;
    }
    let action = parse_action(action_val)?;

    let mut subs: Vec<i32> = Vec::new();
    if let Some(Value::Array(arr)) = obj.get("subCommands") {
        for v in arr {
            if let Some(n) = v.as_i64() {
                subs.push(n as i32);
            }
        }
    }
    if subs.len() > KeyMapping::MAX_SUB_COMMANDS {
        return None; // 子命令超限，跳过
    }
    Some(KeyMapping { action, sub_commands: subs })
}

fn layer_to_json(layer: &OperationLayer) -> Value {
    let mut mappings = serde_json::Map::new();
    for (button, mapping) in &layer.button_mappings {
        mappings.insert(
            controller_button_name(*button).to_string(),
            mapping_to_json(mapping),
        );
    }
    let mut obj = serde_json::Map::new();
    // id 为空时回退写 name，保证老数据也有可读 id
    obj.insert(
        "id".to_string(),
        Value::String(if layer.id.is_empty() {
            layer.name.clone()
        } else {
            layer.id.clone()
        }),
    );
    obj.insert("name".to_string(), Value::String(layer.name.clone()));
    if layer.has_trigger_button {
        obj.insert(
            "triggerButton".to_string(),
            Value::String(controller_button_name(layer.trigger_button).to_string()),
        );
    }
    obj.insert("buttonMappings".to_string(), Value::Object(mappings));
    Value::Object(obj)
}

/// 解析一个层。is_common 指示是否公共层（公共层不解析 triggerButton）。
fn parse_layer(v: &Value, is_common: bool) -> OperationLayer {
    let obj = match v.as_object() {
        Some(o) => o,
        None => return OperationLayer::new("Common"),
    };
    let mut layer = OperationLayer::new("Common");
    layer.name = obj.get("name").and_then(|x| x.as_str()).unwrap_or("").to_string();
    // id：优先读取持久化的 id；缺失时回退到 name
    layer.id = obj.get("id").and_then(|x| x.as_str()).unwrap_or("").to_string();
    if layer.id.is_empty() {
        layer.id = if is_common {
            "Common".to_string()
        } else {
            layer.name.clone()
        };
    }

    // 触发按键：仅操作层有
    if !is_common {
        if let Some(tb) = obj.get("triggerButton").and_then(|x| x.as_str()) {
            if let Some(btn) = controller_button_from_name(tb) {
                layer.has_trigger_button = true;
                layer.trigger_button = btn;
            }
        }
    }

    // 按钮映射表：逐条解析，非法条目直接跳过
    if let Some(Value::Object(mappings)) = obj.get("buttonMappings") {
        for (name, mval) in mappings {
            if let Some(btn) = controller_button_from_name(name) {
                if mval.is_object() {
                    if let Some(mapping) = parse_mapping(mval) {
                        layer.button_mappings.insert(btn, mapping);
                    }
                }
            }
        }
    }
    layer
}

// ---- 数值读取辅助 ----
fn num(m: &serde_json::Map<String, Value>, key: &str, def: f32) -> f32 {
    m.get(key)
        .and_then(|v| v.as_f64())
        .map(|x| x as f32)
        .unwrap_or(def)
}
fn int_(m: &serde_json::Map<String, Value>, key: &str, def: i32) -> i32 {
    m.get(key)
        .and_then(|v| v.as_i64())
        .map(|x| x as i32)
        .unwrap_or(def)
}
fn bool_(m: &serde_json::Map<String, Value>, key: &str, def: bool) -> bool {
    m.get(key).and_then(|v| v.as_bool()).unwrap_or(def)
}
