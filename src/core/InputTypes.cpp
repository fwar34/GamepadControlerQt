// ============================================================
// InputTypes.cpp
// 输入类型的工具函数实现
// ------------------------------------------------------------
// 本文件实现 InputTypes.h 中声明的各类"名字 <-> 枚举"转换函数，
// 它们服务于三个场景：
//   1. 配置持久化：把枚举序列化为稳定的字符串（见 ControllerConfig.cpp）
//   2. UI 展示：把枚举转换为人类可读的中文/符号描述（编辑对话框、悬浮窗）
//   3. 解析：把配置文件中的字符串反向解析回枚举（见 ControllerConfig.cpp）
//
// 重要约定：
//   - 序列化名（controllerButtonName / mouseButtonName / AndroidKey 映射）
//     必须与安卓版保持完全一致，否则无法复用安卓端的配置文件。
//   - 枚举内部使用 Android KeyCode 体系（AndroidKey::A 等），
//     与 Windows 虚拟键码相互独立，注入时再经 VK 映射表转换。
// ============================================================

#include "InputTypes.h"

#include <QHash>

// ============================================================
// controllerButtonName：手柄按钮 -> 序列化名（大写英文）
// ============================================================
// 该名称写入配置文件与悬浮窗过滤逻辑，须与安卓版保持一致的枚举名
//（如 A/B/X/Y、LB/RB、LT/RT、L3/R3、MENU/OPTIONS、DPAD_* 等）。
QString controllerButtonName(ControllerButton b) {
    switch (b) {
        case ControllerButton::A: return QStringLiteral("A");
        case ControllerButton::B: return QStringLiteral("B");
        case ControllerButton::X: return QStringLiteral("X");
        case ControllerButton::Y: return QStringLiteral("Y");
        case ControllerButton::LEFT_SHOULDER: return QStringLiteral("LB");
        case ControllerButton::RIGHT_SHOULDER: return QStringLiteral("RB");
        case ControllerButton::LEFT_TRIGGER_CLICK: return QStringLiteral("LT");
        case ControllerButton::RIGHT_TRIGGER_CLICK: return QStringLiteral("RT");
        case ControllerButton::LEFT_STICK_CLICK: return QStringLiteral("L3");
        case ControllerButton::RIGHT_STICK_CLICK: return QStringLiteral("R3");
        case ControllerButton::MENU: return QStringLiteral("MENU");
        case ControllerButton::OPTIONS: return QStringLiteral("OPTIONS");
        case ControllerButton::GUIDE: return QStringLiteral("GUIDE");
        case ControllerButton::DPAD_UP: return QStringLiteral("DPAD_UP");
        case ControllerButton::DPAD_DOWN: return QStringLiteral("DPAD_DOWN");
        case ControllerButton::DPAD_LEFT: return QStringLiteral("DPAD_LEFT");
        case ControllerButton::DPAD_RIGHT: return QStringLiteral("DPAD_RIGHT");
        case ControllerButton::TOUCHPAD_CLICK: return QStringLiteral("TOUCHPAD_CLICK");
    }
    return QString();
}

// ============================================================
// controllerButtonFromName：序列化名 -> 手柄按钮（解析用）
// ============================================================
// 线性遍历全部按钮逐一比对，成功时写入 out 并返回 true；
// 失败返回 false（例如配置里出现了未知按钮名）。
bool controllerButtonFromName(const QString& name, ControllerButton* out) {
    for (const ControllerButton b : allControllerButtons()) {
        if (controllerButtonName(b) == name) {
            *out = b;
            return true;
        }
    }
    return false;
}

// ============================================================
// controllerButtonDisplayName：手柄按钮 -> 中文展示名
// ============================================================
// 仅用于 UI 显示（编辑对话框按钮列表、悬浮窗按键提示），
// 不参与配置序列化，因此可以放心使用中文。
QString controllerButtonDisplayName(ControllerButton b) {
    switch (b) {
        case ControllerButton::A: return QStringLiteral("A键");
        case ControllerButton::B: return QStringLiteral("B键");
        case ControllerButton::X: return QStringLiteral("X键");
        case ControllerButton::Y: return QStringLiteral("Y键");
        case ControllerButton::LEFT_SHOULDER: return QStringLiteral("LB肩键");
        case ControllerButton::RIGHT_SHOULDER: return QStringLiteral("RB肩键");
        case ControllerButton::LEFT_TRIGGER_CLICK: return QStringLiteral("LT扳机");
        case ControllerButton::RIGHT_TRIGGER_CLICK: return QStringLiteral("RT扳机");
        case ControllerButton::LEFT_STICK_CLICK: return QStringLiteral("L3摇杆按下");
        case ControllerButton::RIGHT_STICK_CLICK: return QStringLiteral("R3摇杆按下");
        case ControllerButton::MENU: return QStringLiteral("菜单键");
        case ControllerButton::OPTIONS: return QStringLiteral("视图键");
        case ControllerButton::GUIDE: return QStringLiteral("Home键");
        case ControllerButton::DPAD_UP: return QStringLiteral("方向键上");
        case ControllerButton::DPAD_DOWN: return QStringLiteral("方向键下");
        case ControllerButton::DPAD_LEFT: return QStringLiteral("方向键左");
        case ControllerButton::DPAD_RIGHT: return QStringLiteral("方向键右");
        case ControllerButton::TOUCHPAD_CLICK: return QStringLiteral("触控板点击");
    }
    return QString();
}

