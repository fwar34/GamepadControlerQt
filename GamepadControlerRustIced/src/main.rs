mod core;
mod ui;

use ui::app::App;

/// daemon 窗口标题：全部窗口共用
fn daemon_title(_: &App, _: iced::window::Id) -> String {
    "Gamepad 键鼠映射".to_string()
}

fn main() -> iced::Result {
    let shared = std::sync::Arc::new(ui::shared::AppShared::new());
    {
        let profile = core::config_manager::load();
        if let Ok(mut core) = shared.core.lock() {
            core.load_profile(profile);
        }
    }

    // 加载微软雅黑字体并设为默认
    let mut app = iced::daemon(App::boot_with_shared(shared), App::update, App::view)
        .theme(App::theme)
        .subscription(App::subscription)
        .title(daemon_title)
        .default_font(iced::Font::with_name("Microsoft YaHei"));

    if let Ok(data) = std::fs::read("C:\\Windows\\Fonts\\msyh.ttc") {
        app = app.font(data);
    }

    app.run()
}
