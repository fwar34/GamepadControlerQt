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
// 线程模型：内部创建独立轮询线程（默认 8ms ≈ 125Hz），
//           使用 Sleep 而非 QTimer，确保应用在后台时仍能正常轮询。
//
// 关键设计：
//   1. 连接防抖：XInput 偶尔会短暂返回错误（如 USB 通信抖动），
//      若每次都立刻判定"断开"会造成状态闪烁。这里采用
//      connectionFailCount_ 连续失败计数，只有达到 MAX_CONNECTION_FAILS
//      次才真正判定断开。
//   2. 释放兜底：断开（或 stop()）时，把所有仍处于"按下"状态的按钮
//      强制发一遍松开事件，避免按键/鼠标键卡死。
// ============================================================

// 【C++ 语法】#include 预处理指令：引入头文件，双引号表示优先在当前文件所在目录查找（本类声明位于同目录的 XInputGamepadSource.h）。
#include "XInputGamepadSource.h"  // 引入本类头文件（含类声明、信号定义）

// 定义 WIN32_LEAN_AND_MEAN / NOMINMAX 以加速 Windows 头文件编译
// 并避免 windows.h 与 Qt 的 min/max 宏冲突
// 【C++ 语法】条件编译指令：#ifndef 判断宏"是否未定义"，配合 #define/#endif 防止宏重复定义。
#ifndef WIN32_LEAN_AND_MEAN  // 若尚未定义 WIN32_LEAN_AND_MEAN 则进入该分支
#define WIN32_LEAN_AND_MEAN  // 定义该宏：简化 windows.h，剔除不常用的 API 以加速编译
#endif  // 结束条件编译分支
#ifndef NOMINMAX  // 若尚未定义 NOMINMAX 则进入该分支
#define NOMINMAX  // 定义该宏：禁用 windows.h 中的 min/max 宏，避免与 Qt/C++ 标准库的 min/max 冲突
#endif  // 结束条件编译分支
#include <windows.h>  // Windows 核心 API 头文件（提供 DWORD、Sleep、ERROR_SUCCESS 等）
// 【XInput】xinput.h：XInput 官方头文件，提供 XInputGetState 函数、XINPUT_STATE / XINPUT_GAMEPAD 结构体以及 XINPUT_GAMEPAD_* 等位掩码常量。
#include <xinput.h>  // 引入 XInput 手柄 API

// 【Qt 语法】QtGlobal：Qt 全局工具头文件，提供 qMax 等内联工具函数/宏。
#include <QtGlobal>  // 引入 Qt 全局工具（qMax 等）

