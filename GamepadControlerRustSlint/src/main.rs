// =====================================================================
// main.rs —— 程序入口（Slint GUI）
//
// 装配顺序：
//   1. 构建共享状态（AppCore + LookRunner + XInputGamepadSource）
//   2. 从 exe 同目录加载配置文件（无则生成默认）
//   3. 创建主窗口 + 独立置顶透明悬浮窗，启动 50ms 轮询
//
// 数据流：XInputGamepadSource → AppCore(SteamInput→Mapper) → SendInput
// =====================================================================

mod core;
mod ui;

// 两个 .slint 文件分别生成 out/main.rs 与 out/overlay.rs，
// slint::include_modules!() 只指向最后一个，故手动 include 两份生成代码。
include!(concat!(env!("OUT_DIR"), "/main.rs"));
include!(concat!(env!("OUT_DIR"), "/overlay.rs"));

use core::config_manager;
use slint::ComponentHandle;
use std::sync::Arc;
use ui::shared::AppShared;

fn main() -> Result<(), slint::PlatformError> {
    // ---- 1. 构建共享状态并加载配置 ----
    let shared = Arc::new(AppShared::new());
    {
        let profile = config_manager::load();
        if let Ok(mut core) = shared.core.lock() {
            core.load_profile(profile);
        }
    }

    // ---- 2. 创建主窗口与悬浮窗 ----
    let ui = MainWindow::new()?;
    let overlay = OverlayWindow::new()?;

    // 悬浮窗：置顶透明无边框（置顶在 overlay.slint 中声明），初始隐藏
    ui::overlay::setup(&overlay, Arc::clone(&shared));
    overlay.hide()?;

    // 主窗口：连接回调 + 轮询（持有悬浮窗句柄用于显隐）
    ui::main_window::setup(&ui, Arc::clone(&shared), overlay.as_weak());

    // ---- 3. 运行事件循环（主窗口关闭后退出）----
    ui.run()?;
    Ok(())
}
