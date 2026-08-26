// =====================================================================
// xinput_source.rs —— XInput 手柄读取源
//
// 等效 Qt 版 XInputGamepadSource.h/.cpp（Windows XInput，不用 QtGamepad）。
// 内部独立线程以固定周期（默认 8ms = 125Hz）轮询 XInputGetState，
// 确保应用在后台/非焦点时仍能正常轮询。
//
// 关键设计：
//   1. 连接防抖：连续 MAX_CONNECTION_FAILS 次轮询失败才判定断开，
//      避免 USB 短暂通信错误导致界面闪烁。
//   2. 释放兜底：断开（或 stop）时，把所有仍处于"按下"状态的按钮
//      强制发一遍松开事件，避免按键/鼠标键卡死。
// =====================================================================

use crate::core::input_types::{ControllerButton, ControllerStick};
use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread::{self, JoinHandle};
use std::time::Duration;
use windows::Win32::Foundation::ERROR_SUCCESS;
use windows::Win32::UI::Input::XboxController::{XInputGetState, XINPUT_STATE};

/// 手柄源事件（回调给上层）
#[derive(Debug, Clone, Copy)]
pub enum SourceEvent {
    /// 连接状态变化（connected=true 已连接）
    Connected(bool),
    /// 按钮按下/松开
    Button(ControllerButton, bool),
    /// 摇杆输入（x,y 归一化到 [-1,1]，未应用死区）
    Stick(ControllerStick, f32, f32),
}

/// 手柄源事件回调（上层为 AppCore 的共享锁）
pub type SourceCallback = Arc<dyn Fn(SourceEvent) + Send + Sync>;

/// XInput 按钮位定义（wButtons 位掩码，与 XINPUT_GAMEPAD_BUTTON_FLAGS 一致）
const BIT_A: u16 = 0x1000;
const BIT_B: u16 = 0x2000;
const BIT_X: u16 = 0x4000;
const BIT_Y: u16 = 0x8000;
const BIT_DPAD_UP: u16 = 0x0001;
const BIT_DPAD_DOWN: u16 = 0x0002;
const BIT_DPAD_LEFT: u16 = 0x0004;
const BIT_DPAD_RIGHT: u16 = 0x0008;
const BIT_START: u16 = 0x0010;
const BIT_BACK: u16 = 0x0020;
const BIT_LEFT_THUMB: u16 = 0x0040;
const BIT_RIGHT_THUMB: u16 = 0x0080;
const BIT_LEFT_SHOULDER: u16 = 0x0100;
const BIT_RIGHT_SHOULDER: u16 = 0x0200;

/// XInput 按钮位映射：XInput wButtons 位 -> 统一按钮枚举
const BUTTON_DEFS: [(u16, ControllerButton); 14] = [
    (BIT_DPAD_UP, ControllerButton::DpadUp),
    (BIT_DPAD_DOWN, ControllerButton::DpadDown),
    (BIT_DPAD_LEFT, ControllerButton::DpadLeft),
    (BIT_DPAD_RIGHT, ControllerButton::DpadRight),
    (BIT_START, ControllerButton::Menu),
    (BIT_BACK, ControllerButton::Options),
    (BIT_LEFT_THUMB, ControllerButton::LeftStickClick),
    (BIT_RIGHT_THUMB, ControllerButton::RightStickClick),
    (BIT_LEFT_SHOULDER, ControllerButton::LeftShoulder),
    (BIT_RIGHT_SHOULDER, ControllerButton::RightShoulder),
    (BIT_A, ControllerButton::A),
    (BIT_B, ControllerButton::B),
    (BIT_X, ControllerButton::X),
    (BIT_Y, ControllerButton::Y),
];

/// 摇杆原始值(SHORT) -> 归一化浮点 -1.0 ~ 1.0
fn axis_to_float(value: i16) -> f32 {
    const MAX: f32 = 32767.0;
    let v = value as f32;
    if v > MAX {
        return 1.0;
    }
    if v < -MAX {
        return -1.0;
    }
    v / MAX
}

const MAX_CONNECTION_FAILS: u32 = 3;

// ---------------------------------------------------------------------
// Poller —— 轮询状态机（在轮询线程内独占使用）
// 持有跨轮询的状态：上一按钮状态 / 连接状态 / 失败计数。
// ---------------------------------------------------------------------
struct Poller {
    player_index: u32,
    connected: bool,
    prev_button_states: HashMap<ControllerButton, bool>,
    connection_fail_count: u32,
    callback: SourceCallback,
}

impl Poller {
    fn new(player_index: u32, callback: SourceCallback) -> Self {
        Self {
            player_index,
            connected: false,
            prev_button_states: HashMap::new(),
            connection_fail_count: 0,
            callback,
        }
    }