// 【C++ 语法】匿名命名空间：namespace {} 内不写名字即匿名，其中的符号具有内部链接（仅在当前翻译单元 .cpp 内可见），避免与其它编译单元产生符号冲突。
namespace {  // 匿名命名空间开始

// ------------------------------------------------------------
// XInput 位掩码 -> 统一按钮枚举 的映射描述
// ------------------------------------------------------------
// XInput 用 XINPUT_GAMEPAD::wButtons 的位标志表示按键状态，
// 这里把"手柄物理按键"映射到引擎内部统一的 ControllerButton，
// 上层（SteamInput / UI / 配置）只认识 ControllerButton。
// 【C++ 语法】struct 结构体类型：成员默认 public，用于把多个相关字段打包；此处定义"XInput 位掩码 + 统一按钮枚举"的一对一映射。
struct XInputButtonDef {  // 定义映射结构体
    WORD bit;              // XInput 按钮位（如 XINPUT_GAMEPAD_A）
    ControllerButton button;  // 对应的统一按钮枚举
};  // 结构体定义结束

// 手柄按钮映射表：列出所有可用的数字按键
// 【C++ 语法】const 常量数组：XInputButtonDef 类型的数组，花括号 {} 内逐项进行聚合初始化；const 表示数组内容不可修改。
const XInputButtonDef kButtonDefs[] = {  // 定义按钮映射常量表
    { XINPUT_GAMEPAD_DPAD_UP,        ControllerButton::DPAD_UP },      // 方向键上 → DPAD_UP
    { XINPUT_GAMEPAD_DPAD_DOWN,      ControllerButton::DPAD_DOWN },    // 方向键下 → DPAD_DOWN
    { XINPUT_GAMEPAD_DPAD_LEFT,      ControllerButton::DPAD_LEFT },    // 方向键左 → DPAD_LEFT
    { XINPUT_GAMEPAD_DPAD_RIGHT,     ControllerButton::DPAD_RIGHT },   // 方向键右 → DPAD_RIGHT
    { XINPUT_GAMEPAD_START,          ControllerButton::MENU },         // START 键 → MENU
    { XINPUT_GAMEPAD_BACK,           ControllerButton::OPTIONS },      // BACK 键 → OPTIONS
    { XINPUT_GAMEPAD_LEFT_THUMB,     ControllerButton::LEFT_STICK_CLICK },  // 左摇杆按下 → LEFT_STICK_CLICK
    { XINPUT_GAMEPAD_RIGHT_THUMB,    ControllerButton::RIGHT_STICK_CLICK }, // 右摇杆按下 → RIGHT_STICK_CLICK
    { XINPUT_GAMEPAD_LEFT_SHOULDER,  ControllerButton::LEFT_SHOULDER },     // 左肩键 LB → LEFT_SHOULDER
    { XINPUT_GAMEPAD_RIGHT_SHOULDER, ControllerButton::RIGHT_SHOULDER },    // 右肩键 RB → RIGHT_SHOULDER
    { XINPUT_GAMEPAD_A,              ControllerButton::A },            // A 键 → A
    { XINPUT_GAMEPAD_B,              ControllerButton::B },            // B 键 → B
    { XINPUT_GAMEPAD_X,              ControllerButton::X },            // X 键 → X
    { XINPUT_GAMEPAD_Y,              ControllerButton::Y },            // Y 键 → Y
};  // 数组定义结束

// ------------------------------------------------------------
// axisToFloat：摇杆原始值(SHORT) -> 归一化浮点 -1.0 ~ 1.0
// ------------------------------------------------------------
// XInput 摇杆返回有符号 16 位整数，满量程为 ±32767。
// 除以 32767 即可映射到 [-1, 1]（超过满量程的值钳制到边界）。
// 注意：死区（死区）不在这里处理，统一由 SteamInput::handleStickInput
// 做缩放式死区，避免各层重复处理。
// 【C++ 语法】函数定义：SHORT 为 Windows 定义的有符号 16 位整型（short 的 typedef）；返回值类型为 float。
float axisToFloat(SHORT value) {  // 定义"摇杆原始值 → 归一化浮点"的函数
    // 【C++ 语法】static_cast<目标类型>(表达式)：C++ 显式类型转换；const 修饰局部变量表示其值不可修改。
    const float v = static_cast<float>(value);  // 把 SHORT 显式转换为 float 并存入常量 v
    const float max = 32767.0f;  // 满量程值：XInput 摇杆最大正值 32767（f 后缀表示 float 字面量）
    // 【C++ 语法】if 条件判断语句；条件成立时执行 return 提前返回并退出函数。
    if (v > max) return 1.0f;    // 防止个别手柄数值越界
    if (v < -max) return -1.0f;  // 负方向越界则钳制到 -1.0
    return v / max;  // 归一化：除以满量程，得到 [-1, 1] 区间
}  // 函数结束

}  // namespace

// ============================================================
// 构造
// ============================================================
// 【C++ 语法】构造函数定义：类名::函数名 使用作用域解析符 :: 指明定义的是哪个类的成员；冒号后 ": QObject(parent)" 为初始化列表，用于调用基类构造函数并传入 parent；{} 为空函数体。
XInputGamepadSource::XInputGamepadSource(QObject* parent) : QObject(parent) {}  // 构造：把 parent 传给 QObject 基类（函数体为空，无额外初始化）

