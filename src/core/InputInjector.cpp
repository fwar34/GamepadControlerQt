// 【C++ 语法】#include 预处理指令：用双引号 "" 包含本模块对应的头文件（声明了 InputInjector 接口与转换函数），编译前把其内容插入本文件。
#include "InputInjector.h"

// 【C++ 语法】#include <...>：尖括号表示在系统/Qt 标准包含目录中搜索头文件。QMutex 是 Qt 提供的互斥锁类，用于多线程同步。
#include <QMutex>
// 【C++ 语法】QMutexLocker 是 Qt 的 RAII（资源获取即初始化）锁工具类：构造时自动加锁，离开作用域（如函数返回）时自动解锁。
#include <QMutexLocker>
// 【C++ 语法】QSet<T> 是 Qt 的哈希集合容器：元素唯一、自动去重，插入/查找/删除平均复杂度为 O(1)。
#include <QSet>
// 【Windows API】windows.h：Windows 平台主头文件，提供 SendInput、INPUT 结构体、MOUSEEVENTF_*/KEYEVENTF_* 常量、DWORD/WORD 类型等。
#include <windows.h>

// =====================================================================
// Windows 本机注入器实现
//
// 使用 SendInput() 直接向系统注入键盘/鼠标事件
// （等效安卓版的 BridgeInputInjector + Windows 端 C 客户端逻辑，
//  去掉了 TCP 桥接，本机直接注入到当前前台窗口）。
//
// 线程安全：所有注入方法内部通过 QMutex 保护按键状态，
// 可安全地从 GUI 线程（按钮/左摇杆）与 look 线程（右摇杆）并发调用。
// =====================================================================

// ---------------------------------------------------------------------
// Android KeyCode -> Windows 虚拟键码（VK）
// 复用安卓版 BridgeInputInjector 的映射表。
// 支持：字母/数字/F1-F12/小键盘/修饰键/方向键/符号键/锁键等，
// 未知键返回 0（调用方会忽略）。
// ---------------------------------------------------------------------

