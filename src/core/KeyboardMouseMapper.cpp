#include "KeyboardMouseMapper.h"

#include <chrono>
#include <cmath>
#include <thread>
#include <windows.h>
#include <mmsystem.h>

// =====================================================================
// KeyboardMouseMapper —— 键鼠映射器实现
//
// 执行 SteamInput 广播的按钮/摇杆事件，转换为 Windows 键鼠注入。
// 包括：按钮映射（键盘+子命令/鼠标单击/MouseToggle 锁存）、
//       左摇杆 WASD 移动、右摇杆 125Hz 平滑视角控制线程。
// =====================================================================

KeyboardMouseMapper::KeyboardMouseMapper(SteamInput* input, InputInjector* injector, QObject* parent)
    : QObject(parent), input_(input), injector_(injector) {}

KeyboardMouseMapper::~KeyboardMouseMapper() {
    stop();
}

// 开始映射：连接 SteamInput 信号、同步全局设置、启动 look 线程
void KeyboardMouseMapper::start() {
    if (running_.load()) return;
    // 必须用 DirectConnection：buttonMapped/stickMapped 在「手柄轮询线程」发出，
    // 若用默认 AutoConnection（接收者在主线程）会变成 QueuedConnection，
    // 导致所有键鼠注入都跑到主线程执行。一旦注入的鼠标按下落在程序自身标题栏上，
    // Windows 会进入非客户区模态追踪循环阻塞主线程，松开事件排不进主线程队列，
    // 注入的 mouse up 永远发不出去 → 标题栏按钮点击不生效、手柄输入整体无响应。
    // DirectConnection 让注入在独立的手柄线程执行，主线程被模态循环占用时
    // 仍能发送 mouse up 让模态循环退出。线程安全由 stateMutex_ + 注入器内部互斥保证。
    const Qt::ConnectionType directUnique =
        static_cast<Qt::ConnectionType>(Qt::DirectConnection | Qt::UniqueConnection);
    connect(input_, &SteamInput::buttonMapped,
            this, &KeyboardMouseMapper::onButtonMapped, directUnique);
    connect(input_, &SteamInput::stickMapped,
            this, &KeyboardMouseMapper::onStickMapped, directUnique);
    connect(input_, &SteamInput::profileChanged,
            this, &KeyboardMouseMapper::onProfileChanged, directUnique);
    onProfileChanged();
    running_.store(true);
    lookThread_ = std::thread(&KeyboardMouseMapper::lookLoop, this);
}

// 停止映射：停 look 线程、断开信号、释放全部注入状态（含 MouseToggle 锁存）
void KeyboardMouseMapper::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (lookThread_.joinable())
        lookThread_.join();
    disconnect(input_, nullptr, this, nullptr);

    releaseAllInputs();
}

// 释放全部注入状态（物理按键/鼠标键 + 所有保持状态，含 MouseToggle 锁存）。
// 供 stop() 和手柄断开（main.cpp 连接 connectedChanged(false)）时调用，
// 避免 toggle 保持的鼠标键在断开后卡死。
void KeyboardMouseMapper::releaseAllInputs() {
    // 与手柄线程的 onButtonMapped/onStickMapped 互斥，
    // 保证「已注入的 down」一定会被这里（或后续松开事件）配对补发 up。
    QMutexLocker locker(&stateMutex_);
    // 释放所有物理注入（含 MouseToggle 保持按下的鼠标键）
    injector_->releaseAll();
    pressedMainKeys_.clear();
    pressedSubKeys_.clear();
    pressedMouseButtons_.clear();
    leftStickPressedKeys_.clear();
    toggledMouseButtons_.clear();
    latestLookX_.store(0.f);
    latestLookY_.store(0.f);
    smoothedLookX_ = 0.f;
    smoothedLookY_ = 0.f;
}

// 配置被替换：将全局设置同步到 look 线程的原子量（smoothing 等）
void KeyboardMouseMapper::onProfileChanged() {
    const GlobalSettings& s = input_->profile.globalSettings;
    lookSensitivity_.store(s.lookSensitivity);
    lookSmoothing_.store(s.lookSmoothing);
    lookAcceleration_.store(s.lookAcceleration);
}

// ---------------------------------------------------------------
// 按钮映射执行
// ---------------------------------------------------------------

