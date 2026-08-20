#include "KeyboardMouseMapper.h"

#include <chrono>
#include <cmath>
#include <thread>
#include <windows.h>
#include <mmsystem.h>

// =====================================================================
// 键鼠映射器实现
// =====================================================================

KeyboardMouseMapper::KeyboardMouseMapper(SteamInput* input, InputInjector* injector, QObject* parent)
    : QObject(parent), input_(input), injector_(injector) {}

KeyboardMouseMapper::~KeyboardMouseMapper() {
    stop();
}

void KeyboardMouseMapper::start() {
    if (running_.load()) return;
    connect(input_, &SteamInput::buttonMapped,
            this, &KeyboardMouseMapper::onButtonMapped, Qt::UniqueConnection);
    connect(input_, &SteamInput::stickMapped,
            this, &KeyboardMouseMapper::onStickMapped, Qt::UniqueConnection);
    connect(input_, &SteamInput::profileChanged,
            this, &KeyboardMouseMapper::onProfileChanged, Qt::UniqueConnection);
    onProfileChanged();
    running_.store(true);
    lookThread_ = std::thread(&KeyboardMouseMapper::lookLoop, this);
}

void KeyboardMouseMapper::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (lookThread_.joinable())
        lookThread_.join();
    disconnect(input_, nullptr, this, nullptr);

    releaseAllInputs();
}

void KeyboardMouseMapper::releaseAllInputs() {
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

void KeyboardMouseMapper::onProfileChanged() {
    const GlobalSettings& s = input_->profile.globalSettings;
    lookSensitivity_.store(s.lookSensitivity);
    lookSmoothing_.store(s.lookSmoothing);
    lookAcceleration_.store(s.lookAcceleration);
}

// ---------------------------------------------------------------
// 按钮映射执行
// ---------------------------------------------------------------

void KeyboardMouseMapper::onButtonMapped(ControllerButton button, bool isPressed, const KeyMapping& mapping) {
    // 松开：按"已注入状态"精确释放（与当前层映射无关，防止切层导致按键卡死）
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
        case MappedAction::Type::SwitchLayer:
            break;  // 由 SteamInput 引擎处理
        case MappedAction::Type::MouseMove:
        case MappedAction::Type::LookAround:
            break;  // 摇杆动作在 handleStick 中处理
    }
}

void KeyboardMouseMapper::releaseButtonInjection(ControllerButton button) {
    // 释放子命令（逆序）
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

void KeyboardMouseMapper::handleMouseClick(ControllerButton button, MouseButton mb) {
    if (pressedMouseButtons_.contains(button)) return;
    injector_->sendMouseDown(mb);
    pressedMouseButtons_.insert(button, mb);
}

void KeyboardMouseMapper::handleMouseToggle(ControllerButton button, MouseButton mb) {
    // 按下切换保持状态，松开时由 releaseButtonInjection 保持（不释放）
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

void KeyboardMouseMapper::handleStick(ControllerStick stick, float x, float y) {
    if (stick == ControllerStick::RIGHT_STICK) {
        // 仅记录最新值，由 look 线程按固定频率发送
        latestLookX_.store(x);
        latestLookY_.store(y);
        return;
    }

    // 左摇杆 -> WASD 8 方向（阈值 0.5）
    constexpr float THRESHOLD = 0.5f;
    const bool up = y < -THRESHOLD;
    const bool down = y > THRESHOLD;
    const bool left = x < -THRESHOLD;
    const bool right = x > THRESHOLD;

    QSet<int> target;
    if (up) target.insert(AndroidKey::W);
    if (down) target.insert(AndroidKey::S);
    if (left) target.insert(AndroidKey::A);
    if (right) target.insert(AndroidKey::D);

    // 计算需要释放的键（原按下但当前未按）
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

void KeyboardMouseMapper::lookLoop() {
    // 提高 Windows 计时器分辨率，保证 8ms 节拍准确
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

void KeyboardMouseMapper::processLookTick(float dt) {
    float rx = latestLookX_.load();
    float ry = latestLookY_.load();
    const float sens = lookSensitivity_.load();
    const float smoothing = lookSmoothing_.load();
    const float accel = qBound(0.5f, lookAcceleration_.load(), 3.0f);

    // 幅值钳制
    float mag = std::sqrt(rx * rx + ry * ry);
    if (mag > 1.f) {
        rx /= mag;
        ry /= mag;
        mag = 1.f;
    }

    // 加速曲线：幅值 -> 更高幅值
    if (rx != 0.f || ry != 0.f) {
        const float curve = std::pow(mag, accel);
        const float scale = curve / mag;
        rx *= scale;
        ry *= scale;
    }

    // 时间常数 EMA 平滑
    const float tau = qBound(0.f, smoothing, 0.95f) * LOOK_SMOOTH_TAU_MAX;
    const float alpha = (tau <= 0.f) ? 1.f : (1.f - std::exp(-dt / tau));
    smoothedLookX_ = smoothedLookX_ * (1.f - alpha) + rx * alpha;
    smoothedLookY_ = smoothedLookY_ * (1.f - alpha) + ry * alpha;

    // 位移积分：480px/秒 × 灵敏度 × dt
    const float dx = smoothedLookX_ * sens * LOOK_SPEED_PX_PER_SEC * dt;
    const float dy = smoothedLookY_ * sens * LOOK_SPEED_PX_PER_SEC * dt;
    if (dx != 0.f || dy != 0.f)
        injector_->sendMouseMove(dx, dy);
}
