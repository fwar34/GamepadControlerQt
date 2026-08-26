// =====================================================================
// theme.rs —— 深色主题颜色常量（egui）
//
// 与 Qt 版主窗口深色主题保持一致的配色，并提供 egui 视觉/字体安装。
// =====================================================================

use eframe::egui;
use egui::{Color32, FontData, FontDefinitions, FontFamily};

/// 0xRRGGBB -> Color32
pub fn rgb(hex: u32) -> Color32 {
    Color32::from_rgb(
        ((hex >> 16) & 0xFF) as u8,
        ((hex >> 8) & 0xFF) as u8,
        (hex & 0xFF) as u8,
    )
}

/// 0xAARRGGBB -> Color32（带 alpha）
pub fn argb(argb: u32) -> Color32 {
    Color32::from_rgba_unmultiplied(
        ((argb >> 16) & 0xFF) as u8,
        ((argb >> 8) & 0xFF) as u8,
        (argb & 0xFF) as u8,
        ((argb >> 24) & 0xFF) as u8,
    )
}

// ---- 基础色板（与 Qt 版一致）----
pub const BG_DEEP: u32 = 0x232529; // 最深背景
pub const BG: u32 = 0x2b2d31; // 窗口背景
pub const BG_PANEL: u32 = 0x2f3136; // 面板
pub const BG_INSET: u32 = 0x33363b; // 内嵌输入/列表背景
pub const BORDER: u32 = 0x3f434a; // 默认边框
pub const BORDER_STRONG: u32 = 0x4a4e55;
pub const TEXT: u32 = 0xe8eaee; // 主文字
pub const TEXT_DIM: u32 = 0xa3a8b2; // 次要文字
pub const TEXT_FAINT: u32 = 0x6c727c; // 弱化文字
pub const ACCENT: u32 = 0x7fc9c4; // 青绿高亮（激活/焦点/hover）
pub const ACCENT_DIM: u32 = 0x5aa39f;
pub const WARN: u32 = 0xf0a34a; // 橙色警示（L3 锁存）
pub const DANGER: u32 = 0xe5554f;
pub const OK: u32 = 0x4fc08d; // 连接正常/运行中

/// 安装中文字体（微软雅黑），否则中文会渲染为方块
pub fn install_fonts(ctx: &egui::Context) {
    let mut fonts = FontDefinitions::default();
    for path in ["C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simhei.ttf"] {
        if let Ok(data) = std::fs::read(path) {
            let name = format!("cjk-{}", path.rsplit('/').next().unwrap_or("font"));
            fonts
                .font_data
                .insert(name.clone().into(), FontData::from_owned(data).into());
            fonts
                .families
                .entry(FontFamily::Proportional)
                .or_default()
                .push(name.clone().into());
            fonts
                .families
                .entry(FontFamily::Monospace)
                .or_default()
                .push(name.into());
            break;
        }
    }
    ctx.set_fonts(fonts);
}

/// 应用深色主题到 egui 上下文
pub fn apply(ctx: &egui::Context) {
    ctx.set_theme(egui::ThemePreference::Dark);

    let mut v = egui::Visuals::dark();
    v.panel_fill = rgb(BG);
    v.window_fill = rgb(BG_PANEL);
    v.faint_bg_color = rgb(BG_INSET);
    v.extreme_bg_color = rgb(BG_DEEP);
    v.override_text_color = Some(rgb(TEXT));
    v.hyperlink_color = rgb(ACCENT);
    v.selection.bg_fill = rgb(ACCENT_DIM);
    v.selection.stroke.color = rgb(TEXT);

    // 圆角 / 描边 / 阴影
    let radius = egui::CornerRadius::same(6);
    v.window_corner_radius = egui::CornerRadius::same(8);
    v.window_stroke = egui::Stroke::new(1.0, rgb(BORDER));
    v.window_shadow = egui::Shadow::NONE;
    v.popup_shadow = egui::Shadow::NONE;

    // 控件各状态配色（与 Qt 深色主题一致）
    let mut ni = v.widgets.noninteractive;
    ni.bg_fill = rgb(BG_PANEL);
    ni.weak_bg_fill = rgb(BG_INSET);
    ni.bg_stroke = egui::Stroke::new(1.0, rgb(BORDER));
    ni.fg_stroke.color = rgb(TEXT_DIM);
    ni.corner_radius = radius;
    let mut inert = v.widgets.inactive;
    inert.bg_fill = rgb(BG_INSET);
    inert.weak_bg_fill = rgb(BG_INSET);
    inert.fg_stroke.color = rgb(TEXT);
    inert.corner_radius = radius;
    let mut hovered = v.widgets.hovered;
    hovered.bg_fill = rgb(BORDER_STRONG);
    hovered.weak_bg_fill = rgb(BORDER);
    hovered.fg_stroke.color = rgb(TEXT);
    hovered.corner_radius = radius;
    let mut active = v.widgets.active;
    active.bg_fill = rgb(ACCENT_DIM);
    active.weak_bg_fill = rgb(BORDER);
    active.fg_stroke.color = rgb(BG_DEEP);
    active.corner_radius = radius;
    v.widgets.noninteractive = ni;
    v.widgets.inactive = inert;
    v.widgets.hovered = hovered;
    v.widgets.active = active;

    let mut style = (*ctx.style_of(egui::Theme::Dark)).clone();
    style.visuals = v;
    style.spacing.item_spacing = egui::vec2(8.0, 8.0);
    style.spacing.button_padding = egui::vec2(12.0, 6.0);
    style.spacing.interact_size = egui::vec2(32.0, 26.0);
    ctx.set_style_of(egui::Theme::Dark, style);
}

// ---------------------------------------------------------------------
// 通用控件辅助（与程序整体深色风格一致）
// ---------------------------------------------------------------------

/// 主操作按钮（青绿填充 + 深色文字）
pub fn btn_accent(ui: &mut egui::Ui, text: &str) -> egui::Response {
    ui.add(
        egui::Button::new(egui::RichText::new(text).color(rgb(BG_DEEP)).strong().size(14.0))
            .fill(rgb(ACCENT))
            .corner_radius(6.0),
    )
}

/// 次要按钮（内嵌底色 + 细边框）
pub fn btn_ghost(ui: &mut egui::Ui, text: &str) -> egui::Response {
    ui.add(
        egui::Button::new(egui::RichText::new(text).color(rgb(TEXT)).size(14.0))
            .fill(rgb(BG_INSET))
            .stroke(egui::Stroke::new(1.0, rgb(BORDER)))
            .corner_radius(6.0),
    )
}

/// 危险按钮（红底白字）
pub fn btn_danger(ui: &mut egui::Ui, text: &str) -> egui::Response {
    ui.add(
        egui::Button::new(
            egui::RichText::new(text)
                .color(egui::Color32::WHITE)
                .strong()
                .size(14.0),
        )
        .fill(rgb(DANGER))
        .corner_radius(6.0),
    )
}

/// 分组卡片容器：圆角 + 面板底色 + 细边框
pub fn card<R>(ui: &mut egui::Ui, add_contents: impl FnOnce(&mut egui::Ui) -> R) -> R {
    egui::Frame::default()
        .fill(rgb(BG_PANEL))
        .stroke(egui::Stroke::new(1.0, rgb(BORDER)))
        .corner_radius(8.0)
        .inner_margin(egui::Margin::same(12))
        .show(ui, add_contents)
        .inner
}
