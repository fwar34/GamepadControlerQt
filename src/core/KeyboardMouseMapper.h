#pragma once

#include "InputInjector.h"
#include "SteamInput.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <atomic>
#include <thread>

// =====================================================================
// KeyboardMouseMapper —— 键鼠映射器（等效安卓版 KeyboardMouseMapper）
//
// 职责：
//   - 监听 SteamInput 的 buttonMapped/stickMapped 信号并执行键鼠注入
//   - 按钮映射：键盘（含子命令组合键）、鼠标单击、鼠标长按锁存（MouseToggle）
//   - 松开时按「已注入状态」精确释放（不依赖当前层映射，避免切层导致卡死）
//   - 左摇杆 -> WASD 8 方向移动（阈值 0.5）
//   - 右摇杆 -> 固定 125Hz（LOOK_TICK_MS=8ms）平滑视角移动循环，
//     处理流程：幅值钳制 -> 加速度曲线 -> 时间常数 EMA 平滑 -> 位移积分
//
// 线程模型：
//   - 手柄轮询线程：onButtonMapped/onStickMapped 经 DirectConnection 直接执行，
//     键鼠注入不经过主线程事件队列（避免主线程被模态循环占用时注入卡死）
//   - 主线程：releaseAllInputs（stop / 前台切换 / 手柄断开）、配置同步
//   - look 线程：独立 std::thread，以固定节拍读取右摇杆原子量并注入鼠标移动
// 手柄线程与主线程通过 stateMutex_ 串行化对注入状态容器的访问。
// =====================================================================
class KeyboardMouseMapper : public QObject {
    Q_OBJECT
public:
    KeyboardMouseMapper(SteamInput* input, InputInjector* injector, QObject* parent = nullptr);
    ~KeyboardMouseMapper() override;

    // 开始映射：连接信号、同步全局设置、启动 look 线程
    void start();
    // 停止映射：停 look 线程、断开信号、释放全部注入状态
    void stop();
    bool isRunning() const { return running_.load(); }

    // 释放全部注入状态（物理按键/鼠标键 + 所有保持状态，含 MouseToggle 锁存）。
    // 供 stop() 和手柄断开（connectedChanged(false)）时调用，
    // 避免 toggle 保持的鼠标键在断开后卡死。
    void releaseAllInputs();

signals:
    // MouseToggle 锁存状态变化（供 UI 提示）：
    // button=触发手柄键，mb=被锁存的鼠标键，active=true 刚锁存按住 / false 已解除。
    // 可能从手柄线程或主线程发出（AutoConnection 自动转队列到 UI 线程）。
    void mouseToggleChanged(ControllerButton button, MouseButton mb, bool active);

private slots:
    // 按钮命中映射：按下执行动作并记录注入状态；松开精确释放
    void onButtonMapped(ControllerButton button, bool isPressed, const KeyMapping& mapping);
    // 摇杆输入：左摇杆 WASD 移动，右摇杆仅记录最新值供 look 线程读取
    void onStickMapped(ControllerStick stick, float x, float y);
    // 配置被替换：同步全局设置到 look 线程的原子量
    void onProfileChanged();

private:
    // 按「已注入状态」释放某按钮的全部注入（子命令逆序 -> 主键 -> 鼠标）
    // 注意：不处理 MouseToggle 锁存（toggle 由用户主动锁存，松开不改变状态）
    void releaseButtonInjection(ControllerButton button);
    // 键盘映射：先主键后子命令依次按下
    void handleKeyboardKey(ControllerButton button, int mainKeyCode, const QVector<int>& subs);
    // 鼠标单击：按下/松开跟随手柄
    void handleMouseClick(ControllerButton button, MouseButton mb);
    // 鼠标长按锁存：按住时按下并记录，松开不改变状态
    void handleMouseToggle(ControllerButton button, MouseButton mb);
    // 摇杆处理（WASD 移动 / 记录右摇杆）
    void handleStick(ControllerStick stick, float x, float y);
    // look 线程单次节拍：平滑 -> 位移 -> 注入鼠标移动
    void processLookTick(float dt);
    // look 线程主循环（125Hz）
    void lookLoop();

    // ---- 视角控制常量 ----
    static constexpr float LOOK_SPEED_PX_PER_SEC = 480.0f;   // 满幅摇杆每秒像素位移
    static constexpr float LOOK_SMOOTH_TAU_MAX = 0.048f;     // 最大时间常数（smoothing=1 时）
    static constexpr long LOOK_TICK_MS = 8;                  // 节拍周期（125Hz）

    SteamInput* input_;
    InputInjector* injector_;

    // 注入状态互斥锁：保护下方状态容器。
    // onButtonMapped/onStickMapped 在「手柄轮询线程」执行，
    // releaseAllInputs 在「GUI 线程」（onCheckForeground / stop / 手柄断开）执行。
    // 无锁时两者并发修改状态会导致 down/up 不对称 —— 例：
    //   注入 leftdown 后 releaseAllInputs 清空状态，松开时 up 被吞 → 鼠标键永久卡死。
    QMutex stateMutex_;

    // ---- 当前注入状态（按下时记录，松开时按状态精确释放） ----
    QHash<ControllerButton, int> pressedMainKeys_;            // 按钮 -> 主键 keyCode
    QHash<ControllerButton, QVector<int>> pressedSubKeys_;    // 按钮 -> 已按下的子命令
    QHash<ControllerButton, MouseButton> pressedMouseButtons_;  // 按钮 -> 鼠标键
    QSet<int> leftStickPressedKeys_;                          // WASD 当前按下的 keyCode
    QHash<ControllerButton, MouseButton> toggledMouseButtons_; // 长按保持（MouseToggle）的鼠标键

    // ---- 右摇杆状态（look 线程读取，主线程写入） ----
    std::atomic<float> latestLookX_{0.f};        // 最新摇杆 x（已死区/归一化）
    std::atomic<float> latestLookY_{0.f};        // 最新摇杆 y
    std::atomic<float> lookSensitivity_{0.5f};   // 灵敏度
    std::atomic<float> lookSmoothing_{0.5f};     // 平滑系数
    std::atomic<float> lookAcceleration_{1.5f};  // 加速度曲线指数

    // ---- 平滑状态（仅 look 线程使用） ----
    float smoothedLookX_ = 0.f;
    float smoothedLookY_ = 0.f;

    std::atomic<bool> running_{false};           // look 线程运行标志
    std::thread lookThread_;                     // 视角控制线程
};