// 【C++ 语法】函数定义：int 返回类型 + 参数列表 (int androidKeyCode) + 函数体 {}。此函数把 Android 键码映射为 Windows 虚拟键码 VK。
int androidKeyCodeToWindowsVK(int androidKeyCode) {
    // 【C++ 语法】if 条件语句：条件为真则执行紧接其后的下一条语句（无花括号时只包含一条语句）；&& 是逻辑与（两边都为真结果才为真）。
    if (androidKeyCode >= 29 && androidKeyCode <= 54)    // 安卓字母 A-Z 的键码区间：29~54
        return 0x41 + (androidKeyCode - 29);    // 【C++ 语法】return 返回函数结果并结束函数；0x41 是十六进制数（=65），即 VK_A，按偏移推算 A~Z 的 VK
    // 数字 0-9: 7..16 -> VK_0(0x30)..VK_9
    if (androidKeyCode >= 7 && androidKeyCode <= 16)    // 安卓数字键 0-9 的键码区间：7~16
        return 0x30 + (androidKeyCode - 7);    // 0x30（=48）是 VK_0，按偏移推算 0~9 的 VK
    // F1-F12: 131..142 -> VK_F1(0x70)..VK_F12
    if (androidKeyCode >= 131 && androidKeyCode <= 142)    // 安卓 F1-F12 的键码区间：131~142
        return 0x70 + (androidKeyCode - 131);    // 0x70（=112）是 VK_F1，按偏移推算 F1~F12 的 VK
    // 小键盘 0-9: 144..153 -> VK_NUMPAD0(0x60)..VK_NUMPAD9
    if (androidKeyCode >= 144 && androidKeyCode <= 153)    // 安卓小键盘 0-9 的键码区间：144~153
        return 0x60 + (androidKeyCode - 144);    // 0x60（=96）是 VK_NUMPAD0，按偏移推算小键盘 0~9 的 VK

    // 【C++ 语法】switch 语句：根据括号内整型表达式的值与各 case 后的常量依次比较，命中则执行该分支；配合 return 可直接结束整个函数。
    switch (androidKeyCode) {    // switch 分支块开始（{ 左花括号）
        // 修饰键（左右独立，VK_LSHIFT/RSHIFT/LCTRL/RCTRL/LMENU/RMENU）
        case AndroidKey::SHIFT_LEFT: return 0xA0;    // 左 Shift：VK_LSHIFT = 0xA0
        case AndroidKey::SHIFT_RIGHT: return 0xA1;    // 右 Shift：VK_RSHIFT = 0xA1
        case AndroidKey::CTRL_LEFT: return 0xA2;    // 左 Ctrl：VK_LCONTROL = 0xA2
        case AndroidKey::CTRL_RIGHT: return 0xA3;    // 右 Ctrl：VK_RCONTROL = 0xA3
        case AndroidKey::ALT_LEFT: return 0xA4;    // 左 Alt：VK_LMENU = 0xA4
        case AndroidKey::ALT_RIGHT: return 0xA5;    // 右 Alt：VK_RMENU = 0xA5
        // 控制/功能键
        case AndroidKey::SPACE: return 0x20;    // 空格键：VK_SPACE = 0x20
        case AndroidKey::ENTER: return 0x0D;    // 回车键：VK_RETURN = 0x0D
        case AndroidKey::TAB: return 0x09;    // Tab 键：VK_TAB = 0x09
        case AndroidKey::ESCAPE: return 0x1B;    // Esc 键：VK_ESCAPE = 0x1B
        case AndroidKey::BACK: return 0x08;    // 退格键：VK_BACK = 0x08
        case AndroidKey::DEL: return 0x2E;    // Delete 键：VK_DELETE = 0x2E
        case AndroidKey::INSERT: return 0x2D;    // Insert 键：VK_INSERT = 0x2D
        case AndroidKey::HOME: return 0x24;    // Home 键：VK_HOME = 0x24
        case AndroidKey::PAGE_UP: return 0x21;    // PageUp 键：VK_PRIOR = 0x21
        case AndroidKey::PAGE_DOWN: return 0x22;    // PageDown 键：VK_NEXT = 0x22
        case AndroidKey::MOVE_END: return 0x23;    // End 键：VK_END = 0x23
        // 方向键（注意：这里的 DPAD_* 是键盘方向键码，
        // 与手柄 DPad 无关；手柄方向键由 XInput 读取）
        case AndroidKey::DPAD_UP: return 0x26;    // 上方向键：VK_UP = 0x26
        case AndroidKey::DPAD_DOWN: return 0x28;    // 下方向键：VK_DOWN = 0x28
        case AndroidKey::DPAD_LEFT: return 0x25;    // 左方向键：VK_LEFT = 0x25
        case AndroidKey::DPAD_RIGHT: return 0x27;    // 右方向键：VK_RIGHT = 0x27
        // 符号键
        case AndroidKey::MINUS: return 0xBD;    // 减号 -：VK_OEM_MINUS = 0xBD
        case AndroidKey::EQUALS: return 0xBB;    // 等号 =：VK_OEM_PLUS = 0xBB
        case AndroidKey::LEFT_BRACKET: return 0xDB;    // 左方括号 [：VK_OEM_4 = 0xDB
        case AndroidKey::RIGHT_BRACKET: return 0xDD;    // 右方括号 ]：VK_OEM_6 = 0xDD
        case AndroidKey::BACKSLASH: return 0xDC;    // 反斜杠 \：VK_OEM_5 = 0xDC
        case AndroidKey::SEMICOLON: return 0xBA;    // 分号 ;：VK_OEM_1 = 0xBA
        case AndroidKey::APOSTROPHE: return 0xDE;    // 单引号 '：VK_OEM_7 = 0xDE
        case AndroidKey::COMMA: return 0xBC;    // 逗号 ,：VK_OEM_COMMA = 0xBC
        case AndroidKey::PERIOD: return 0xBE;    // 句点 .：VK_OEM_PERIOD = 0xBE
        case AndroidKey::SLASH: return 0xBF;    // 斜杠 /：VK_OEM_2 = 0xBF
        case AndroidKey::GRAVE: return 0xC0;    // 反引号 `：VK_OEM_3 = 0xC0
        // 锁键
        case AndroidKey::CAPS_LOCK: return 0x14;    // Caps Lock 键：VK_CAPITAL = 0x14
        case AndroidKey::NUM_LOCK: return 0x90;    // Num Lock 键：VK_NUMLOCK = 0x90
        case AndroidKey::SCROLL_LOCK: return 0x91;    // Scroll Lock 键：VK_SCROLL = 0x91
        // 【C++ 语法】default：switch 中所有 case 均不匹配时的兜底分支（此处返回 0 表示不支持的键）。
        default: return 0;  // 不支持的键
    }    // switch 分支块结束（} 右花括号）
}    // 函数结束（} 右花括号）

