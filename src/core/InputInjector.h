// 【C++ 语法】#pragma once：头文件守卫的另一种写法，确保本文件在同一个编译单元中只被包含（预处理）一次，防止重复定义。
#pragma once

// 【C++ 语法】#include 预处理指令：编译前把指定头文件的内容插入到本文件。使用双引号 "" 包含，表示优先在源文件所在目录查找该头文件。
#include "InputTypes.h"    // 引入输入相关类型定义（如 MouseButton 枚举）

// =====================================================================
// InputInjector —— 键鼠注入器接口（等效安卓版 InputInjector）
//
// Windows 本机版实现（WindowsInputInjector，见 InputInjector.cpp）
// 使用 SendInput() 直接向系统注入键盘/鼠标事件，
// 无需任何跨进程桥接（安卓版用 TCP + Bridge 传输）。
//
// 说明：
//  - 所有 send* 方法在 GUI 线程之外也可能被调用（右摇杆视角控制线程），
//    实现内部通过 QMutex 保护按键状态；
//  - 注入目标为当前前台窗口（即用户正在操作的任意程序，本机使用场景）。
// =====================================================================

// 【C++ 语法】class：定义一个类。本类只声明纯虚函数（= 0），因此是抽象基类（接口），不能直接实例化，必须由派生类实现全部纯虚函数后才能创建对象。
class InputInjector {    // 接口类定义开始（{ 为类体左花括号）
// 【C++ 语法】访问限定符 public：从本行起，其后的成员对所有代码可见可调用，是类对外开放的接口部分。
public:
// 【C++ 语法】virtual 虚析构函数：通过基类指针 delete 派生类对象时，会正确调用派生类的析构函数（实现多态销毁）；= default 表示使用编译器默认生成的析构实现。
    virtual ~InputInjector() = default;    // 虚析构函数（使用默认实现）

    // 注入能力是否可用（Windows 本机实现恒为 true）
    // 【C++ 语法】成员函数声明末尾的 const：表示该函数不会修改对象成员状态（只读函数）；= 0 表示纯虚函数（本接口不提供实现，派生类必须实现）。
    virtual bool isAvailable() const = 0;    // 查询注入能力是否可用（纯虚接口）

    // 按下/松开某个按键（入参为 Android KeyCode，内部转为 VK）
    // 【C++ 语法】virtual ... = 0：纯虚函数声明，是接口方法；派生类必须提供实现，否则派生类也仍是抽象类。int 参数按值传递。
    virtual void sendKeyDown(int androidKeyCode) = 0;    // 按下按键（纯虚接口）
    virtual void sendKeyUp(int androidKeyCode) = 0;    // 松开按键（纯虚接口）

    // 按下/松开某个鼠标按键（LEFT/RIGHT/MIDDLE/FORWARD/BACK）
    // 【C++ 语法】MouseButton 为枚举类型参数（按值传递）；= 0 纯虚接口。
    virtual void sendMouseDown(MouseButton button) = 0;    // 按下鼠标按键（纯虚接口）
    virtual void sendMouseUp(MouseButton button) = 0;    // 松开鼠标按键（纯虚接口）

    // 滚动鼠标滚轮（steps>0 上滚、<0 下滚，单位：格）
    // 【C++ 语法】int 参数按值传递；= 0 纯虚接口。
    virtual void sendMouseWheel(int steps) = 0;    // 滚动鼠标滚轮（纯虚接口）

    // 相对移动鼠标（单位：像素，支持小数；内部做亚像素余量累积，
    // 达到 1px 才补发，避免精度丢失）
    // 【C++ 语法】float 参数按值传递（支持小数位移）；= 0 纯虚接口。
    virtual void sendMouseMove(float dx, float dy) = 0;    // 相对移动鼠标（纯虚接口）

    // 释放所有已注入的键盘/鼠标按键（手柄断开、停止映射、退出时调用，
    // 防止按键卡死）
    // 【C++ 语法】无参数纯虚接口。
    virtual void releaseAll() = 0;    // 释放所有已注入的按键（纯虚接口）
};    // 接口类定义结束（右花括号 + 分号）

// Android KeyCode -> Windows 虚拟键码（VK）
// 复用安卓版 BridgeInputInjector 的完整映射表（字母/数字/F1-F12/
// 修饰键/小键盘/方向键/符号键等），未知键返回 0
// 【C++ 语法】普通自由函数（非成员函数）声明：以分号结尾表示只声明不定义（定义在 .cpp 中）；返回 int，参数为 int。
int androidKeyCodeToWindowsVK(int androidKeyCode);    // Android KeyCode -> Windows 虚拟键码（VK）转换函数声明

// 创建 Windows 本机注入器（基于 SendInput）
// 【C++ 语法】函数声明返回类型为 InputInjector*（指向 InputInjector 对象的指针）；该函数是工厂函数，用于创建具体注入器对象。
InputInjector* createWindowsInputInjector();    // 创建 Windows 本机注入器（返回堆对象指针，调用方负责 delete）
