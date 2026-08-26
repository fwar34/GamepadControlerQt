// =====================================================================
// injector.rs —— Windows 本机键鼠注入器（SendInput 实现）
//
// 等效 Qt 版 InputInjector.h/.cpp（WindowsInputInjector）。
// 使用 SendInput() 直接向系统注入键盘/鼠标事件，无跨进程桥接。
//
// 线程安全：所有注入方法内部通过 Mutex 保护按键状态，
// 可安全地从 GUI 线程（按钮/左摇杆）与 look 线程（右摇杆）并发调用。
//
// 说明：
//  - 注入目标为当前前台窗口（即用户正在操作的任意程序，本机使用场景）；
//  - KeyCode 入参为 Android KeyCode，内部转为 VK/扫描码后注入。
// =====================================================================

use crate::core::input_types::{android_key, MouseButton};
use std::collections::HashSet;
use std::sync::Mutex;
use windows::Win32::UI::Input::KeyboardAndMouse::*;

// windows crate 0.62 未导出这些 XButton / 滚轮常量，自行定义（与 Windows SDK 一致）
const XBUTTON1: u32 = 1; // 第 4 鼠标键（后退）
const XBUTTON2: u32 = 2; // 第 5 鼠标键（前进）
const WHEEL_DELTA: i32 = 120; // 滚轮一格

// ---------------------------------------------------------------------
// Android KeyCode -> Windows 虚拟键码（VK）
// 复用安卓版 BridgeInputInjector 的映射表，未知键返回 0。
// ---------------------------------------------------------------------
pub fn android_key_code_to_windows_vk(code: i32) -> u16 {
    // 字母 A-Z: 29..54 -> VK_A(0x41)..VK_Z
    if (29..=54).contains(&code) {
        return 0x41 + (code - 29) as u16;
    }
    // 数字 0-9: 7..16 -> VK_0(0x30)..VK_9
    if (7..=16).contains(&code) {
        return 0x30 + (code - 7) as u16;
    }
    // F1-F12: 131..142 -> VK_F1(0x70)..VK_F12
    if (131..=142).contains(&code) {
        return 0x70 + (code - 131) as u16;
    }
    // 小键盘 0-9: 144..153 -> VK_NUMPAD0(0x60)..VK_NUMPAD9
    if (144..=153).contains(&code) {
        return 0x60 + (code - 144) as u16;
    }
    match code {
        android_key::SHIFT_LEFT => 0xA0,
        android_key::SHIFT_RIGHT => 0xA1,
        android_key::CTRL_LEFT => 0xA2,
        android_key::CTRL_RIGHT => 0xA3,
        android_key::ALT_LEFT => 0xA4,
        android_key::ALT_RIGHT => 0xA5,
        android_key::SPACE => 0x20,
        android_key::ENTER => 0x0D,
        android_key::TAB => 0x09,
        android_key::ESCAPE => 0x1B,
        android_key::BACK => 0x08,
        android_key::DEL => 0x2E,
        android_key::INSERT => 0x2D,
        android_key::HOME => 0x24,
        android_key::PAGE_UP => 0x21,
        android_key::PAGE_DOWN => 0x22,
        android_key::MOVE_END => 0x23,
        android_key::DPAD_UP => 0x26,
        android_key::DPAD_DOWN => 0x28,
        android_key::DPAD_LEFT => 0x25,
        android_key::DPAD_RIGHT => 0x27,
        android_key::MINUS => 0xBD,
        android_key::EQUALS => 0xBB,
        android_key::LEFT_BRACKET => 0xDB,
        android_key::RIGHT_BRACKET => 0xDD,
        android_key::BACKSLASH => 0xDC,
        android_key::SEMICOLON => 0xBA,
        android_key::APOSTROPHE => 0xDE,
        android_key::COMMA => 0xBC,
        android_key::PERIOD => 0xBE,
        android_key::SLASH => 0xBF,
        android_key::GRAVE => 0xC0,
        android_key::CAPS_LOCK => 0x14,
        android_key::NUM_LOCK => 0x90,
        android_key::SCROLL_LOCK => 0x91,
        _ => 0,
    }
}