// ---------------------------------------------------------------------
// androidKeyCodeToWindowsScanCode：Android KeyCode -> Windows 扫描码（硬件码）
// 硬编码映射，不依赖 MapVirtualKey（后台线程可能无正确键盘布局上下文）。
// 扫描码用于 KEYEVENTF_SCANCODE 模式，DirectInput/Raw Input 游戏主要识别此码。
// ---------------------------------------------------------------------

// 【C++ 语法】函数定义：把 Android 键码映射为 Windows 物理扫描码（硬件码），返回值 int。
int androidKeyCodeToWindowsScanCode(int androidKeyCode) {
    // 注意：KEYEVENTF_SCANCODE 模式下 Windows 只认 wScan（硬件扫描码），
    // 扫描码必须对应键盘的真实物理按键位置，不能按连续值推算！
    // （如数字键 2 的物理扫描码是 0x03，而 0x0D 是 '=' 键——之前的
    //   连续公式把"2"错当成"="，导致游戏中按键输出全错。）

    // 字母 A-Z: 29..54。QWERTY 物理扫描码（非连续，必须查表）
    // 【C++ 语法】if + 花括号 {}：需要执行多条语句时必须用花括号包起来；此处进入字母 A-Z 处理分支。
    if (androidKeyCode >= 29 && androidKeyCode <= 54) {    // 安卓字母键码区间：29~54
        // 【C++ 语法】static const int sc[26]：static 使数组只初始化一次（静态存储期）；const 表示元素只读；[26] 声明包含 26 个 int 元素的定长数组。
        static const int sc[26] = {    // 字母物理扫描码查表数组（初始化列表开始）
            0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,  // A-M
            0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C   // N-Z
        };    // 数组初始化列表结束
        return sc[androidKeyCode - 29];    // 【C++ 语法】sc[...] 下标访问数组元素，用键码偏移量取对应扫描码
    }    // if 分支结束
    // 数字 0-9: 7..16（KEYCODE_0..KEYCODE_9）
    // 物理扫描码：1=0x02 2=0x03 ... 9=0x0A 0=0x0B（0 在 9 之后，非连续）
    if (androidKeyCode >= 7 && androidKeyCode <= 16) {    // 安卓数字键码区间：7~16
        static const int sc[10] = {0x0B, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};    // 数字键物理扫描码查表数组
        return sc[androidKeyCode - 7];    // 按下标取对应数字键扫描码
    }    // if 分支结束
    // F1-F10: 131..140 -> 0x3B..0x44（连续）
    // 注意：F11/F12 的物理扫描码不与 F1-F10 连续（F11=0x57, F12=0x58），
    // 若按连续公式推算会得到 0x45(NumLock)/0x46(ScrollLock)，游戏内按键全错。
    if (androidKeyCode >= 131 && androidKeyCode <= 140)    // 安卓 F1-F10 键码区间：131~140
        return 0x3B + (androidKeyCode - 131);    // 0x3B 是 F1 扫描码，F1-F10 连续递增
    if (androidKeyCode == AndroidKey::F11) return 0x57;    // F11 扫描码单独处理（不连续）：0x57
    if (androidKeyCode == AndroidKey::F12) return 0x58;    // F12 扫描码单独处理（不连续）：0x58
    // 小键盘 0-9: 144..153（按数字键盘物理布局，非连续）
    if (androidKeyCode >= 144 && androidKeyCode <= 153) {    // 安卓小键盘键码区间：144~153
        static const int sc[10] = {0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49};    // 小键盘数字物理扫描码查表数组
        return sc[androidKeyCode - 144];    // 按下标取对应小键盘扫描码
    }    // if 分支结束

    switch (androidKeyCode) {    // 逐键匹配的 switch 分支（语义同上方 VK 转换函数）
        // 修饰键
        case AndroidKey::SHIFT_LEFT: return 0x2A;    // 左 Shift 物理扫描码：0x2A
        case AndroidKey::SHIFT_RIGHT: return 0x36;    // 右 Shift 物理扫描码：0x36
        case AndroidKey::CTRL_LEFT: return 0x1D;    // 左 Ctrl 物理扫描码：0x1D
        case AndroidKey::CTRL_RIGHT: return 0x1D;  // E0 扩展
        case AndroidKey::ALT_LEFT: return 0x38;    // 左 Alt 物理扫描码：0x38
        case AndroidKey::ALT_RIGHT: return 0x38;   // E0 扩展
        // 控制/功能键
        case AndroidKey::SPACE: return 0x39;    // 空格键物理扫描码：0x39
        case AndroidKey::ENTER: return 0x1C;    // 回车键物理扫描码：0x1C
        case AndroidKey::TAB: return 0x0F;    // Tab 键物理扫描码：0x0F
        case AndroidKey::ESCAPE: return 0x01;    // Esc 键物理扫描码：0x01
        case AndroidKey::BACK: return 0x0E;    // 退格键物理扫描码：0x0E
        case AndroidKey::DEL: return 0x53;   // E0 扩展
        case AndroidKey::INSERT: return 0x52; // E0 扩展
        case AndroidKey::HOME: return 0x47;   // E0 扩展
        case AndroidKey::PAGE_UP: return 0x49; // E0 扩展
        case AndroidKey::PAGE_DOWN: return 0x51; // E0 扩展
        case AndroidKey::MOVE_END: return 0x4F; // E0 扩展
        // 方向键（E0 扩展）
        case AndroidKey::DPAD_UP: return 0x48;    // 上方向键物理扫描码：0x48
        case AndroidKey::DPAD_DOWN: return 0x50;    // 下方向键物理扫描码：0x50
        case AndroidKey::DPAD_LEFT: return 0x4B;    // 左方向键物理扫描码：0x4B
        case AndroidKey::DPAD_RIGHT: return 0x4D;    // 右方向键物理扫描码：0x4D
        // 符号键
        case AndroidKey::MINUS: return 0x0C;    // 减号物理扫描码：0x0C
        case AndroidKey::EQUALS: return 0x0D;    // 等号物理扫描码：0x0D
        case AndroidKey::LEFT_BRACKET: return 0x1A;    // 左方括号物理扫描码：0x1A
        case AndroidKey::RIGHT_BRACKET: return 0x1B;    // 右方括号物理扫描码：0x1B
        case AndroidKey::BACKSLASH: return 0x2B;    // 反斜杠物理扫描码：0x2B
        case AndroidKey::SEMICOLON: return 0x27;    // 分号物理扫描码：0x27
        case AndroidKey::APOSTROPHE: return 0x28;    // 单引号物理扫描码：0x28
        case AndroidKey::COMMA: return 0x33;    // 逗号物理扫描码：0x33
        case AndroidKey::PERIOD: return 0x34;    // 句点物理扫描码：0x34
        case AndroidKey::SLASH: return 0x35;    // 斜杠物理扫描码：0x35
        case AndroidKey::GRAVE: return 0x29;    // 反引号物理扫描码：0x29
        // 锁键
        case AndroidKey::CAPS_LOCK: return 0x3A;    // Caps Lock 物理扫描码：0x3A
        case AndroidKey::NUM_LOCK: return 0x45;  // E0 扩展（Numpad）
        case AndroidKey::SCROLL_LOCK: return 0x46;    // Scroll Lock 物理扫描码：0x46
        default: return 0;    // 兜底：未知键返回 0
    }    // switch 分支块结束
}    // 函数结束（} 右花括号）

