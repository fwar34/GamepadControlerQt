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

// 【Rust 语法】use 导入语句：导入本 crate 内 core 模块下的 InputInjector（注入器接口，封装 Windows SendInput）。
use crate::core::injector::InputInjector;
// 【Rust 语法】use 花括号导入：从同一模块按需导入多个符号（android_key 键码常量、手柄按钮/摇杆/鼠标键枚举）。
use crate::core::input_types::{android_key, ControllerButton, ControllerStick, MouseButton};
// 【Rust 语法】use 花括号导入：导入映射引擎的动作类型枚举 ActionType 与按键映射结构体 KeyMapping。
use crate::core::mapping_types::{ActionType, KeyMapping};
// 【Rust 语法】use 导入语句：导入 UI 事件类型 UiEvent（用于向 UI 线程发通知）。
use crate::core::UiEvent;
// 【Rust 语法】use 嵌套导入：HashMap（键值映射）与 HashSet（不重复元素集合）。
use std::collections::{HashMap, HashSet};
// 【Rust 语法】use 嵌套导入：原子类型 AtomicBool/AtomicU32 与内存序 Ordering，用于多线程无锁共享数据。
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
// 【Rust 语法】use 导入语句：mpsc（多生产者单消费者）通道的发送端 Sender，用于向 UI 线程发送事件。
use std::sync::mpsc::Sender;
// 【Rust 语法】use 导入语句：Arc<T> 原子引用计数智能指针，让多个线程共享同一份数据的所有权。
use std::sync::Arc;
// 【Rust 语法】use 导入语句：线程模块，用于创建/管理后台线程（std::thread）。
use std::thread;
// 【Rust 语法】use 嵌套导入：Duration 时间段、Instant 时刻（用于计时与固定节拍控制）。
use std::time::{Duration, Instant};

// ---- 视角控制常量 ----
// 【Rust 语法】const 定义编译期常量：全大写命名，类型标注为 f32（单精度浮点数）。
const LOOK_SPEED_PX_PER_SEC: f32 = 480.0; // 满幅摇杆每秒像素位移
const LOOK_SMOOTH_TAU_MAX: f32 = 0.048; // 最大时间常数（smoothing=1 时）
const LOOK_TICK_MS: u64 = 8; // 节拍周期（125Hz）

// ---------------------------------------------------------------------
// LookState —— 右摇杆原子状态（look 线程读取，主线程/手柄线程写入）
// 使用 AtomicU32 存储 f32 位模式（Rust 无原生 AtomicF32）。
// ---------------------------------------------------------------------
// 【Rust 语法】struct 结构体：不派生任何 trait；字段全为原子类型（内部可变，无需 &mut 也能跨线程修改）。
pub struct LookState {
    pub latest_x: AtomicU32, // 【Rust 语法】AtomicU32：原子无符号 32 位整数，多线程可无锁安全读写；此处存右摇杆最新 X 的位模式
    pub latest_y: AtomicU32, // 右摇杆最新 Y 值（f32 的位模式）
    pub sensitivity: AtomicU32, // 视角灵敏度（位模式存储）
    pub smoothing: AtomicU32, // 平滑度（位模式存储）
    pub acceleration: AtomicU32, // 加速度（位模式存储）
    pub running: AtomicBool, // 【Rust 语法】AtomicBool：原子布尔，标记 look 线程是否在运行
} // 结束 LookState 结构体定义

// 【Rust 语法】自由函数（不在 impl 内，属于模块级函数）：把 f32 以位模式写入 AtomicU32。
fn store_f32(a: &AtomicU32, v: f32) {
    // 【Rust 语法】store：写入原子值；`v.to_bits()` 把 f32 转为 u32 位模式；Ordering::Relaxed 为最宽松内存序（无跨线程顺序保证需求）。
    a.store(v.to_bits(), Ordering::Relaxed);
} // 结束 store_f32 函数
// 【Rust 语法】自由函数：从 AtomicU32 读出位模式并还原为 f32。
fn load_f32(a: &AtomicU32) -> f32 {
    f32::from_bits(a.load(Ordering::Relaxed)) // 【Rust 语法】load 读取原子值；from_bits 把 u32 位模式还原为 f32
} // 结束 load_f32 函数

