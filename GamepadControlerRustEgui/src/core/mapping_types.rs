// =====================================================================
// mapping_types.rs —— 映射数据模型
//
// 等效 Qt 版 MappingTypes.h（MappedAction / KeyMapping / OperationLayer /
// GlobalSettings / OperationSet / ControllerProfile）。
//
// 三层结构总览（从高到低）：
//   操作集（OperationSet）—— 最顶层容器，由 1 个公共层 + 最多 10 个
//     操作层组成；切换操作集时，其下所有操作层整体切换。
//   公共层（common_layer）—— 始终激活，优先度最低，作为兜底映射；
//   操作层（layers）—— 通过公共层的 SwitchLayer 映射按住激活、松开回退。
//  按键查询顺序（get_effective_mapping）：最后激活的操作层 -> ... -> 公共层。
// =====================================================================

use crate::core::input_types::*;
use std::collections::HashMap;

/// 单个操作集内最多操作层数
pub const K_MAX_LAYERS_PER_SET: usize = 10;

// ---------------------------------------------------------------------
// MappedAction —— 映射动作
// ---------------------------------------------------------------------
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ActionType {
    KeyboardKey,
    MouseClick,
    SwitchLayer,
    MouseMove,
    LookAround,
    MouseToggle,
    WheelUp,
    WheelDown,
    ToggleMapping,
    ToggleOnScreenKeyboard,
    ToggleOverlay,
}

// MappedAction 含 Option<String>（layer_name），不能 Copy，只 Clone。
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MappedAction {
    pub r#type: ActionType,
    pub key_code: i32,
    pub mouse_button: MouseButton,
    pub layer_name: Option<String>,
}

impl MappedAction {
    pub fn keyboard_key(code: i32) -> Self {
        Self {
            r#type: ActionType::KeyboardKey,
            key_code: code,
            mouse_button: MouseButton::Left,
            layer_name: None,
        }
    }
    pub fn mouse_click(b: MouseButton) -> Self {
        Self {
            r#type: ActionType::MouseClick,
            key_code: 0,
            mouse_button: b,
            layer_name: None,
        }
    }
    pub fn mouse_toggle(b: MouseButton) -> Self {
        Self {
            r#type: ActionType::MouseToggle,
            key_code: 0,
            mouse_button: b,
            layer_name: None,
        }
    }
    pub fn wheel_up() -> Self {
        Self {
            r#type: ActionType::WheelUp,
            key_code: 0,
            mouse_button: MouseButton::Left,
            layer_name: None,
        }
    }
    pub fn wheel_down() -> Self {
        Self {
            r#type: ActionType::WheelDown,
            key_code: 0,
            mouse_button: MouseButton::Left,
            layer_name: None,
        }
    }
    pub fn switch_layer(name: &str) -> Self {
        Self {
            r#type: ActionType::SwitchLayer,
            key_code: 0,
            mouse_button: MouseButton::Left,
            layer_name: Some(name.to_string()),
        }
    }
    pub fn mouse_move() -> Self {
        Self {
            r#type: ActionType::MouseMove,
            key_code: 0,
            mouse_button: MouseButton::Left,
            layer_name: None,
        }
    }
    pub fn look_around() -> Self {
        Self {
            r#type: ActionType::LookAround,
            key_code: 0,
            mouse_button: MouseButton::Left,
            layer_name: None,
        }
    }
    pub fn toggle_mapping() -> Self {
        Self {
            r#type: ActionType::ToggleMapping,
            key_code: 0,
            mouse_button: MouseButton::Left,
            layer_name: None,
        }
    }
    pub fn toggle_on_screen_keyboard() -> Self {
        Self {
            r#type: ActionType::ToggleOnScreenKeyboard,
            key_code: 0,
            mouse_button: MouseButton::Left,
            layer_name: None,
        }
    }
    pub fn toggle_overlay() -> Self {
        Self {
            r#type: ActionType::ToggleOverlay,
            key_code: 0,
            mouse_button: MouseButton::Left,
            layer_name: None,
        }
    }
}

// ---------------------------------------------------------------------
// KeyMapping —— 单个按键映射
// ---------------------------------------------------------------------
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct KeyMapping {
    pub action: MappedAction,
    pub sub_commands: Vec<i32>, // Android KeyCode，最多 MAX_SUB_COMMANDS 个
}

impl KeyMapping {
    pub const MAX_SUB_COMMANDS: usize = 3;