// 【C++ 语法】匿名命名空间（namespace {}）：无名字的命名空间，其中定义的所有名字只在本 .cpp 编译单元内可见，可避免与其它翻译单元的符号冲突（等价于内部链接）。
namespace {    // 匿名命名空间开始

// 鼠标按键对应的 SendInput 事件标志（按下/松开）与 XButton 数据
// 【C++ 语法】struct：定义一个结构体（与 class 不同，成员默认 public）。此处把鼠标按键的"按下/松开标志 + 附加数据"打包为一个整体。
struct MouseFlags {    // 结构体定义开始
    DWORD down;    // 【Windows API】DWORD：Windows 定义的 32 位无符号整型；down 记录"按下"事件标志
    DWORD up;    // up 记录"松开"事件标志
    DWORD data;    // data 记录附加数据（如 XButton 侧键编号）
};    // 结构体定义结束（右花括号 + 分号）

// FORWARD/BACK 为 XButton 侧键，需通过 MOUSEEVENTF_XDOWN/XUP + mouseData 区分。
// Windows 约定：XBUTTON1=第 4 键（后退）、XBUTTON2=第 5 键（前进），
// 注意与浏览器/游戏的"后退键/前进键"一致，不能弄反。

// 【C++ 语法】函数定义：返回类型为 MouseFlags（按值返回，返回整个结构体的拷贝）；switch 内用 return {…} 初始化列表直接构造返回值。
MouseFlags mouseFlagsFor(MouseButton b) {    // 根据鼠标按键返回对应的事件标志
    switch (b) {    // 按鼠标按键类型分支匹配
        // 【C++ 语法】return {a, b, c}：花括号初始化列表，按成员声明顺序依次初始化 down/up/data 三个字段。
        case MouseButton::LEFT: return {MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP, 0};    // 左键：按下/松开标志 + 无附加数据
        case MouseButton::RIGHT: return {MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP, 0};    // 右键
        case MouseButton::MIDDLE: return {MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP, 0};    // 中键
        // 【Windows API】MOUSEEVENTF_XDOWN/XUP：第 4/5 侧键（XButton）的按下/松开标志；具体是第几键通过 mouseData 字段（XBUTTON1/XBUTTON2 常量）区分。
        case MouseButton::FORWARD: return {MOUSEEVENTF_XDOWN, MOUSEEVENTF_XUP, XBUTTON2};    // 前进侧键：附加数据 XBUTTON2
        case MouseButton::BACK: return {MOUSEEVENTF_XDOWN, MOUSEEVENTF_XUP, XBUTTON1};    // 后退侧键：附加数据 XBUTTON1
    }    // switch 分支块结束
    return {0, 0, 0};    // 兜底：未知按键返回全 0（无有效标志）
}    // 函数结束（} 右花括号）

}  // namespace    // 匿名命名空间结束