// ============================================================
// allControllerButtons：返回所有可映射的手柄按钮集合
// ============================================================
// 供 UI 遍历按钮列表、以及 controllerButtonFromName 遍历比对使用。
// GUIDE / TOUCHPAD_CLICK 等按键即使 XInput 无对应物理位，也保留在
// 枚举中，兼容安卓配置文件。
QVector<ControllerButton> allControllerButtons() {
    return {
        ControllerButton::A,
        ControllerButton::B,
        ControllerButton::X,
        ControllerButton::Y,
        ControllerButton::LEFT_SHOULDER,
        ControllerButton::RIGHT_SHOULDER,
        ControllerButton::LEFT_TRIGGER_CLICK,
        ControllerButton::RIGHT_TRIGGER_CLICK,
        ControllerButton::LEFT_STICK_CLICK,
        ControllerButton::RIGHT_STICK_CLICK,
        ControllerButton::MENU,
        ControllerButton::OPTIONS,
        ControllerButton::DPAD_UP,
        ControllerButton::DPAD_DOWN,
        ControllerButton::DPAD_LEFT,
        ControllerButton::DPAD_RIGHT,
        ControllerButton::GUIDE,
        ControllerButton::TOUCHPAD_CLICK,
    };
}

// ============================================================
// mouseButtonName：鼠标键 -> 序列化名（大写）
// ============================================================
// 与安卓版枚举名保持一致（大写），保证配置文件兼容。
QString mouseButtonName(MouseButton b) {
    switch (b) {
        case MouseButton::LEFT: return QStringLiteral("LEFT");
        case MouseButton::RIGHT: return QStringLiteral("RIGHT");
        case MouseButton::MIDDLE: return QStringLiteral("MIDDLE");
        case MouseButton::FORWARD: return QStringLiteral("FORWARD");
        case MouseButton::BACK: return QStringLiteral("BACK");
    }
    return QString();
}

// ============================================================
// mouseButtonFromName：鼠标键名 -> 枚举（解析用）
// ============================================================
// 兼容大写（安卓格式）与小写：统一转大写后查表。
bool mouseButtonFromName(const QString& name, MouseButton* out) {
    const QString key = name.toUpper();
    const QHash<QString, MouseButton> map = {
        {QStringLiteral("LEFT"), MouseButton::LEFT},
        {QStringLiteral("RIGHT"), MouseButton::RIGHT},
        {QStringLiteral("MIDDLE"), MouseButton::MIDDLE},
        {QStringLiteral("FORWARD"), MouseButton::FORWARD},
        {QStringLiteral("BACK"), MouseButton::BACK},
    };
    const auto it = map.constFind(key);
    if (it != map.constEnd()) {
        *out = it.value();
        return true;
    }
    return false;
}

// ============================================================
// mouseButtonDisplayName：鼠标键 -> 中文展示名
// ============================================================
QString mouseButtonDisplayName(MouseButton b) {
    switch (b) {
        case MouseButton::LEFT: return QStringLiteral("鼠标左键");
        case MouseButton::RIGHT: return QStringLiteral("鼠标右键");
        case MouseButton::MIDDLE: return QStringLiteral("鼠标中键");
        case MouseButton::FORWARD: return QStringLiteral("鼠标前进键");
        case MouseButton::BACK: return QStringLiteral("鼠标后退键");
    }
    return QString();
}