// ============================================================
// 析构：确保轮询线程停止
// ============================================================
XInputGamepadSource::~XInputGamepadSource() {  // 析构函数定义
    stop();  // 停止轮询线程并释放按下的按键（防止卡键）
}  // 析构函数结束

// ============================================================
// setPollInterval：动态调整轮询间隔（毫秒）
// ============================================================
// 至少 1ms，避免除零或异常高频轮询导致 CPU 占用过高。
void XInputGamepadSource::setPollInterval(int ms) {  // 定义"设置轮询间隔"成员函数
    // 【Qt 语法】qMax(a, b)：Qt 提供的取最大值内联函数模板，返回两个参数中的较大者。
    pollIntervalMs_ = qMax(1, ms);  // 取 1 与 ms 的较大者，保证轮询间隔至少 1ms
}  // 函数结束

// ============================================================
// start：启动轮询线程
// ============================================================
// 幂等操作：已在运行时不做任何事。
// 启动前先把连接失败计数清零，保证上一次断开留下的计数
// 不会让本次连接被误判为立即断开。
void XInputGamepadSource::start() {  // 定义"启动轮询"成员函数
    // 【C++ 语法】std::atomic<bool>::load()：原子读取当前值（线程安全）；此处用于幂等判断，已在运行则直接返回。
    if (running_.load()) return;  // 若轮询线程已在运行则直接返回（幂等）
    connectionFailCount_ = 0;  // 清零连接失败计数，避免上次断开残留计数导致误判
    // 【C++ 语法】std::atomic<bool>::store(值)：原子写入新值（线程安全）。
    running_.store(true);  // 置运行标志为 true，线程循环随即开始
    // 【C++ 语法】std::thread(函数指针, 对象指针)：创建新线程；成员函数需以对象地址 this 作为第一个实参，使其在 this 的上下文里执行 pollLoop。
    pollThread_ = std::thread(&XInputGamepadSource::pollLoop, this);  // 创建轮询线程，线程入口为 pollLoop 成员函数
    poll();  // 立即轮询一次，快速反馈连接状态
}  // start 结束

// ============================================================
// stop：停止轮询线程并清理
// ============================================================
// 停止后：
//   1. 把所有仍处于按下状态的按钮补发一次松开事件，
//      避免残留的键鼠注入导致按键卡死；
//   2. 若仍显示已连接，则置为未连接并广播 connectedChanged(false)。
void XInputGamepadSource::stop() {  // 定义"停止轮询"成员函数
    running_.store(false);  // 置运行标志为 false，线程循环条件随即失效
    // 【C++ 语法】std::thread::joinable()：判断线程对象是否关联了可等待的执行线程（已启动且尚未 join/detach）。
    if (pollThread_.joinable())  // 若线程可等待则执行 join
        // 【C++ 语法】std::thread::join()：阻塞当前线程直到目标线程执行完毕并回收其资源；线程仍在运行时对象析构会触发 std::terminate，因此必须先 join。
        pollThread_.join();  // 等待轮询线程退出

    // 释放所有已按下的按钮，避免键鼠卡死
    // 【C++ 语法】for 循环 + 迭代器：auto 自动推导迭代器类型；begin()/end() 返回容器首/尾迭代器；++it 为前缀自增（向前移动迭代器）。
    for (auto it = prevButtonStates_.begin(); it != prevButtonStates_.end(); ++it) {  // 遍历按钮状态哈希表
        // 【Qt 语法】QHash 迭代器访问：it.value() 读取当前键对应的值，it.key() 读取当前迭代位置对应的键。
        if (it.value()) {  // 若该按钮上次是按下状态
            it.value() = false;  // 把状态记录改写为松开
            // 【Qt 语法】emit 关键字：用于发射信号；此处发射 buttonChanged(按钮, false) 表示该按钮松开。
            emit buttonChanged(it.key(), false);  // 发射"松开"事件，兜底防止按键卡死
        }  // 结束 if
    }  // 结束 for
    if (connected_) {  // 若仍显示已连接
        connected_ = false;  // 置为未连接
        emit connectedChanged(false);  // 广播连接断开信号
    }  // 结束 if
}  // stop 结束