// ---------------------------------------------------------------------
// Android KeyCode -> Windows 扫描码（硬件码）
// 硬编码映射，不依赖 MapVirtualKey（后台线程可能无正确键盘布局上下文）。
// 扫描码用于 KEYEVENTF_SCANCODE 模式，DirectInput / Raw Input 游戏主要识别此码。
// ---------------------------------------------------------------------
pub fn android_key_code_to_windows_scan_code(code: i32) -> u16 {
    // 注意：KEYEVENTF_SCANCODE 模式下 Windows 只认 wScan（硬件扫描码），
    // 必须对应键盘真实物理位置，不能按连续值推算！
    // 字母 A-Z: 29..54（QWERTY 物理扫描码，非连续，必须查表）
    if (29..=54).contains(&code) {
        const SC: [u16; 26] = [
            0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, // A-M
            0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C, // N-Z
        ];
        return SC[(code - 29) as usize];
    }
    // 数字 0-9: 7..16（0 在 9 之后，非连续）
    if (7..=16).contains(&code) {
        const SC: [u16; 10] = [0x0B, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A];
        return SC[(code - 7) as usize];
    }
    // F1-F10: 131..140 -> 0x3B..0x44（连续）
    // F11/F12 物理扫描码不与 F1-F10 连续（F11=0x57, F12=0x58），单独处理
    if (131..=140).contains(&code) {
        return 0x3B + (code - 131) as u16;
    }
    if code == android_key::F11 {
        return 0x57;
    }
    if code == android_key::F12 {
        return 0x58;
    }
    // 小键盘 0-9: 144..153（按数字键盘物理布局，非连续）
    if (144..=153).contains(&code) {
        const SC: [u16; 10] = [0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49];
        return SC[(code - 144) as usize];
    }
    match code {
        android_key::SHIFT_LEFT => 0x2A,
        android_key::SHIFT_RIGHT => 0x36,
        android_key::CTRL_LEFT => 0x1D,
        android_key::CTRL_RIGHT => 0x1D, // E0 扩展
        android_key::ALT_LEFT => 0x38,
        android_key::ALT_RIGHT => 0x38, // E0 扩展
        android_key::SPACE => 0x39,
        android_key::ENTER => 0x1C,
        android_key::TAB => 0x0F,
        android_key::ESCAPE => 0x01,
        android_key::BACK => 0x0E,
        android_key::DEL => 0x53,    // E0 扩展
        android_key::INSERT => 0x52, // E0 扩展
        android_key::HOME => 0x47,   // E0 扩展
        android_key::PAGE_UP => 0x49, // E0 扩展
        android_key::PAGE_DOWN => 0x51, // E0 扩展
        android_key::MOVE_END => 0x4F, // E0 扩展
        android_key::DPAD_UP => 0x48,
        android_key::DPAD_DOWN => 0x50,
        android_key::DPAD_LEFT => 0x4B,
        android_key::DPAD_RIGHT => 0x4D,
        android_key::MINUS => 0x0C,
        android_key::EQUALS => 0x0D,
        android_key::LEFT_BRACKET => 0x1A,
        android_key::RIGHT_BRACKET => 0x1B,
        android_key::BACKSLASH => 0x2B,
        android_key::SEMICOLON => 0x27,
        android_key::APOSTROPHE => 0x28,
        android_key::COMMA => 0x33,
        android_key::PERIOD => 0x34,
        android_key::SLASH => 0x35,
        android_key::GRAVE => 0x29,
        android_key::CAPS_LOCK => 0x3A,
        android_key::NUM_LOCK => 0x45, // E0 扩展（Numpad）
        android_key::SCROLL_LOCK => 0x46,
        _ => 0,
    }
}

