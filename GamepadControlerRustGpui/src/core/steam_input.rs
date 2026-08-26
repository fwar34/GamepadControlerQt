// =====================================================================
// steam_input.rs —— 映射引擎
//
// 等效 Qt 版 SteamInput.h/.cpp。
// 职责：
//   - 维护当前激活的操作层栈（公共层始终激活、优先级最低）
//   - 按键查询：按「最后激活的操作层 -> ... -> 公共层」顺序查找有效映射
//   - 分发按钮/摇杆输入，并负责层切换动作的运行时处理
//
// 线程模型：
//   - 手柄轮询线程调用 handle_button_event / handle_stick_input（&mut self）
//   - UI 线程通过同一把锁修改配置（操作集切换 / 层编辑 / 全局设置）
//   - 状态变化通过 UI 事件通道（Sender<UiEvent>）通知界面/悬浮窗
// =====================================================================

use crate::core::input_types::{ControllerButton, ControllerStick, Vector2};
use crate::core::mapping_types::{
    ActionType, ControllerProfile, KeyMapping, OperationLayer,
};
use crate::core::UiEvent;
use std::collections::{HashMap, HashSet};
use std::sync::mpsc::Sender;

/// 按钮事件分发结果（由外层 mapper 决定执行注入）
#[derive(Debug, Clone)]
pub enum ButtonDispatch {
    /// 无动作（松开切层键、无映射按下等）
    None,
    /// 交给映射器执行注入
    Execute {
        is_pressed: bool,
        mapping: KeyMapping,
    },
    /// 切换映射启停请求（ToggleMapping）
    ToggleMapping,
    /// 切换屏幕键盘请求（ToggleOnScreenKeyboard）
    ToggleOnScreenKeyboard,
    /// 切换悬浮窗请求（ToggleOverlay）
    ToggleOverlay,
}

/// SteamInput —— 映射引擎
pub struct SteamInput {
    /// 当前配置（操作集列表 + 当前激活操作集 + 全局设置）
    pub profile: ControllerProfile,
    /// UI 事件通道（通知界面/悬浮窗状态变化）
    event_tx: Sender<UiEvent>,
    /// 已激活操作层 id（按下顺序，后加入的优先级更高）
    active_layers: Vec<String>,
    /// 记录「哪个按键激活了哪个层」，松开该按键时停用对应层
    button_triggered_layers: HashMap<ControllerButton, String>,
    /// 当前物理按下的手柄按键集合
    held_buttons: HashSet<ControllerButton>,
    /// 当前激活层 id（未激活任何操作层时为 "Common"）
    active_layer_name: String,
}

impl SteamInput {
    pub fn new(event_tx: Sender<UiEvent>) -> Self {
        Self {
            profile: ControllerProfile::create_default(),
            event_tx,
            active_layers: Vec::new(),
            button_triggered_layers: HashMap::new(),
            held_buttons: HashSet::new(),
            active_layer_name: "Common".to_string(),
        }
    }

    /// 整体替换配置（启动加载配置、重置默认时调用），同时清空所有激活层
    pub fn load_profile(&mut self, new_profile: ControllerProfile) {
        self.profile = new_profile;
        self.deactivate_all_layers();
        self.emit(UiEvent::ProfileChanged);
    }

    /// 仅更新全局设置（界面滑块实时调整时调用，不重置已激活层）
    pub fn set_global_settings(&mut self, settings: crate::core::mapping_types::GlobalSettings) {
        self.profile.global_settings = settings;
        self.emit(UiEvent::ProfileChanged);
    }

    // -----------------------------------------------------------------
    // 操作集管理
    // -----------------------------------------------------------------

    /// 切换当前操作集（按 id）：清空已激活层栈（旧操作集内的层不再有效）。
    /// id 无效返回 false（不产生任何副作用）。
    pub fn switch_operation_set(&mut self, set_id: &str) -> bool {
        if !self.profile.set_active_operation_set(set_id) {
            return false;
        }
        if self.profile.active_operation_set_id != set_id {
            return false; // 防御
        }
        self.deactivate_all_layers();
        self.emit(UiEvent::ProfileChanged);
        self.emit(UiEvent::OperationSetChanged(self.profile.active_operation_set_name()));
        true
    }

