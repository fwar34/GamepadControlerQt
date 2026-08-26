// =====================================================================
// input_types.rs —— 手柄/鼠标/键盘基础类型
//
// 与 Qt 版（InputTypes.h）保持一致：
//  - KeyCode 沿用 Android KeyEvent 常量，保证配置文件格式兼容；
//  - 手柄按钮/鼠标按钮/摇杆统一为本地枚举，避免暴露 Windows XInput
//    或 Android KeyEvent 的底层差异。
// =====================================================================

// ---------------------------------------------------------------------
// Android KeyEvent keycode 常量（配置文件中保存的按键值）
// 运行时通过 injector::android_key_code_to_windows_vk() 转换为 Windows
// 虚拟键码（VK）后再注入系统。
// ---------------------------------------------------------------------
#[allow(non_upper_case_globals)]
pub mod android_key {
    // 字母 A-Z: 29..54
    pub const A: i32 = 29;
    pub const B: i32 = 30;
    pub const C: i32 = 31;
    pub const D: i32 = 32;
    pub const E: i32 = 33;
    pub const F: i32 = 34;
    pub const G: i32 = 35;
    pub const H: i32 = 36;
    pub const I: i32 = 37;
    pub const J: i32 = 38;
    pub const K: i32 = 39;
    pub const L: i32 = 40;
    pub const M: i32 = 41;
    pub const N: i32 = 42;
    pub const O: i32 = 43;
    pub const P: i32 = 44;
    pub const Q: i32 = 45;
    pub const R: i32 = 46;
    pub const S: i32 = 47;
    pub const T: i32 = 48;
    pub const U: i32 = 49;
    pub const V: i32 = 50;
    pub const W: i32 = 51;
    pub const X: i32 = 52;
    pub const Y: i32 = 53;
    pub const Z: i32 = 54;
    // 数字 0-9: 7..16
    pub const N0: i32 = 7;
    pub const N1: i32 = 8;
    pub const N2: i32 = 9;
    pub const N3: i32 = 10;
    pub const N4: i32 = 11;
    pub const N5: i32 = 12;
    pub const N6: i32 = 13;
    pub const N7: i32 = 14;
    pub const N8: i32 = 15;
    pub const N9: i32 = 16;
    // 功能键 F1-F12: 131..142
    pub const F1: i32 = 131;
    pub const F2: i32 = 132;
    pub const F3: i32 = 133;
    pub const F4: i32 = 134;
    pub const F5: i32 = 135;
    pub const F6: i32 = 136;
    pub const F7: i32 = 137;
    pub const F8: i32 = 138;
    pub const F9: i32 = 139;
    pub const F10: i32 = 140;
    pub const F11: i32 = 141;
    pub const F12: i32 = 142;
    // 修饰键
    pub const SHIFT_LEFT: i32 = 59;
    pub const SHIFT_RIGHT: i32 = 60;
    pub const CTRL_LEFT: i32 = 113;
    pub const CTRL_RIGHT: i32 = 114;
    pub const ALT_LEFT: i32 = 57;
    pub const ALT_RIGHT: i32 = 58;
    // 特殊键
    pub const SPACE: i32 = 62;
    pub const ENTER: i32 = 66;
    pub const TAB: i32 = 61;
    pub const ESCAPE: i32 = 111;
    pub const BACK: i32 = 4;
    pub const DEL: i32 = 67;
    pub const INSERT: i32 = 124;
    pub const HOME: i32 = 123;
    pub const PAGE_UP: i32 = 92;
    pub const PAGE_DOWN: i32 = 93;
    pub const MOVE_END: i32 = 122;
    // 方向键（键盘方向键码，与手柄 DPad 无关）
    pub const DPAD_UP: i32 = 19;
    pub const DPAD_DOWN: i32 = 20;
    pub const DPAD_LEFT: i32 = 21;
    pub const DPAD_RIGHT: i32 = 22;
    // 符号键
    pub const MINUS: i32 = 69;
    pub const EQUALS: i32 = 70;
    pub const LEFT_BRACKET: i32 = 71;
    pub const RIGHT_BRACKET: i32 = 72;
    pub const BACKSLASH: i32 = 73;
    pub const SEMICOLON: i32 = 74;
    pub const APOSTROPHE: i32 = 75;
    pub const COMMA: i32 = 55;
    pub const PERIOD: i32 = 56;
    pub const SLASH: i32 = 76;
    pub const GRAVE: i32 = 68;
    // 锁键
    pub const CAPS_LOCK: i32 = 115;
    pub const NUM_LOCK: i32 = 143;
    pub const SCROLL_LOCK: i32 = 116;
    // 小键盘 0-9: 144..153
    pub const NUMPAD_0: i32 = 144;
    pub const NUMPAD_1: i32 = 145;
    pub const NUMPAD_2: i32 = 146;
    pub const NUMPAD_3: i32 = 147;
    pub const NUMPAD_4: i32 = 148;
    pub const NUMPAD_5: i32 = 149;
    pub const NUMPAD_6: i32 = 150;
    pub const NUMPAD_7: i32 = 151;
    pub const NUMPAD_8: i32 = 152;
    pub const NUMPAD_9: i32 = 153;
}

