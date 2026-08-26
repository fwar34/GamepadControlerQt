// =====================================================================
// mapper.rs —— 键鼠映射器
//
// 等效 Qt 版 KeyboardMouseMapper.h/.cpp。
// 职责：
//   - 执行 SteamInput 分发的按钮/摇杆事件，转换为键鼠注入
//   - 按钮映射：键盘（含子命令组合键）、鼠标单击、鼠标长按锁存（MouseToggle）
//   - 松开时按「已注入状态」精确释放（不依赖当前层映射，避免切层卡死）
//   - 左摇杆 -> WASD 8 方向移动（阈值 0.5）
//   - 右摇杆 -> 固定 125Hz（LOOK_TICK_MS=8ms）平滑视角移动循环
//
// 线程模型：
//   - 手柄轮询线程：handle_button / handle_stick 直接执行（与 UI 线程
//     通过 AppCore 的同一把锁串行化）
//   - UI 线程：release_all_inputs（stop / 前台切换 / 手柄断开）
//   - look 线程：独立 std::thread，固定节拍读取右摇杆原子量并注入鼠标移动
// =====================================================================

use crate::core::injector::InputInjector;
use crate::core::input_types::{android_key, ControllerButton, ControllerStick, MouseButton};
use crate::core::mapping_types::{ActionType, KeyMapping};
use crate::core::UiEvent;
use std::collections::{HashMap, HashSet};
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::sync::mpsc::Sender;
use std::sync::Arc;
use std::thread;
use std::time::{Duration, Instant};

// ---- 视角控制常量 ----
const LOOK_SPEED_PX_PER_SEC: f32 = 480.0; // 满幅摇杆每秒像素位移
const LOOK_SMOOTH_TAU_MAX: f32 = 0.048; // 最大时间常数（smoothing=1 时）
const LOOK_TICK_MS: u64 = 8; // 节拍周期（125Hz）

// ---------------------------------------------------------------------
// LookState —— 右摇杆原子状态（look 线程读取，主线程/手柄线程写入）
// 使用 AtomicU32 存储 f32 位模式（Rust 无原生 AtomicF32）。
// ---------------------------------------------------------------------
pub struct LookState {
    pub latest_x: AtomicU32,
    pub latest_y: AtomicU32,
    pub sensitivity: AtomicU32,
    pub smoothing: AtomicU32,
    pub acceleration: AtomicU32,
    pub running: AtomicBool,
}

fn store_f32(a: &AtomicU32, v: f32) {
    a.store(v.to_bits(), Ordering::Relaxed);
}
fn load_f32(a: &AtomicU32) -> f32 {
    f32::from_bits(a.load(Ordering::Relaxed))
}

impl Default for LookState {
    fn default() -> Self {
        Self {
            latest_x: AtomicU32::new(0.0f32.to_bits()),
            latest_y: AtomicU32::new(0.0f32.to_bits()),
            sensitivity: AtomicU32::new(0.5f32.to_bits()),
            smoothing: AtomicU32::new(0.5f32.to_bits()),
            acceleration: AtomicU32::new(1.5f32.to_bits()),
            running: AtomicBool::new(false),
        }
    }
}

impl LookState {
    pub fn update_settings(&self, sensitivity: f32, smoothing: f32, acceleration: f32) {
        store_f32(&self.sensitivity, sensitivity);
        store_f32(&self.smoothing, smoothing);
        store_f32(&self.acceleration, acceleration);
    }
}

// ---------------------------------------------------------------------
// MapperState —— 当前注入状态（按下时记录，松开时按状态精确释放）
// 与 SteamInput 位于同一把锁（AppCore）内，避免并发 down/up 不对称。
// ---------------------------------------------------------------------
#[derive(Default)]
pub struct MapperState {
    /// 按钮 -> 主键 keyCode
    pub pressed_main_keys: HashMap<ControllerButton, i32>,
    /// 按钮 -> 已按下的子命令
    pub pressed_sub_keys: HashMap<ControllerButton, Vec<i32>>,
    /// 按钮 -> 鼠标键
    pub pressed_mouse_buttons: HashMap<ControllerButton, MouseButton>,
    /// WASD 当前按下的 keyCode
    pub left_stick_pressed_keys: HashSet<i32>,
    /// 长按保持（MouseToggle）的鼠标键：按钮 -> 鼠标键
    pub toggled_mouse_buttons: HashMap<ControllerButton, MouseButton>,
    /// 平滑状态（仅 look 线程使用）
    pub smoothed_look_x: f32,
    pub smoothed_look_y: f32,
}