// 【Rust 语法】为 LookState 实现 Default trait（特性）：提供一套默认实例（手动实现，因原子类型无法自动派生默认值）。
impl Default for LookState {
    fn default() -> Self { // 实现 Default trait 要求的 default 方法
        Self { // 构造 LookState 默认实例
            latest_x: AtomicU32::new(0.0f32.to_bits()), // 【Rust 语法】AtomicU32::new(初值) 创建原子量；0.0f32 浮点字面量转位模式
            latest_y: AtomicU32::new(0.0f32.to_bits()), // Y 初始 0
            sensitivity: AtomicU32::new(0.5f32.to_bits()), // 灵敏度默认 0.5
            smoothing: AtomicU32::new(0.5f32.to_bits()), // 平滑度默认 0.5
            acceleration: AtomicU32::new(1.5f32.to_bits()), // 加速度默认 1.5
            running: AtomicBool::new(false), // 初始标记为未运行
        } // 结束 Self 结构体字面量
    } // 结束 default 函数
} // 结束 impl Default for LookState 块

// 【Rust 语法】impl 块：为 LookState 实现方法。
impl LookState {
    // 【Rust 语法】方法：`&self` 不可变借用即可（原子字段具备内部可变性，无需 &mut）。
    pub fn update_settings(&self, sensitivity: f32, smoothing: f32, acceleration: f32) {
        store_f32(&self.sensitivity, sensitivity); // 写入新的灵敏度
        store_f32(&self.smoothing, smoothing); // 写入新的平滑度
        store_f32(&self.acceleration, acceleration); // 写入新的加速度
    } // 结束 update_settings 函数
} // 结束 impl LookState 块

// ---------------------------------------------------------------------
// MapperState —— 当前注入状态（按下时记录，松开时按状态精确释放）
// 与 SteamInput 位于同一把锁（AppCore）内，避免并发 down/up 不对称。
// ---------------------------------------------------------------------
// 【Rust 语法】#[derive(Default)]：自动为所有字段实现默认值（HashMap/HashSet 默认空、f32 默认 0.0，均满足 Default）。
#[derive(Default)]
// 【Rust 语法】struct 结构体：记录各手柄按钮当前已注入的键鼠状态，供松开时精确释放。
pub struct MapperState {
    /// 按钮 -> 主键 keyCode
    pub pressed_main_keys: HashMap<ControllerButton, i32>, // 当前按下的主键记录（手柄按钮 -> 键码）
    /// 按钮 -> 已按下的子命令
    pub pressed_sub_keys: HashMap<ControllerButton, Vec<i32>>, // 已按下的组合键子命令记录
    /// 按钮 -> 鼠标键
    pub pressed_mouse_buttons: HashMap<ControllerButton, MouseButton>, // 当前按下的鼠标键记录
    /// WASD 当前按下的 keyCode
    pub left_stick_pressed_keys: HashSet<i32>, // 左摇杆 WASD 当前按下的键码集合
    /// 长按保持（MouseToggle）的鼠标键：按钮 -> 鼠标键
    pub toggled_mouse_buttons: HashMap<ControllerButton, MouseButton>, // MouseToggle 锁存的鼠标键记录
    /// 平滑状态（仅 look 线程使用）
    pub smoothed_look_x: f32, // 视角 EMA 平滑后的 X 值
    pub smoothed_look_y: f32, // 视角 EMA 平滑后的 Y 值
} // 结束 MapperState 结构体定义

