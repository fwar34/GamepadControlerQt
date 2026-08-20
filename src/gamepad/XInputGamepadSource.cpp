#include "XInputGamepadSource.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xinput.h>

#include <QtGlobal>

namespace {

// XInput 按钮位 -> 统一按钮枚举
struct XInputButtonDef {
    WORD bit;
    ControllerButton button;
};

const XInputButtonDef kButtonDefs[] = {
    { XINPUT_GAMEPAD_DPAD_UP,        ControllerButton::DPAD_UP },
    { XINPUT_GAMEPAD_DPAD_DOWN,      ControllerButton::DPAD_DOWN },
    { XINPUT_GAMEPAD_DPAD_LEFT,      ControllerButton::DPAD_LEFT },
    { XINPUT_GAMEPAD_DPAD_RIGHT,     ControllerButton::DPAD_RIGHT },
    { XINPUT_GAMEPAD_START,          ControllerButton::MENU },
    { XINPUT_GAMEPAD_BACK,           ControllerButton::OPTIONS },
    { XINPUT_GAMEPAD_LEFT_THUMB,     ControllerButton::LEFT_STICK_CLICK },
    { XINPUT_GAMEPAD_RIGHT_THUMB,    ControllerButton::RIGHT_STICK_CLICK },
    { XINPUT_GAMEPAD_LEFT_SHOULDER,  ControllerButton::LEFT_SHOULDER },
    { XINPUT_GAMEPAD_RIGHT_SHOULDER, ControllerButton::RIGHT_SHOULDER },
    { XINPUT_GAMEPAD_A,              ControllerButton::A },
    { XINPUT_GAMEPAD_B,              ControllerButton::B },
    { XINPUT_GAMEPAD_X,              ControllerButton::X },
    { XINPUT_GAMEPAD_Y,              ControllerButton::Y },
};

// 摇杆原始值(short) -> -1.0~1.0
float axisToFloat(SHORT value) {
    const float v = static_cast<float>(value);
    const float max = 32767.0f;
    if (v > max) return 1.0f;
    if (v < -max) return -1.0f;
    return v / max;
}

}  // namespace

XInputGamepadSource::XInputGamepadSource(QObject* parent) : QObject(parent) {
    timer_.setInterval(8);
    connect(&timer_, &QTimer::timeout, this, &XInputGamepadSource::poll);
}

void XInputGamepadSource::setPollInterval(int ms) {
    timer_.setInterval(qMax(1, ms));
}

void XInputGamepadSource::start() {
    if (!timer_.isActive()) {
        connectionFailCount_ = 0;
        timer_.start();
        poll();  // 立即轮询一次，快速反馈连接状态
    }
}

void XInputGamepadSource::stop() {
    if (timer_.isActive())
        timer_.stop();

    // 释放所有已按下的按钮，避免键鼠卡死
    for (auto it = prevButtonStates_.begin(); it != prevButtonStates_.end(); ++it) {
        if (it.value()) {
            it.value() = false;
            emit buttonChanged(it.key(), false);
        }
    }
    if (connected_) {
        connected_ = false;
        emit connectedChanged(false);
    }
}

void XInputGamepadSource::poll() {
    XINPUT_STATE state{};
    const DWORD result = XInputGetState(static_cast<DWORD>(playerIndex_), &state);

    if (result == ERROR_SUCCESS) {
        connectionFailCount_ = 0;
        if (!connected_) {
            connected_ = true;
            emit connectedChanged(true);
        }
        const XINPUT_GAMEPAD& pad = state.Gamepad;

        // 数字按键
        for (const XInputButtonDef& def : kButtonDefs) {
            const bool pressed = (pad.wButtons & def.bit) != 0;
            const bool prev = prevButtonStates_.value(def.button, false);
            if (pressed != prev) {
                prevButtonStates_[def.button] = pressed;
                emit buttonChanged(def.button, pressed);
            }
        }

        // 模拟扳机（半程以上视为按下）
        auto updateTrigger = [this](ControllerButton b, bool pressed) {
            const bool prev = prevButtonStates_.value(b, false);
            if (pressed != prev) {
                prevButtonStates_[b] = pressed;
                emit buttonChanged(b, pressed);
            }
        };
        updateTrigger(ControllerButton::LEFT_TRIGGER_CLICK, pad.bLeftTrigger >= 128);
        updateTrigger(ControllerButton::RIGHT_TRIGGER_CLICK, pad.bRightTrigger >= 128);

        // 摇杆（归一化 -1.0~1.0，死区由 SteamInput 处理）
        emit stickChanged(ControllerStick::LEFT_STICK,
                          axisToFloat(pad.sThumbLX), axisToFloat(pad.sThumbLY));
        emit stickChanged(ControllerStick::RIGHT_STICK,
                          axisToFloat(pad.sThumbRX), axisToFloat(pad.sThumbRY));
    } else {
        connectionFailCount_++;
        // 只有当失败次数达到阈值时，才认为手柄真正断开
        if (connectionFailCount_ >= MAX_CONNECTION_FAILS && connected_) {
            connected_ = false;
            // 释放所有已按下的按键，避免heldButtons_堆积
            for (auto it = prevButtonStates_.begin(); it != prevButtonStates_.end(); ++it) {
                if (it.value()) {
                    it.value() = false;
                    emit buttonChanged(it.key(), false);
                }
            }
            prevButtonStates_.clear();
            emit connectedChanged(false);
        }
    }
}
