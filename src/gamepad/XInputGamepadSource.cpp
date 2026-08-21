// ============================================================
// XInputGamepadSource.cpp
// 手柄输入源（Windows XInput 实现）
// ------------------------------------------------------------
// 职责：通过 Windows 自带的 XInput 接口轮询 XBox 手柄状态，
//       将底层原始数据转换为统一的输入信号发射出去：
//         - buttonChanged(按钮, 按下?)   -> 数字按键事件
//         - stickChanged(摇杆, x, y)     -> 摇杆轴事件
//         - connectedChanged(是否连接)   -> 连接状态变化
//
// 线程模型：本类不创建线程，而是借助 QTimer（默认 8ms ≈ 125Hz）
//           在 Qt 主线程事件循环中定时调用 poll() 完成轮询。
//
// 关键设计：
//   1. 连接防抖：XInput 偶尔会短暂返回错误（如 USB 通信抖动），
//      若每次都立刻判定"断开"会造成状态闪烁。这里采用
//      connectionFailCount_ 连续失败计数，只有达到 MAX_CONNECTION_FAILS
//      次才真正判定断开。
//   2. 释放兜底：断开（或 stop()）时，把所有仍处于"按下"状态的按钮
//      强制发一遍松开事件，避免按键/鼠标键卡死。
// ============================================================

#include "XInputGamepadSource.h"

// 定义 WIN32_LEAN_AND_MEAN / NOMINMAX 以加速 Windows 头文件编译
// 并避免 windows.h 与 Qt 的 min/max 宏冲突
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

// ------------------------------------------------------------
// XInput 位掩码 -> 统一按钮枚举 的映射描述
// ------------------------------------------------------------
// XInput 用 XINPUT_GAMEPAD::wButtons 的位标志表示按键状态，
// 这里把"手柄物理按键"映射到引擎内部统一的 ControllerButton，
// 上层（SteamInput / UI / 配置）只认识 ControllerButton。
struct XInputButtonDef {
    WORD bit;              // XInput 按钮位（如 XINPUT_GAMEPAD_A）
    ControllerButton button;  // 对应的统一按钮枚举
};

// 手柄按钮映射表：列出所有可用的数字按键
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

// ------------------------------------------------------------
// axisToFloat：摇杆原始值(SHORT) -> 归一化浮点 -1.0 ~ 1.0
// ------------------------------------------------------------
// XInput 摇杆返回有符号 16 位整数，满量程为 ±32767。
// 除以 32767 即可映射到 [-1, 1]（超过满量程的值钳制到边界）。
// 注意：死区（死区）不在这里处理，统一由 SteamInput::handleStickInput
// 做缩放式死区，避免各层重复处理。
float axisToFloat(SHORT value) {
    const float v = static_cast<float>(value);
    const float max = 32767.0f;
    if (v > max) return 1.0f;    // 防止个别手柄数值越界
    if (v < -max) return -1.0f;
    return v / max;
}

}  // namespace

// ============================================================
// 构造：创建 8ms 定时器并连接轮询槽
// ============================================================
// 默认轮询频率 8ms ≈ 125Hz，与键盘/鼠标注入线程频率一致，
// 保证摇杆数据延迟可控。
XInputGamepadSource::XInputGamepadSource(QObject* parent) : QObject(parent) {
    timer_.setInterval(8);
    connect(&timer_, &QTimer::timeout, this, &XInputGamepadSource::poll);
}

// ============================================================
// setPollInterval：动态调整轮询间隔（毫秒）
// ============================================================
// 至少 1ms，避免除零或异常高频轮询导致 CPU 占用过高。
void XInputGamepadSource::setPollInterval(int ms) {
    timer_.setInterval(qMax(1, ms));
}

// ============================================================
// start：启动轮询
// ============================================================
// 幂等操作：已在运行时不做任何事。
// 启动前先把连接失败计数清零，保证上一次断开留下的计数
// 不会让本次连接被误判为立即断开。
void XInputGamepadSource::start() {
    if (!timer_.isActive()) {
        connectionFailCount_ = 0;   // 重置防抖计数
        timer_.start();
        poll();  // 立即轮询一次，快速反馈连接状态（无需等首个定时器周期）
    }
}

// ============================================================
// stop：停止轮询并清理
// ============================================================
// 停止后：
//   1. 把所有仍处于按下状态的按钮补发一次松开事件，
//      避免残留的键鼠注入导致按键卡死；
//   2. 若仍显示已连接，则置为未连接并广播 connectedChanged(false)。
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

// ============================================================
// poll：单次轮询手柄状态（由 QTimer 定时触发）
// ============================================================
// 通过 XInputGetState 读取指定玩家索引的手柄快照：
//   - 成功（ERROR_SUCCESS）：
//       清零失败计数；若此前未连接则广播已连接；
//       逐项对比数字按键、扳机、摇杆，只对"状态发生变化"的
//       项发信号，避免无谓的信号风暴。
//   - 失败：
//       失败计数 +1；仅当连续失败达到 MAX_CONNECTION_FAILS
//       才判定断开，并在断开时释放所有按下按键。
void XInputGamepadSource::poll() {
    XINPUT_STATE state{};
    const DWORD result = XInputGetState(static_cast<DWORD>(playerIndex_), &state);

    if (result == ERROR_SUCCESS) {
        // ---- 连接成功 ----
        connectionFailCount_ = 0;   // 只要有成功就读，就视为在线
        if (!connected_) {
            connected_ = true;
            emit connectedChanged(true);
        }
        const XINPUT_GAMEPAD& pad = state.Gamepad;

        // 数字按键：遍历映射表，逐位检查按下状态并对比上次
        for (const XInputButtonDef& def : kButtonDefs) {
            const bool pressed = (pad.wButtons & def.bit) != 0;
            const bool prev = prevButtonStates_.value(def.button, false);
            if (pressed != prev) {
                prevButtonStates_[def.button] = pressed;
                emit buttonChanged(def.button, pressed);
            }
        }

        // 模拟扳机（LT/RT 是 0~255 的模拟量）：
        // 半程以上（>=128）视为"按下"，与 XInput 官方建议阈值一致。
        auto updateTrigger = [this](ControllerButton b, bool pressed) {
            const bool prev = prevButtonStates_.value(b, false);
            if (pressed != prev) {
                prevButtonStates_[b] = pressed;
                emit buttonChanged(b, pressed);
            }
        };
        updateTrigger(ControllerButton::LEFT_TRIGGER_CLICK, pad.bLeftTrigger >= 128);
        updateTrigger(ControllerButton::RIGHT_TRIGGER_CLICK, pad.bRightTrigger >= 128);

        // 摇杆：归一化 -1.0~1.0 后发射（死区由 SteamInput 统一处理）
        emit stickChanged(ControllerStick::LEFT_STICK,
                          axisToFloat(pad.sThumbLX), axisToFloat(pad.sThumbLY));
        emit stickChanged(ControllerStick::RIGHT_STICK,
                          axisToFloat(pad.sThumbRX), axisToFloat(pad.sThumbRY));
    } else {
        // ---- 读取失败（可能短暂抖动，也可能真正断开）----
        connectionFailCount_++;
        // 只有当失败次数达到阈值时，才认为手柄真正断开
        if (connectionFailCount_ >= MAX_CONNECTION_FAILS && connected_) {
            connected_ = false;
            // 释放所有已按下的按键，避免 heldButtons_ 堆积 / 键鼠卡死
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
