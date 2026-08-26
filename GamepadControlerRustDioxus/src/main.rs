// =====================================================================
// main.rs —— 程序入口（Dioxus 桌面版）
//
// 装配顺序：
//   1. 构建共享状态（AppCore + LookRunner + XInputGamepadSource）
//   2. 从 exe 同目录加载配置文件（无则生成默认）
//   3. 悬浮窗线程 + 主窗口线程各启动一个独立 Dioxus 实例
//
// 数据流：XInputGamepadSource → AppCore(SteamInput→Mapper) → SendInput
//
// 多窗口说明：Dioxus 0.6 的 runtime 是 thread_local 的，同一进程内
// 多线程各跑一个 launch_cfg 即可得到互不干扰的独立窗口。
// =====================================================================

mod core;
mod ui;

use core::config_manager;
use dioxus::desktop::{Config, WindowBuilder};
use std::sync::{Arc, OnceLock};
use ui::main_window::MainApp;
use ui::overlay::OverlayApp;
use ui::shared::AppShared;

/// 全局共享状态：主窗口与悬浮窗（不同线程的 Dioxus 实例）经此共享 core
static SHARED: OnceLock<Arc<AppShared>> = OnceLock::new();

fn main() {
    // ---- 1. 构建共享状态并加载配置 ----
    let shared = Arc::new(AppShared::new());
    {
        let profile = config_manager::load();
        if let Ok(mut core) = shared.core.lock() {
            core.load_profile(profile);
        }
    }
    if SHARED.set(shared).is_err() {
        panic!("SHARED already set");
    }

    // ---- 2. 悬浮窗：独立线程 + 独立窗口（置顶小窗，始终运行）----
    // Dioxus 0.6 无 launch_cfg，统一用 LaunchBuilder::desktop().launch(root)。
    // LaunchBuilder 非 Send，必须在目标线程内构造。
    std::thread::spawn(|| {
        dioxus::LaunchBuilder::desktop()
            .with_cfg(Config::new().with_window(
                WindowBuilder::new()
                    .with_title("手柄悬浮窗")
                    .with_inner_size(dioxus::desktop::LogicalSize::new(360.0, 220.0))
                    .with_always_on_top(true)
                    .with_resizable(false),
            ))
            .launch(OverlayApp);
    });

    // ---- 3. 主窗口（主线程）----
    dioxus::LaunchBuilder::desktop()
        .with_cfg(Config::new().with_window(
            WindowBuilder::new()
                .with_title("Gamepad 键鼠映射")
                .with_inner_size(dioxus::desktop::LogicalSize::new(920.0, 680.0)),
        ))
        .launch(MainApp);
}
