// 【C++ 语法】#pragma once：非标准但被主流编译器（MSVC / GCC / Clang）广泛支持的预处理指令，
// 作用：确保本头文件在同一个编译单元中只被包含一次，避免重复定义错误
#pragma once

// 【C++ 语法】#include 预处理指令：在编译前把被包含文件的全部内容原样插入到当前位置
#include "InputInjector.h"  // 【Qt】双引号：优先在本文件所在目录查找；引入注入器接口 InputInjector 的声明
#include "SteamInput.h"     // 【Qt】引入 SteamInput 映射引擎及 ControllerButton/ControllerStick/KeyMapping 等类型定义

// 【C++ 语法】尖括号形式：在编译器配置的标准库 / Qt 包含目录中查找头文件
#include <QHash>    // 【Qt】QHash<Key,Value>：Qt 提供的哈希表（键值对）容器
#include <QMutex>   // 【Qt 线程】QMutex：Qt 互斥锁，用于串行化多线程对共享状态的访问
#include <QObject>  // 【Qt】QObject：Qt 对象模型的基类，提供信号/槽、元对象系统、对象树、事件等能力
#include <QSet>     // 【Qt】QSet<T>：Qt 集合容器（元素唯一、无序）
#include <atomic>   // 【C++ 语法】C++11 标准库：std::atomic 原子类型，跨线程读写无数据竞争
#include <thread>   // 【C++ 语法】C++11 标准库：std::thread 线程类，用于创建独立线程

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

// 【Qt】定义一个继承自 QObject 的类，从而获得信号/槽、元对象系统等 Qt 对象能力
class KeyboardMouseMapper : public QObject {  // 【C++ 语法】class 类定义：public 表示公开继承 QObject（基类公开接口对子类保持公开）
    Q_OBJECT  // 【Qt】Q_OBJECT 宏（须出现在类体首部）：启用 Qt 元对象系统（MOC 处理），使信号/槽、tr()、qobject_cast 可用
public:  // 【C++ 语法】访问说明符：其后的成员为公开，外部代码可访问
    // 【C++ 语法】构造函数声明：在 .cpp 中定义；三个形参（两个指针 + 一个带默认实参的父对象指针）
    KeyboardMouseMapper(SteamInput* input, InputInjector* injector, QObject* parent = nullptr);  // 【C++ 语法】=nullptr 为默认实参：调用时可省略该参数
    ~KeyboardMouseMapper() override;  // 【C++ 语法】析构函数：对象销毁时自动调用；override 表示重写基类的虚析构（用于停止线程、释放注入）

    // 开始映射：连接信号、同步全局设置、启动 look 线程
    void start();  // 【C++ 语法】成员函数声明（定义在 .cpp）：void 无返回值
    // 停止映射：停 look 线程、断开信号、释放全部注入状态
    void stop();  // 【C++ 语法】成员函数声明
    // 【C++ 语法】内联成员函数：函数体直接写在类内；const 限定表示不修改成员变量；直接返回原子运行标志当前值
    bool isRunning() const { return running_.load(); }  // 【C++ 语法】const 成员函数 + std::atomic::load() 原子读

    // 释放全部注入状态（物理按键/鼠标键 + 所有保持状态，含 MouseToggle 锁存）。
    // 供 stop() 和手柄断开（connectedChanged(false)）时调用，
    // 避免 toggle 保持的鼠标键在断开后卡死。
    void releaseAllInputs();  // 【C++ 语法】成员函数声明：整体释放注入状态

signals:  // 【Qt】signals 宏（Qt 关键字）：信号区开始；信号是只声明不实现的成员函数，由 MOC 生成，外部用 emit 触发
    // MouseToggle 锁存状态变化（供 UI 提示）：
    // button=触发手柄键，mb=被锁存的鼠标键，active=true 刚锁存按住 / false 已解除。
    // 可能从手柄线程或主线程发出（AutoConnection 自动转队列到 UI 线程）。
    void mouseToggleChanged(ControllerButton button, MouseButton mb, bool active);  // 【Qt】信号声明：信号会自动把参数传给连接的槽函数

private slots:  // 【Qt】private slots 宏（Qt 关键字）：声明私有槽区；槽是能响应信号的普通成员函数
    // 按钮命中映射：按下执行动作并记录注入状态；松开精确释放
    void onButtonMapped(ControllerButton button, bool isPressed, const KeyMapping& mapping);  // 【C++ 语法】const KeyMapping&：常量引用形参，只读访问避免拷贝
    // 摇杆输入：左摇杆 WASD 移动，右摇杆仅记录最新值供 look 线程读取
    void onStickMapped(ControllerStick stick, float x, float y);  // 【C++ 语法】基本类型形参按值传递（拷贝副本）
    // 配置被替换：同步全局设置到 look 线程的原子量
    void onProfileChanged();  // 【C++ 语法】无参槽函数声明

private:  // 【C++ 语法】访问说明符：以下成员为私有，仅类内成员函数可访问
    // 按「已注入状态」释放某按钮的全部注入（子命令逆序 -> 主键 -> 鼠标）
    // 注意：不处理 MouseToggle 锁存（toggle 由用户主动锁存，松开不改变状态）
    void releaseButtonInjection(ControllerButton button);  // 【C++ 语法】私有成员函数声明
    // 键盘映射：先主键后子命令依次按下
    void handleKeyboardKey(ControllerButton button, int mainKeyCode, const QVector<int>& subs);  // 【C++ 语法】const 引用传递容器形参，避免拷贝开销
    // 鼠标单击：按下/松开跟随手柄
    void handleMouseClick(ControllerButton button, MouseButton mb);  // 【C++ 语法】私有成员函数声明
    // 鼠标长按锁存：按住时按下并记录，松开不改变状态
    void handleMouseToggle(ControllerButton button, MouseButton mb);  // 【C++ 语法】私有成员函数声明
    // 摇杆处理（WASD 移动 / 记录右摇杆）
    void handleStick(ControllerStick stick, float x, float y);  // 【C++ 语法】私有成员函数声明
    // look 线程单次节拍：平滑 -> 位移 -> 注入鼠标移动
    void processLookTick(float dt);  // 【C++ 语法】私有成员函数声明（dt=本次节拍间隔秒数）
    // look 线程主循环（125Hz）
    void lookLoop();  // 【C++ 语法】私有成员函数声明：将作为 std::thread 线程的入口

