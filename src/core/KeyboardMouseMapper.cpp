// 【C++ 语法】#include 预处理指令：在编译前把被包含文件内容原样插入此处；双引号表示优先在本文件目录查找
#include "KeyboardMouseMapper.h"

// 【C++ 语法】<chrono>：C++11 标准库头文件，提供高精度时间与时长运算（steady_clock、duration、duration_cast 等）
#include <chrono>
// 【C++ 语法】<cmath>：C++ 标准库头文件，提供数学函数 std::sqrt（平方根）、std::pow（幂）、std::exp（指数）
#include <cmath>
// 【C++ 语法】<thread>：C++11 标准库头文件，提供 std::thread 线程类与 std::this_thread::sleep_for 线程睡眠
#include <thread>
// 【C++ 语法】<windows.h>：Windows 系统 API 头文件（Win32 数据类型与函数声明）
#include <windows.h>
// 【C++ 语法】<mmsystem.h>：Windows 多媒体 API 头文件，提供 timeBeginPeriod/timeEndPeriod（调整计时器分辨率）
#include <mmsystem.h>

// =====================================================================
// KeyboardMouseMapper —— 键鼠映射器实现
//
// 执行 SteamInput 广播的按钮/摇杆事件，转换为 Windows 键鼠注入。
// 包括：按钮映射（键盘+子命令/鼠标单击/MouseToggle 锁存）、
//       左摇杆 WASD 移动、右摇杆 125Hz 平滑视角控制线程。
// =====================================================================

// 【C++ 语法】成员函数在类外定义：格式为「返回类型 类名::函数名(形参)」，:: 是作用域解析运算符
// 【C++ 语法】构造函数：创建对象时自动调用；冒号后为成员初始化列表，在进入函数体之前完成基类/成员初始化
KeyboardMouseMapper::KeyboardMouseMapper(SteamInput* input, InputInjector* injector, QObject* parent)
    : QObject(parent), input_(input), injector_(injector) {}  // 【C++ 语法】初始化列表：QObject(parent) 初始化基类；input_/injector_ 保存传入指针；{} 为空函数体

// 【C++ 语法】析构函数定义（~类名）：对象销毁时自动调用，此处用于安全停止线程并释放注入状态
KeyboardMouseMapper::~KeyboardMouseMapper() {  // 【C++ 语法】析构函数体开始
    stop();  // 【C++ 语法】调用成员函数 stop()：停止 look 线程并释放全部注入状态
}  // 【C++ 语法】析构函数体结束

// 开始映射：连接 SteamInput 信号、同步全局设置、启动 look 线程
void KeyboardMouseMapper::start() {  // 【C++ 语法】成员函数定义：void 表示无返回值
    if (running_.load()) return;  // 【C++ 语法】if 单语句：原子运行标志已为 true（正在运行）则直接返回，避免重复启动
    // 必须用 DirectConnection：buttonMapped/stickMapped 在「手柄轮询线程」发出，
    // 若用默认 AutoConnection（接收者在主线程）会变成 QueuedConnection，
    // 导致所有键鼠注入都跑到主线程执行。一旦注入的鼠标按下落在程序自身标题栏上，
    // Windows 会进入非客户区模态追踪循环阻塞主线程，松开事件排不进主线程队列，
    // 注入的 mouse up 永远发不出去 → 标题栏按钮点击不生效、手柄输入整体无响应。
    // DirectConnection 让注入在独立的手柄线程执行，主线程被模态循环占用时
    // 仍能发送 mouse up 让模态循环退出。线程安全由 stateMutex_ + 注入器内部互斥保证。
    // 【C++ 语法】const 常量变量：值在初始化后不可修改
    // 【Qt 线程】连接类型：Qt::DirectConnection = 槽在发送者线程立即同步执行；Qt::UniqueConnection = 防止同一连接被重复添加
    const Qt::ConnectionType directUnique =  // 【C++ 语法】声明并初始化常量，类型为 Qt::ConnectionType（枚举类型）
        static_cast<Qt::ConnectionType>(Qt::DirectConnection | Qt::UniqueConnection);  // 【C++ 语法】| 按位或合并两个选项；static_cast 显式类型转换
    // 【Qt】connect 函数：把「发送者对象 + 信号地址」连接到「接收者对象 + 槽地址」；& 取成员函数地址（编译期检查参数匹配）
    connect(input_, &SteamInput::buttonMapped,  // 【C++ 语法】前两个参数：信号发送者指针 input_ + 信号成员函数地址
            this, &KeyboardMouseMapper::onButtonMapped, directUnique);  // 【C++ 语法】后三个参数：接收者 this + 槽地址 + 连接方式；分号结束语句
    connect(input_, &SteamInput::stickMapped,  // 【Qt】连接摇杆信号 stickMapped 到槽 onStickMapped
            this, &KeyboardMouseMapper::onStickMapped, directUnique);  // 【C++ 语法】参数同上：摇杆事件进入本类处理
    connect(input_, &SteamInput::profileChanged,  // 【Qt】连接配置变更信号 profileChanged 到槽 onProfileChanged
            this, &KeyboardMouseMapper::onProfileChanged, directUnique);  // 【C++ 语法】参数同上
    onProfileChanged();  // 【C++ 语法】手动直接调用槽函数：立即同步一次全局设置到原子量（无需等待信号）
    running_.store(true);  // 【C++ 语法】std::atomic::store：原子地写入 true，通知 look 线程可以运行
    lookThread_ = std::thread(&KeyboardMouseMapper::lookLoop, this);  // 【C++ 线程】构造 std::thread：在新线程中执行 lookLoop 成员函数，this 作为调用对象指针
}  // 【C++ 语法】start() 函数体结束