    /// 生成可读描述字符串，如 "W+ALT"
    pub fn describe(&self) -> String {
        let mut parts: Vec<String> = Vec::new();
        match self.action.r#type {
            ActionType::KeyboardKey => parts.push(key_code_to_name(self.action.key_code)),
            ActionType::MouseClick => parts.push(mouse_button_display_name(self.action.mouse_button).to_string()),
            ActionType::MouseToggle => parts.push(format!("长按{}", mouse_button_display_name(self.action.mouse_button))),
            ActionType::WheelUp => parts.push("滚轮上滚".to_string()),
            ActionType::WheelDown => parts.push("滚轮下滚".to_string()),
            ActionType::SwitchLayer => parts.push(format!("切换→{}", self.action.layer_name.as_deref().unwrap_or(""))),
            ActionType::MouseMove => parts.push("鼠标移动".to_string()),
            ActionType::LookAround => parts.push("视角控制".to_string()),
            ActionType::ToggleMapping => parts.push("切换映射".to_string()),
            ActionType::ToggleOnScreenKeyboard => parts.push("切换屏幕键盘".to_string()),
            ActionType::ToggleOverlay => parts.push("切换悬浮窗".to_string()),
        }
        for sub in &self.sub_commands {
            parts.push(key_code_to_name(*sub));
        }
        parts.join("+")
    }
}

// ---------------------------------------------------------------------
// OperationLayer —— 操作层（一组按键映射）
// ---------------------------------------------------------------------
#[derive(Debug, Clone)]
pub struct OperationLayer {
    /// 唯一标识符，固定不变（如 "Layer1"、"Common"），运行时逻辑只认 id
    pub id: String,
    /// 显示名称，用户可修改
    pub name: String,
    pub has_trigger_button: bool,
    pub trigger_button: ControllerButton,
    /// 按钮 -> 映射
    pub button_mappings: HashMap<ControllerButton, KeyMapping>,
}

impl OperationLayer {
    pub fn new(id: &str) -> Self {
        Self {
            id: id.to_string(),
            name: id.to_string(),
            has_trigger_button: false,
            trigger_button: ControllerButton::A,
            button_mappings: HashMap::new(),
        }
    }

    pub fn get_mapping(&self, b: ControllerButton) -> Option<&KeyMapping> {
        self.button_mappings.get(&b)
    }
}

// ---------------------------------------------------------------------
// GlobalSettings —— 全局设置
// ---------------------------------------------------------------------
#[derive(Debug, Clone)]
pub struct GlobalSettings {
    pub deadzone: f32,
    pub look_sensitivity: f32,
    pub cursor_speed: f32,
    pub look_smoothing: f32,
    pub look_acceleration: f32,
    pub invert_look_x: bool,
    pub invert_look_y: bool,
    pub overlay_x: i32,
    pub overlay_y: i32,
    pub overlay_scale: f64,
    pub main_window_x: i32,
    pub main_window_y: i32,
    pub release_on_foreground_change: bool,
    pub confirm_on_close: bool,
}

impl Default for GlobalSettings {
    fn default() -> Self {
        Self {
            deadzone: 0.15,
            look_sensitivity: 0.5,
            cursor_speed: 1.0,
            look_smoothing: 0.5,
            look_acceleration: 1.5,
            invert_look_x: false,
            invert_look_y: false,
            overlay_x: -1,
            overlay_y: -1,
            overlay_scale: 1.0,
            main_window_x: -1,
            main_window_y: -1,
            release_on_foreground_change: true,
            confirm_on_close: true,
        }
    }
}

// ---------------------------------------------------------------------
// OperationSet —— 操作集（最顶层容器）
// ---------------------------------------------------------------------
#[derive(Debug, Clone)]
pub struct OperationSet {
    pub id: String,
    pub name: String,
    pub common_layer: OperationLayer,
    pub layers: Vec<OperationLayer>,
}

impl OperationSet {
    /// 创建一个全新的空操作集：空公共层 + K_MAX_LAYERS_PER_SET 个空操作层
    pub fn create_empty(set_id: &str, set_name: &str) -> Self {
        let mut common = OperationLayer::new("Common");
        common.name = "Common".to_string();
        let layer_ids = [
            "Layer1", "Layer2", "Layer3", "Layer4", "Layer5",
            "Layer6", "Layer7", "Layer8", "Layer9", "Layer10",
        ];
        let layers = layer_ids
            .iter()
            .map(|id| {
                let mut l = OperationLayer::new(id);
                l.name = layer_display_name(id);
                l
            })
            .collect();
        Self {
            id: set_id.to_string(),
            name: set_name.to_string(),
            common_layer: common,
            layers,
        }
    }
}

// ---------------------------------------------------------------------
// ControllerProfile —— 完整配置
// ---------------------------------------------------------------------
#[derive(Debug, Clone)]
pub struct ControllerProfile {
    /// 操作集列表（至少 1 个）
    pub operation_sets: Vec<OperationSet>,
    /// 当前激活的操作集 id
    pub active_operation_set_id: String,
    pub global_settings: GlobalSettings,
}

