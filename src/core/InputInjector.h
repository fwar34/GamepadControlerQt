#pragma once

#include "InputTypes.h"

// =====================================================================
// 键鼠注入器接口（等效安卓版 InputInjector）
// Windows 本机版实现使用 SendInput 直接注入
// =====================================================================
class InputInjector {
public:
    virtual ~InputInjector() = default;

    virtual bool isAvailable() const = 0;
    virtual void sendKeyDown(int androidKeyCode) = 0;
    virtual void sendKeyUp(int androidKeyCode) = 0;
    virtual void sendMouseDown(MouseButton button) = 0;
    virtual void sendMouseUp(MouseButton button) = 0;
    virtual void sendMouseMove(float dx, float dy) = 0;
    virtual void releaseAll() = 0;
};

// Android KeyCode -> Windows 虚拟键码（复用安卓版 BridgeInputInjector 的映射表）
int androidKeyCodeToWindowsVK(int androidKeyCode);

// 创建 Windows 本机注入器（SendInput）
InputInjector* createWindowsInputInjector();