// 停止映射：停 look 线程、断开信号、释放全部注入状态（含 MouseToggle 锁存）
void KeyboardMouseMapper::stop() {  // 【C++ 语法】成员函数定义：停止映射的入口
    if (!running_.load()) return;  // 【C++ 语法】! 逻辑非：运行标志为 false（未运行）则直接返回
    running_.store(false);  // 【C++ 语法】原子写入 false：通知 look 线程退出 while 循环
    if (lookThread_.joinable())  // 【C++ 线程】joinable()：判断线程对象是否关联着一个可等待的线程（默认构造/detach 后为 false）
        lookThread_.join();  // 【C++ 线程】join()：阻塞等待 look 线程运行结束（不 join 则 std::thread 析构时崩溃）
    disconnect(input_, nullptr, this, nullptr);  // 【Qt】disconnect：断开 input_ 与 this 之间全部信号连接（nullptr 表示不限信号/槽）
    releaseAllInputs();  // 【C++ 语法】调用成员函数：释放所有已注入的按键/鼠标状态
}  // 【C++ 语法】stop() 函数体结束

// 释放全部注入状态（物理按键/鼠标键 + 所有保持状态，含 MouseToggle 锁存）。
// 供 stop() 和手柄断开（main.cpp 连接 connectedChanged(false)）时调用，
// 避免 toggle 保持的鼠标键在断开后卡死。
void KeyboardMouseMapper::releaseAllInputs() {  // 【C++ 语法】成员函数定义：整体释放注入状态
    // 与手柄线程的 onButtonMapped/onStickMapped 互斥，
    // 保证「已注入的 down」一定会被这里（或后续松开事件）配对补发 up。
    // 【Qt 线程】QMutexLocker：RAII 互斥锁守卫 —— 构造时对 stateMutex_ 加锁，离开函数/作用域时自动解锁
    QMutexLocker locker(&stateMutex_);  // 【C++ 语法】栈对象 locker 持锁；&stateMutex_ 取互斥锁地址传入
    // 释放所有物理注入（含 MouseToggle 保持按下的鼠标键）
    injector_->releaseAll();  // 【C++ 语法】-> 指针解引用后调用成员函数；releaseAll() 释放全部物理键鼠
    // 先复制再清空，逐个通知 UI 解除锁存提示（避免遍历容器时发送信号）
    const QHash<ControllerButton, MouseButton> toggled = toggledMouseButtons_;  // 【C++ 语法】const + 拷贝构造：先复制锁存映射，避免遍历时容器被修改
    toggledMouseButtons_.clear();  // 【Qt】QHash::clear()：清空锁存映射容器（已复制到 toggled）
    for (auto it = toggled.cbegin(); it != toggled.cend(); ++it)  // 【C++ 语法】for 循环 + 迭代器：auto 自动推导类型；cbegin/cend 常量迭代器；++it 前进
        emit mouseToggleChanged(it.key(), it.value(), false);  // 【Qt】emit 发出信号通知 UI 锁存解除；it.key()/it.value() 取当前元素的键与值
    pressedMainKeys_.clear();  // 【Qt】QHash::clear：清空主键注入状态记录
    pressedSubKeys_.clear();  // 【Qt】QHash::clear：清空子命令注入状态记录
    pressedMouseButtons_.clear();  // 【Qt】QHash::clear：清空鼠标键注入状态记录
    leftStickPressedKeys_.clear();  // 【Qt】QSet::clear：清空左摇杆 WASD 按下键记录
    toggledMouseButtons_.clear();  // 【Qt】QHash::clear：再次清空锁存记录（上方已清，此处为防御性保证）
    latestLookX_.store(0.f);  // 【C++ 语法】原子写入 0：复位最新右摇杆 X
    latestLookY_.store(0.f);  // 【C++ 语法】原子写入 0：复位最新右摇杆 Y
    smoothedLookX_ = 0.f;  // 【C++ 语法】普通成员赋值：复位平滑值（此函数在 GUI 线程，look 线程已停，无并发）
    smoothedLookY_ = 0.f;  // 【C++ 语法】普通成员赋值：复位平滑值
}  // 【C++ 语法】releaseAllInputs() 函数体结束