impl ControllerProfile {
    /// 当前激活的操作集（无有效激活集时回退第一个；空则 None）
    pub fn active_set(&self) -> Option<&OperationSet> {
        self.operation_sets
            .iter()
            .find(|s| s.id == self.active_operation_set_id)
            .or_else(|| self.operation_sets.first())
    }

    pub fn active_set_mut(&mut self) -> Option<&mut OperationSet> {
        let id = self.active_operation_set_id.clone();
        if let Some(pos) = self.operation_sets.iter().position(|s| s.id == id) {
            Some(&mut self.operation_sets[pos])
        } else {
            self.operation_sets.first_mut()
        }
    }

    /// 当前激活操作集的公共层
    pub fn common_layer(&self) -> Option<&OperationLayer> {
        self.active_set().map(|s| &s.common_layer)
    }

    /// 当前激活操作集的公共层（可变）
    pub fn common_layer_mut(&mut self) -> Option<&mut OperationLayer> {
        self.active_set_mut().map(|s| &mut s.common_layer)
    }

    /// 当前激活操作集的操作层
    pub fn layers(&self) -> Vec<&OperationLayer> {
        match self.active_set() {
            Some(s) => s.layers.iter().collect(),
            None => Vec::new(),
        }
    }

    pub fn layers_mut(&mut self) -> Vec<&mut OperationLayer> {
        match self.active_set_mut() {
            Some(s) => s.layers.iter_mut().collect(),
            None => Vec::new(),
        }
    }

    /// 当前激活操作集的显示名
    pub fn active_operation_set_name(&self) -> String {
        self.active_set().map(|s| s.name.clone()).unwrap_or_default()
    }

    /// 按 id 设置当前激活操作集；无效 id 返回 false
    pub fn set_active_operation_set(&mut self, id: &str) -> bool {
        if self.operation_sets.iter().any(|s| s.id == id) {
            self.active_operation_set_id = id.to_string();
            true
        } else {
            false
        }
    }

    /// 生成一个不与现有操作集重复的新 id（"Set1"、"Set2"...）
    pub fn unique_operation_set_id(&self) -> String {
        let mut max = 0;
        for s in &self.operation_sets {
            if let Some(rest) = s.id.strip_prefix("Set") {
                if let Ok(n) = rest.parse::<i32>() {
                    if n > max {
                        max = n;
                    }
                }
            }
        }
        format!("Set{}", max + 1)
    }

    /// 按 id 查找层（仅当前激活操作集内，含公共层）
    pub fn find_layer(&self, id: &str) -> Option<&OperationLayer> {
        let set = self.active_set()?;
        if set.common_layer.id == id {
            return Some(&set.common_layer);
        }
        set.layers.iter().find(|l| l.id == id)
    }

    /// 由触发按键找操作层（当前激活操作集内，仅供 UI 展示）
    pub fn find_layer_by_trigger(&self, b: ControllerButton) -> Option<&OperationLayer> {
        let set = self.active_set()?;
        set.layers
            .iter()
            .find(|l| l.has_trigger_button && l.trigger_button == b)
    }

    /// 生成默认配置（WoW 预设：1 个"默认操作集"，含公共层 + 10 个操作层）
    pub fn create_default() -> Self {
        let mut p = Self {
            operation_sets: vec![OperationSet::create_empty("Set1", "默认操作集")],
            active_operation_set_id: "Set1".to_string(),
            global_settings: GlobalSettings::default(),
        };
        let common = p.common_layer_mut().expect("default set exists");
        // ---- 公共层默认映射 ----
        common.button_mappings.insert(
            ControllerButton::A,
            KeyMapping {
                action: MappedAction::keyboard_key(android_key::SPACE),
                sub_commands: vec![],
            },
        );
        common.button_mappings.insert(
            ControllerButton::B,
            KeyMapping {
                action: MappedAction::mouse_click(MouseButton::Right),
                sub_commands: vec![],
            },
        );
        common.button_mappings.insert(
            ControllerButton::X,
            KeyMapping {
                action: MappedAction::mouse_click(MouseButton::Left),
                sub_commands: vec![],
            },
        );
        common.button_mappings.insert(
            ControllerButton::Y,
            KeyMapping {
                action: MappedAction::keyboard_key(android_key::I),
                sub_commands: vec![],
            },
        );
        common.button_mappings.insert(
            ControllerButton::Menu,
            KeyMapping {
                action: MappedAction::keyboard_key(android_key::ESCAPE),
                sub_commands: vec![],
            },
        );
        common.button_mappings.insert(
            ControllerButton::Options,
            KeyMapping {
                action: MappedAction::keyboard_key(android_key::M),
                sub_commands: vec![],
            },
        );
        // 右摇杆按压 -> 视角控制
        common.button_mappings.insert(
            ControllerButton::RightStickClick,
            KeyMapping {
                action: MappedAction::look_around(),
                sub_commands: vec![],
            },
        );
        p
    }
}
