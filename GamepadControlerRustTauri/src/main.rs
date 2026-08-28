// =====================================================================
// main.rs —— 程序入口（Tauri）
//
// 装配顺序：
//   1. 构建共享状态（AppCore + LookRunner + XInputGamepadSource）
//   2. 从 exe 同目录加载配置文件（无则生成默认）
//   3. 启动 Tauri：主窗口（自动创建）+ 悬浮窗（setup 中创建，默认隐藏）
// 退出：RunEvent::Exit 统一钩子——停止映射释放注入 + 自动保存配置
//
// 数据流：XInputGamepadSource → AppCore(SteamInput→Mapper) → SendInput
//          └─ 前端(WebView) ── invoke 命令 ──> 后端(commands.rs)
// =====================================================================

#![windows_subsystem = "windows"]

mod commands;
mod core;
mod ui;

use std::sync::atomic::AtomicBool;
use std::sync::{Arc, Mutex};
use tauri::Manager;
use windows::Win32::Foundation::{GetLastError, ERROR_ALREADY_EXISTS};
use windows::Win32::System::Threading::CreateMutexW;
use windows::Win32::UI::WindowsAndMessaging::{FindWindowW, SetForegroundWindow, ShowWindow, SW_RESTORE};

/// 已有实例运行时聚焦其主窗口（按主窗口标题查找并恢复/置顶）
fn focus_existing_main_window() {
    unsafe {
        // 按窗口标题查找已有实例的主窗口（FindWindowW 返回 Result，失败即未找到）
        let Ok(hwnd) = FindWindowW(None, windows::core::w!("Gamepad 键鼠映射")) else {
            return; // 未找到主窗口则不做聚焦
        };
        if !hwnd.0.is_null() {
            ShowWindow(hwnd, SW_RESTORE); // 最小化则恢复显示
            let _ = SetForegroundWindow(hwnd); // 置顶聚焦
        }
    }
}

/// 全局共享状态：跨线程共享的核心 + 悬浮窗显隐标志 + 悬浮窗透明度
pub struct AppState {
    pub shared: Arc<ui::shared::AppShared>,
    pub overlay_visible: Arc<AtomicBool>,
    /// 悬浮窗卡片背景透明度（0.2 ~ 1.0，前端覆盖层应用）
    pub overlay_opacity: Arc<Mutex<f32>>,
}

fn main() {
    // ---- 0. 单进程限制：命名互斥体检测是否已有实例在运行 ----
    let _single_instance = {
        // 创建命名互斥体；若已存在（ERROR_ALREADY_EXISTS）说明已有实例在运行。
        // 名称位于 Global 命名空间，多个进程共享（w! 宏只能接收字符串字面量）
        let handle = unsafe {
            CreateMutexW(None, true, windows::core::w!("Global\\GamepadControlerTauriSingleInstance"))
        };
        let already_running = unsafe { GetLastError() } == ERROR_ALREADY_EXISTS;
        if already_running {
            // 已有实例 → 聚焦其主窗口后直接退出本实例
            focus_existing_main_window();
            return;
        }
        // 首次实例：持有互斥体句柄直到进程退出（否则句柄释放后互斥体会被销毁）
        handle
    };
    let _ = _single_instance;

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
            overlay_opacity: Arc::new(Mutex::new(0.85)),
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
            commands::set_overlay_opacity,
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
        .build(tauri::generate_context!())
        .expect("error while running tauri application")
        .run(|app_handle, event| {
            // 应用退出前：停止映射释放全部注入（防止卡键）+ 自动保存配置文件
            if let tauri::RunEvent::Exit = event {
                let state = app_handle.state::<AppState>();
                state.shared.stop_mapping(); // 释放所有按键注入，防止卡键
                let profile = {
                    let core = state.shared.core.lock().unwrap();
                    core.steam.profile.clone() // 取当前配置快照
                };
                core::config_manager::save(&profile); // 自动保存到配置文件
            }
        });
}
