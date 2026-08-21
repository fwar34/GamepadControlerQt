#pragma once

#include "../core/InputTypes.h"

#include <QHash>
#include <QObject>
#include <QTimer>

// =====================================================================
// XInputGamepadSource —— XInput 手柄读取源
//
// 使用 Windows 原生接口 XInput（不用 QtGamepad），等效安卓版
// ControllerDevice + ControllerInputMapper。
//
// 工作原理：
//   - 内部 QTimer 以固定周期（默认 8ms = 125Hz）轮询 XInputGetState
//   - 将 XInput 按钮位掩码 / 扳机 / 摇杆转换为统一的
//     ControllerButton / ControllerStick 事件发出
//   - 扳机（LT/RT）阈值 >=128 视为按下；摇杆 short 归一化到 [-1,1]
//
// 连接防抖：
//   - 连续 MAX_CONNECTION_FAILS 次轮询失败才判定断开
//     （避免 USB 短暂通信错误导致界面闪烁）
//   - start() 时重置失败计数
//   - 判定断开时释放所有已按下的按钮
// =====================================================================
class XInputGamepadSource : public QObject {
    Q_OBJECT
public:
    explicit XInputGamepadSource(QObject* parent = nullptr);

    // 启动轮询（重置连接失败计数）
    void start();
    // 停止轮询
    void stop();
    // 当前是否已连接
    bool isConnected() const { return connected_; }

    // XInput 槽位（本机通常为 0；多手柄时可指定 0-3）
    void setPlayerIndex(int index) { playerIndex_ = index; }
    int playerIndex() const { return playerIndex_; }

    // 轮询周期（毫秒），默认 8ms（125Hz）
    void setPollInterval(int ms);

signals:
    // 连接状态变化（connected=true 已连接）
    void connectedChanged(bool connected);
    // 按钮按下/松开
    void buttonChanged(ControllerButton button, bool isPressed);
    // 摇杆输入（x,y 归一化到 [-1,1]，未应用死区）
    void stickChanged(ControllerStick stick, float x, float y);

private:
    // 单次轮询（QTimer 槽）
    void poll();

    int playerIndex_ = 0;
    bool connected_ = false;
    QTimer timer_;
    // 上一次按钮状态，用于检测变化并发出事件
    QHash<ControllerButton, bool> prevButtonStates_;
    // 连接失败计数，避免短暂错误导致状态闪烁
    int connectionFailCount_ = 0;
    static constexpr int MAX_CONNECTION_FAILS = 3;
};
