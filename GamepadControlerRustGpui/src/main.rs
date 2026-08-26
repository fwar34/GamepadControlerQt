// =====================================================================
// main.rs —— 程序入口（gpui GUI）
//
// 装配顺序：
//   1. 构建共享状态（AppCore + LookRunner + XInputGamepadSource）
//   2. 从 exe 同目录加载配置文件（无则生成默认）
//   3. 打开主窗口；所有窗口关闭后退出
//
// 数据流：XInputGamepadSource → AppCore(SteamInput→Mapper) → SendInput
// =====================================================================

mod core;
mod ui;

use core::config_manager;
use gpui::*;
use std::sync::Arc;
use ui::main_window::MainWindowView;
use ui::shared::AppShared;

fn main() {
    // ---- 1. 构建共享状态并加载配置 ----
    let shared = Arc::new(AppShared::new());
    {
        let profile = config_manager::load();
        if let Ok(mut core) = shared.core.lock() {
            core.load_profile(profile);
        }
    }

    // ---- 2. 启动 GUI ----
    Application::new().run(move |cx: &mut App| {
        // gpui-component 组件（输入框等）依赖全局 Theme，必须先初始化，
        // 否则渲染 Input 时会 panic（"no state of type Theme exists"）
        gpui_component::theme::init(cx);

        // 所有窗口关闭后退出
        // App 级订阅必须永久存活：若被 drop 则回调被取消，关窗后程序无法退出
        std::mem::forget(cx.on_window_closed(|cx| {
            if cx.windows().is_empty() {
                cx.quit();
            }
        }));

        let shared = Arc::clone(&shared);
        let bounds = Bounds::centered(None, size(px(900.0), px(640.0)), cx);
        let _main = cx
            .open_window(
                WindowOptions {
                    window_bounds: Some(WindowBounds::Windowed(bounds)),
                    ..Default::default()
                },
                |window, cx| cx.new(|cx| MainWindowView::new(shared, window, cx)),
            )
            .expect("failed to open main window");

        cx.activate(true); // 前台激活
    });
}
