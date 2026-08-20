#pragma once

#include "../core/InputTypes.h"

#include <QHash>
#include <QObject>
#include <QTimer>

// =====================================================================
// XInput 手柄读取源（Windows 原生接口，替代 QtGamepad）
// 等效安卓版 ControllerDevice + ControllerInputMapper。
// 通过 XInputGetState 在 125Hz 轮询手柄状态，转换为统一的
// ControllerButton / ControllerStick 事件发出。
// =====================================================================
class XInputGamepadSource : public QObject {
    Q_OBJECT
public:
    explicit XInputGamepadSource(QObject* parent = nullptr);

    void start();
    void stop();
    bool isConnected() const { return connected_; }

    // XInput 槽位（本机通常为 0）
    void setPlayerIndex(int index) { playerIndex_ = index; }
    int playerIndex() const { return playerIndex_; }

    // 轮询周期（毫秒），默认 8ms（125Hz）
    void setPollInterval(int ms);

signals:
    void connectedChanged(bool connected);
    void buttonChanged(ControllerButton button, bool isPressed);
    void stickChanged(ControllerStick stick, float x, float y);

private:
    void poll();

    int playerIndex_ = 0;
    bool connected_ = false;
    QTimer timer_;
    // 上一次按钮状态，用于检测变化并发出事件
    QHash<ControllerButton, bool> prevButtonStates_;
};