// ---------------------------------------------------------------------
// WindowsInputInjector —— SendInput 实现
//
// 状态记录（pressedKeys_/pressedButtons_）用于：
//   - 去重：同一键未松开前不会重复注入按下事件
//   - 精确释放：releaseAll 时遍历释放所有仍按住的键/鼠标键
//   - sendKeyUp/sendMouseUp 只在确实按下过时才发送松开事件
// ---------------------------------------------------------------------

// 【C++ 语法】class 继承：WindowsInputInjector 以 public 方式继承自 InputInjector（is-a 关系）；override 显式声明重写基类虚函数（编译器会校验）。
class WindowsInputInjector : public InputInjector {    // 派生类定义开始（{ 类体左花括号）
public:    // public 访问限定符：以下成员对外可见
    // 【C++ 语法】override 重写 + 内联函数体：返回类型 bool，末尾 const 表示不修改成员，函数体在声明处直接实现（{ return true; }）。
    bool isAvailable() const override { return true; }    // 本机实现注入能力恒可用

    // 按下按键（入参为 Android KeyCode；去重后注入）
    // 【C++ 语法】成员函数定义：override 重写基类虚函数，带函数体 {}。
    void sendKeyDown(int androidKeyCode) override {    // 按下按键（函数体开始）
        if (androidKeyCodeToWindowsVK(androidKeyCode) == 0) return;    // VK 为 0 表示不支持的键，直接忽略
        QMutexLocker locker(&mutex_);    // 【C++ 语法】RAII 锁对象：构造时对 &mutex_（取成员互斥量地址）加锁；函数退出（作用域结束）时自动解锁
        if (pressedKeys_.contains(androidKeyCode)) return;   // 已按下，忽略重复
        pressedKeys_.insert(androidKeyCode);    // 【C++ 语法】QSet::insert()：把键码插入集合，记录"当前按下"状态（自动去重）
        injectKey(androidKeyCode, true);    // 调用内部函数注入键盘"按下"事件（true=按下）
    }    // 函数体结束

