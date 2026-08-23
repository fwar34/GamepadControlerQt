#pragma once

#include <QHash>
#include <QString>
#include <QVector>
#include <cmath>

// =====================================================================
// 手柄/鼠标/键盘基础类型
//
// 与安卓版（SteamLike）保持一致：
//  - KeyCode 沿用 Android KeyEvent 常量，保证配置文件格式兼容；
//  - 手柄按钮/鼠标按钮/摇杆统一为本地枚举，避免暴露 Windows XInput
//    或 Android KeyEvent 的底层差异。
// =====================================================================

// ---------------------------------------------------------------------
// Android KeyEvent keycode 常量（配置文件中保存的按键值）
//
// 这些值是安卓系统定义的标准按键码，与具体键盘硬件无关。
// 运行时通过 InputInjector::androidKeyCodeToWindowsVK() 转换为
// Windows 虚拟键码（VK）后再注入系统。
// ---------------------------------------------------------------------
namespace AndroidKey {
    // 字母 A-Z: 29..54
    constexpr int A = 29, B = 30, C = 31, D = 32, E = 33, F = 34, G = 35, H = 36,
                  I = 37, J = 38, K = 39, L = 40, M = 41, N = 42, O = 43, P = 44,
                  Q = 45, R = 46, S = 47, T = 48, U = 49, V = 50, W = 51, X = 52,
                  Y = 53, Z = 54;
    // 数字 0-9: 7..16
    constexpr int N0 = 7, N1 = 8, N2 = 9, N3 = 10, N4 = 11,
                  N5 = 12, N6 = 13, N7 = 14, N8 = 15, N9 = 16;
    // 功能键 F1-F12: 131..142
    constexpr int F1 = 131, F2 = 132, F3 = 133, F4 = 134, F5 = 135, F6 = 136,
                  F7 = 137, F8 = 138, F9 = 139, F10 = 140, F11 = 141, F12 = 142;
    // 修饰键
    constexpr int SHIFT_LEFT = 59, SHIFT_RIGHT = 60,
                  CTRL_LEFT = 113, CTRL_RIGHT = 114,
                  ALT_LEFT = 57, ALT_RIGHT = 58;
    // 特殊键
    constexpr int SPACE = 62, ENTER = 66, TAB = 61, ESCAPE = 111, BACK = 4,
                  DEL = 67, INSERT = 124, HOME = 123, PAGE_UP = 92,
                  PAGE_DOWN = 93, MOVE_END = 122;
    // 方向键
    constexpr int DPAD_UP = 19, DPAD_DOWN = 20, DPAD_LEFT = 21, DPAD_RIGHT = 22;
    // 符号键
    constexpr int MINUS = 69, EQUALS = 70, LEFT_BRACKET = 71, RIGHT_BRACKET = 72,
                  BACKSLASH = 73, SEMICOLON = 74, APOSTROPHE = 75, COMMA = 55,
                  PERIOD = 56, SLASH = 76, GRAVE = 68;
    // 锁键
    constexpr int CAPS_LOCK = 115, NUM_LOCK = 143, SCROLL_LOCK = 116;
    // 小键盘 0-9: 144..153
    constexpr int NUMPAD_0 = 144, NUMPAD_1 = 145, NUMPAD_2 = 146, NUMPAD_3 = 147,
                  NUMPAD_4 = 148, NUMPAD_5 = 149, NUMPAD_6 = 150, NUMPAD_7 = 151,
                  NUMPAD_8 = 152, NUMPAD_9 = 153;
}

// ---------------------------------------------------------------------
// ControllerButton —— 手柄物理按键的统一枚举
//
// 各按键来源（XInput）：
//  - A/B/X/Y、LEFT/RIGHT_SHOULDER(LB/RB)、MENU(START)、OPTIONS(BACK)
//  - LEFT/RIGHT_TRIGGER_CLICK：扳机键（LT/RT），XInput 中为 0-255 模拟值，
//    阈值 >=128 视为按下
//  - LEFT/RIGHT_STICK_CLICK：摇杆按下（L3/R3）
//  - GUIDE：Xbox 中央键（XInput 需通过 XInputGetKeystroke 单独读取，当前未用）
//  - DPAD_*：方向键
//  - TOUCHPAD_CLICK：触摸板点击（XInput 无对应物理位，保留枚举以兼容安卓配置）
// ---------------------------------------------------------------------
enum class ControllerButton {
    A, B, X, Y,
    LEFT_SHOULDER, RIGHT_SHOULDER,
    LEFT_TRIGGER_CLICK, RIGHT_TRIGGER_CLICK,
    LEFT_STICK_CLICK, RIGHT_STICK_CLICK,
    MENU, OPTIONS, GUIDE,
    DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT,
    TOUCHPAD_CLICK
};