    // ---- 视角控制常量 ----
    // 【C++ 语法】static constexpr：编译期常量成员 —— static 属类不属对象，constexpr 编译时即可求值
    static constexpr float LOOK_SPEED_PX_PER_SEC = 480.0f;   // 满幅摇杆每秒像素位移
    static constexpr float LOOK_SMOOTH_TAU_MAX = 0.048f;     // 最大时间常数（smoothing=1 时）
    static constexpr long LOOK_TICK_MS = 8;                  // 节拍周期（125Hz）

    // 【C++ 语法】原始指针成员：保存外部传入的对象地址；本类不拥有这些对象（不负责 delete）
    SteamInput* input_;      // 【C++ 语法】指针成员：指向 SteamInput 输入源（提供 profile 数据与信号）
    InputInjector* injector_;  // 【C++ 语法】指针成员：指向注入器（负责真正的键鼠注入）

    // 注入状态互斥锁：保护下方状态容器。
    // onButtonMapped/onStickMapped 在「手柄轮询线程」执行，
    // releaseAllInputs 在「GUI 线程」（onCheckForeground / stop / 手柄断开）执行。
    // 无锁时两者并发修改状态会导致 down/up 不对称 —— 例：
    //   注入 leftdown 后 releaseAllInputs 清空状态，松开时 up 被吞 → 鼠标键永久卡死。
    // 【Qt 线程】QMutex 互斥锁成员：配合 QMutexLocker 使用（构造加锁 / 析构自动解锁）
    QMutex stateMutex_;  // 【Qt 线程】状态互斥锁：串行化手柄线程与主线程对注入状态容器的访问

    // ---- 当前注入状态（按下时记录，松开时按状态精确释放） ----
    // 【Qt】QHash<Key,Value>：键值对哈希表，按键（手柄按钮）查询对应的注入状态值
    QHash<ControllerButton, int> pressedMainKeys_;            // 按钮 -> 主键 keyCode
    QHash<ControllerButton, QVector<int>> pressedSubKeys_;    // 按钮 -> 已按下的子命令
    QHash<ControllerButton, MouseButton> pressedMouseButtons_;  // 按钮 -> 鼠标键
    QSet<int> leftStickPressedKeys_;                          // WASD 当前按下的 keyCode
    QHash<ControllerButton, MouseButton> toggledMouseButtons_; // 长按保持（MouseToggle）的鼠标键

    // ---- 右摇杆状态（look 线程读取，主线程写入） ----
    // 【C++ 语法】std::atomic<float>：原子浮点变量；{0.f} 为列表初始化默认值；跨线程读写安全
    std::atomic<float> latestLookX_{0.f};        // 最新摇杆 x（已死区/归一化）
    std::atomic<float> latestLookY_{0.f};        // 最新摇杆 y
    std::atomic<float> lookSensitivity_{0.5f};   // 灵敏度
    std::atomic<float> lookSmoothing_{0.5f};     // 平滑系数
    std::atomic<float> lookAcceleration_{1.5f};  // 加速度曲线指数

    // ---- 平滑状态（仅 look 线程使用） ----
    // 【C++ 语法】普通 float 成员：仅由 look 线程读写（无并发），故不需要原子量
    float smoothedLookX_ = 0.f;  // 平滑后的摇杆 X（EMA 低通滤波输出）
    float smoothedLookY_ = 0.f;  // 平滑后的摇杆 Y

    // 【C++ 语法】std::atomic<bool> 原子布尔：线程间安全读写；true 时 look 线程持续运行
    std::atomic<bool> running_{false};           // look 线程运行标志
    // 【C++ 线程】std::thread 成员：持有 look 线程句柄；start() 创建、stop() join() 等待结束
    std::thread lookThread_;                     // 视角控制线程
};  // 【C++ 语法】类定义结束：右花括号 } 闭合类体，分号 ; 是类声明必需的结尾（漏写会编译错误）