// 配置被替换：将全局设置同步到 look 线程的原子量（smoothing 等）
void KeyboardMouseMapper::onProfileChanged() {  // 【C++ 语法】槽函数定义：响应 profileChanged 信号
    const GlobalSettings& s = input_->profile.globalSettings;  // 【C++ 语法】const 引用：s 是全局设置的只读别名（不拷贝），经 input_->profile 访问
    lookSensitivity_.store(s.lookSensitivity);  // 【C++ 语法】原子写入：同步灵敏度设置给 look 线程
    lookSmoothing_.store(s.lookSmoothing);  // 【C++ 语法】原子写入：同步平滑系数
    lookAcceleration_.store(s.lookAcceleration);  // 【C++ 语法】原子写入：同步加速度曲线指数
}  // 【C++ 语法】onProfileChanged() 函数体结束

// ---------------------------------------------------------------
// 按钮映射执行
// ---------------------------------------------------------------

// 按钮命中映射入口。
// 松开：按「已注入状态」精确释放（与当前层映射无关，
//       防止长按触发键切换层后松开时释放错对象导致按键卡死）。
// 按下：根据动作类型分发到具体处理器。
void KeyboardMouseMapper::onButtonMapped(ControllerButton button, bool isPressed, const KeyMapping& mapping) {  // 【C++ 语法】槽函数定义：按钮按下/松开事件入口；const KeyMapping& 为常量引用形参
    // 与 GUI 线程的 releaseAllInputs 互斥，防止并发修改注入状态导致 down/up 不对称
    // 【Qt 线程】QMutexLocker：函数入口即加锁，整个处理期间独占 stateMutex_
    QMutexLocker locker(&stateMutex_);  // 【C++ 语法】RAII 锁对象：离开函数时自动解锁
    if (!isPressed) {  // 【C++ 语法】if 分支：松开事件（isPressed 为 false）
        releaseButtonInjection(button);  // 【C++ 语法】按已注入状态精确释放该按钮的全部注入
        return;  // 【C++ 语法】return：提前结束函数，跳过下方按下处理
    }  // 【C++ 语法】if（松开事件）分支结束

    switch (mapping.action.type) {  // 【C++ 语法】switch 分支语句：按动作类型的枚举值分发处理
        case MappedAction::Type::KeyboardKey:  // 【C++ 语法】case 标签：动作类型为「键盘按键」
            handleKeyboardKey(button, mapping.action.keyCode, mapping.subCommands);  // 【C++ 语法】调用键盘映射处理（主键 + 子命令组合键）
            break;  // 【C++ 语法】break：跳出整个 switch（否则会落入下一个 case）
        case MappedAction::Type::MouseClick:  // 【C++ 语法】case 标签：动作类型为「鼠标单击」
            handleMouseClick(button, mapping.action.mouseButton);  // 【C++ 语法】调用鼠标单击处理
            break;  // 【C++ 语法】跳出 switch
        case MappedAction::Type::MouseToggle:  // 【C++ 语法】case 标签：动作类型为「鼠标长按锁存」
            handleMouseToggle(button, mapping.action.mouseButton);  // 【C++ 语法】调用锁存处理
            break;  // 【C++ 语法】跳出 switch
        // 【C++ 语法】case 标签：动作类型为「滚轮向上」；sendMouseWheel(+1) 表示向上滚动
        case MappedAction::Type::WheelUp:
            injector_->sendMouseWheel(1);   // 瞬时事件，无松开处理
            break;  // 【C++ 语法】跳出 switch
        case MappedAction::Type::WheelDown:  // 【C++ 语法】case 标签：动作类型为「滚轮向下」
            injector_->sendMouseWheel(-1);  // 【C++ 语法】参数 -1 表示向下滚动一格
            break;  // 【C++ 语法】跳出 switch
        case MappedAction::Type::SwitchLayer:  // 【C++ 语法】case 标签：切换图层动作
        case MappedAction::Type::ToggleMapping:  // 【C++ 语法】case 标签：切换映射总开关
        case MappedAction::Type::ToggleOnScreenKeyboard:  // 【C++ 语法】case 标签：切换屏幕键盘显示
        case MappedAction::Type::ToggleOverlay:  // 【C++ 语法】case 标签：切换悬浮窗显示
            break;  // 由 SteamInput 引擎处理  // 【C++ 语法】引擎级动作无需本类处理，直接跳过
        case MappedAction::Type::MouseMove:  // 【C++ 语法】case 标签：鼠标移动动作
        case MappedAction::Type::LookAround:  // 【C++ 语法】case 标签：视角环视动作
            break;  // 摇杆动作在 handleStick 中处理  // 【C++ 语法】摇杆类动作统一在 handleStick 处理，此处跳过
    }  // 【C++ 语法】switch 语句结束
}  // 【C++ 语法】onButtonMapped() 函数体结束