// 【Rust 语法】impl 块：为 MapperState 实现方法。
impl MapperState {
    /// 释放全部注入状态（物理按键/鼠标键 + 所有保持状态，含 MouseToggle 锁存）。
    /// 供 stop 和手柄断开时调用，避免 toggle 保持的鼠标键在断开后卡死。
    // 【Rust 语法】方法：`&mut self` 可变借用；参数均用 `&` 引用（借用，不转移所有权）：注入器、事件发送端、视角状态。
    pub fn release_all_inputs(
        &mut self, // 可变借用自身（需要清空记录字段）
        injector: &InputInjector, // 注入器引用（只读）
        event_tx: &Sender<UiEvent>, // 事件通道发送端引用（只读）
        look: &LookState, // 视角状态引用（只读，用于清零摇杆值）
    ) { // 结束参数列表，函数体开始
        injector.release_all(); // 让注入器一次性松开所有物理按键/鼠标键
        // 先复制再清空，逐个通知 UI 解除锁存提示
        // 【Rust 语法】drain() 方法：取出并清空 HashMap 的全部键值对并返回迭代器；collect() 把迭代器收集为 Vec（元组向量）。
        let toggled: Vec<(ControllerButton, MouseButton)> =
            self.toggled_mouse_buttons.drain().collect(); // 把全部锁存记录取出并收集为向量
        // 【Rust 语法】for 循环 + 元组解构：每次迭代把 (按钮, 鼠标键) 元组拆解到 button、mb 两个变量。
        for (button, mb) in toggled {
            // 【Rust 语法】`let _ = ...`：显式忽略表达式返回的 Result（发送失败也无需处理）。
            let _ = event_tx.send(UiEvent::MouseToggleChanged { // 通知 UI：该鼠标键的锁存已解除
                button, // 手柄按钮
                mb, // 鼠标键
                active: false, // 标记为非激活（解除锁存）
            }); // 结束事件构造与发送
        } // 结束 for 循环
        self.pressed_main_keys.clear(); // 清空主键记录
        self.pressed_sub_keys.clear(); // 清空子命令记录
        self.pressed_mouse_buttons.clear(); // 清空鼠标键记录
        self.left_stick_pressed_keys.clear(); // 清空左摇杆按键记录
        self.toggled_mouse_buttons.clear(); // 清空锁存记录
        store_f32(&look.latest_x, 0.0); // 清零右摇杆 X 值
        store_f32(&look.latest_y, 0.0); // 清零右摇杆 Y 值
        self.smoothed_look_x = 0.0; // 复位平滑 X
        self.smoothed_look_y = 0.0; // 复位平滑 Y
    } // 结束 release_all_inputs 函数

    /// 按「已注入状态」释放某按钮的全部注入（子命令逆序 -> 主键 -> 鼠标）。
    /// 注意：不处理 MouseToggle 锁存（toggle 由用户主动锁存，松开不改变状态）。
    // 【Rust 语法】方法：`&mut self` 可变借用（需要从记录中移除条目）。
    pub fn release_button_injection(&mut self, button: ControllerButton, injector: &InputInjector) {
        // 释放子命令（逆序，与按下顺序相反）
        // 【Rust 语法】if let 模式匹配：remove(&button) 移除并返回 Option<Vec<i32>>，取出时绑定到 subs。
        if let Some(subs) = self.pressed_sub_keys.remove(&button) {
            // 【Rust 语法】`subs.iter().rev()`：反向迭代引用；`sub` 为元素引用 &i32，`*sub` 解引用取 i32 值。
            for sub in subs.iter().rev() {
                injector.send_key_up(*sub); // 逐个发送键抬起事件
            } // 结束 for 循环
        } // 结束 if let 分支
        // 释放主键
        if let Some(main) = self.pressed_main_keys.remove(&button) {
            injector.send_key_up(main); // 发送主键抬起
        } // 结束 if let 分支
        // 释放鼠标（不处理长按保持的）
        if let Some(mb) = self.pressed_mouse_buttons.remove(&button) {
            injector.send_mouse_up(mb); // 发送鼠标键抬起
        } // 结束 if let 分支
    } // 结束 release_button_injection 函数

