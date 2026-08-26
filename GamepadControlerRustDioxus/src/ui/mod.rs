// =====================================================================
// ui —— Dioxus 界面层
//
// 窗口模型（Dioxus 桌面端基于 WebView，多窗口用「多线程多实例」）：
//   - 主窗口（main_window）：主线程，包含主界面 / 层编辑 / 使用说明视图
//   - 悬浮窗（overlay）：独立线程 + 独立窗口，始终运行，主窗口控制显隐
// =====================================================================

pub mod help;
pub mod layer_edit;
pub mod main_window;
pub mod overlay;
pub mod shared;
pub mod theme;
