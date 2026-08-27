// =====================================================================
// main.rs —— 程序入口（egui）
//
// 装配顺序：
//   1. 构建共享状态（AppCore + LookRunner + XInputGamepadSource）
//   2. 从 exe 同目录加载配置文件（无则生成默认）
//   3. 运行主窗口（主 / 层编辑 / 帮助 三视图）
//      内含独立置顶透明悬浮窗 viewport（同一事件循环，可靠显隐）
//
// 数据流：XInputGamepadSource → AppCore(SteamInput→Mapper) → SendInput
//          └─ UI 线程周期性 lock core 读取快照刷新界面
// =====================================================================

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod core;
mod ui;

use core::config_manager;
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

    // ---- 2. 主窗口（内含独立置顶透明悬浮窗 viewport）----
    ui::main_window::run(shared)
}