// ---------------------------------------------------------------------
// ControllerStick —— 摇杆
//  - LEFT_STICK：默认映射 WASD 8 方向移动
//  - RIGHT_STICK：默认映射视角控制（LookAround），由独立线程按固定节拍处理
//  - DPAD_AS_STICK：预留，将方向键模拟为摇杆
// ---------------------------------------------------------------------
enum class ControllerStick {
    LEFT_STICK, RIGHT_STICK, DPAD_AS_STICK
};

// 鼠标按键（名称用大写，与安卓版枚举名一致，保证配置文件兼容）
enum class MouseButton {
    LEFT, RIGHT, MIDDLE, FORWARD, BACK
};

// QHash/QSet 需要 qHash 重载（Qt 5.15 对 enum 没有默认实现）
inline uint qHash(ControllerButton key, uint seed = 0) noexcept {
    return ::qHash(static_cast<int>(key), seed);
}
inline uint qHash(MouseButton key, uint seed = 0) noexcept {
    return ::qHash(static_cast<int>(key), seed);
}

// ---------------------------------------------------------------------
// Vector2 —— 二维向量（摇杆输入）
// 摇杆原始输入为 x/y ∈ [-1,1]，经过死区处理后用于移动/视角控制。
// ---------------------------------------------------------------------
struct Vector2 {
    float x = 0.f;
    float y = 0.f;

    // 向量长度
    float magnitude() const {
        return std::sqrt(x * x + y * y);
    }

    // 归一化：长度不足 1e-6 视为零向量
    Vector2 normalized() const {
        const float m = magnitude();
        if (m < 1e-6f) return Vector2{0.f, 0.f};
        return Vector2{x / m, y / m};
    }

    // 死区缩放：(mag - deadzone) / (1 - deadzone)
    // 输入小于死区返回零向量，超过死区后按比例线性放大，
    // 保证摇杆推到底（mag=1）时输出仍为满幅（1）。
    Vector2 withDeadzone(float deadzone) const {
        const float m = magnitude();
        if (m <= deadzone) return Vector2{0.f, 0.f};
        const float scale = (m - deadzone) / (1.f - deadzone);
        return Vector2{x * scale, y * scale};
    }
};

// ---------------------------------------------------------------------
// 辅助函数
// 提供三种命名空间：
//  1. controllerButtonName/FromName —— 配置文件中使用的内部名（英文枚举名）
//  2. controllerButtonDisplayName —— 界面显示名（中文，如"A键"）
//  3. keyCodeToName —— Android KeyCode 的可读名称
// ---------------------------------------------------------------------

// 统一按钮枚举 -> 内部名（"A"、"DPAD_UP"），用于配置文件读写
QString controllerButtonName(ControllerButton b);
// 内部名 -> 按钮枚举；解析失败返回 false
bool controllerButtonFromName(const QString& name, ControllerButton* out);
// 统一按钮枚举 -> 显示名（"A键"、"方向键上"），用于界面
QString controllerButtonDisplayName(ControllerButton b);
// 所有按钮（按固定显示顺序，用于编辑界面遍历）
QVector<ControllerButton> allControllerButtons();

// 鼠标按键 -> 内部名（"LEFT"），用于配置文件
QString mouseButtonName(MouseButton b);
// 内部名 -> 鼠标按键；解析失败返回 false（兼容大小写）
bool mouseButtonFromName(const QString& name, MouseButton* out);
// 鼠标按键 -> 显示名（"左键"），用于界面
QString mouseButtonDisplayName(MouseButton b);

// Android KeyCode -> 可读名称（与安卓版 keyCodeToName 一致）
QString keyCodeToName(int keyCode);

// 操作层显示名（Layer1->"Layer1 战斗"等，WoW 预设；未知层名原样返回）
QString layerDisplayName(const QString& layerName);
