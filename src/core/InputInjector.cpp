#include "InputInjector.h"

#include <QMutex>
#include <QMutexLocker>
#include <QSet>
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
int androidKeyCodeToWindowsVK(int androidKeyCode) {
    // 字母 A-Z: 29..54 -> VK_A(0x41)..VK_Z
    if (androidKeyCode >= 29 && androidKeyCode <= 54)
        return 0x41 + (androidKeyCode - 29);
    // 数字 0-9: 7..16 -> VK_0(0x30)..VK_9
    if (androidKeyCode >= 7 && androidKeyCode <= 16)
        return 0x30 + (androidKeyCode - 7);
    // F1-F12: 131..142 -> VK_F1(0x70)..VK_F12
    if (androidKeyCode >= 131 && androidKeyCode <= 142)
        return 0x70 + (androidKeyCode - 131);
    // 小键盘 0-9: 144..153 -> VK_NUMPAD0(0x60)..VK_NUMPAD9
    if (androidKeyCode >= 144 && androidKeyCode <= 153)
        return 0x60 + (androidKeyCode - 144);

    switch (androidKeyCode) {
        // 修饰键（左右独立，VK_LSHIFT/RSHIFT/LCTRL/RCTRL/LMENU/RMENU）
        case AndroidKey::SHIFT_LEFT: return 0xA0;
        case AndroidKey::SHIFT_RIGHT: return 0xA1;
        case AndroidKey::CTRL_LEFT: return 0xA2;
        case AndroidKey::CTRL_RIGHT: return 0xA3;
        case AndroidKey::ALT_LEFT: return 0xA4;
        case AndroidKey::ALT_RIGHT: return 0xA5;
        // 控制/功能键
        case AndroidKey::SPACE: return 0x20;
        case AndroidKey::ENTER: return 0x0D;
        case AndroidKey::TAB: return 0x09;
        case AndroidKey::ESCAPE: return 0x1B;
        case AndroidKey::BACK: return 0x08;
        case AndroidKey::DEL: return 0x2E;
        case AndroidKey::INSERT: return 0x2D;
        case AndroidKey::HOME: return 0x24;
        case AndroidKey::PAGE_UP: return 0x21;
        case AndroidKey::PAGE_DOWN: return 0x22;
        case AndroidKey::MOVE_END: return 0x23;
        // 方向键（注意：这里的 DPAD_* 是键盘方向键码，
        // 与手柄 DPad 无关；手柄方向键由 XInput 读取）
        case AndroidKey::DPAD_UP: return 0x26;
        case AndroidKey::DPAD_DOWN: return 0x28;
        case AndroidKey::DPAD_LEFT: return 0x25;
        case AndroidKey::DPAD_RIGHT: return 0x27;
        // 符号键
        case AndroidKey::MINUS: return 0xBD;
        case AndroidKey::EQUALS: return 0xBB;
        case AndroidKey::LEFT_BRACKET: return 0xDB;
        case AndroidKey::RIGHT_BRACKET: return 0xDD;
        case AndroidKey::BACKSLASH: return 0xDC;
        case AndroidKey::SEMICOLON: return 0xBA;
        case AndroidKey::APOSTROPHE: return 0xDE;
        case AndroidKey::COMMA: return 0xBC;
        case AndroidKey::PERIOD: return 0xBE;
        case AndroidKey::SLASH: return 0xBF;
        case AndroidKey::GRAVE: return 0xC0;
        // 锁键
        case AndroidKey::CAPS_LOCK: return 0x14;
        case AndroidKey::NUM_LOCK: return 0x90;
        case AndroidKey::SCROLL_LOCK: return 0x91;
        default: return 0;  // 不支持的键
    }
}

namespace {

// 鼠标按键对应的 SendInput 事件标志（按下/松开）与 XButton 数据
struct MouseFlags {
    DWORD down;
    DWORD up;
    DWORD data;
};

// FORWARD/BACK 为 XButton 侧键，需通过 MOUSEEVENTF_XDOWN/XUP + mouseData 区分
MouseFlags mouseFlagsFor(MouseButton b) {
    switch (b) {
        case MouseButton::LEFT: return {MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP, 0};
        case MouseButton::RIGHT: return {MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP, 0};
        case MouseButton::MIDDLE: return {MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP, 0};
        case MouseButton::FORWARD: return {MOUSEEVENTF_XDOWN, MOUSEEVENTF_XUP, XBUTTON1};
        case MouseButton::BACK: return {MOUSEEVENTF_XDOWN, MOUSEEVENTF_XUP, XBUTTON2};
    }
    return {0, 0, 0};
}

}  // namespace