    /// 操作集结构变化后统一通知（新增/删除/复制/重命名后调用）。
    /// 调用方须先 deactivate_all_layers()。
    pub fn notify_operation_set_changed(&mut self) {
        self.emit(UiEvent::ProfileChanged);
        self.emit(UiEvent::OperationSetChanged(self.profile.active_operation_set_name()));
    }

    // -----------------------------------------------------------------
    // 层管理
    // -----------------------------------------------------------------

    /// 激活一个操作层（追加到栈顶，优先级最高）。
    /// 忽略空 id/Common（公共层不可"激活"）；重复激活同一层被忽略。
    pub fn activate_layer(&mut self, layer_id: &str) {
        if layer_id.is_empty() || layer_id == "Common" {
            return;
        }
        if !self.profile.find_layer(layer_id).is_some() {
            return; // 层不存在（当前操作集内）
        }
        if !self.active_layers.contains(&layer_id.to_string()) {
            self.active_layers.push(layer_id.to_string());
            self.update_active_layer_name();
        }
    }

    /// 停用一个操作层（从栈中移除所有匹配项）
    pub fn deactivate_layer(&mut self, layer_id: &str) {
        let before = self.active_layers.len();
        self.active_layers.retain(|id| id != layer_id);
        if self.active_layers.len() != before {
            self.update_active_layer_name();
        }
    }

    /// 停用所有操作层，回到公共层；同时清空触发层记录
    pub fn deactivate_all_layers(&mut self) {
        self.active_layers.clear();
        self.button_triggered_layers.clear();
        self.update_active_layer_name();
    }

    /// 指定层（按 id）当前是否激活
    pub fn is_layer_active(&self, id: &str) -> bool {
        self.active_layers.iter().any(|l| l == id)
    }

    /// 当前激活层 id（未激活任何操作层时为 "Common"）
    pub fn active_layer_name(&self) -> &str {
        &self.active_layer_name
    }

    /// 当前物理按下的手柄按键集合
    pub fn held_buttons(&self) -> &HashSet<ControllerButton> {
        &self.held_buttons
    }

    /// 清空物理按持记录（手柄断开/停止映射时调用，避免状态残留）
    pub fn clear_held_buttons(&mut self) {
        self.held_buttons.clear();
    }

    /// 当前激活层列表（按激活顺序，公共层不在其中）
    pub fn get_active_layers(&self) -> Vec<&OperationLayer> {
        self.active_layers
            .iter()
            .filter_map(|id| self.profile.find_layer(id))
            .collect()
    }

    // -----------------------------------------------------------------
    // 查询
    // -----------------------------------------------------------------

    /// 查询按钮在当前层栈下的有效映射：
    /// 从最后激活的操作层开始，逐层回退到公共层，返回第一个命中。
    pub fn get_effective_mapping(&self, button: ControllerButton) -> Option<KeyMapping> {
        for id in self.active_layers.iter().rev() {
            if let Some(layer) = self.profile.find_layer(id) {
                if let Some(m) = layer.get_mapping(button) {
                    return Some(m.clone());
                }
            }
        }
        self.profile
            .common_layer()
            .and_then(|cl| cl.get_mapping(button))
            .cloned()
    }

    // -----------------------------------------------------------------
    // 输入入口（由手柄读取源调用）
    // -----------------------------------------------------------------