    /// 按钮命中映射：按下执行动作并记录注入状态；松开精确释放。
    // 【Rust 语法】方法：`&mut self` 可变借用；mapping/injector/event_tx 均为引用（只读借用注入器与事件通道）。
    pub fn handle_button(
        &mut self, // 可变借用自身（需要记录/释放注入状态）
        button: ControllerButton, // 触发的手柄按钮
        is_pressed: bool, // 是否按下（true 按下 / false 松开）
        mapping: &KeyMapping, // 命中的按键映射（只读借用）
        injector: &InputInjector, // 注入器引用（只读）
        event_tx: &Sender<UiEvent>, // 事件通道发送端引用（只读）
    ) { // 结束参数列表，函数体开始
        if !is_pressed { // 松开分支
            self.release_button_injection(button, injector); // 按已注入状态精确释放
            return; // 提前结束函数
        } // 结束 if 判断

        // 【Rust 语法】match 模式匹配：按动作类型分发到对应处理逻辑。
        match mapping.action.r#type {
            ActionType::KeyboardKey => { // 键盘按键动作
                self.handle_keyboard_key(button, mapping.action.key_code, &mapping.sub_commands, injector); // 处理组合键按下（主键+子命令）
            } // 结束键盘按键分支
            ActionType::MouseClick => { // 鼠标单击动作
                self.handle_mouse_click(button, mapping.action.mouse_button, injector); // 处理鼠标按下
            } // 结束鼠标单击分支
            ActionType::MouseToggle => { // 鼠标长按锁存动作
                self.handle_mouse_toggle(button, mapping.action.mouse_button, injector, event_tx); // 处理锁存切换
            } // 结束鼠标长按锁存分支
            ActionType::WheelUp => injector.send_mouse_wheel(1), // 滚轮上滚：瞬时事件，无松开处理
            ActionType::WheelDown => injector.send_mouse_wheel(-1), // 滚轮下滚：瞬时事件
            // 【Rust 语法】模式 `|`（或）匹配：多个变体共享同一分支代码。
            ActionType::SwitchLayer // 切换层动作
            | ActionType::ToggleMapping // 切换映射
            | ActionType::ToggleOnScreenKeyboard // 切换屏幕键盘
            | ActionType::ToggleOverlay => {} // 由 SteamInput 引擎处理，此处不注入
            ActionType::MouseMove | ActionType::LookAround => {} // 摇杆动作在 handle_stick 中处理
        } // 结束 match 表达式
    } // 结束 handle_button 函数

    /// 键盘映射：先按下主键，再依次按下各子命令（组合键，如 Alt+3）。
    // 【Rust 语法】私有方法（无 pub 修饰）：`&[i32]` 是切片类型，可看作对数组/向量元素的借用视图。
    fn handle_keyboard_key(
        &mut self, // 可变借用自身（需要记录主键/子命令按下状态）
        button: ControllerButton, // 触发的手柄按钮
        main_key_code: i32, // 主键键码
        subs: &[i32], // 子命令键码切片（只读借用）
        injector: &InputInjector, // 注入器引用（只读）
    ) { // 结束参数列表，函数体开始
        if self.pressed_main_keys.contains_key(&button) { // 若该按钮的主键已按下
            return; // 已按下，忽略重复
        } // 结束 if 判断
        // 【Rust 语法】`subs.len().min(...)`：取长度与上限二者中的较小值（限制子命令数量）。
        let n = subs.len().min(KeyMapping::MAX_SUB_COMMANDS);
        // 【Rust 语法】Vec::with_capacity(n)：预分配容量避免多次扩容；`let mut` 声明可变绑定。
        let mut valid_subs: Vec<i32> = Vec::with_capacity(n);
        // 【Rust 语法】`0..n` 范围表达式：生成从 0 到 n-1 的整数序列，for 逐个迭代。
        for i in 0..n {
            if subs[i] == main_key_code { // 若子命令与主键相同
                continue; // 避免子命令与主键重复，跳过该索引
            } // 结束 if 判断
            valid_subs.push(subs[i]); // 收集有效的子命令键码
        } // 结束 for 循环
        injector.send_key_down(main_key_code); // 先按下主键
        for &sub in &valid_subs { // 【Rust 语法】`&sub` 解构引用：迭代时直接把 &i32 解引用绑定为 i32 值
            injector.send_key_down(sub); // 依次按下各子命令（形成组合键）
        } // 结束 for 循环
        self.pressed_main_keys.insert(button, main_key_code); // 记录主键已按下
        self.pressed_sub_keys.insert(button, valid_subs); // 记录子命令已按下
    } // 结束 handle_keyboard_key 函数

    /// 鼠标单击：按下/松开跟随手柄
    // 【Rust 语法】私有方法：`&mut self` 可变借用。
    fn handle_mouse_click(
        &mut self, // 可变借用自身（需要记录鼠标键按下状态）
        button: ControllerButton, // 触发的手柄按钮
        mb: MouseButton, // 要按下的鼠标键
        injector: &InputInjector, // 注入器引用（只读）
    ) { // 结束参数列表，函数体开始
        if self.pressed_mouse_buttons.contains_key(&button) { // 若该按钮的鼠标键已按下
            return; // 已按下则忽略重复
        } // 结束 if 判断
        injector.send_mouse_down(mb); // 注入鼠标键按下
        self.pressed_mouse_buttons.insert(button, mb); // 记录按下状态
    } // 结束 handle_mouse_click 函数

    /// 鼠标长按锁存（MouseToggle）：每次按下切换保持状态。
    /// 松开手柄键时 release_button_injection 不处理该记录（保持锁存状态）。
    // 【Rust 语法】私有方法：`&mut self` 可变借用；event_tx 为事件通道发送端引用。
    fn handle_mouse_toggle(
        &mut self, // 可变借用自身（需要读写锁存记录）
        button: ControllerButton, // 触发的手柄按钮
        mb: MouseButton, // 要锁存的鼠标键
        injector: &InputInjector, // 注入器引用（只读）
        event_tx: &Sender<UiEvent>, // 事件通道发送端引用（只读）
    ) { // 结束参数列表，函数体开始
        if self.toggled_mouse_buttons.contains_key(&button) { // 若当前已锁存，则本次按下为"解除锁存"
            injector.send_mouse_up(mb); // 抬起鼠标键
            self.toggled_mouse_buttons.remove(&button); // 移除锁存记录
            let _ = event_tx.send(UiEvent::MouseToggleChanged { // 通知 UI：锁存已解除
                button, // 手柄按钮
                mb, // 鼠标键
                active: false, // 标记为非激活
            }); // 结束事件构造与发送
        } else { // 否则本次按下为"建立锁存"
            injector.send_mouse_down(mb); // 按下并保持鼠标键（锁存）
            self.toggled_mouse_buttons.insert(button, mb); // 记录锁存状态
            let _ = event_tx.send(UiEvent::MouseToggleChanged { // 通知 UI：已建立锁存
                button, // 手柄按钮
                mb, // 鼠标键
                active: true, // 标记为激活
            }); // 结束事件构造与发送
        } // 结束 else 分支
    } // 结束 handle_mouse_toggle 函数

    /// 摇杆处理：
    ///   - 右摇杆：仅记录最新值到原子量（look 线程按固定节拍读取并平滑发送）
    ///   - 左摇杆：WASD 8 方向移动（阈值 0.5），与上一次按键状态做差集
    // 【Rust 语法】方法：`&mut self` 可变借用（需修改左摇杆按键集合）。
    pub fn handle_stick(
        &mut self, // 可变借用自身（需要维护左摇杆按键集合）
        stick: ControllerStick, // 摇杆标识（左/右）
        x: f32, // 摇杆 X 轴归一化值（-1 ~ 1）
        y: f32, // 摇杆 Y 轴归一化值（-1 ~ 1）
        injector: &InputInjector, // 注入器引用（只读）
        look: &LookState, // 视角状态引用（右摇杆时写入最新值）
    ) { // 结束参数列表，函数体开始
        if stick == ControllerStick::RightStick { // 右摇杆分支
            store_f32(&look.latest_x, x); // 仅记录最新 X 值到原子量
            store_f32(&look.latest_y, y); // 仅记录最新 Y 值到原子量
            return; // 右摇杆不做按键处理，立即返回
        } // 结束右摇杆分支

        // 左摇杆 -> WASD 8 方向（阈值 0.5）
        // 注意：XInput 的 Y 轴向上为正（向上推 => y>0），判定要跟物理方向一致。
        // 【Rust 语法】const 局部常量：在函数体内声明的常量（此处为摇杆判定阈值）。
        const THRESHOLD: f32 = 0.5;
        let up = y > THRESHOLD; // 摇杆向上（y 为正）-> W
        let down = y < -THRESHOLD; // 摇杆向下（y 为负）-> S
        let left = x < -THRESHOLD; // 摇杆向左（x 为负）-> A
        let right = x > THRESHOLD; // 摇杆向右（x 为正）-> D

        let mut target: HashSet<i32> = HashSet::new(); // 本轮目标按键集合（显式类型标注）
        if up { // 摇杆向上
            target.insert(android_key::W); // 向上：加入 W 键
        } // 结束 if 判断
        if down { // 摇杆向下
            target.insert(android_key::S); // 向下：加入 S 键
        } // 结束 if 判断
        if left { // 摇杆向左
            target.insert(android_key::A); // 向左：加入 A 键
        } // 结束 if 判断
        if right { // 摇杆向右
            target.insert(android_key::D); // 向右：加入 D 键
        } // 结束 if 判断

        // 先收集需要释放的键再统一处理，避免遍历时修改容器
        // 【Rust 语法】迭代器链：iter() 引用迭代 -> copied() 拷贝为值 -> filter(闭包) 过滤 -> collect() 收集为 Vec。
        let to_release: Vec<i32> = self
            .left_stick_pressed_keys // 左摇杆当前按下的键集合
            .iter() // 迭代集合元素引用
            .copied() // 把 &i32 拷贝为 i32 值
            .filter(|kc| !target.contains(kc)) // 保留"上一轮按下但本轮不再需要"的键（差集）
            .collect(); // 收集为 Vec<i32>
        for kc in to_release { // 遍历待释放的键
            injector.send_key_up(kc); // 抬起不再需要的键
            self.left_stick_pressed_keys.remove(&kc); // 从按下记录中移除
        } // 结束 for 循环
        // 计算需要按下的键（新按下的）
        for kc in target {
            if !self.left_stick_pressed_keys.contains(&kc) { // 若该键尚未按下
                injector.send_key_down(kc); // 注入按下
                self.left_stick_pressed_keys.insert(kc); // 记录已按下
            } // 结束 if 判断
        } // 结束 for 循环
    } // 结束 handle_stick 函数
} // 结束 impl MapperState 块