// 按「已注入状态」释放某按钮的全部注入：
//   子命令逆序 -> 主键 -> 鼠标键。
// 注意：不处理 MouseToggle 锁存（toggle 是用户主动锁存机制，
//       松开手柄键不应改变其状态，与安卓版语义一致）。
void KeyboardMouseMapper::releaseButtonInjection(ControllerButton button) {  // 【C++ 语法】私有成员函数定义
    // 释放子命令（逆序，与按下顺序相反）
    if (pressedSubKeys_.contains(button)) {  // 【Qt】QHash::contains：判断该按钮是否有子命令记录
        const QVector<int>& subs = pressedSubKeys_.value(button);  // 【C++ 语法】const 引用绑定 value() 返回的子命令向量，避免拷贝
        for (int i = subs.size() - 1; i >= 0; --i)  // 【C++ 语法】for 循环：从最后一个子命令倒序遍历到 0
            injector_->sendKeyUp(subs[i]);  // 【C++ 语法】逐个子命令发送松开（逆序与按下顺序相反）
        pressedSubKeys_.remove(button);  // 【Qt】QHash::remove：删除该按钮的子命令记录
    }  // 【C++ 语法】if（有子命令记录）分支结束
    // 释放主键
    if (pressedMainKeys_.contains(button)) {  // 【Qt】QHash::contains：判断该按钮是否有主键记录
        injector_->sendKeyUp(pressedMainKeys_.take(button));  // 【Qt】QHash::take：取出并删除记录，返回原主键值用于松开
    }  // 【C++ 语法】if（有主键记录）分支结束
    // 释放鼠标（不处理长按保持的）
    if (pressedMouseButtons_.contains(button)) {  // 【Qt】QHash::contains：判断该按钮是否有鼠标键记录
        injector_->sendMouseUp(pressedMouseButtons_.take(button));  // 【Qt】QHash::take：取出并删除记录，松开对应鼠标键
    }  // 【C++ 语法】if（有鼠标键记录）分支结束
}  // 【C++ 语法】releaseButtonInjection() 函数体结束