impl MapperState {
    /// 释放全部注入状态（物理按键/鼠标键 + 所有保持状态，含 MouseToggle 锁存）。
    /// 供 stop 和手柄断开时调用，避免 toggle 保持的鼠标键在断开后卡死。
    pub fn release_all_inputs(
        &mut self,
        injector: &InputInjector,
        event_tx: &Sender<UiEvent>,
        look: &LookState,
    ) {
        injector.release_all();
        // 先复制再清空，逐个通知 UI 解除锁存提示
        let toggled: Vec<(ControllerButton, MouseButton)> =
            self.toggled_mouse_buttons.drain().collect();
        for (button, mb) in toggled {
            let _ = event_tx.send(UiEvent::MouseToggleChanged {
                button,
                mb,
                active: false,
            });
        }
        self.pressed_main_keys.clear();
        self.pressed_sub_keys.clear();
        self.pressed_mouse_buttons.clear();
        self.left_stick_pressed_keys.clear();
        self.toggled_mouse_buttons.clear();
        store_f32(&look.latest_x, 0.0);
        store_f32(&look.latest_y, 0.0);
        self.smoothed_look_x = 0.0;
        self.smoothed_look_y = 0.0;
    }

    /// 按「已注入状态」释放某按钮的全部注入（子命令逆序 -> 主键 -> 鼠标）。
    /// 注意：不处理 MouseToggle 锁存（toggle 由用户主动锁存，松开不改变状态）。
    pub fn release_button_injection(&mut self, button: ControllerButton, injector: &InputInjector) {
        // 释放子命令（逆序，与按下顺序相反）
        if let Some(subs) = self.pressed_sub_keys.remove(&button) {
            for sub in subs.iter().rev() {
                injector.send_key_up(*sub);
            }
        }
        // 释放主键
        if let Some(main) = self.pressed_main_keys.remove(&button) {
            injector.send_key_up(main);
        }
        // 释放鼠标（不处理长按保持的）
        if let Some(mb) = self.pressed_mouse_buttons.remove(&button) {
            injector.send_mouse_up(mb);
        }
    }