// ---------------------------------------------------------------------
// LookRunner —— 视角控制线程（125Hz）
// ---------------------------------------------------------------------
// 【Rust 语法】struct 结构体：负责管理独立的视角控制后台线程。
pub struct LookRunner {
    pub state: Arc<LookState>, // 【Rust 语法】Arc<LookState>：与手柄线程/UI 线程共享视角状态（引用计数共享）
    pub injector: Arc<InputInjector>, // 共享的注入器（Arc 包装）
    handle: Option<thread::JoinHandle<()>>, // 【Rust 语法】Option<JoinHandle<()>>：后台线程句柄；() 表示该线程不返回值
} // 结束 LookRunner 结构体定义

// 【Rust 语法】impl 块：为 LookRunner 实现方法。
impl LookRunner {
    /// 注入器与视角状态均由 AppCore 持有，通过 Arc 共享，
    /// 保证手柄线程（写 latest_x/y）与 look 线程（读）看到同一份状态。
    // 【Rust 语法】关联函数（无 self）：构造器；参数为 Arc 智能指针（克隆引用计数即可共享同一份数据，不复制底层内容）。
    pub fn new(injector: Arc<InputInjector>, state: Arc<LookState>) -> Self {
        Self { // 构造 LookRunner 实例
            state, // 【Rust 语法】字段简写：同名变量直接赋值给字段
            injector, // 字段简写：注入器
            handle: None, // 初始未启动线程，句柄为 None
        } // 结束 Self 结构体字面量
    } // 结束 new 函数