// 键盘映射：先按下主键，再依次按下各子命令（组合键，如 Alt+3）。
// 子命令会过滤掉与主键重复的项；已有主键按下时忽略重复触发。
void KeyboardMouseMapper::handleKeyboardKey(ControllerButton button, int mainKeyCode, const QVector<int>& subs) {  // 【C++ 语法】成员函数定义：键盘组合键处理
    // 【C++ 语法】if 单语句：主键已按下则直接返回（return 提前结束函数），避免重复注入
    if (pressedMainKeys_.contains(button)) return;  // 已按下，忽略重复

    const int n = qMin(subs.size(), KeyMapping::MAX_SUB_COMMANDS);  // 【Qt】qMin(a,b)：返回较小值，限制子命令数量不超过上限
    QVector<int> validSubs;  // 【Qt】QVector<int>：动态数组，保存过滤后的有效子命令键码
    validSubs.reserve(n);  // 【Qt】QVector::reserve(n)：预分配容量 n，避免后续 append 反复扩容
    for (int i = 0; i < n; ++i) {  // 【C++ 语法】for 循环：遍历前 n 个子命令
        // 【C++ 语法】if 单语句：子命令与主键相同时 continue 跳过（进入下一次循环）
        if (subs[i] == mainKeyCode) continue;  // 避免子命令与主键重复
        validSubs.append(subs[i]);  // 【Qt】QVector::append：把不重复的子命令加入数组
    }  // 【C++ 语法】for 循环结束

    injector_->sendKeyDown(mainKeyCode);  // 【C++ 语法】先注入按下主键
    for (const int sub : validSubs)  // 【C++ 语法】范围 for（C++11）：依次取出 validSubs 中每个子命令到 sub
        injector_->sendKeyDown(sub);  // 【C++ 语法】再注入按下各子命令，构成组合键
    pressedMainKeys_.insert(button, mainKeyCode);  // 【Qt】QHash::insert：记录该按钮的主键注入状态
    pressedSubKeys_.insert(button, validSubs);  // 【Qt】QHash::insert：记录该按钮的子命令注入状态
}  // 【C++ 语法】handleKeyboardKey() 函数体结束

// 鼠标单击：按下/松开跟随手柄（松开时由 releaseButtonInjection 释放）
void KeyboardMouseMapper::handleMouseClick(ControllerButton button, MouseButton mb) {  // 【C++ 语法】成员函数定义
    if (pressedMouseButtons_.contains(button)) return;  // 【C++ 语法】已按下则直接返回，忽略重复触发
    injector_->sendMouseDown(mb);  // 【C++ 语法】注入鼠标按下
    pressedMouseButtons_.insert(button, mb);  // 【Qt】QHash::insert：记录该按钮的鼠标注入状态
}  // 【C++ 语法】handleMouseClick() 函数体结束

// 鼠标长按锁存（MouseToggle）：每次按下切换保持状态。
//  - 首次按下：注入按下并记录（之后松开手柄键不释放）；
//  - 再次按下：注入松开并清除记录。
// 松开手柄键时 releaseButtonInjection 不处理该记录（保持锁存状态）。
void KeyboardMouseMapper::handleMouseToggle(ControllerButton button, MouseButton mb) {  // 【C++ 语法】成员函数定义：锁存切换逻辑
    if (toggledMouseButtons_.contains(button)) {  // 【Qt】QHash::contains：判断该按钮当前是否处于锁存状态
        injector_->sendMouseUp(mb);  // 【C++ 语法】已锁存则再次按下时注入鼠标松开
        toggledMouseButtons_.remove(button);  // 【Qt】QHash::remove：清除该按钮的锁存记录
        emit mouseToggleChanged(button, mb, false);  // 【Qt】emit 发出信号：通知 UI 锁存已解除
    } else {  // 【C++ 语法】else 分支：当前未锁存
        injector_->sendMouseDown(mb);  // 【C++ 语法】首次按下则注入鼠标按下
        toggledMouseButtons_.insert(button, mb);  // 【Qt】QHash::insert：记录该按钮处于锁存状态
        emit mouseToggleChanged(button, mb, true);  // 【Qt】emit 发出信号：通知 UI 已锁存
    }  // 【C++ 语法】if/else 分支结束
}  // 【C++ 语法】handleMouseToggle() 函数体结束

