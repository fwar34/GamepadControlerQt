// =====================================================================
// app.rs —— 应用核心组合层（AppCore）
//
// 组合 SteamInput（映射引擎）+ MapperState（注入状态）+ 注入器 + 视角
// 状态，对外提供统一入口：
//   - 手柄轮询线程：handle_source_event（按钮/摇杆/连接事件）
//   - UI 线程：配置管理（操作集增删改、层编辑、全局设置、启停）
//
// 并发模型：
//   AppCore 整体由 Arc<Mutex<AppCore>> 共享；手柄线程与 UI 线程都需
//   获取锁后再操作。临界区都是短操作（映射查询 + 注入调用），不会
//   长时间阻塞。注入器内部自带 Mutex，look 线程不经 AppCore 锁。
// =====================================================================

use crate::core::injector::InputInjector;
use crate::core::mapper::{LookState, MapperState};
use crate::core::mapping_types::{ControllerProfile, GlobalSettings, OperationSet};
use crate::core::steam_input::{ButtonDispatch, SteamInput};
use crate::core::UiEvent;
use crate::core::xinput_source::SourceEvent;
use std::sync::mpsc::Sender;
use std::sync::Arc;

pub struct AppCore {
    pub steam: SteamInput,
    pub mapper: MapperState,
    pub injector: Arc<InputInjector>,
    pub look: Arc<LookState>,
    /// 手柄当前是否连接（UI 轮询显示）
    pub connected: bool,
    /// 配置修订号：任何配置修改（操作集/层/全局设置）都会递增，
    /// UI 据此判断是否需要重新渲染列表等。
    pub profile_rev: u64,
    event_tx: Sender<UiEvent>,
}

impl AppCore {
    pub fn new(event_tx: Sender<UiEvent>) -> Self {
        let injector = Arc::new(InputInjector::new());
        Self {
            steam: SteamInput::new(event_tx.clone()),
            mapper: MapperState::default(),
            look: Arc::new(LookState::default()),
            injector,
            connected: false,
            profile_rev: 0,
            event_tx,
        }
    }

    fn emit(&self, e: UiEvent) {
        // 无界 mpsc 通道的 send 不阻塞（receiver 断开时返回 Err，忽略即可）
        let _ = self.event_tx.send(e);
    }

    // -----------------------------------------------------------------
    // 手柄事件入口（轮询线程回调）
    // -----------------------------------------------------------------
    pub fn handle_source_event(&mut self, event: SourceEvent) {
        match event {
            SourceEvent::Connected(connected) => {
                self.handle_connected(connected);
            }
            SourceEvent::Button(button, is_pressed) => {
                let dispatch = self.steam.handle_button_event(button, is_pressed);
                match dispatch {
                    ButtonDispatch::Execute { is_pressed, mapping } => {
                        self.mapper.handle_button(
                            button,
                            is_pressed,
                            &mapping,
                            self.injector.as_ref(),
                            &self.event_tx,
                        );
                    }
                    ButtonDispatch::ToggleMapping => {
                        self.emit(UiEvent::ToggleMappingRequested);
                    }
                    ButtonDispatch::ToggleOverlay => {
                        self.emit(UiEvent::ToggleOverlayRequested);
                    }
                    ButtonDispatch::ToggleOnScreenKeyboard => {
                        // Windows 版无屏幕键盘，忽略
                    }
                    ButtonDispatch::None => {}
                }
            }
            SourceEvent::Stick(stick, x, y) => {
                let (stick, x, y) = self.steam.handle_stick_input(stick, x, y);
                self.mapper
                    .handle_stick(stick, x, y, self.injector.as_ref(), self.look.as_ref());
            }
        }
    }

    fn handle_connected(&mut self, connected: bool) {
        self.connected = connected;
        self.emit(UiEvent::Connected(connected));
        if !connected {
            // 断开：释放全部注入（含 MouseToggle 锁存）+ 清空层栈与按持记录
            self.mapper
                .release_all_inputs(self.injector.as_ref(), &self.event_tx, self.look.as_ref());
            self.steam.deactivate_all_layers();
            self.steam.clear_held_buttons();
        }
    }

    // -----------------------------------------------------------------
    // 启停
    // -----------------------------------------------------------------
    /// 开始映射：更新视角设置参数
    pub fn start_mapping(&mut self) {
        let gs = &self.steam.profile.global_settings;
        self.look
            .update_settings(gs.look_sensitivity, gs.look_smoothing, gs.look_acceleration);
    }

    /// 停止映射：释放全部注入 + 清空层栈
    pub fn stop_mapping(&mut self) {
        self.mapper
            .release_all_inputs(self.injector.as_ref(), &self.event_tx, self.look.as_ref());
        self.steam.deactivate_all_layers();
        self.steam.clear_held_buttons();
    }

