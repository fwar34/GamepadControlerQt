#pragma once

#include "InputTypes.h"

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
class InputInjector {
public:
    virtual ~InputInjector() = default;

    // 注入能力是否可用（Windows 本机实现恒为 true）
    virtual bool isAvailable() const = 0;

    // 按下/松开某个按键（入参为 Android KeyCode，内部转为 VK）
    virtual void sendKeyDown(int androidKeyCode) = 0;
    virtual void sendKeyUp(int androidKeyCode) = 0;

    // 按下/松开某个鼠标按键（LEFT/RIGHT/MIDDLE/FORWARD/BACK）
    virtual void sendMouseDown(MouseButton button) = 0;
    virtual void sendMouseUp(MouseButton button) = 0;

    // 相对移动鼠标（单位：像素，支持小数；内部做亚像素余量累积，
    // 达到 1px 才补发，避免精度丢失）
    virtual void sendMouseMove(float dx, float dy) = 0;

    // 释放所有已注入的键盘/鼠标按键（手柄断开、停止映射、退出时调用，
    // 防止按键卡死）
    virtual void releaseAll() = 0;
};

// Android KeyCode -> Windows 虚拟键码（VK）
// 复用安卓版 BridgeInputInjector 的完整映射表（字母/数字/F1-F12/
// 修饰键/小键盘/方向键/符号键等），未知键返回 0
int androidKeyCodeToWindowsVK(int androidKeyCode);

// 创建 Windows 本机注入器（基于 SendInput）
InputInjector* createWindowsInputInjector();