// ---------------------------------------------------------------
// 摇杆处理
// ---------------------------------------------------------------

void KeyboardMouseMapper::onStickMapped(ControllerStick stick, float x, float y) {  // 【C++ 语法】槽函数定义：摇杆信号入口；形参为摇杆标识与 x/y 分量
    handleStick(stick, x, y);  // 【C++ 语法】直接转发给统一的摇杆处理函数 handleStick
}  // 【C++ 语法】onStickMapped() 函数体结束

// 摇杆处理：
//   - 右摇杆：仅记录最新值到原子量（look 线程按固定节拍读取并平滑发送）
//   - 左摇杆：WASD 8 方向移动（阈值 0.5），
//     与上一次按键状态做差集，只按下新增、释放消失的键
void KeyboardMouseMapper::handleStick(ControllerStick stick, float x, float y) {  // 【C++ 语法】成员函数定义：处理左右摇杆
    if (stick == ControllerStick::RIGHT_STICK) {  // 【C++ 语法】if 分支：判断是否为右摇杆
        latestLookX_.store(x);  // 【C++ 语法】原子写入：记录最新右摇杆 X 供 look 线程读取
        latestLookY_.store(y);  // 【C++ 语法】原子写入：记录最新右摇杆 Y
        return;  // 【C++ 语法】右摇杆只记录最新值，此处直接返回
    }  // 【C++ 语法】if（右摇杆）分支结束

    // 左摇杆 WASD 移动：修改 leftStickPressedKeys_，与 releaseAllInputs 互斥
    // 【Qt 线程】QMutexLocker：对注入状态加锁，防止与主线程 releaseAllInputs 并发修改
    QMutexLocker locker(&stateMutex_);  // 【C++ 语法】RAII 锁对象：离开函数自动解锁

    // 左摇杆 -> WASD 8 方向（阈值 0.5）
    // 注意：XInput 的 Y 轴向上为正（向上推 => y>0），判定要跟物理方向一致。
    constexpr float THRESHOLD = 0.5f;  // 【C++ 语法】constexpr：编译期常量，阈值 0.5
    const bool up = y > THRESHOLD;     // 摇杆向上（y 为正）-> W  // 【C++ 语法】比较结果（bool）赋给常量
    const bool down = y < -THRESHOLD;  // 摇杆向下（y 为负）-> S  // 【C++ 语法】比较结果赋给常量
    const bool left = x < -THRESHOLD;  // 【C++ 语法】摇杆向左（x 为负）判定
    const bool right = x > THRESHOLD;  // 【C++ 语法】摇杆向右（x 为正）判定

    QSet<int> target;  // 【Qt】QSet<int>：本次应处于按下状态的目标键集合
    if (up) target.insert(AndroidKey::W);  // 【C++ 语法】if 单语句：向上则把 W 键加入目标集合
    if (down) target.insert(AndroidKey::S);  // 【C++ 语法】向下则加入 S 键
    if (left) target.insert(AndroidKey::A);  // 【C++ 语法】向左则加入 A 键
    if (right) target.insert(AndroidKey::D);  // 【C++ 语法】向右则加入 D 键

    // 计算需要释放的键（原按下但当前未按）。
    // 注意：先收集到 toRelease 再统一处理，
    // 避免遍历 QSet 的同时修改容器导致未定义行为。
    QVector<int> toRelease;  // 【Qt】QVector<int>：收集需要释放的按键码
    for (const int kc : leftStickPressedKeys_) {  // 【C++ 语法】范围 for：遍历当前已按下的 WASD 键集合
        if (!target.contains(kc))  // 【Qt】QSet::contains：判断目标集合中是否包含该键
            toRelease.append(kc);  // 【Qt】QVector::append：目标里没有则加入待释放列表
    }  // 【C++ 语法】for（收集待释放键）循环结束
    for (const int kc : toRelease) {  // 【C++ 语法】范围 for：逐个处理待释放键
        injector_->sendKeyUp(kc);  // 【C++ 语法】注入松开该键
        leftStickPressedKeys_.remove(kc);  // 【Qt】QSet::remove：从已按下集合中移除该键
    }  // 【C++ 语法】for（释放消失键）循环结束
    // 计算需要按下的键（新按下的）
    for (const int kc : target) {  // 【C++ 语法】范围 for：遍历目标键集合
        if (!leftStickPressedKeys_.contains(kc)) {  // 【Qt】QSet::contains：若该键尚未处于按下状态
            injector_->sendKeyDown(kc);  // 【C++ 语法】注入按下该键
            leftStickPressedKeys_.insert(kc);  // 【Qt】QSet::insert：将该键加入已按下集合
        }  // 【C++ 语法】if 结束
    }  // 【C++ 语法】for 结束
}  // 【C++ 语法】handleStick() 函数体结束