    // 松开按键（只在确实按下过时发送）
    void sendKeyUp(int androidKeyCode) override {    // 松开按键（函数体开始）
        if (androidKeyCodeToWindowsVK(androidKeyCode) == 0) return;    // 不支持的键直接忽略
        QMutexLocker locker(&mutex_);    // 加锁（作用域结束自动解锁）
        if (!pressedKeys_.remove(androidKeyCode)) return;    // 【C++ 语法】QSet::remove()：删除元素并返回是否删除成功；! 取反——没按下过则提前返回
        injectKey(androidKeyCode, false);    // 注入键盘"松开"事件（false=松开）
    }    // 函数体结束

    // 按下鼠标按键（去重）
    void sendMouseDown(MouseButton button) override {    // 按下鼠标按键（函数体开始）
        QMutexLocker locker(&mutex_);    // 加锁（作用域结束自动解锁）
        if (pressedButtons_.contains(button)) return;    // 已按下则忽略重复
        pressedButtons_.insert(button);    // 记录该鼠标键为"按下"状态
        injectMouseButtonRaw(button, true);    // 注入鼠标按键"按下"事件
    }    // 函数体结束

    // 松开鼠标按键（只在确实按下过时发送）
    void sendMouseUp(MouseButton button) override {    // 松开鼠标按键（函数体开始）
        QMutexLocker locker(&mutex_);    // 加锁（作用域结束自动解锁）
        if (!pressedButtons_.remove(button)) return;    // 没按下过则忽略
        injectMouseButtonRaw(button, false);    // 注入鼠标按键"松开"事件
    }    // 函数体结束

    // 滚动鼠标滚轮（steps>0 上滚、<0 下滚，单位：格）。
    // 瞬时事件，不记录按键状态（无按下/松开语义）。
    void sendMouseWheel(int steps) override {    // 滚动滚轮（函数体开始）
        if (steps == 0) return;    // 步数为 0 时不做任何操作
        INPUT input;    // 【Windows API】INPUT 结构体：SendInput 使用的输入事件联合体（type 字段决定使用键盘/鼠标/硬件成员）
        memset(&input, 0, sizeof(input));    // 【C++ 语法】memset(指针, 值, 字节数)：把结构体内存全部清零（& 取地址，sizeof 取字节数），避免未初始化字段造成误触发
        input.type = INPUT_MOUSE;    // 【Windows API】type=INPUT_MOUSE：本事件为鼠标事件（对应使用联合体中的 mi 成员）
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;    // 【Windows API】dwFlags=MOUSEEVENTF_WHEEL：滚轮事件标志
        input.mi.mouseData = static_cast<DWORD>(steps * WHEEL_DELTA);  // 1 格 = 120
        SendInput(1, &input, sizeof(INPUT));    // 【Windows API】SendInput(事件个数, 事件数组指针, 单个事件字节数)：向系统注入输入事件
    }    // 函数体结束

    // 相对移动鼠标（像素，允许小数）。
    // 亚像素余量累积：小数部分保留到 mouseRemainder_，
    // 累积满 1px 才补发，避免右摇杆平滑移动的精度丢失。
    void sendMouseMove(float dx, float dy) override {    // 相对移动鼠标（函数体开始）
        QMutexLocker locker(&mutex_);    // 加锁（作用域结束自动解锁）
        mouseRemainderX_ += dx;    // 【C++ 语法】复合赋值 +=：等价于 mouseRemainderX_ = mouseRemainderX_ + dx；累加 X 方向位移（含小数）
        mouseRemainderY_ += dy;    // 累加 Y 方向位移（含小数）
        const int ix = static_cast<int>(mouseRemainderX_);    // 【C++ 语法】static_cast<int>()：显式类型转换（float→int，直接丢弃小数部分）；const 局部变量初始化后不可修改
        const int iy = static_cast<int>(mouseRemainderY_);    // 取 Y 方向位移的整数部分
        if (ix == 0 && iy == 0) return;    // 整数位移均为 0 时本帧不注入（小数先累积起来）
        mouseRemainderX_ -= ix;   // 扣掉已注入的整数部分
        mouseRemainderY_ -= iy;    // 扣掉已注入的 Y 整数部分，保留小数余量
        injectMouseMoveRaw(ix, iy);    // 注入 X/Y 整数像素位移
    }    // 函数体结束

