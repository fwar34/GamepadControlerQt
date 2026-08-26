// =====================================================================
// main.rs —— 程序入口（Tauri）
//
// 装配顺序：
//   1. 构建共享状态（AppCore + LookRunner + XInputGamepadSource）
//   2. 从 exe 同目录加载配置文件（无则生成默认）
//   3. 启动 Tauri：主窗口（自动创建）+ 悬浮窗（setup 中创建，默认隐藏）
//
// 数据流：XInputGamepadSource → AppCore(SteamInput→Mapper) → SendInput
//          └─ 前端(WebView) ── invoke 命令 ──> 后端(commands.rs)
// =====================================================================

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod commands;
mod core;
mod ui;

use std::sync::atomic::AtomicBool;
use std::sync::Arc;
use tauri::Manager;

/// 全局共享状态：跨线程共享的核心 + 悬浮窗显隐标志
pub struct AppState {
    pub shared: Arc<ui::shared::AppShared>,
    pub overlay_visible: Arc<AtomicBool>,
}

fn main() {
    // ---- 1. 构建共享状态并加载配置 ----
    let shared = Arc::new(ui::shared::AppShared::new());
    {
        let profile = core::config_manager::load();
        if let Ok(mut core) = shared.core.lock() {
            core.load_profile(profile);
        }
    }

    // ---- 2. 启动 Tauri ----
    tauri::Builder::default()
        .manage(AppState {
            shared,
            overlay_visible: Arc::new(AtomicBool::new(false)),
        })
        .invoke_handler(tauri::generate_handler![
            commands::get_snapshot,
            commands::get_overlay_snapshot,
            commands::start_mapping,
            commands::stop_mapping,
            commands::add_operation_set,
            commands::rename_operation_set,
            commands::copy_operation_set,
            commands::delete_operation_set,
            commands::switch_operation_set,
            commands::adjust_setting,
            commands::save_config,
            commands::reset_config,
            commands::toggle_overlay,
            commands::quit_app,
            commands::get_layer_edit_snapshot,
            commands::get_mapping,
            commands::set_mapping,
            commands::clear_mapping,
            commands::toggle_sub,
        ])
        .setup(|app| {
            // 悬浮窗：无边框透明置顶小窗，默认隐藏；前端 overlay.html 负责渲染
            let _overlay = tauri::WebviewWindowBuilder::new(
                app,
                "overlay",
                tauri::WebviewUrl::App("overlay.html".into()),
            )
            .title("手柄悬浮窗")
            .inner_size(320.0, 220.0)
            .resizable(false)
            .decorations(false)
            .transparent(true)
            .always_on_top(true)
            .skip_taskbar(true)
            .visible(false)
            .build()?;
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
