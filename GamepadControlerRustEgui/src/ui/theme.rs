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
    let mut v = egui::Visuals::dark();
    v.panel_fill = rgb(BG);
    v.window_fill = rgb(BG_PANEL);
    v.faint_bg_color = rgb(BG_INSET);
    v.extreme_bg_color = rgb(BG_DEEP);
    v.override_text_color = Some(rgb(TEXT));
    v.selection.bg_fill = rgb(ACCENT_DIM);
    v.selection.stroke.color = rgb(TEXT);
    v.hyperlink_color = rgb(ACCENT);
    v.widgets.noninteractive.bg_fill = rgb(BG_PANEL);
    v.widgets.noninteractive.fg_stroke.color = rgb(TEXT_DIM);
    v.widgets.inactive.bg_fill = rgb(BG_INSET);
    v.widgets.inactive.fg_stroke.color = rgb(TEXT);
    v.widgets.hovered.bg_fill = rgb(BORDER);
    v.widgets.hovered.fg_stroke.color = rgb(TEXT);
    v.widgets.active.bg_fill = rgb(ACCENT_DIM);
    v.widgets.active.fg_stroke.color = rgb(BG_DEEP);
    ctx.set_visuals(v);

    let mut style = (*ctx.style_of(egui::Theme::Dark)).clone();
    style.spacing.item_spacing = egui::vec2(6.0, 6.0);
    style.spacing.button_padding = egui::vec2(10.0, 4.0);
    ctx.set_style_of(egui::Theme::Dark, style);
}