// ---------------------------------------------------------------------
// ControllerButton —— 手柄物理按键的统一枚举
// ---------------------------------------------------------------------
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ControllerButton {
    A,
    B,
    X,
    Y,
    LeftShoulder,        // LB
    RightShoulder,       // RB
    LeftTriggerClick,    // LT
    RightTriggerClick,   // RT
    LeftStickClick,      // L3
    RightStickClick,     // R3
    Menu,                // START
    Options,             // BACK
    Guide,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    TouchpadClick,
}

// ---------------------------------------------------------------------
// ControllerStick —— 摇杆
// ---------------------------------------------------------------------
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ControllerStick {
    LeftStick,
    RightStick,
    DpadAsStick,
}

// 鼠标按键（名称用大写，与安卓版枚举名一致，保证配置文件兼容）
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum MouseButton {
    Left,
    Right,
    Middle,
    Forward,
    Back,
}

// ---------------------------------------------------------------------
// Vector2 —— 二维向量（摇杆输入）
// ---------------------------------------------------------------------
#[derive(Debug, Clone, Copy)]
pub struct Vector2 {
    pub x: f32,
    pub y: f32,
}

impl Vector2 {
    pub fn new(x: f32, y: f32) -> Self {
        Self { x, y }
    }

    pub fn magnitude(&self) -> f32 {
        (self.x * self.x + self.y * self.y).sqrt()
    }

    // 归一化：长度不足 1e-6 视为零向量
    pub fn normalized(&self) -> Self {
        let m = self.magnitude();
        if m < 1e-6 {
            return Self { x: 0.0, y: 0.0 };
        }
        Self {
            x: self.x / m,
            y: self.y / m,
        }
    }

    // 死区缩放：(mag - deadzone) / (1 - deadzone)
    pub fn with_deadzone(&self, deadzone: f32) -> Self {
        let m = self.magnitude();
        if m <= deadzone {
            return Self { x: 0.0, y: 0.0 };
        }
        let scale = (m - deadzone) / (1.0 - deadzone);
        Self {
            x: self.x * scale,
            y: self.y * scale,
        }
    }
}

// ---------------------------------------------------------------------
// 名称映射：配置持久化名 / 界面显示名
// ---------------------------------------------------------------------

