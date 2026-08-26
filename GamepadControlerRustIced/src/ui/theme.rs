// =====================================================================
// theme.rs —— 深色主题颜色常量
//
// 与 Qt 版主窗口深色主题保持一致的配色。
// =====================================================================

use iced::Color;

/// 0xRRGGBB -> Color
pub fn rgb(hex: u32) -> Color {
    Color {
        r: ((hex >> 16) & 0xFF) as f32 / 255.0,
        g: ((hex >> 8) & 0xFF) as f32 / 255.0,
        b: (hex & 0xFF) as f32 / 255.0,
        a: 1.0,
    }
}

/// 0xAARRGGBB -> Color（带 alpha）
pub fn argb(argb: u32) -> Color {
    Color {
        r: ((argb >> 16) & 0xFF) as f32 / 255.0,
        g: ((argb >> 8) & 0xFF) as f32 / 255.0,
        b: (argb & 0xFF) as f32 / 255.0,
        a: ((argb >> 24) & 0xFF) as f32 / 255.0,
    }
}

// ---- 基础色板（与 Qt 版一致）----
pub const BG_DEEP: u32 = 0x232529;
pub const BG: u32 = 0x2b2d31;
pub const BG_PANEL: u32 = 0x2f3136;
pub const BG_INSET: u32 = 0x33363b;
pub const BORDER: u32 = 0x3f434a;
pub const BORDER_STRONG: u32 = 0x4a4e55;
pub const TEXT: u32 = 0xe8eaee;
pub const TEXT_DIM: u32 = 0xa3a8b2;
pub const TEXT_FAINT: u32 = 0x6c727c;
pub const ACCENT: u32 = 0x7fc9c4;
pub const ACCENT_DIM: u32 = 0x5aa39f;
pub const WARN: u32 = 0xf0a34a;
pub const DANGER: u32 = 0xe5554f;
pub const OK: u32 = 0x4fc08d;