// 按钮命中映射入口。
// 松开：按「已注入状态」精确释放（与当前层映射无关，
//       防止长按触发键切换层后松开时释放错对象导致按键卡死）。
// 按下：根据动作类型分发到具体处理器。
void KeyboardMouseMapper::onButtonMapped(ControllerButton button, bool isPressed, const KeyMapping& mapping) {
    // 与 GUI 线程的 releaseAllInputs 互斥，防止并发修改注入状态导致 down/up 不对称
    QMutexLocker locker(&stateMutex_);
    if (!isPressed) {
        releaseButtonInjection(button);
        return;
    }

    switch (mapping.action.type) {
        case MappedAction::Type::KeyboardKey:
            handleKeyboardKey(button, mapping.action.keyCode, mapping.subCommands);
            break;
        case MappedAction::Type::MouseClick:
            handleMouseClick(button, mapping.action.mouseButton);
            break;
        case MappedAction::Type::MouseToggle:
            handleMouseToggle(button, mapping.action.mouseButton);
            break;
        case MappedAction::Type::WheelUp:
            injector_->sendMouseWheel(1);   // 瞬时事件，无松开处理
            break;
        case MappedAction::Type::WheelDown:
            injector_->sendMouseWheel(-1);
            break;
        case MappedAction::Type::SwitchLayer:
        case MappedAction::Type::ToggleMapping:
        case MappedAction::Type::ToggleOnScreenKeyboard:
        case MappedAction::Type::ToggleOverlay:
            break;  // 由 SteamInput 引擎处理
        case MappedAction::Type::MouseMove:
        case MappedAction::Type::LookAround:
            break;  // 摇杆动作在 handleStick 中处理
    }
}

// 按「已注入状态」释放某按钮的全部注入：
//   子命令逆序 -> 主键 -> 鼠标键。
// 注意：不处理 MouseToggle 锁存（toggle 是用户主动锁存机制，
//       松开手柄键不应改变其状态，与安卓版语义一致）。
void KeyboardMouseMapper::releaseButtonInjection(ControllerButton button) {
    // 释放子命令（逆序，与按下顺序相反）
    if (pressedSubKeys_.contains(button)) {
        const QVector<int>& subs = pressedSubKeys_.value(button);
        for (int i = subs.size() - 1; i >= 0; --i)
            injector_->sendKeyUp(subs[i]);
        pressedSubKeys_.remove(button);
    }
    // 释放主键
    if (pressedMainKeys_.contains(button)) {
        injector_->sendKeyUp(pressedMainKeys_.take(button));
    }
    // 释放鼠标（不处理长按保持的）
    if (pressedMouseButtons_.contains(button)) {
        injector_->sendMouseUp(pressedMouseButtons_.take(button));
    }
}

// 键盘映射：先按下主键，再依次按下各子命令（组合键，如 Alt+3）。
// 子命令会过滤掉与主键重复的项；已有主键按下时忽略重复触发。
void KeyboardMouseMapper::handleKeyboardKey(ControllerButton button, int mainKeyCode, const QVector<int>& subs) {
    if (pressedMainKeys_.contains(button)) return;  // 已按下，忽略重复

    const int n = qMin(subs.size(), KeyMapping::MAX_SUB_COMMANDS);
    QVector<int> validSubs;
    validSubs.reserve(n);
    for (int i = 0; i < n; ++i) {
        if (subs[i] == mainKeyCode) continue;  // 避免子命令与主键重复
        validSubs.append(subs[i]);
    }

    injector_->sendKeyDown(mainKeyCode);
    for (const int sub : validSubs)
        injector_->sendKeyDown(sub);

    pressedMainKeys_.insert(button, mainKeyCode);
    pressedSubKeys_.insert(button, validSubs);
}

// 鼠标单击：按下/松开跟随手柄（松开时由 releaseButtonInjection 释放）
void KeyboardMouseMapper::handleMouseClick(ControllerButton button, MouseButton mb) {
    if (pressedMouseButtons_.contains(button)) return;
    injector_->sendMouseDown(mb);
    pressedMouseButtons_.insert(button, mb);
}

// 鼠标长按锁存（MouseToggle）：每次按下切换保持状态。
//  - 首次按下：注入按下并记录（之后松开手柄键不释放）；
//  - 再次按下：注入松开并清除记录。
// 松开手柄键时 releaseButtonInjection 不处理该记录（保持锁存状态）。
void KeyboardMouseMapper::handleMouseToggle(ControllerButton button, MouseButton mb) {
    if (toggledMouseButtons_.contains(button)) {
        injector_->sendMouseUp(mb);
        toggledMouseButtons_.remove(button);
    } else {
        injector_->sendMouseDown(mb);
        toggledMouseButtons_.insert(button, mb);
    }
}

// ---------------------------------------------------------------
// 摇杆处理
// ---------------------------------------------------------------

void KeyboardMouseMapper::onStickMapped(ControllerStick stick, float x, float y) {
    handleStick(stick, x, y);
}