// ---------------------------------------------------------------
// 视角循环（125Hz）
// ---------------------------------------------------------------

// look 线程主循环：固定 8ms 节拍调用 processLookTick。
// 使用 timeBeginPeriod(1) 提高系统计时器分辨率，保证节拍准确；
// 实际处理耗时计入 dt（限制在 0.001~0.05s），保证位移积分的时间基准。
void KeyboardMouseMapper::lookLoop() {  // 【C++ 线程】线程入口函数：由 std::thread 在独立线程中执行
    timeBeginPeriod(1);  // 【C++ 语法】Win32 API：把系统计时器最小分辨率设为 1ms，提高 sleep 与计时精度
    auto lastTick = std::chrono::steady_clock::now();  // 【C++ 语法】auto 自动推导类型；steady_clock 单调时钟（不随系统时间跳变）；记录上一次节拍时刻
    while (running_.load()) {  // 【C++ 线程】循环条件：原子读运行标志，为 true 才继续（外部 stop 写 false 即退出）
        const auto tickStart = std::chrono::steady_clock::now();  // 【C++ 语法】const auto：记录本次节拍开始时刻
        const float dt = qBound(0.001f,  // 【Qt】qBound(min,val,max)：把时间差钳制在 [0.001, 0.05] 秒之间
                                static_cast<float>(std::chrono::duration<double>(tickStart - lastTick).count()),  // 【C++ 语法】把两时刻的时长差转成 double 秒，再 static_cast 成 float
                                0.05f);  // 【C++ 语法】上限 0.05s：防止异常长耗时导致位移突跳
        lastTick = tickStart;  // 【C++ 语法】更新「上一次节拍」时刻为本次开始时刻
        processLookTick(dt);  // 【C++ 语法】调用单次节拍处理（读取摇杆 -> 平滑 -> 位移 -> 注入）
        const long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(  // 【C++ 语法】duration_cast：时长类型转换；long 为有符号长整型
            std::chrono::steady_clock::now() - tickStart).count();  // 【C++ 语法】计算本次节拍实际耗时（毫秒数）
        const long sleepMs = LOOK_TICK_MS - elapsedMs;  // 【C++ 语法】目标 8ms 减去已耗时，得到还需睡眠的时间
        if (sleepMs > 0)  // 【C++ 语法】if 单语句：还有剩余时间才睡眠（防止负值）
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));  // 【C++ 线程】sleep_for：让当前线程睡眠指定毫秒，维持固定节拍
    }  // 【C++ 线程】while 循环结束（running_ 变 false 后退出）
    timeEndPeriod(1);  // 【C++ 语法】Win32 API：恢复系统计时器默认分辨率（与 timeBeginPeriod 配对）
}  // 【C++ 语法】lookLoop() 线程入口函数体结束

