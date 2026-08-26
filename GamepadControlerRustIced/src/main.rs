// =====================================================================
// main.rs —— 程序入口（Iced GUI）
//
// 装配顺序：
//   1. 构建共享状态（AppCore + LookRunner + XInputGamepadSource）
//   2. 从 exe 同目录加载配置文件（无则生成默认）
//   3. 启动 Iced 应用
//
// 数据流：XInputGamepadSource → AppCore(SteamInput→Mapper) → SendInput
// =====================================================================

mod core;
mod ui;

use core::config_manager;
use std::sync::Arc;
use ui::app::App;

fn main() -> iced::Result {
    // ---- 1. 构建共享状态并加载配置 ----
    let shared = Arc::new(ui::shared::AppShared::new());
    {
        let profile = config_manager::load();
        if let Ok(mut core) = shared.core.lock() {
            core.load_profile(profile);
        }
    }

    // ---- 2. 启动 Iced GUI ----
    iced::application("Gamepad 键鼠映射", App::update, App::view)
        .theme(App::theme)
        .subscription(App::subscription)
        .run_with(App::new_with_shared(shared))
}