    /// 按钮按下/松开事件；SwitchLayer 动作在此处理（按住激活/松开回退），
    /// 其余动作通过返回值交给映射器执行。
    pub fn handle_button_event(&mut self, button: ControllerButton, is_pressed: bool) -> ButtonDispatch {
        if is_pressed {
            self.held_buttons.insert(button);
        } else {
            self.held_buttons.remove(&button);
        }

        // 松开时：若该按键激活了某个层，停用该层并返回（不触发映射）
        if !is_pressed {
            if let Some(triggered_id) = self.button_triggered_layers.remove(&button) {
                self.deactivate_layer(&triggered_id);
                return ButtonDispatch::None;
            }
        }

        // 查询当前层栈下的有效映射；无映射则不产生任何事件
        let Some(mapping) = self.get_effective_mapping(button) else {
            // 松开时：即使该按钮在「松开时刻」的层栈下已无映射，仍要广播松开事件，
            // 让映射器按「已注入状态」精确释放（防止此前注入的按键卡死）。
            if !is_pressed {
                return ButtonDispatch::Execute {
                    is_pressed,
                    mapping: KeyMapping {
                        action: crate::core::mapping_types::MappedAction::mouse_move(),
                        sub_commands: vec![],
                    },
                };
            }
            return ButtonDispatch::None;
        };

        // 切换层动作由引擎处理：按住激活目标层并记录触发按钮
        if mapping.action.r#type == ActionType::SwitchLayer {
            if is_pressed {
                let target = mapping.action.layer_name.clone().unwrap_or_default();
                // 先按 id 查找；找不到则按 name 查找（兼容旧配置）
                let target_id = self
                    .profile
                    .find_layer(&target)
                    .map(|l| l.id.clone())
                    .or_else(|| {
                        self.profile
                            .layers()
                            .into_iter()
                            .find(|l| l.name == target)
                            .map(|l| l.id.clone())
                    });
                if let Some(tid) = target_id {
                    if !self.is_layer_active(&tid) {
                        self.activate_layer(&tid);
                        self.button_triggered_layers.insert(button, tid);
                    }
                }
            }
            return ButtonDispatch::None;
        }

        // 切换类动作：仅在按下时触发，松开忽略
        if is_pressed {
            match mapping.action.r#type {
                ActionType::ToggleMapping => return ButtonDispatch::ToggleMapping,
                ActionType::ToggleOnScreenKeyboard => {
                    return ButtonDispatch::ToggleOnScreenKeyboard;
                }
                ActionType::ToggleOverlay => return ButtonDispatch::ToggleOverlay,
                _ => {}
            }
        }

        // 其余动作（键盘/鼠标/视角等）交给映射器执行
        ButtonDispatch::Execute {
            is_pressed,
            mapping,
        }
    }

    /// 摇杆输入入口：先应用全局死区（缩放式），再返回处理后值。
    pub fn handle_stick_input(
        &mut self,
        stick: ControllerStick,
        x: f32,
        y: f32,
    ) -> (ControllerStick, f32, f32) {
        let dz = self.profile.global_settings.deadzone;
        let d = Vector2::new(x, y).with_deadzone(dz);
        let mut out_x = d.x;
        let mut out_y = d.y;
        if stick == ControllerStick::RightStick {
            if self.profile.global_settings.invert_look_x {
                out_x = -out_x;
            }
            if self.profile.global_settings.invert_look_y {
                out_y = -out_y;
            }
        }
        (stick, out_x, out_y)
    }

    // -----------------------------------------------------------------
    // 私有辅助
    // -----------------------------------------------------------------

    /// 重新计算当前激活层名（栈顶层的显示名；无激活层时为 "Common"），
    /// 变化时发出 LayerChanged 事件。
    fn update_active_layer_name(&mut self) {
        let name = match self.active_layers.last() {
            Some(id) => self
                .profile
                .find_layer(id)
                .map(|l| l.name.clone())
                .unwrap_or_else(|| "Common".to_string()),
            None => "Common".to_string(),
        };
        if name != self.active_layer_name {
            let changed = name.clone();
            self.active_layer_name = name;
            self.emit(UiEvent::LayerChanged(changed));
        }
    }

    fn emit(&self, event: UiEvent) {
        // 无界 mpsc 通道的 send 不阻塞（receiver 断开时返回 Err，忽略即可）
        let _ = self.event_tx.send(event);
    }
}