/// 判断 Android KeyCode 是否为扩展键（需要 KEYEVENTF_EXTENDEDKEY）
fn is_extended_key(code: i32) -> bool {
    matches!(
        code,
        android_key::DPAD_UP
            | android_key::DPAD_DOWN
            | android_key::DPAD_LEFT
            | android_key::DPAD_RIGHT
            | android_key::INSERT
            | android_key::DEL
            | android_key::HOME
            | android_key::MOVE_END
            | android_key::PAGE_UP
            | android_key::PAGE_DOWN
            | android_key::CTRL_RIGHT
            | android_key::ALT_RIGHT
            | android_key::NUM_LOCK
    )
}

/// 鼠标按键对应的 SendInput 事件标志（按下/松开）与 XButton 数据
fn mouse_flags_for(b: MouseButton) -> (MOUSE_EVENT_FLAGS, MOUSE_EVENT_FLAGS, u32) {
    // Windows 约定：XBUTTON1=第 4 键（后退）、XBUTTON2=第 5 键（前进），不能弄反。
    match b {
        MouseButton::Left => (MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP, 0),
        MouseButton::Right => (MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP, 0),
        MouseButton::Middle => (MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP, 0),
        MouseButton::Forward => (MOUSEEVENTF_XDOWN, MOUSEEVENTF_XUP, XBUTTON2),
        MouseButton::Back => (MOUSEEVENTF_XDOWN, MOUSEEVENTF_XUP, XBUTTON1),
    }
}

/// 注入单个键盘事件（down=true 按下，false 松开）
/// 使用 KEYEVENTF_SCANCODE（物理扫描码）模式：MSDN 规定该模式下 wVk 必须为 0。
unsafe fn inject_key(code: i32, down: bool) {
    let sc = android_key_code_to_windows_scan_code(code);
    if sc == 0 {
        return;
    }
    let mut flags = KEYEVENTF_SCANCODE;
    if !down {
        flags |= KEYEVENTF_KEYUP;
    }
    if is_extended_key(code) {
        flags |= KEYEVENTF_EXTENDEDKEY;
    }
    let mut input = INPUT {
        r#type: INPUT_KEYBOARD,
        Anonymous: INPUT_0 {
            ki: KEYBDINPUT {
                wVk: VIRTUAL_KEY(0),
                wScan: sc,
                dwFlags: flags,
                time: 0,
                dwExtraInfo: 0,
            },
        },
    };
    SendInput(&[input], 1);
}

/// 注入单个鼠标按键事件
unsafe fn inject_mouse_button_raw(b: MouseButton, down: bool) {
    let (df, uf, data) = mouse_flags_for(b);
    let flag = if down { df } else { uf };
    if flag.0 == 0 {
        return;
    }
    let mut input = INPUT {
        r#type: INPUT_MOUSE,
        Anonymous: INPUT_0 {
            mi: MOUSEINPUT {
                dx: 0,
                dy: 0,
                mouseData: data,
                dwFlags: flag,
                time: 0,
                dwExtraInfo: 0,
            },
        },
    };
    SendInput(&[input], 1);
}

/// 注入相对鼠标移动事件
unsafe fn inject_mouse_move_raw(dx: i32, dy: i32) {
    let mut input = INPUT {
        r#type: INPUT_MOUSE,
        Anonymous: INPUT_0 {
            mi: MOUSEINPUT {
                dx,
                dy,
                mouseData: 0,
                dwFlags: MOUSEEVENTF_MOVE,
                time: 0,
                dwExtraInfo: 0,
            },
        },
    };
    SendInput(&[input], 1);
}

/// WindowsInputInjector —— SendInput 实现
///
/// 状态记录（pressed_keys_/pressed_buttons_）用于：
///  - 去重：同一键未松开前不会重复注入按下事件
///  - 精确释放：release_all 时遍历释放所有仍按住的键/鼠标键
pub struct InputInjector {
    state: Mutex<InjectorState>,
}

struct InjectorState {
    /// 当前按下的 Android KeyCode
    pressed_keys: HashSet<i32>,
    /// 当前按下的鼠标键
    pressed_buttons: HashSet<MouseButton>,
    /// 亚像素余量累积（X 方向）
    mouse_remainder_x: f32,
    /// 亚像素余量累积（Y 方向）
    mouse_remainder_y: f32,
}

