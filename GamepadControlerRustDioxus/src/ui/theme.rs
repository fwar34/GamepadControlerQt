// =====================================================================
// theme.rs —— 深色主题颜色常量 + CSS 样式辅助（Dioxus 版）
//
// 与 Qt 版主窗口深色主题保持一致的配色。Dioxus 桌面端渲染为 WebView，
// 样式使用内联 CSS 字符串（与 gpui 链式布局对应）。
// =====================================================================

/// 0xRRGGBB -> "#rrggbb"
pub fn hex(c: u32) -> String {
    format!("#{:06x}", c & 0xFFFFFF)
}

/// 0xAARRGGBB -> "rgba(r,g,b,a)"
pub fn argb(argb: u32) -> String {
    let a = ((argb >> 24) & 0xFF) as f32 / 255.0;
    format!("rgba({},{},{},{:.3})", (argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, a)
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

// ---- 通用字体（与 Qt 版微软雅黑观感一致）----
pub const FONT: &str = "'Microsoft YaHei','Segoe UI',sans-serif";

/// 窗口根容器样式
pub fn root_css() -> String {
    format!(
        "width:100%;height:100vh;margin:0;padding:0;box-sizing:border-box;\
         background:{};color:{};font-family:{};font-size:14px;overflow:hidden;",
        hex(BG),
        hex(TEXT),
        FONT
    )
}

/// 面板样式
pub fn panel_css() -> String {
    format!(
        "background:{};border:1px solid {};border-radius:10px;padding:12px;box-sizing:border-box;",
        hex(BG_PANEL),
        hex(BORDER)
    )
}

/// 内嵌输入/列表区
pub fn inset_css() -> String {
    format!(
        "background:{};border:1px solid {};border-radius:6px;padding:6px;box-sizing:border-box;",
        hex(BG_INSET),
        hex(BORDER)
    )
}

/// 通用按钮样式；`accent=true` 高亮主按钮
pub fn btn_css(accent: bool) -> String {
    let (bg, border, color) = if accent {
        (hex(ACCENT), hex(ACCENT), hex(BG_DEEP))
    } else {
        (hex(BG_INSET), hex(BORDER_STRONG), hex(TEXT))
    };
    format!(
        "background:{bg};border:1px solid {border};color:{color};border-radius:6px;\
         padding:5px 12px;cursor:pointer;font-family:{font};font-size:13px;\
         white-space:nowrap;user-select:none;",
        bg = bg,
        border = border,
        color = color,
        font = FONT
    )
}

/// 操作集 chip：`active=true` 高亮当前激活集
pub fn chip_css(active: bool) -> String {
    let (bg, border, color) = if active {
        (hex(ACCENT), hex(ACCENT), hex(BG_DEEP))
    } else {
        (hex(BG_INSET), hex(BORDER), hex(TEXT))
    };
    format!(
        "background:{bg};border:1px solid {border};color:{color};border-radius:12px;\
         padding:3px 10px;cursor:pointer;font-family:{font};font-size:13px;\
         white-space:nowrap;user-select:none;",
        bg = bg,
        border = border,
        color = color,
        font = FONT
    )
}

/// 层按钮：`active=true` 高亮当前激活层
pub fn layer_btn_css(active: bool) -> String {
    let (bg, border, color) = if active {
        (hex(ACCENT), hex(ACCENT), hex(BG_DEEP))
    } else {
        (hex(BG_INSET), hex(BORDER), hex(TEXT))
    };
    format!(
        "background:{bg};border:1px solid {border};color:{color};border-radius:6px;\
         padding:6px 10px;cursor:pointer;font-family:{font};font-size:13px;\
         width:100%;text-align:left;box-sizing:border-box;user-select:none;",
        bg = bg,
        border = border,
        color = color,
        font = FONT
    )
}

/// 手柄按键按钮（层编辑页）：`pressed=true` 表示正在按下
pub fn pad_btn_css(pressed: bool, accent: bool) -> String {
    let bg = if pressed {
        hex(ACCENT)
    } else if accent {
        hex(ACCENT_DIM)
    } else {
        hex(BG_INSET)
    };
    let color = if pressed || accent { hex(BG_DEEP) } else { hex(TEXT) };
    format!(
        "background:{bg};border:1px solid {border};color:{color};border-radius:6px;\
         padding:6px 4px;cursor:pointer;font-family:{font};font-size:12px;\
         width:100%;text-align:center;box-sizing:border-box;user-select:none;",
        bg = bg,
        border = hex(BORDER),
        color = color,
        font = FONT
    )
}