// ---------------------------------------------------------------------
// WindowsInputInjector —— SendInput 实现
//
// 状态记录（pressedKeys_/pressedButtons_）用于：
//   - 去重：同一键未松开前不会重复注入按下事件
//   - 精确释放：releaseAll 时遍历释放所有仍按住的键/鼠标键
//   - sendKeyUp/sendMouseUp 只在确实按下过时才发送松开事件
// ---------------------------------------------------------------------
class WindowsInputInjector : public InputInjector {
public:
    bool isAvailable() const override { return true; }

    // 按下按键（入参为 Android KeyCode -> 转 VK；去重后注入）
    void sendKeyDown(int androidKeyCode) override {
        const int vk = androidKeyCodeToWindowsVK(androidKeyCode);
        if (vk == 0) return;
        QMutexLocker locker(&mutex_);
        if (pressedKeys_.contains(vk)) return;   // 已按下，忽略重复
        pressedKeys_.insert(vk);
        injectKey(static_cast<WORD>(vk), true);
    }

    // 松开按键（只在确实按下过时发送）
    void sendKeyUp(int androidKeyCode) override {
        const int vk = androidKeyCodeToWindowsVK(androidKeyCode);
        if (vk == 0) return;
        QMutexLocker locker(&mutex_);
        if (!pressedKeys_.remove(vk)) return;
        injectKey(static_cast<WORD>(vk), false);
    }

    // 按下鼠标按键（去重）
    void sendMouseDown(MouseButton button) override {
        QMutexLocker locker(&mutex_);
        if (pressedButtons_.contains(button)) return;
        pressedButtons_.insert(button);
        injectMouseButtonRaw(button, true);
    }

    // 松开鼠标按键（只在确实按下过时发送）
    void sendMouseUp(MouseButton button) override {
        QMutexLocker locker(&mutex_);
        if (!pressedButtons_.remove(button)) return;
        injectMouseButtonRaw(button, false);
    }

    // 相对移动鼠标（像素，允许小数）。
    // 亚像素余量累积：小数部分保留到 mouseRemainder_，
    // 累积满 1px 才补发，避免右摇杆平滑移动的精度丢失。
    void sendMouseMove(float dx, float dy) override {
        QMutexLocker locker(&mutex_);
        mouseRemainderX_ += dx;
        mouseRemainderY_ += dy;
        const int ix = static_cast<int>(mouseRemainderX_);
        const int iy = static_cast<int>(mouseRemainderY_);
        if (ix == 0 && iy == 0) return;
        mouseRemainderX_ -= ix;   // 扣掉已注入的整数部分
        mouseRemainderY_ -= iy;
        injectMouseMoveRaw(ix, iy);
    }

    // 释放所有仍按住的键盘/鼠标键（手柄断开、停止映射、退出时调用），
    // 防止按键卡死。同时清零亚像素余量。
    void releaseAll() override {
        QMutexLocker locker(&mutex_);
        for (const int vk : pressedKeys_)
            injectKey(static_cast<WORD>(vk), false);
        pressedKeys_.clear();
        for (const MouseButton b : pressedButtons_)
            injectMouseButtonRaw(b, false);
        pressedButtons_.clear();
        mouseRemainderX_ = 0.f;
        mouseRemainderY_ = 0.f;
    }

private:
    // 注入单个键盘事件（down=true 按下，false 松开）
    void injectKey(WORD vk, bool down) {
        INPUT input;
        memset(&input, 0, sizeof(input));
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = vk;
        input.ki.wScan = 0;
        input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
        SendInput(1, &input, sizeof(INPUT));
    }

    // 注入单个鼠标按键事件
    void injectMouseButtonRaw(MouseButton button, bool down) {
        const MouseFlags f = mouseFlagsFor(button);
        if (f.down == 0) return;
        INPUT input;
        memset(&input, 0, sizeof(input));
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = down ? f.down : f.up;
        input.mi.mouseData = f.data;
        SendInput(1, &input, sizeof(INPUT));
    }

    // 注入相对鼠标移动事件
    void injectMouseMoveRaw(int dx, int dy) {
        INPUT input;
        memset(&input, 0, sizeof(input));
        input.type = INPUT_MOUSE;
        input.mi.dx = dx;
        input.mi.dy = dy;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        SendInput(1, &input, sizeof(INPUT));
    }

    mutable QMutex mutex_;                // 保护以下状态（多线程并发调用）
    QSet<int> pressedKeys_;               // 当前按下的 VK
    QSet<MouseButton> pressedButtons_;    // 当前按下的鼠标键
    float mouseRemainderX_ = 0.f;         // 亚像素余量累积（X 方向）
    float mouseRemainderY_ = 0.f;         // 亚像素余量累积（Y 方向）
};

InputInjector* createWindowsInputInjector() {
    return new WindowsInputInjector();
}