/// 手柄按钮 -> 序列化名（大写英文，写入配置文件，与安卓版一致）
pub fn controller_button_name(b: ControllerButton) -> &'static str {
    match b {
        ControllerButton::A => "A",
        ControllerButton::B => "B",
        ControllerButton::X => "X",
        ControllerButton::Y => "Y",
        ControllerButton::LeftShoulder => "LB",
        ControllerButton::RightShoulder => "RB",
        ControllerButton::LeftTriggerClick => "LT",
        ControllerButton::RightTriggerClick => "RT",
        ControllerButton::LeftStickClick => "L3",
        ControllerButton::RightStickClick => "R3",
        ControllerButton::Menu => "MENU",
        ControllerButton::Options => "OPTIONS",
        ControllerButton::Guide => "GUIDE",
        ControllerButton::DpadUp => "DPAD_UP",
        ControllerButton::DpadDown => "DPAD_DOWN",
        ControllerButton::DpadLeft => "DPAD_LEFT",
        ControllerButton::DpadRight => "DPAD_RIGHT",
        ControllerButton::TouchpadClick => "TOUCHPAD_CLICK",
    }
}

/// 序列化名 -> 手柄按钮；解析失败返回 None
pub fn controller_button_from_name(name: &str) -> Option<ControllerButton> {
    all_controller_buttons()
        .into_iter()
        .find(|&b| controller_button_name(b) == name)
}

/// 手柄按钮 -> 中文显示名（仅 UI 使用）
pub fn controller_button_display_name(b: ControllerButton) -> &'static str {
    match b {
        ControllerButton::A => "A键",
        ControllerButton::B => "B键",
        ControllerButton::X => "X键",
        ControllerButton::Y => "Y键",
        ControllerButton::LeftShoulder => "LB肩键",
        ControllerButton::RightShoulder => "RB肩键",
        ControllerButton::LeftTriggerClick => "LT扳机",
        ControllerButton::RightTriggerClick => "RT扳机",
        ControllerButton::LeftStickClick => "L3摇杆按下",
        ControllerButton::RightStickClick => "R3摇杆按下",
        ControllerButton::Menu => "菜单键",
        ControllerButton::Options => "视图键",
        ControllerButton::Guide => "Home键",
        ControllerButton::DpadUp => "方向键上",
        ControllerButton::DpadDown => "方向键下",
        ControllerButton::DpadLeft => "方向键左",
        ControllerButton::DpadRight => "方向键右",
        ControllerButton::TouchpadClick => "触控板点击",
    }
}

/// 所有可映射的手柄按钮（固定显示顺序）
pub fn all_controller_buttons() -> Vec<ControllerButton> {
    vec![
        ControllerButton::A,
        ControllerButton::B,
        ControllerButton::X,
        ControllerButton::Y,
        ControllerButton::LeftShoulder,
        ControllerButton::RightShoulder,
        ControllerButton::LeftTriggerClick,
        ControllerButton::RightTriggerClick,
        ControllerButton::LeftStickClick,
        ControllerButton::RightStickClick,
        ControllerButton::Menu,
        ControllerButton::Options,
        ControllerButton::DpadUp,
        ControllerButton::DpadDown,
        ControllerButton::DpadLeft,
        ControllerButton::DpadRight,
        ControllerButton::Guide,
        ControllerButton::TouchpadClick,
    ]
}

/// 鼠标键 -> 序列化名（大写，与安卓版一致）
pub fn mouse_button_name(b: MouseButton) -> &'static str {
    match b {
        MouseButton::Left => "LEFT",
        MouseButton::Right => "RIGHT",
        MouseButton::Middle => "MIDDLE",
        MouseButton::Forward => "FORWARD",
        MouseButton::Back => "BACK",
    }
}

/// 鼠标键名 -> 枚举（兼容大小写）
pub fn mouse_button_from_name(name: &str) -> Option<MouseButton> {
    let upper = name.to_ascii_uppercase();
    match upper.as_str() {
        "LEFT" => Some(MouseButton::Left),
        "RIGHT" => Some(MouseButton::Right),
        "MIDDLE" => Some(MouseButton::Middle),
        "FORWARD" => Some(MouseButton::Forward),
        "BACK" => Some(MouseButton::Back),
        _ => None,
    }
}