// ============================================================
// keyCodeToName：Android KeyCode -> 展示名
// ============================================================
// 用于 KeyMapping::describe()（编辑对话框/按钮列表里的映射描述）。
// 先处理有规律的区间（字母/数字/F1-F12/小键盘），再处理散键。
QString keyCodeToName(int keyCode) {
    // 字母 A-Z：Android KeyCode 29~54 连续对应 ASCII 'A'~'Z'
    if (keyCode >= 29 && keyCode <= 54)
        return QString(QChar('A' + (keyCode - 29)));
    // 数字 0-9：Android KeyCode 7~16 连续对应 '0'~'9'
    if (keyCode >= 7 && keyCode <= 16)
        return QString(QChar('0' + (keyCode - 7)));
    // 功能键 F1-F12：Android KeyCode 131~142 连续
    if (keyCode >= 131 && keyCode <= 142)
        return QStringLiteral("F%1").arg(keyCode - 131 + 1);
    // 小键盘 0-9：Android KeyCode 144~153 连续
    if (keyCode >= 144 && keyCode <= 153)
        return QStringLiteral("Num%1").arg(keyCode - 144);

    // 其余散键逐一映射为易读名称
    switch (keyCode) {
        case AndroidKey::SPACE: return QStringLiteral("Space");
        case AndroidKey::ENTER: return QStringLiteral("Enter");
        case AndroidKey::TAB: return QStringLiteral("Tab");
        case AndroidKey::ESCAPE: return QStringLiteral("Esc");
        case AndroidKey::BACK: return QStringLiteral("Back");
        case AndroidKey::DEL: return QStringLiteral("Backspace");
        case AndroidKey::INSERT: return QStringLiteral("Insert");
        case AndroidKey::HOME: return QStringLiteral("Home");
        case AndroidKey::PAGE_UP: return QStringLiteral("PageUp");
        case AndroidKey::PAGE_DOWN: return QStringLiteral("PageDown");
        case AndroidKey::MOVE_END: return QStringLiteral("End");
        case AndroidKey::SHIFT_LEFT:
        case AndroidKey::SHIFT_RIGHT: return QStringLiteral("Shift");
        case AndroidKey::CTRL_LEFT:
        case AndroidKey::CTRL_RIGHT: return QStringLiteral("Ctrl");
        case AndroidKey::ALT_LEFT:
        case AndroidKey::ALT_RIGHT: return QStringLiteral("Alt");
        case AndroidKey::DPAD_UP: return QStringLiteral("↑");
        case AndroidKey::DPAD_DOWN: return QStringLiteral("↓");
        case AndroidKey::DPAD_LEFT: return QStringLiteral("←");
        case AndroidKey::DPAD_RIGHT: return QStringLiteral("→");
        case AndroidKey::MINUS: return QStringLiteral("-");
        case AndroidKey::EQUALS: return QStringLiteral("=");
        case AndroidKey::LEFT_BRACKET: return QStringLiteral("[");
        case AndroidKey::RIGHT_BRACKET: return QStringLiteral("]");
        case AndroidKey::BACKSLASH: return QStringLiteral("\\");
        case AndroidKey::SEMICOLON: return QStringLiteral(";");
        case AndroidKey::APOSTROPHE: return QStringLiteral("'");
        case AndroidKey::COMMA: return QStringLiteral(",");
        case AndroidKey::PERIOD: return QStringLiteral(".");
        case AndroidKey::SLASH: return QStringLiteral("/");
        case AndroidKey::GRAVE: return QStringLiteral("`");
        case AndroidKey::CAPS_LOCK: return QStringLiteral("CapsLock");
        case AndroidKey::NUM_LOCK: return QStringLiteral("NumLock");
        case AndroidKey::SCROLL_LOCK: return QStringLiteral("ScrollLock");
        default:
            return QStringLiteral("Key(%1)").arg(keyCode);
    }
}

// ============================================================
// layerDisplayName：层名 -> 带预设中文别名的展示名
// ============================================================
// 与安卓版 WoW 动作集预设（WoWActionSets.LAYER_NAMES）保持一致，
// 例如 "Layer1" 显示为 "Layer1 战斗"。非预设层名原样返回。
QString layerDisplayName(const QString& layerName) {
    static const QHash<QString, QString> names = {
        {QStringLiteral("Layer1"), QStringLiteral("战斗")},
        {QStringLiteral("Layer2"), QStringLiteral("骑乘")},
        {QStringLiteral("Layer3"), QStringLiteral("瞄准")},
        {QStringLiteral("Layer4"), QStringLiteral("拾取")},
        {QStringLiteral("Layer5"), QStringLiteral("潜行")},
        {QStringLiteral("Layer6"), QStringLiteral("钓鱼")},
        {QStringLiteral("Layer7"), QStringLiteral("对战")},
        {QStringLiteral("Layer8"), QStringLiteral("团本")},
        {QStringLiteral("Layer9"), QStringLiteral("旅行")},
        {QStringLiteral("Layer10"), QStringLiteral("自定义")},
    };
    const auto it = names.constFind(layerName);
    if (it != names.constEnd())
        return layerName + QStringLiteral(" ") + it.value();
    return layerName;
}
