#pragma once

#include "InputInjector.h"
#include "SteamInput.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <atomic>
#include <thread>

// =====================================================================
// 键鼠映射器（等效安卓版 KeyboardMouseMapper）
// 职责：
//   - 执行按钮映射（键盘+子命令、鼠标点击/长按）
//   - 松开时按"已注入状态"精确释放
//   - 左摇杆 -> WASD（8 方向）
//   - 右摇杆 -> 固定 125Hz 平滑视角移动循环
// =====================================================================
class KeyboardMouseMapper : public QObject {
    Q_OBJECT
public:
    KeyboardMouseMapper(SteamInput* input, InputInjector* injector, QObject* parent = nullptr);
    ~KeyboardMouseMapper() override;

    void start();
    void stop();
    bool isRunning() const { return running_.load(); }

    // 释放全部注入状态（物理按键/鼠标键 + 所有保持状态，含 MouseToggle 锁存）。
    // 供 stop() 和手柄断开时调用，避免 toggle 保持的鼠标键卡死。
    void releaseAllInputs();

private slots:
    void onButtonMapped(ControllerButton button, bool isPressed, const KeyMapping& mapping);
    void onStickMapped(ControllerStick stick, float x, float y);
    void onProfileChanged();

private:
    void releaseButtonInjection(ControllerButton button);
    void handleKeyboardKey(ControllerButton button, int mainKeyCode, const QVector<int>& subs);
    void handleMouseClick(ControllerButton button, MouseButton mb);
    void handleMouseToggle(ControllerButton button, MouseButton mb);
    void handleStick(ControllerStick stick, float x, float y);
    void processLookTick(float dt);
    void lookLoop();

    static constexpr float LOOK_SPEED_PX_PER_SEC = 480.0f;
    static constexpr float LOOK_SMOOTH_TAU_MAX = 0.048f;
    static constexpr long LOOK_TICK_MS = 8;

    SteamInput* input_;
    InputInjector* injector_;

    // 当前注入状态（按下时记录，松开时按状态精确释放）
    QHash<ControllerButton, int> pressedMainKeys_;            // 按钮 -> 主键 keyCode
    QHash<ControllerButton, QVector<int>> pressedSubKeys_;    // 按钮 -> 已按下的子命令
    QHash<ControllerButton, MouseButton> pressedMouseButtons_;  // 按钮 -> 鼠标键
    QSet<int> leftStickPressedKeys_;                          // WASD 当前按下的 keyCode
    QHash<ControllerButton, MouseButton> toggledMouseButtons_; // 长按保持的鼠标键

    // 右摇杆状态（look 线程读取，主线程写入）
    std::atomic<float> latestLookX_{0.f};
    std::atomic<float> latestLookY_{0.f};
    std::atomic<float> lookSensitivity_{0.5f};
    std::atomic<float> lookSmoothing_{0.5f};
    std::atomic<float> lookAcceleration_{1.5f};

    // 平滑状态（仅 look 线程使用）
    float smoothedLookX_ = 0.f;
    float smoothedLookY_ = 0.f;

    std::atomic<bool> running_{false};
    std::thread lookThread_;
};