// look 线程单次节拍：右摇杆 -> 平滑 -> 位移 -> 注入鼠标移动。
// 处理流水线：
//   1. 幅值钳制：mag 超过 1 时归一化
//   2. 加速度曲线：pow(mag, accel)，推得越深位移越大
//   3. 时间常数 EMA 平滑：alpha = 1-exp(-dt/tau)，tau = smoothing*0.048s
//      （smoothing=0 时 tau=0，直接采用当前值，无平滑）
//   4. 位移积分：dx = 平滑值 × 灵敏度 × 480px/s × dt
void KeyboardMouseMapper::processLookTick(float dt) {  // 【C++ 语法】成员函数定义：look 线程每次节拍调用
    float rx = latestLookX_.load();  // 【C++ 语法】原子读 load()：读取最新右摇杆 X
    float ry = latestLookY_.load();  // 【C++ 语法】原子读：读取最新右摇杆 Y
    const float sens = lookSensitivity_.load();  // 【C++ 语法】原子读：读取灵敏度设置
    const float smoothing = lookSmoothing_.load();  // 【C++ 语法】原子读：读取平滑系数
    const float accel = qBound(0.5f, lookAcceleration_.load(), 3.0f);  // 【Qt】qBound：把加速度指数钳制到 [0.5, 3.0]

    // 幅值钳制：摇杆输入理论上 <=1，但小数误差可能略超，归一化处理
    float mag = std::sqrt(rx * rx + ry * ry);  // 【C++ 语法】std::sqrt 开平方：计算摇杆向量的长度（幅值）
    if (mag > 1.f) {  // 【C++ 语法】if 分支：幅值超过 1（理论上不会，防御浮点误差）
        rx /= mag;  // 【C++ 语法】/= 复合赋值：X 除以幅值，归一化方向
        ry /= mag;  // 【C++ 语法】/= 复合赋值：Y 除以幅值
        mag = 1.f;  // 【C++ 语法】幅值钳制为 1
    }  // 【C++ 语法】if（幅值超限）分支结束

    // 加速曲线：幅值 -> 更高幅值（推得越深，输出增长越快）
    if (rx != 0.f || ry != 0.f) {  // 【C++ 语法】|| 逻辑或：X、Y 任一非零才做加速处理
        const float curve = std::pow(mag, accel);  // 【C++ 语法】std::pow(mag, accel)：幅值的 accel 次幂，作为加速后的幅值
        const float scale = curve / mag;  // 【C++ 语法】缩放系数 = 加速后幅值 ÷ 原幅值
        rx *= scale;  // 【C++ 语法】*= 复合赋值：按比例放大 X
        ry *= scale;  // 【C++ 语法】*= 复合赋值：按比例放大 Y
    }  // 【C++ 语法】if（加速处理）分支结束

    // 时间常数 EMA 平滑（低通滤波，消除摇杆抖动）
    const float tau = qBound(0.f, smoothing, 0.95f) * LOOK_SMOOTH_TAU_MAX;  // 【C++ 语法】钳制平滑系数后乘以最大时间常数，得到时间常数 tau
    const float alpha = (tau <= 0.f) ? 1.f : (1.f - std::exp(-dt / tau));  // 【C++ 语法】三目运算符 ?: —— tau<=0 时 alpha=1（无平滑）；否则 1-exp(-dt/tau)
    smoothedLookX_ = smoothedLookX_ * (1.f - alpha) + rx * alpha;  // 【C++ 语法】EMA 递推：新平滑值 = 旧值*(1-alpha) + 输入*alpha
    smoothedLookY_ = smoothedLookY_ * (1.f - alpha) + ry * alpha;  // 【C++ 语法】EMA 递推（Y 轴分量）

    // 位移积分：480px/秒 × 灵敏度 × dt（亚像素由注入器余量累积补发）
    // Y 轴取反：XInput 右摇杆向上推 => ry>0，而鼠标向上移动需要 dy<0（屏幕 Y 向下为正）。
    const float dx = smoothedLookX_ * sens * LOOK_SPEED_PX_PER_SEC * dt;  // 【C++ 语法】X 方向位移 = 平滑值 × 灵敏度 × 速率 × 时间
    const float dy = -smoothedLookY_ * sens * LOOK_SPEED_PX_PER_SEC * dt;  // 【C++ 语法】Y 方向位移；负号使屏幕坐标方向正确
    if (dx != 0.f || dy != 0.f)  // 【C++ 语法】位移非零才注入，避免无意义的调用
        injector_->sendMouseMove(dx, dy);  // 【C++ 语法】注入相对鼠标移动（亚像素余量由注入器累积补发）
}  // 【C++ 语法】processLookTick() 函数体结束