    /// 按钮命中映射：按下执行动作并记录注入状态；松开精确释放。
    pub fn handle_button(
        &mut self,
        button: ControllerButton,
        is_pressed: bool,
        mapping: &KeyMapping,
        injector: &InputInjector,
        event_tx: &Sender<UiEvent>,
    ) {
        if !is_pressed {
            self.release_button_injection(button, injector);
            return;
        }

        match mapping.action.r#type {
            ActionType::KeyboardKey => {
                self.handle_keyboard_key(button, mapping.action.key_code, &mapping.sub_commands, injector);
            }
            ActionType::MouseClick => {
                self.handle_mouse_click(button, mapping.action.mouse_button, injector);
            }
            ActionType::MouseToggle => {
                self.handle_mouse_toggle(button, mapping.action.mouse_button, injector, event_tx);
            }
            ActionType::WheelUp => injector.send_mouse_wheel(1), // 瞬时事件，无松开处理
            ActionType::WheelDown => injector.send_mouse_wheel(-1),
            ActionType::SwitchLayer
            | ActionType::ToggleMapping
            | ActionType::ToggleOnScreenKeyboard
            | ActionType::ToggleOverlay => {} // 由 SteamInput 引擎处理
            ActionType::MouseMove | ActionType::LookAround => {} // 摇杆动作在 handle_stick 中处理
        }
    }

    /// 键盘映射：先按下主键，再依次按下各子命令（组合键，如 Alt+3）。
    fn handle_keyboard_key(
        &mut self,
        button: ControllerButton,
        main_key_code: i32,
        subs: &[i32],
        injector: &InputInjector,
    ) {
        if self.pressed_main_keys.contains_key(&button) {
            return; // 已按下，忽略重复
        }
        let n = subs.len().min(KeyMapping::MAX_SUB_COMMANDS);
        let mut valid_subs: Vec<i32> = Vec::with_capacity(n);
        for i in 0..n {
            if subs[i] == main_key_code {
                continue; // 避免子命令与主键重复
            }
            valid_subs.push(subs[i]);
        }
        injector.send_key_down(main_key_code);
        for &sub in &valid_subs {
            injector.send_key_down(sub);
        }
        self.pressed_main_keys.insert(button, main_key_code);
        self.pressed_sub_keys.insert(button, valid_subs);
    }

    /// 鼠标单击：按下/松开跟随手柄
    fn handle_mouse_click(
        &mut self,
        button: ControllerButton,
        mb: MouseButton,
        injector: &InputInjector,
    ) {
        if self.pressed_mouse_buttons.contains_key(&button) {
            return;
        }
        injector.send_mouse_down(mb);
        self.pressed_mouse_buttons.insert(button, mb);
    }

    /// 鼠标长按锁存（MouseToggle）：每次按下切换保持状态。
    /// 松开手柄键时 release_button_injection 不处理该记录（保持锁存状态）。
    fn handle_mouse_toggle(
        &mut self,
        button: ControllerButton,
        mb: MouseButton,
        injector: &InputInjector,
        event_tx: &Sender<UiEvent>,
    ) {
        if self.toggled_mouse_buttons.contains_key(&button) {
            injector.send_mouse_up(mb);
            self.toggled_mouse_buttons.remove(&button);
            let _ = event_tx.send(UiEvent::MouseToggleChanged {
                button,
                mb,
                active: false,
            });
        } else {
            injector.send_mouse_down(mb);
            self.toggled_mouse_buttons.insert(button, mb);
            let _ = event_tx.send(UiEvent::MouseToggleChanged {
                button,
                mb,
                active: true,
            });
        }
    }

    /// 摇杆处理：
    ///   - 右摇杆：仅记录最新值到原子量（look 线程按固定节拍读取并平滑发送）
    ///   - 左摇杆：WASD 8 方向移动（阈值 0.5），与上一次按键状态做差集
    pub fn handle_stick(
        &mut self,
        stick: ControllerStick,
        x: f32,
        y: f32,
        injector: &InputInjector,
        look: &LookState,
    ) {
        if stick == ControllerStick::RightStick {
            store_f32(&look.latest_x, x);
            store_f32(&look.latest_y, y);
            return;
        }

        // 左摇杆 -> WASD 8 方向（阈值 0.5）
        // 注意：XInput 的 Y 轴向上为正（向上推 => y>0），判定要跟物理方向一致。
        const THRESHOLD: f32 = 0.5;
        let up = y > THRESHOLD; // 摇杆向上（y 为正）-> W
        let down = y < -THRESHOLD; // 摇杆向下（y 为负）-> S
        let left = x < -THRESHOLD;
        let right = x > THRESHOLD;

        let mut target: HashSet<i32> = HashSet::new();
        if up {
            target.insert(android_key::W);
        }
        if down {
            target.insert(android_key::S);
        }
        if left {
            target.insert(android_key::A);
        }
        if right {
            target.insert(android_key::D);
        }

        // 先收集需要释放的键再统一处理，避免遍历时修改容器
        let to_release: Vec<i32> = self
            .left_stick_pressed_keys
            .iter()
            .copied()
            .filter(|kc| !target.contains(kc))
            .collect();
        for kc in to_release {
            injector.send_key_up(kc);
            self.left_stick_pressed_keys.remove(&kc);
        }
        // 计算需要按下的键（新按下的）
        for kc in target {
            if !self.left_stick_pressed_keys.contains(&kc) {
                injector.send_key_down(kc);
                self.left_stick_pressed_keys.insert(kc);
            }
        }
    }
}

// ---------------------------------------------------------------------
// LookRunner —— 视角控制线程（125Hz）
// ---------------------------------------------------------------------
pub struct LookRunner {
    pub state: Arc<LookState>,
    pub injector: Arc<InputInjector>,
    handle: Option<thread::JoinHandle<()>>,
}

impl LookRunner {
    /// 注入器与视角状态均由 AppCore 持有，通过 Arc 共享，
    /// 保证手柄线程（写 latest_x/y）与 look 线程（读）看到同一份状态。
    pub fn new(injector: Arc<InputInjector>, state: Arc<LookState>) -> Self {
        Self {
            state,
            injector,
            handle: None,
        }
    }

