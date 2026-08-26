// =====================================================================
// core —— 核心逻辑模块
//
// 数据流（单方向）：
//   XInputGamepadSource → SteamInput → KeyboardMouseMapper → InputInjector
//                      (轮询线程)        (映射引擎)          (注入)
//
// AppCore 组合 SteamInput + MapperState + 注入器 + 视角状态，并由手柄
// 轮询线程与 UI 线程通过同一把 Mutex 串行访问，保证并发安全。
// =====================================================================

pub mod app;
pub mod config;
pub mod config_manager;
pub mod input_types;
pub mod injector;
pub mod mapper;
pub mod mapping_types;
pub mod steam_input;
pub mod xinput_source;

pub use app::AppCore;

// ---------------------------------------------------------------------
// UiEvent —— 核心 -> UI 的事件（经 std mpsc 通道非阻塞发送）
// UI 线程周期性地 drain 通道，再驱动 Iced 重绘对应界面元素。
// ---------------------------------------------------------------------
#[derive(Debug, Clone)]
pub enum UiEvent {
    /// 当前激活层显示名变化（切层时）
    LayerChanged(String),
    /// 当前操作集显示名变化（切换/重命名操作集时）
    OperationSetChanged(String),
    /// 配置整体变化（层编辑/操作集结构变化/全局设置变化）
    ProfileChanged,
    /// 手柄连接状态变化
    Connected(bool),
    /// 鼠标长按锁存状态变化（悬浮窗橙色警示 / 边框高亮）
    MouseToggleChanged {
        button: input_types::ControllerButton,
        mb: input_types::MouseButton,
        active: bool,
    },
    /// 手柄侧发起 ToggleMapping 请求（切映射启停）
    ToggleMappingRequested,
    /// 手柄侧发起 ToggleOverlay 请求（悬浮窗显隐）
    ToggleOverlayRequested,
}