    // 释放所有仍按住的键盘/鼠标键（手柄断开、停止映射、退出时调用），
    // 防止按键卡死。同时清零亚像素余量。
    void releaseAll() override {    // 释放所有按键（函数体开始）
        QMutexLocker locker(&mutex_);    // 加锁（作用域结束自动解锁）
        // 【C++ 语法】范围 for（range-based for）：for (元素类型 变量 : 容器) 逐个取出容器中的元素；此处用 const int ak 只读遍历集合。
        for (const int ak : pressedKeys_)    // 遍历当前所有"按下"状态的按键
            injectKey(ak, false);    // 逐个注入"松开"事件
        pressedKeys_.clear();    // 【C++ 语法】QSet::clear()：清空集合全部元素（重置按键按下状态）
        for (const MouseButton b : pressedButtons_)    // 遍历当前所有"按下"状态的鼠标键
            injectMouseButtonRaw(b, false);    // 逐个注入"松开"事件
        pressedButtons_.clear();    // 清空鼠标键按下状态
        mouseRemainderX_ = 0.f;    // 【C++ 语法】0.f：float 类型的字面量（f 后缀表示 float）；清零 X 方向亚像素余量
        mouseRemainderY_ = 0.f;    // 清零 Y 方向亚像素余量
    }    // 函数体结束

private:    // 【C++ 语法】private 访问限定符：以下成员只能在本类内部访问（对外隐藏实现细节）
    // 判断 Android KeyCode 是否为扩展键（需要 KEYEVENTF_EXTENDEDKEY）
    // 【C++ 语法】static 成员函数：属于类本身而非某个对象实例，可在无对象的情况下调用（内部不访问 this）。
    static bool isExtendedKey(int androidKeyCode) {    // 判断是否为扩展键（函数体开始）
        return androidKeyCode == AndroidKey::DPAD_UP    // 【C++ 语法】逻辑或 ||：任一比较为真则整个表达式为真；此表达式跨多行书写（运算符留在行尾表示未结束）
            || androidKeyCode == AndroidKey::DPAD_DOWN    // 下方向键：属于扩展键
            || androidKeyCode == AndroidKey::DPAD_LEFT    // 左方向键：属于扩展键
            || androidKeyCode == AndroidKey::DPAD_RIGHT    // 右方向键：属于扩展键
            || androidKeyCode == AndroidKey::INSERT    // Insert 键：属于扩展键
            || androidKeyCode == AndroidKey::DEL    // Delete 键：属于扩展键
            || androidKeyCode == AndroidKey::HOME    // Home 键：属于扩展键
            || androidKeyCode == AndroidKey::MOVE_END    // End 键：属于扩展键
            || androidKeyCode == AndroidKey::PAGE_UP    // PageUp 键：属于扩展键
            || androidKeyCode == AndroidKey::PAGE_DOWN    // PageDown 键：属于扩展键
            || androidKeyCode == AndroidKey::CTRL_RIGHT    // 右 Ctrl：属于扩展键
            || androidKeyCode == AndroidKey::ALT_RIGHT    // 右 Alt：属于扩展键
            || androidKeyCode == AndroidKey::NUM_LOCK;    // Num Lock：属于扩展键（以上键注入时需附加 KEYEVENTF_EXTENDEDKEY 标志）
    }    // 函数体结束

