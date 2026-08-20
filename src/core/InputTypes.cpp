#include "InputTypes.h"

#include <QHash>

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

bool controllerButtonFromName(const QString& name, ControllerButton* out) {
    for (const ControllerButton b : allControllerButtons()) {
        if (controllerButtonName(b) == name) {
            *out = b;
            return true;
        }
    }
    return false;
}

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

QString mouseButtonName(MouseButton b) {
    // 与安卓版枚举名保持一致（大写），保证配置文件兼容
    switch (b) {
        case MouseButton::LEFT: return QStringLiteral("LEFT");
        case MouseButton::RIGHT: return QStringLiteral("RIGHT");
        case MouseButton::MIDDLE: return QStringLiteral("MIDDLE");
        case MouseButton::FORWARD: return QStringLiteral("FORWARD");
        case MouseButton::BACK: return QStringLiteral("BACK");
    }
    return QString();
}

bool mouseButtonFromName(const QString& name, MouseButton* out) {
    // 兼容大写（安卓格式）与小写
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

QString keyCodeToName(int keyCode) {
    if (keyCode >= 29 && keyCode <= 54)
        return QString(QChar('A' + (keyCode - 29)));
    if (keyCode >= 7 && keyCode <= 16)
        return QString(QChar('0' + (keyCode - 7)));
    if (keyCode >= 131 && keyCode <= 142)
        return QStringLiteral("F%1").arg(keyCode - 131 + 1);
    if (keyCode >= 144 && keyCode <= 153)
        return QStringLiteral("Num%1").arg(keyCode - 144);

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

QString layerDisplayName(const QString& layerName) {
    // WoW 动作集预设显示名（与安卓版 WoWActionSets.LAYER_NAMES 一致）
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
