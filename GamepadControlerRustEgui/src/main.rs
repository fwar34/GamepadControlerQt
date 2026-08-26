// =====================================================================
// main.rs —— 程序入口（egui）
//
// 装配顺序：
//   1. 构建共享状态（AppCore + LookRunner + XInputGamepadSource）
//   2. 从 exe 同目录加载配置文件（无则生成默认）
//   3. 后台线程运行悬浮窗（透明置顶，默认隐藏）
//   4. 主线程运行主窗口（主 / 层编辑 / 帮助 三视图）
//
// 数据流：XInputGamepadSource → AppCore(SteamInput→Mapper) → SendInput
//          └─ UI 线程周期性 lock core 读取快照刷新界面
// =====================================================================

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod core;
mod ui;

use core::config_manager;
use std::sync::atomic::AtomicBool;
use std::sync::Arc;

fn main() -> eframe::Result {
    // ---- 1. 构建共享状态并加载配置 ----
    let shared = Arc::new(ui::shared::AppShared::new());
    {
        let profile = config_manager::load();
        if let Ok(mut core) = shared.core.lock() {
            core.load_profile(profile);
        }
    }

    // 悬浮窗显隐标志（主窗口按钮翻转，悬浮窗线程轮询）
    let overlay_visible = Arc::new(AtomicBool::new(false));

    // ---- 2. 后台线程：悬浮窗 ----
    let ov_shared = Arc::clone(&shared);
    let ov_visible = Arc::clone(&overlay_visible);
    std::thread::spawn(move || {
        let _ = ui::overlay::run(ov_shared, ov_visible);
    });

    // ---- 3. 主线程：主窗口 ----
    ui::main_window::run(shared, overlay_visible)
}
