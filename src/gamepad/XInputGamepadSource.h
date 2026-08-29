#pragma once  // 头文件防重复包含保护（整个文件只编译一次）

// 【C++ 语法】#include 预处理指令：引入其他头文件的声明。双引号 "..." 表示从相对路径查找（本文件位于 src/gamepad/，../core/ 表示向上进入 src/core/）；尖括号 <...> 表示在编译器的系统/库 include 路径中查找。
#include "../core/InputTypes.h"  // 引入引擎统一的输入类型定义（ControllerButton / ControllerStick 枚举等）

// 【C++ 语法】下面依次引入 Qt 与 C++ 标准库头文件：
#include <QHash>  // Qt 哈希表容器（键值对存储，按键快速查找）
#include <QObject>  // Qt 基类，提供信号槽（signals/slots）与元对象系统
#include <atomic>  // C++ 标准库原子类型，用于跨线程安全读写（免锁）
#include <thread>  // C++ 标准库线程类，用于创建独立轮询线程

// =====================================================================
// XInputGamepadSource —— XInput 手柄读取源
//
// 使用 Windows 原生接口 XInput（不用 QtGamepad），等效安卓版
// ControllerDevice + ControllerInputMapper。
//
// 工作原理：
//   - 内部独立线程以固定周期（默认 8ms = 125Hz）轮询 XInputGetState
//   - 使用独立线程而非 QTimer，确保应用在后台/非焦点时仍能正常轮询
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
// 【C++ 语法】class 类定义：类名后冒号开始继承列表，"public QObject" 表示公有继承 Qt 基类 QObject（获得信号槽、父子对象内存管理等能力）。
class XInputGamepadSource : public QObject {  // 定义手柄输入源类，继承自 QObject
    // 【C++ 语法】Q_OBJECT 宏：Qt 元对象编译器（MOC）识别此标记后自动生成元对象代码，使 signals/slots、tr() 等特性生效；定义了自定义信号的类必须包含。
    Q_OBJECT  // 启用 Qt 元对象系统（信号/槽）
public:  // public 访问区：以下成员对外可见
    // 【C++ 语法】explicit：禁止隐式类型转换调用构造函数；QObject* parent 是 Qt 父子对象指针，默认参数 nullptr 表示无父对象（省略 parent 也可正常调用）。
    explicit XInputGamepadSource(QObject* parent = nullptr);  // 构造函数声明，parent 用于 Qt 父子对象内存管理
    // 【C++ 语法】析构函数 ~类名()：对象销毁时自动调用；override 关键字声明覆写基类虚函数（此处覆写 QObject 的析构）。
    ~XInputGamepadSource() override;  // 析构函数声明，负责停止轮询线程

    // 启动轮询（重置连接失败计数）
    void start();  // 声明：启动轮询线程
    // 停止轮询
    void stop();  // 声明：停止轮询线程并释放按键
    // 当前是否已连接
    // 【C++ 语法】内联成员函数：const 修饰符声明该函数不会修改成员变量；函数体直接写在类内（编译器可内联展开）。
    bool isConnected() const { return connected_; }  // 返回当前连接状态

    // XInput 槽位（本机通常为 0；多手柄时可指定 0-3）
    void setPlayerIndex(int index) { playerIndex_ = index; }  // 设置 XInput 玩家槽位 0~3
    int playerIndex() const { return playerIndex_; }  // 读取当前玩家槽位

    // 轮询周期（毫秒），默认 8ms（125Hz）
    void setPollInterval(int ms);  // 声明：动态调整轮询间隔（毫秒）

signals:  // Qt 信号区开始（signals 为 Qt 关键字；信号可被 emit 发射、可被 connect 连接到槽）
    // 连接状态变化（connected=true 已连接）
    void connectedChanged(bool connected);  // 信号声明：连接状态变化
    // 按钮按下/松开
    void buttonChanged(ControllerButton button, bool isPressed);  // 信号声明：按钮按下/松开事件
    // 摇杆输入（x,y 归一化到 [-1,1]，未应用死区）
    void stickChanged(ControllerStick stick, float x, float y);  // 信号声明：摇杆轴输入（x/y 已归一化）

private:  // private 访问区：以下成员仅类内部可访问，封装实现细节
    // 轮询线程主循环
    void pollLoop();  // 声明：轮询线程主循环（线程入口）
    // 单次轮询
    void poll();  // 声明：单次轮询手柄状态

    int playerIndex_ = 0;  // XInput 玩家槽位，默认 0（本机第一个手柄）
    bool connected_ = false;  // 是否已连接标志，默认未连接
    // 【C++ 语法】std::atomic<bool>：原子布尔类型，跨线程读写不会产生数据竞争；{false} 为列表初始化（初值为 false）。
    std::atomic<bool> running_{false};  // 轮询线程运行标志（线程安全，免锁）
    // 【C++ 语法】std::thread：C++ 标准线程对象；joinable() 判断是否关联了可等待的线程，join() 阻塞等待其结束。
    std::thread pollThread_;  // 轮询线程对象
    int pollIntervalMs_ = 8;  // 轮询间隔（毫秒），默认 8ms ≈ 125Hz
    // 上一次按钮状态，用于检测变化并发出事件
    // 【Qt 语法】QHash<键, 值>：Qt 哈希表容器，按键快速取值；键为按钮枚举，值为按下/松开布尔量。
    QHash<ControllerButton, bool> prevButtonStates_;  // 记录上一次按钮按下状态（用于检测变化）
    // 连接失败计数，避免短暂错误导致状态闪烁
    int connectionFailCount_ = 0;  // 连续读取失败计数，默认 0
    // 【C++ 语法】static constexpr：类内静态编译期常量，编译期即可求值；无需创建对象即可访问。
    static constexpr int MAX_CONNECTION_FAILS = 3;  // 连接防抖阈值：连续失败 3 次才判定断开
};  // 类定义结束（注意分号）
