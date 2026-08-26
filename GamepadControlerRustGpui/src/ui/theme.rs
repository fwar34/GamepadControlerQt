// =====================================================================
// theme.rs —— 深色主题颜色常量
//
// 与 Qt 版主窗口深色主题保持一致的配色（风格美化不属于本次复刻范围，
// 但保持整体深色观感与 Qt 版接近）。
// =====================================================================

use gpui::Rgba;

/// 0xRRGGBB -> Rgba
pub fn rgb(hex: u32) -> Rgba {
    Rgba {
        r: ((hex >> 16) & 0xFF) as f32 / 255.0,
        g: ((hex >> 8) & 0xFF) as f32 / 255.0,
        b: (hex & 0xFF) as f32 / 255.0,
        a: 1.0,
    }
}

/// 0xAARRGGBB -> Rgba（带 alpha）
pub fn argb(argb: u32) -> Rgba {
    Rgba {
        r: ((argb >> 16) & 0xFF) as f32 / 255.0,
        g: ((argb >> 8) & 0xFF) as f32 / 255.0,
        b: (argb & 0xFF) as f32 / 255.0,
        a: ((argb >> 24) & 0xFF) as f32 / 255.0,
    }
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