    /// 开始映射：启动 look 线程
    pub fn start(&mut self) {
        if self.state.running.load(Ordering::SeqCst) {
            return;
        }
        self.state.running.store(true, Ordering::SeqCst);
        let state = Arc::clone(&self.state);
        let injector = Arc::clone(&self.injector);
        self.handle = Some(thread::spawn(move || {
            // timeBeginPeriod(1) 提高系统计时器分辨率，保证节拍准确
            let mut last_tick = Instant::now();
            let mut smoothed_x = 0.0f32;
            let mut smoothed_y = 0.0f32;
            while state.running.load(Ordering::SeqCst) {
                let tick_start = Instant::now();
                let dt = (tick_start - last_tick).as_secs_f32().clamp(0.001, 0.05);
                last_tick = tick_start;
                process_look_tick(&state, &injector, dt, &mut smoothed_x, &mut smoothed_y);

                let elapsed = tick_start.elapsed().as_millis() as i64;
                let sleep_ms = LOOK_TICK_MS as i64 - elapsed;
                if sleep_ms > 0 {
                    thread::sleep(Duration::from_millis(sleep_ms as u64));
                }
            }
        }));
    }

    /// 停止映射：停 look 线程
    pub fn stop(&mut self) {
        self.state.running.store(false, Ordering::SeqCst);
        if let Some(h) = self.handle.take() {
            let _ = h.join();
        }
    }
}

impl Drop for LookRunner {
    fn drop(&mut self) {
        self.stop();
    }
}

/// look 线程单次节拍：右摇杆 -> 平滑 -> 位移 -> 注入鼠标移动。
/// 处理流水线：
///   1. 幅值钳制：mag 超过 1 时归一化
///   2. 加速度曲线：pow(mag, accel)，推得越深位移越大
///   3. 时间常数 EMA 平滑：alpha = 1-exp(-dt/tau)，tau = smoothing*0.048s
///   4. 位移积分：dx = 平滑值 × 灵敏度 × 480px/s × dt
fn process_look_tick(
    state: &LookState,
    injector: &InputInjector,
    dt: f32,
    smoothed_x: &mut f32,
    smoothed_y: &mut f32,
) {
    let mut rx = load_f32(&state.latest_x);
    let mut ry = load_f32(&state.latest_y);
    let sens = load_f32(&state.sensitivity);
    let smoothing = load_f32(&state.smoothing);
    let accel = load_f32(&state.acceleration).clamp(0.5, 3.0);

    // 幅值钳制：摇杆输入理论上 <=1，但小数误差可能略超，归一化处理
    let mut mag = (rx * rx + ry * ry).sqrt();
    if mag > 1.0 {
        rx /= mag;
        ry /= mag;
        mag = 1.0;
    }

    // 加速曲线：幅值 -> 更高幅值（推得越深，输出增长越快）
    if rx != 0.0 || ry != 0.0 {
        let curve = mag.powf(accel);
        let scale = curve / mag;
        rx *= scale;
        ry *= scale;
    }

    // 时间常数 EMA 平滑（低通滤波，消除摇杆抖动）
    let tau = smoothing.clamp(0.0, 0.95) * LOOK_SMOOTH_TAU_MAX;
    let alpha = if tau <= 0.0 { 1.0 } else { 1.0 - (-dt / tau).exp() };
    *smoothed_x = *smoothed_x * (1.0 - alpha) + rx * alpha;
    *smoothed_y = *smoothed_y * (1.0 - alpha) + ry * alpha;

    // 位移积分：480px/秒 × 灵敏度 × dt（亚像素由注入器余量累积补发）
    // Y 轴取反：XInput 右摇杆向上推 => ry>0，而鼠标向上移动需要 dy<0（屏幕 Y 向下为正）。
    let dx = *smoothed_x * sens * LOOK_SPEED_PX_PER_SEC * dt;
    let dy = -*smoothed_y * sens * LOOK_SPEED_PX_PER_SEC * dt;
    if dx != 0.0 || dy != 0.0 {
        injector.send_mouse_move(dx, dy);
    }
}