    /// 单次轮询手柄状态
    fn poll(&mut self) {
        let mut state = XINPUT_STATE::default();
        let result = unsafe { XInputGetState(self.player_index, &mut state) };

        if result == ERROR_SUCCESS.0 {
            // ---- 连接成功 ----
            self.connection_fail_count = 0; // 只要有成功就读，就视为在线
            if !self.connected {
                self.connected = true;
                (self.callback)(SourceEvent::Connected(true));
            }
            let pad = state.Gamepad;

            // 数字按键：遍历映射表，逐位检查按下状态并对比上次
            for (bit, button) in BUTTON_DEFS {
                let pressed = (pad.wButtons.0 & bit) != 0;
                let prev = *self.prev_button_states.get(&button).unwrap_or(&false);
                if pressed != prev {
                    self.prev_button_states.insert(button, pressed);
                    (self.callback)(SourceEvent::Button(button, pressed));
                }
            }

            // 模拟扳机（LT/RT 是 0~255 的模拟量）：半程以上（>=128）视为"按下"
            self.update_trigger(ControllerButton::LeftTriggerClick, pad.bLeftTrigger >= 128);
            self.update_trigger(ControllerButton::RightTriggerClick, pad.bRightTrigger >= 128);

            // 摇杆：归一化后发射（死区由 SteamInput 统一处理）
            (self.callback)(SourceEvent::Stick(
                ControllerStick::LeftStick,
                axis_to_float(pad.sThumbLX),
                axis_to_float(pad.sThumbLY),
            ));
            (self.callback)(SourceEvent::Stick(
                ControllerStick::RightStick,
                axis_to_float(pad.sThumbRX),
                axis_to_float(pad.sThumbRY),
            ));
        } else {
            // ---- 读取失败（可能短暂抖动，也可能真正断开）----
            self.connection_fail_count += 1;
            if self.connection_fail_count >= MAX_CONNECTION_FAILS && self.connected {
                self.connected = false;
                // 释放所有已按下的按钮，避免 heldButtons 堆积 / 键鼠卡死
                let pressed: Vec<ControllerButton> = self
                    .prev_button_states
                    .iter()
                    .filter(|(_, &p)| p)
                    .map(|(b, _)| *b)
                    .collect();
                for b in pressed {
                    self.prev_button_states.insert(b, false);
                    (self.callback)(SourceEvent::Button(b, false));
                }
                self.prev_button_states.clear();
                (self.callback)(SourceEvent::Connected(false));
            }
        }
    }

    fn update_trigger(&mut self, button: ControllerButton, pressed: bool) {
        let prev = *self.prev_button_states.get(&button).unwrap_or(&false);
        if pressed != prev {
            self.prev_button_states.insert(button, pressed);
            (self.callback)(SourceEvent::Button(button, pressed));
        }
    }
}

pub struct XInputGamepadSource {
    player_index: u32,
    running: Arc<AtomicBool>,
    poll_interval_ms: u64,
    handle: Option<JoinHandle<()>>,
    callback: SourceCallback,
}

impl XInputGamepadSource {
    pub fn new(callback: SourceCallback) -> Self {
        Self {
            player_index: 0,
            running: Arc::new(AtomicBool::new(false)),
            poll_interval_ms: 8,
            handle: None,
            callback,
        }
    }

    /// 启动轮询（重置连接失败计数）
    pub fn start(&mut self) {
        if self.running.load(Ordering::SeqCst) {
            return;
        }
        self.running.store(true, Ordering::SeqCst);
        let running = Arc::clone(&self.running);
        let poll_interval_ms = self.poll_interval_ms;
        let player_index = self.player_index;
        let callback = Arc::clone(&self.callback);
        self.handle = Some(thread::spawn(move || {
            let mut poller = Poller::new(player_index, callback);
            poller.poll(); // 立即轮询一次，快速反馈连接状态
            while running.load(Ordering::SeqCst) {
                thread::sleep(Duration::from_millis(poll_interval_ms));
                poller.poll();
            }
        }));
    }

    /// 停止轮询并清理（通知上层释放所有注入）
    pub fn stop(&mut self) {
        self.running.store(false, Ordering::SeqCst);
        if let Some(h) = self.handle.take() {
            let _ = h.join();
        }
        // 无论是否曾连接都发一次断开事件，上层据此 release_all_inputs
        // （幂等，重复调用无害；确保退出/停止时无残留键鼠注入）。
        (self.callback)(SourceEvent::Connected(false));
    }
}

impl Drop for XInputGamepadSource {
    fn drop(&mut self) {
        self.stop();
    }
}