/// 鼠标键 -> 中文显示名
pub fn mouse_button_display_name(b: MouseButton) -> &'static str {
    match b {
        MouseButton::Left => "鼠标左键",
        MouseButton::Right => "鼠标右键",
        MouseButton::Middle => "鼠标中键",
        MouseButton::Forward => "鼠标前进键",
        MouseButton::Back => "鼠标后退键",
    }
}

/// Android KeyCode -> 可读名称（与安卓版 keyCodeToName 一致）
pub fn key_code_to_name(key_code: i32) -> String {
    // 字母 A-Z：29..54
    if (29..=54).contains(&key_code) {
        return ((b'A' + (key_code - 29) as u8) as char).to_string();
    }
    // 数字 0-9：7..16
    if (7..=16).contains(&key_code) {
        return ((b'0' + (key_code - 7) as u8) as char).to_string();
    }
    // F1-F12：131..142
    if (131..=142).contains(&key_code) {
        return format!("F{}", key_code - 131 + 1);
    }
    // 小键盘 0-9：144..153
    if (144..=153).contains(&key_code) {
        return format!("Num{}", key_code - 144);
    }
    match key_code {
        android_key::SPACE => "Space".to_string(),
        android_key::ENTER => "Enter".to_string(),
        android_key::TAB => "Tab".to_string(),
        android_key::ESCAPE => "Esc".to_string(),
        android_key::BACK => "Back".to_string(),
        android_key::DEL => "Backspace".to_string(),
        android_key::INSERT => "Insert".to_string(),
        android_key::HOME => "Home".to_string(),
        android_key::PAGE_UP => "PageUp".to_string(),
        android_key::PAGE_DOWN => "PageDown".to_string(),
        android_key::MOVE_END => "End".to_string(),
        android_key::SHIFT_LEFT | android_key::SHIFT_RIGHT => "Shift".to_string(),
        android_key::CTRL_LEFT | android_key::CTRL_RIGHT => "Ctrl".to_string(),
        android_key::ALT_LEFT | android_key::ALT_RIGHT => "Alt".to_string(),
        android_key::DPAD_UP => "↑".to_string(),
        android_key::DPAD_DOWN => "↓".to_string(),
        android_key::DPAD_LEFT => "←".to_string(),
        android_key::DPAD_RIGHT => "→".to_string(),
        android_key::MINUS => "-".to_string(),
        android_key::EQUALS => "=".to_string(),
        android_key::LEFT_BRACKET => "[".to_string(),
        android_key::RIGHT_BRACKET => "]".to_string(),
        android_key::BACKSLASH => "\\".to_string(),
        android_key::SEMICOLON => ";".to_string(),
        android_key::APOSTROPHE => "'".to_string(),
        android_key::COMMA => ",".to_string(),
        android_key::PERIOD => ".".to_string(),
        android_key::SLASH => "/".to_string(),
        android_key::GRAVE => "`".to_string(),
        android_key::CAPS_LOCK => "CapsLock".to_string(),
        android_key::NUM_LOCK => "NumLock".to_string(),
        android_key::SCROLL_LOCK => "ScrollLock".to_string(),
        _ => format!("Key({})", key_code),
    }
}

/// 层名 -> 带预设中文别名的展示名（WoW 预设）
pub fn layer_display_name(layer_name: &str) -> String {
    let alias = match layer_name {
        "Layer1" => "战斗",
        "Layer2" => "骑乘",
        "Layer3" => "瞄准",
        "Layer4" => "拾取",
        "Layer5" => "潜行",
        "Layer6" => "钓鱼",
        "Layer7" => "对战",
        "Layer8" => "团本",
        "Layer9" => "旅行",
        "Layer10" => "自定义",
        _ => return layer_name.to_string(),
    };
    format!("{} {}", layer_name, alias)
}
