mod core;
mod ui;

use ui::app::App;

/// 内嵌的静态思源黑体 Regular（由系统 Noto Sans SC 可变字体实例化而来，
/// 已去除 gvar 可变轮廓、固定字重 400）：无 hinting、无可变轮廓，
/// 字形圆润平滑且规避可变字体渲染异常。OFL 开源授权，可随程序分发。
const NOTO_SANS_SC: &[u8] = include_bytes!("../fonts/NotoSansSC-Regular.ttf");

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

    // 使用内嵌的静态思源黑体（Noto Sans SC）：字形圆润、渲染稳定
    let mut app = iced::daemon(App::boot_with_shared(shared), App::update, App::view)
        .theme(App::theme)
        .subscription(App::subscription)
        .title(daemon_title)
        .default_font(iced::Font::with_name("Noto Sans SC"))
        .font(NOTO_SANS_SC.to_vec());

    app.run()
}