// 摇杆处理：
//   - 右摇杆：仅记录最新值到原子量（look 线程按固定节拍读取并平滑发送）
//   - 左摇杆：WASD 8 方向移动（阈值 0.5），
//     与上一次按键状态做差集，只按下新增、释放消失的键
void KeyboardMouseMapper::handleStick(ControllerStick stick, float x, float y) {
    if (stick == ControllerStick::RIGHT_STICK) {
        latestLookX_.store(x);
        latestLookY_.store(y);
        return;
    }

    // 左摇杆 WASD 移动：修改 leftStickPressedKeys_，与 releaseAllInputs 互斥
    QMutexLocker locker(&stateMutex_);

    // 左摇杆 -> WASD 8 方向（阈值 0.5）
    // 注意：XInput 的 Y 轴向上为正（向上推 => y>0），判定要跟物理方向一致。
    constexpr float THRESHOLD = 0.5f;
    const bool up = y > THRESHOLD;     // 摇杆向上（y 为正）-> W
    const bool down = y < -THRESHOLD;  // 摇杆向下（y 为负）-> S
    const bool left = x < -THRESHOLD;
    const bool right = x > THRESHOLD;

    QSet<int> target;
    if (up) target.insert(AndroidKey::W);
    if (down) target.insert(AndroidKey::S);
    if (left) target.insert(AndroidKey::A);
    if (right) target.insert(AndroidKey::D);

    // 计算需要释放的键（原按下但当前未按）。
    // 注意：先收集到 toRelease 再统一处理，
    // 避免遍历 QSet 的同时修改容器导致未定义行为。
    QVector<int> toRelease;
    for (const int kc : leftStickPressedKeys_) {
        if (!target.contains(kc))
            toRelease.append(kc);
    }
    for (const int kc : toRelease) {
        injector_->sendKeyUp(kc);
        leftStickPressedKeys_.remove(kc);
    }
    // 计算需要按下的键（新按下的）
    for (const int kc : target) {
        if (!leftStickPressedKeys_.contains(kc)) {
            injector_->sendKeyDown(kc);
            leftStickPressedKeys_.insert(kc);
        }
    }
}

// ---------------------------------------------------------------
// 视角循环（125Hz）
// ---------------------------------------------------------------

// look 线程主循环：固定 8ms 节拍调用 processLookTick。
// 使用 timeBeginPeriod(1) 提高系统计时器分辨率，保证节拍准确；
// 实际处理耗时计入 dt（限制在 0.001~0.05s），保证位移积分的时间基准。
void KeyboardMouseMapper::lookLoop() {
    timeBeginPeriod(1);
    auto lastTick = std::chrono::steady_clock::now();
    while (running_.load()) {
        const auto tickStart = std::chrono::steady_clock::now();
        const float dt = qBound(0.001f,
                                static_cast<float>(std::chrono::duration<double>(tickStart - lastTick).count()),
                                0.05f);
        lastTick = tickStart;
        processLookTick(dt);

        const long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - tickStart).count();
        const long sleepMs = LOOK_TICK_MS - elapsedMs;
        if (sleepMs > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
    timeEndPeriod(1);
}

// look 线程单次节拍：右摇杆 -> 平滑 -> 位移 -> 注入鼠标移动。
// 处理流水线：
//   1. 幅值钳制：mag 超过 1 时归一化
//   2. 加速度曲线：pow(mag, accel)，推得越深位移越大
//   3. 时间常数 EMA 平滑：alpha = 1-exp(-dt/tau)，tau = smoothing*0.048s
//      （smoothing=0 时 tau=0，直接采用当前值，无平滑）
//   4. 位移积分：dx = 平滑值 × 灵敏度 × 480px/s × dt
void KeyboardMouseMapper::processLookTick(float dt) {
    float rx = latestLookX_.load();
    float ry = latestLookY_.load();
    const float sens = lookSensitivity_.load();
    const float smoothing = lookSmoothing_.load();
    const float accel = qBound(0.5f, lookAcceleration_.load(), 3.0f);

    // 幅值钳制：摇杆输入理论上 <=1，但小数误差可能略超，归一化处理
    float mag = std::sqrt(rx * rx + ry * ry);
    if (mag > 1.f) {
        rx /= mag;
        ry /= mag;
        mag = 1.f;
    }

    // 加速曲线：幅值 -> 更高幅值（推得越深，输出增长越快）
    if (rx != 0.f || ry != 0.f) {
        const float curve = std::pow(mag, accel);
        const float scale = curve / mag;
        rx *= scale;
        ry *= scale;
    }

    // 时间常数 EMA 平滑（低通滤波，消除摇杆抖动）
    const float tau = qBound(0.f, smoothing, 0.95f) * LOOK_SMOOTH_TAU_MAX;
    const float alpha = (tau <= 0.f) ? 1.f : (1.f - std::exp(-dt / tau));
    smoothedLookX_ = smoothedLookX_ * (1.f - alpha) + rx * alpha;
    smoothedLookY_ = smoothedLookY_ * (1.f - alpha) + ry * alpha;

    // 位移积分：480px/秒 × 灵敏度 × dt（亚像素由注入器余量累积补发）
    // Y 轴取反：XInput 右摇杆向上推 => ry>0，而鼠标向上移动需要 dy<0（屏幕 Y 向下为正）。
    const float dx = smoothedLookX_ * sens * LOOK_SPEED_PX_PER_SEC * dt;
    const float dy = -smoothedLookY_ * sens * LOOK_SPEED_PX_PER_SEC * dt;
    if (dx != 0.f || dy != 0.f)
        injector_->sendMouseMove(dx, dy);
}