impl Default for InputInjector {
    fn default() -> Self {
        Self::new()
    }
}

impl InputInjector {
    pub fn new() -> Self {
        Self {
            state: Mutex::new(InjectorState {
                pressed_keys: HashSet::new(),
                pressed_buttons: HashSet::new(),
                mouse_remainder_x: 0.0,
                mouse_remainder_y: 0.0,
            }),
        }
    }

    /// 注入能力是否可用（Windows 本机实现恒为 true）
    pub fn is_available(&self) -> bool {
        true
    }

    /// 按下按键（入参为 Android KeyCode；去重后注入）
    pub fn send_key_down(&self, android_key_code: i32) {
        if android_key_code_to_windows_vk(android_key_code) == 0 {
            return;
        }
        let mut st = self.state.lock().unwrap();
        if !st.pressed_keys.insert(android_key_code) {
            return; // 已按下，忽略重复
        }
        drop(st);
        unsafe { inject_key(android_key_code, true) };
    }

    /// 松开按键（只在确实按下过时发送）
    pub fn send_key_up(&self, android_key_code: i32) {
        if android_key_code_to_windows_vk(android_key_code) == 0 {
            return;
        }
        let mut st = self.state.lock().unwrap();
        if !st.pressed_keys.remove(&android_key_code) {
            return;
        }
        drop(st);
        unsafe { inject_key(android_key_code, false) };
    }

    /// 按下鼠标按键（去重）
    pub fn send_mouse_down(&self, button: MouseButton) {
        let mut st = self.state.lock().unwrap();
        if !st.pressed_buttons.insert(button) {
            return;
        }
        drop(st);
        unsafe { inject_mouse_button_raw(button, true) };
    }

    /// 松开鼠标按键（只在确实按下过时发送）
    pub fn send_mouse_up(&self, button: MouseButton) {
        let mut st = self.state.lock().unwrap();
        if !st.pressed_buttons.remove(&button) {
            return;
        }
        drop(st);
        unsafe { inject_mouse_button_raw(button, false) };
    }

    /// 滚动鼠标滚轮（steps>0 上滚、<0 下滚，单位：格）
    pub fn send_mouse_wheel(&self, steps: i32) {
        if steps == 0 {
            return;
        }
        let mut input = INPUT {
            r#type: INPUT_MOUSE,
            Anonymous: INPUT_0 {
                mi: MOUSEINPUT {
                    dx: 0,
                    dy: 0,
                    mouseData: (steps * WHEEL_DELTA as i32) as u32, // 1 格 = 120
                    dwFlags: MOUSEEVENTF_WHEEL,
                    time: 0,
                    dwExtraInfo: 0,
                },
            },
        };
        unsafe {
            SendInput(&[input], 1);
        }
    }

    /// 相对移动鼠标（像素，允许小数）
    /// 亚像素余量累积：小数部分保留，累积满 1px 才补发。
    pub fn send_mouse_move(&self, dx: f32, dy: f32) {
        let mut st = self.state.lock().unwrap();
        st.mouse_remainder_x += dx;
        st.mouse_remainder_y += dy;
        let ix = st.mouse_remainder_x as i32;
        let iy = st.mouse_remainder_y as i32;
        if ix == 0 && iy == 0 {
            return;
        }
        st.mouse_remainder_x -= ix as f32;
        st.mouse_remainder_y -= iy as f32;
        drop(st);
        unsafe { inject_mouse_move_raw(ix, iy) };
    }

    /// 释放所有仍按住的键盘/鼠标键（手柄断开、停止映射、退出时调用）
    pub fn release_all(&self) {
        let mut st = self.state.lock().unwrap();
        for &k in st.pressed_keys.iter() {
            unsafe { inject_key(k, false) };
        }
        st.pressed_keys.clear();
        for &b in st.pressed_buttons.iter() {
            unsafe { inject_mouse_button_raw(b, false) };
        }
        st.pressed_buttons.clear();
        st.mouse_remainder_x = 0.0;
        st.mouse_remainder_y = 0.0;
    }
}