    /// 前台切换导致停止时调用（仅释放注入，保持手柄连接状态）
    pub fn release_all_inputs(&mut self) {
        self.mapper
            .release_all_inputs(self.injector.as_ref(), &self.event_tx, self.look.as_ref());
        self.steam.deactivate_all_layers();
        self.steam.clear_held_buttons();
    }

    // -----------------------------------------------------------------
    // 配置管理（UI 线程调用）
    // -----------------------------------------------------------------

    /// 整体加载配置（启动 / 重置默认）
    pub fn load_profile(&mut self, profile: ControllerProfile) {
        self.steam.load_profile(profile);
        self.profile_rev += 1;
        let gs = self.steam.profile.global_settings.clone();
        self.look.update_settings(gs.look_sensitivity, gs.look_smoothing, gs.look_acceleration);
    }

    /// 更新全局设置（界面滑块实时调整）
    pub fn update_global_settings(&mut self, settings: GlobalSettings) {
        self.steam.set_global_settings(settings);
        self.profile_rev += 1;
        let gs = self.steam.profile.global_settings.clone();
        self.look
            .update_settings(gs.look_sensitivity, gs.look_smoothing, gs.look_acceleration);
    }

    /// 切换操作集（按 id），成功返回 true
    pub fn switch_operation_set(&mut self, set_id: &str) -> bool {
        if self.steam.switch_operation_set(set_id) {
            self.profile_rev += 1;
            true
        } else {
            false
        }
    }

    /// 新增操作集：自动生成 id，默认名"操作集 N"（防重名）。
    /// 返回新集 id。
    pub fn add_operation_set(&mut self) -> String {
        let id = self.steam.profile.unique_operation_set_id();
        let name = unique_set_name(&self.steam.profile, &format!("操作集{}", id.trim_start_matches("Set")));
        // 添加前先清空层栈，防止 Vec 扩容导致旧层引用失效（用 id 更安全）
        self.steam.deactivate_all_layers();
        let set = OperationSet::create_empty(&id, &name);
        self.steam.profile.operation_sets.push(set);
        self.profile_rev += 1;
        self.steam.notify_operation_set_changed();
        id
    }

    /// 复制操作集为新操作集：直接以新名字创建。
    pub fn copy_operation_set(&mut self, src_id: &str, new_name: &str) -> bool {
        let src_idx = match self
            .steam
            .profile
            .operation_sets
            .iter()
            .position(|s| s.id == src_id)
        {
            Some(i) => i,
            None => return false,
        };
        let new_id = self.steam.profile.unique_operation_set_id();
        let mut copy = self.steam.profile.operation_sets[src_idx].clone();
        copy.id = new_id;
        copy.name = new_name.to_string();
        self.steam.deactivate_all_layers();
        self.steam.profile.operation_sets.push(copy);
        self.profile_rev += 1;
        self.steam.notify_operation_set_changed();
        true
    }

    /// 重命名操作集（仅改显示名，不影响运行时逻辑）
    pub fn rename_operation_set(&mut self, set_id: &str, new_name: &str) -> bool {
        let set = match self
            .steam
            .profile
            .operation_sets
            .iter_mut()
            .find(|s| s.id == set_id)
        {
            Some(s) => s,
            None => return false,
        };
        if new_name.trim().is_empty() {
            return false;
        }
        set.name = new_name.to_string();
        self.profile_rev += 1;
        self.steam.notify_operation_set_changed();
        true
    }

    /// 删除操作集：至少保留一个；删除后回退到第一个集。返回是否成功。
    pub fn delete_operation_set(&mut self, set_id: &str) -> bool {
        if self.steam.profile.operation_sets.len() <= 1 {
            return false;
        }
        let pos = self
            .steam
            .profile
            .operation_sets
            .iter()
            .position(|s| s.id == set_id)
            .unwrap_or(0);
        self.steam.deactivate_all_layers();
        self.steam.profile.operation_sets.remove(pos);
        // 若删除的是当前激活集，回退到第一个
        let active_id = self.steam.profile.active_operation_set_id.clone();
        if !self.steam.profile.set_active_operation_set(&active_id) {
            if let Some(first) = self.steam.profile.operation_sets.first() {
                self.steam.profile.active_operation_set_id = first.id.clone();
            }
        }
        self.profile_rev += 1;
        self.steam.notify_operation_set_changed();
        true
    }

    /// 当前激活操作集 id
    pub fn active_operation_set_id(&self) -> &str {
        &self.steam.profile.active_operation_set_id
    }
}

/// 生成不与现有操作集重名的显示名（追加数字后缀）
fn unique_set_name(profile: &ControllerProfile, base: &str) -> String {
    let used: std::collections::HashSet<&str> =
        profile.operation_sets.iter().map(|s| s.name.as_str()).collect();
    if !used.contains(base) {
        return base.to_string();
    }
    let mut n = 2;
    loop {
        let candidate = format!("{} {}", base, n);
        if !used.contains(candidate.as_str()) {
            return candidate;
        }
        n += 1;
    }
}