    /// 开始映射：启动 look 线程
    // 【Rust 语法】方法：`&mut self` 可变借用（需要写入线程句柄字段）。
    pub fn start(&mut self) {
        if self.state.running.load(Ordering::SeqCst) { // 若已处于运行状态则直接返回
            return; // 已运行则直接返回
        } // 结束 if 判断
        self.state.running.store(true, Ordering::SeqCst); // 置运行标记（SeqCst 全序内存序，保证跨线程可见）
        // 【Rust 语法】Arc::clone(&...)：克隆引用计数得到新的 Arc，把共享数据的所有权带入新线程。
        let state = Arc::clone(&self.state); // 克隆视角状态引用计数，供线程共享
        let injector = Arc::clone(&self.injector); // 克隆注入器引用计数，供线程共享
        // 【Rust 语法】thread::spawn(闭包)：在新系统线程中执行闭包；`move` 把闭包捕获的变量所有权移入线程。
        self.handle = Some(thread::spawn(move || {
            // timeBeginPeriod(1) 提高系统计时器分辨率，保证节拍准确
            // 【Rust 语法】Instant::now() 获取当前时刻；`let mut` 声明可变绑定。
            let mut last_tick = Instant::now(); // 记录上一次节拍的时刻
            let mut smoothed_x = 0.0f32; // 平滑 X 初始为 0
            let mut smoothed_y = 0.0f32; // 平滑 Y 初始为 0
            while state.running.load(Ordering::SeqCst) { // 循环直到 running 被置为 false（停止）
                let tick_start = Instant::now(); // 记录本次节拍开始时刻
                // 【Rust 语法】as_secs_f32() 转秒（f32 类型）；clamp(0.001, 0.05) 把数值钳制到区间内（防止除零/过大）。
                let dt = (tick_start - last_tick).as_secs_f32().clamp(0.001, 0.05); // 距上一帧的时间差（秒）
                last_tick = tick_start; // 更新上一帧时刻
                process_look_tick(&state, &injector, dt, &mut smoothed_x, &mut smoothed_y); // 执行一次视角节拍处理（传入可变引用，原地震用）

                // 【Rust 语法】elapsed() 返回 Duration；as_millis() 转毫秒（u128）；`as i64` 类型转换。
                let elapsed = tick_start.elapsed().as_millis() as i64; // 本次节拍处理已消耗的毫秒数
                let sleep_ms = LOOK_TICK_MS as i64 - elapsed; // 距离目标周期还剩余的睡眠时间
                if sleep_ms > 0 { // 若剩余时间大于 0 则睡眠补齐
                    thread::sleep(Duration::from_millis(sleep_ms as u64)); // 睡眠补齐剩余时间，保证固定节拍
                } // 结束 if 判断
            } // 结束 while 循环
        })); // 结束闭包与 thread::spawn 调用
    } // 结束 start 函数