// ============================================================
// pollLoop：轮询线程主循环
// ============================================================
// 独立线程中以固定间隔调用 poll()，不受 Qt 事件循环影响，
// 确保应用在后台/非焦点时仍能正常读取手柄输入。
void XInputGamepadSource::pollLoop() {  // 定义轮询线程主循环
    // 【C++ 语法】while 循环：循环条件为 running_ 的当前值；stop() 将其置 false 后循环自然退出。
    while (running_.load()) {  // 只要运行标志为 true 就持续轮询
        poll();  // 执行一次手柄状态读取
        // 【Windows 语法】Sleep(毫秒)：Windows 提供的线程睡眠函数；static_cast<DWORD> 把 int 显式转换为 Windows 无符号 32 位整型（Sleep 要求的毫秒参数类型）。
        Sleep(static_cast<DWORD>(pollIntervalMs_));  // 按轮询间隔休眠，形成固定周期（默认 8ms ≈ 125Hz）
    }  // 循环结束
}  // pollLoop 结束

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
void XInputGamepadSource::poll() {  // 定义单次轮询成员函数
    // 【XInput】XINPUT_STATE：XInput 状态结构体，包含 dwPacketNumber（数据包序号，用于判断数据是否更新）与 XINPUT_GAMEPAD Gamepad（手柄按键/摇杆数据）；{} 为值初始化（所有字段清零）。
    XINPUT_STATE state{};  // 声明并清零 XInput 状态结构体
    // 【XInput】XInputGetState(玩家索引, 指向 XINPUT_STATE 的指针)：读取指定槽位手柄的当前状态；返回 ERROR_SUCCESS(0) 表示成功，非 0 表示失败/未连接；&state 为取地址运算（传指针）。
    const DWORD result = XInputGetState(static_cast<DWORD>(playerIndex_), &state);  // 调用 XInput 读取手柄状态，返回码存入常量 result

    // 【XInput】ERROR_SUCCESS：Windows 定义的宏，值为 0，表示调用成功。
    if (result == ERROR_SUCCESS) {  // 读取成功则进入成功分支
        // ---- 连接成功 ----
        connectionFailCount_ = 0;   // 只要有成功就读，就视为在线
        if (!connected_) {  // 若此前未连接
            connected_ = true;  // 置为已连接
            emit connectedChanged(true);  // 发射"已连接"信号
        }  // 结束 if
        // 【C++ 语法】const 引用 &：声明只读引用（别名），不拷贝整个结构体，直接操作 state.Gamepad 本体，避免大对象拷贝开销。
        const XINPUT_GAMEPAD& pad = state.Gamepad;  // 取手柄数据子结构的只读引用

        // 数字按键：遍历映射表，逐位检查按下状态并对比上次
        // 【C++ 语法】基于范围的 for 循环（range-based for）：依次遍历容器 kButtonDefs，每次迭代 def 为当前元素的 const 引用。
        for (const XInputButtonDef& def : kButtonDefs) {  // 遍历按钮映射表
            // 【XInput/位运算】wButtons 为 16 位位掩码；表达式 (pad.wButtons & def.bit) != 0 用按位与 & 判断特定位是否被置 1（某按钮是否按下）。
            const bool pressed = (pad.wButtons & def.bit) != 0;  // 用位与判断该按钮位是否按下
            // 【Qt 语法】QHash::value(键, 默认值)：按键查值；键不存在时返回第二个参数（此处默认值 false，表示视为未按下）。
            const bool prev = prevButtonStates_.value(def.button, false);  // 读取上次该按钮状态（无记录则视为未按下）
            if (pressed != prev) {  // 仅当状态发生变化时才处理（避免无谓的信号风暴）
                // 【Qt 语法】QHash 的 operator[]（下标运算符）：写入或更新指定键对应的值。
                prevButtonStates_[def.button] = pressed;  // 更新状态记录
                emit buttonChanged(def.button, pressed);  // 发射按钮状态变化信号
            }  // 结束 if
        }  // 结束 for

        // 模拟扳机（LT/RT 是 0~255 的模拟量）：
        // 半程以上（>=128）视为"按下"，与 XInput 官方建议阈值一致。
        // 【C++ 语法】lambda 表达式（匿名函数）：auto 自动推导其类型；[this] 为捕获列表（捕获 this 指针以便访问成员并发射信号）；(ControllerButton b, bool pressed) 为参数列表；{ } 为函数体；末尾 ; 结束该语句。
        auto updateTrigger = [this](ControllerButton b, bool pressed) {  // 定义"更新扳机按下状态"的局部 lambda 函数
            const bool prev = prevButtonStates_.value(b, false);  // 读取该扳机键上次状态（无记录视为未按下）
            if (pressed != prev) {  // 状态变化才处理
                prevButtonStates_[b] = pressed;  // 更新状态记录
                emit buttonChanged(b, pressed);  // 发射扳机按键状态变化信号
            }  // 结束 if
        };  // lambda 定义结束
        // 【XInput】bLeftTrigger / bRightTrigger：左右扳机的模拟量，取值范围 0~255；>=128（半程以上）视为按下，与 XInput 官方建议阈值一致。
        updateTrigger(ControllerButton::LEFT_TRIGGER_CLICK, pad.bLeftTrigger >= 128);  // 左扳机 LT：超过半程则视为按下
        updateTrigger(ControllerButton::RIGHT_TRIGGER_CLICK, pad.bRightTrigger >= 128);  // 右扳机 RT：超过半程则视为按下

        // 摇杆：归一化 -1.0~1.0 后发射（死区由 SteamInput 统一处理）
        // 【XInput】sThumbLX / sThumbLY：左摇杆的 X/Y 轴原始值（有符号 16 位，满量程 ±32767）。
        emit stickChanged(ControllerStick::LEFT_STICK,  // 发射左摇杆信号（参数跨行书写，首行为摇杆类型）
                          axisToFloat(pad.sThumbLX), axisToFloat(pad.sThumbLY));  // 把左右轴原始值归一化后作为 x、y 传入
        // 【XInput】sThumbRX / sThumbRY：右摇杆的 X/Y 轴原始值。
        emit stickChanged(ControllerStick::RIGHT_STICK,  // 发射右摇杆信号（参数跨行书写，首行为摇杆类型）
                          axisToFloat(pad.sThumbRX), axisToFloat(pad.sThumbRY));  // 把右摇杆原始值归一化后传入
    } else {  // 读取失败则进入失败分支
        // ---- 读取失败（可能短暂抖动，也可能真正断开）----
        connectionFailCount_++;  // 连续失败计数 +1
        // 只有当失败次数达到阈值时，才认为手柄真正断开
        if (connectionFailCount_ >= MAX_CONNECTION_FAILS && connected_) {  // 达到阈值且此前已连接才判定断开（连接防抖）
            connected_ = false;  // 置为未连接
            // 释放所有已按下的按键，避免 heldButtons_ 堆积 / 键鼠卡死
            // 【C++ 语法】迭代器遍历容器（与 stop() 中相同的手法）：begin()/end() + 前缀自增。
            for (auto it = prevButtonStates_.begin(); it != prevButtonStates_.end(); ++it) {  // 遍历按钮状态表
                if (it.value()) {  // 若处于按下状态
                    it.value() = false;  // 把记录改写为松开
                    emit buttonChanged(it.key(), false);  // 发射松开事件，兜底防止按键卡死
                }  // 结束 if
            }  // 结束 for
            // 【Qt 语法】QHash::clear()：清空容器中的所有键值对。
            prevButtonStates_.clear();  // 清空全部按钮状态记录
            emit connectedChanged(false);  // 广播断开信号
        }  // 结束 if
    }  // 结束 else
}  // poll 结束