    // 注入单个键盘事件（down=true 按下，false 松开）
    // 使用 KEYEVENTF_SCANCODE（物理扫描码）模式：MSDN 规定该模式下 wVk 必须为 0，
    // Windows 会自动把扫描码换算成虚拟键码，DirectInput / Raw Input / GetAsyncKeyState 都能读到。
    void injectKey(int androidKeyCode, bool down) {    // 注入单个键盘事件（私有辅助函数，函数体开始）
        const int sc = androidKeyCodeToWindowsScanCode(androidKeyCode);    // 【C++ 语法】const 局部变量：初始化后不可修改；保存查得的物理扫描码
        if (sc == 0) return;    // 扫描码无效（未知键）则忽略
        INPUT input;    // 定义输入事件结构体实例
        memset(&input, 0, sizeof(input));    // 清零整个结构体（避免残留字段误触发）
        input.type = INPUT_KEYBOARD;    // 【Windows API】type=INPUT_KEYBOARD：本事件为键盘事件（对应使用联合体中的 ki 成员）
        input.ki.wVk = 0;                       // SCANCODE 模式下必须为 0
        input.ki.wScan = static_cast<WORD>(sc);    // 【Windows API】wScan 字段存放硬件扫描码；WORD 是 16 位无符号整型；static_cast<WORD> 做显式类型转换
        input.ki.dwFlags = KEYEVENTF_SCANCODE    // 【Windows API】dwFlags 事件标志：KEYEVENTF_SCANCODE 表示使用物理扫描码模式（该模式下 wVk 必须为 0）
                         | (down ? 0 : KEYEVENTF_KEYUP)    // 【C++ 语法】位或 | 把多个标志合并为一个值；三元运算符 ?: ——down 为真取 0（按下），为假取 KEYEVENTF_KEYUP（松开）
                         | (isExtendedKey(androidKeyCode) ? KEYEVENTF_EXTENDEDKEY : 0);    // 是扩展键则追加 KEYEVENTF_EXTENDEDKEY 标志，否则为 0
        SendInput(1, &input, sizeof(INPUT));    // 注入 1 个键盘事件
    }    // 函数体结束

    // 注入单个鼠标按键事件
    void injectMouseButtonRaw(MouseButton button, bool down) {    // 注入单个鼠标按键事件（私有辅助函数，函数体开始）
        const MouseFlags f = mouseFlagsFor(button);    // 查询该按键对应的事件标志（按值拷贝结构体）
        if (f.down == 0) return;    // 标志无效（未知按键）则忽略
        INPUT input;    // 定义输入事件结构体实例
        memset(&input, 0, sizeof(input));    // 清零整个结构体
        input.type = INPUT_MOUSE;    // 鼠标事件
        input.mi.dwFlags = down ? f.down : f.up;    // 按 down 参数选择"按下"或"松开"事件标志
        input.mi.mouseData = f.data;    // 设置附加数据（XButton 侧键编号等）
        SendInput(1, &input, sizeof(INPUT));    // 注入 1 个鼠标事件
    }    // 函数体结束

    // 注入相对鼠标移动事件
    void injectMouseMoveRaw(int dx, int dy) {    // 注入相对鼠标移动事件（私有辅助函数，函数体开始）
        INPUT input;    // 定义输入事件结构体实例
        memset(&input, 0, sizeof(input));    // 清零整个结构体
        input.type = INPUT_MOUSE;    // 鼠标事件
        input.mi.dx = dx;    // 【Windows API】mi.dx：相对移动模式下为 X 方向位移（像素）
        input.mi.dy = dy;    // mi.dy：相对移动模式下为 Y 方向位移（像素）
        input.mi.dwFlags = MOUSEEVENTF_MOVE;    // 【Windows API】MOUSEEVENTF_MOVE：相对移动事件标志
        SendInput(1, &input, sizeof(INPUT));    // 注入 1 个鼠标移动事件
    }    // 函数体结束

    mutable QMutex mutex_;                // 保护以下状态（多线程并发调用）
    QSet<int> pressedKeys_;               // 当前按下的 VK
    QSet<MouseButton> pressedButtons_;    // 当前按下的鼠标键
    float mouseRemainderX_ = 0.f;         // 亚像素余量累积（X 方向）
    float mouseRemainderY_ = 0.f;         // 亚像素余量累积（Y 方向）
};    // 类定义结束（右花括号 + 分号）

// 【C++ 语法】工厂函数：动态创建 WindowsInputInjector 对象，并以基类指针 InputInjector* 返回（外部只依赖接口，不依赖具体实现类）。
InputInjector* createWindowsInputInjector() {    // 创建 Windows 本机注入器（函数体开始）
    return new WindowsInputInjector();    // 【C++ 语法】new：在堆上动态创建对象并返回其指针；对象生命周期由调用方负责（必须用 delete 释放）
}    // 函数体结束（} 右花括号）