    /// 停止映射：停 look 线程
    // 【Rust 语法】方法：`&mut self` 可变借用（需取出并消费线程句柄）。
    pub fn stop(&mut self) {
        self.state.running.store(false, Ordering::SeqCst); // 置停止标记，循环将退出
        if let Some(h) = self.handle.take() { // 【Rust 语法】take()：取出 Option 中的值并原地置为 None（消费句柄）
            let _ = h.join(); // 【Rust 语法】join()：阻塞等待线程结束；`let _ =` 忽略其返回结果
        } // 结束 if let 分支
    } // 结束 stop 函数
} // 结束 impl LookRunner 块

// 【Rust 语法】impl Drop：实现析构 trait（特性），当 LookRunner 实例被销毁（如离开作用域）时自动调用 drop 方法。
impl Drop for LookRunner {
    fn drop(&mut self) { // 析构函数：对象销毁时被自动调用
        self.stop(); // 销毁时自动停止 look 线程，避免线程泄漏
    } // 结束 drop 函数
} // 结束 impl Drop for LookRunner 块

/// look 线程单次节拍：右摇杆 -> 平滑 -> 位移 -> 注入鼠标移动。
/// 处理流水线：
///   1. 幅值钳制：mag 超过 1 时归一化
///   2. 加速度曲线：pow(mag, accel)，推得越深位移越大
///   3. 时间常数 EMA 平滑：alpha = 1-exp(-dt/tau)，tau = smoothing*0.048s
///   4. 位移积分：dx = 平滑值 × 灵敏度 × 480px/s × dt
// 【Rust 语法】自由函数：参数含借用 `&`（只读）与可变借用 `&mut`（smoothed_x/y 会被原地震用修改）。
fn process_look_tick(
    state: &LookState, // 视角状态（只读借用，含摇杆最新值等）
    injector: &InputInjector, // 注入器（只读借用）
    dt: f32, // 时间步长（秒）
    smoothed_x: &mut f32, // 平滑 X 的可变引用（跨帧累积状态）
    smoothed_y: &mut f32, // 平滑 Y 的可变引用（跨帧累积状态）
) { // 结束参数列表，函数体开始
    let mut rx = load_f32(&state.latest_x); // 读取右摇杆最新 X（声明可变，后续可能被归一化）
    let mut ry = load_f32(&state.latest_y); // 读取右摇杆最新 Y
    let sens = load_f32(&state.sensitivity); // 读取灵敏度
    let smoothing = load_f32(&state.smoothing); // 读取平滑度
    let accel = load_f32(&state.acceleration).clamp(0.5, 3.0); // 读取加速度并钳制在 [0.5, 3.0] 区间

    // 幅值钳制：摇杆输入理论上 <=1，但小数误差可能略超，归一化处理
    let mut mag = (rx * rx + ry * ry).sqrt(); // 计算输入向量幅值（平方和开根号）
    if mag > 1.0 { // 幅值超过 1 则归一化
        rx /= mag; // 归一化 X 分量
        ry /= mag; // 归一化 Y 分量
        mag = 1.0; // 幅值钳制为 1
    } // 结束幅值钳制分支

    // 加速曲线：幅值 -> 更高幅值（推得越深，输出增长越快）
    if rx != 0.0 || ry != 0.0 { // 摇杆有实际输入时
        let curve = mag.powf(accel); // 【Rust 语法】powf(指数)：浮点求幂，计算幅值的 accel 次方
        let scale = curve / mag; // 计算缩放系数
        rx *= scale; // 对 X 分量应用加速缩放
        ry *= scale; // 对 Y 分量应用加速缩放
    } // 结束加速曲线分支

    // 时间常数 EMA 平滑（低通滤波，消除摇杆抖动）
    // 【Rust 语法】clamp：限制数值范围；tau 为滤波时间常数。
    let tau = smoothing.clamp(0.0, 0.95) * LOOK_SMOOTH_TAU_MAX; // 由平滑度换算时间常数
    // 【Rust 语法】if/else 表达式（可作为值使用）：tau<=0 时 alpha=1（无平滑），否则按指数衰减公式计算滤波系数。
    let alpha = if tau <= 0.0 { 1.0 } else { 1.0 - (-dt / tau).exp() };
    *smoothed_x = *smoothed_x * (1.0 - alpha) + rx * alpha; // 【Rust 语法】`*` 解引用可变借用以写入调用方变量；EMA 更新平滑 X
    *smoothed_y = *smoothed_y * (1.0 - alpha) + ry * alpha; // EMA 更新平滑 Y

    // 位移积分：480px/秒 × 灵敏度 × dt（亚像素由注入器余量累积补发）
    // Y 轴取反：XInput 右摇杆向上推 => ry>0，而鼠标向上移动需要 dy<0（屏幕 Y 向下为正）。
    let dx = *smoothed_x * sens * LOOK_SPEED_PX_PER_SEC * dt; // 计算 X 轴位移（像素）
    let dy = -*smoothed_y * sens * LOOK_SPEED_PX_PER_SEC * dt; // 计算 Y 轴位移（取反，因屏幕坐标 Y 向下为正）
    if dx != 0.0 || dy != 0.0 { // 只有存在位移时才注入
        injector.send_mouse_move(dx, dy); // 向系统注入相对鼠标移动
    } // 结束 if 判断
} // 结束 process_look_tick 函数
