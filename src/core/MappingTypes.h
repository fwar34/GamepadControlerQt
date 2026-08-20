#pragma once

#include "InputTypes.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

// =====================================================================
// 映射数据模型（等效安卓版 MappedAction / KeyMapping / OperationLayer /
// GlobalSettings / ControllerProfile）
// =====================================================================

// 映射动作（等效安卓 sealed class MappedAction）
struct MappedAction {
    enum class Type {
        KeyboardKey,   // 键盘按键
        MouseClick,    // 鼠标点击
        SwitchLayer,   // 切换操作层
        MouseMove,     // 鼠标移动（摇杆动作）
        LookAround,    // 视角控制（摇杆动作）
        MouseToggle    // 鼠标长按（按住期间保持按下）
    };

    Type type = Type::MouseMove;
    int keyCode = 0;                                  // KeyboardKey
    MouseButton mouseButton = MouseButton::LEFT;      // MouseClick / MouseToggle
    QString layerName;                                // SwitchLayer

    static MappedAction keyboardKey(int code) {
        MappedAction a;
        a.type = Type::KeyboardKey;
        a.keyCode = code;
        return a;
    }
    static MappedAction mouseClick(MouseButton b) {
        MappedAction a;
        a.type = Type::MouseClick;
        a.mouseButton = b;
        return a;
    }
    static MappedAction mouseToggle(MouseButton b) {
        MappedAction a;
        a.type = Type::MouseToggle;
        a.mouseButton = b;
        return a;
    }
    static MappedAction switchLayer(const QString& name) {
        MappedAction a;
        a.type = Type::SwitchLayer;
        a.layerName = name;
        return a;
    }
    static MappedAction mouseMove() {
        MappedAction a;
        a.type = Type::MouseMove;
        return a;
    }
    static MappedAction lookAround() {
        MappedAction a;
        a.type = Type::LookAround;
        return a;
    }

    bool operator==(const MappedAction& o) const {
        if (type != o.type) return false;
        switch (type) {
            case Type::KeyboardKey: return keyCode == o.keyCode;
            case Type::MouseClick:
            case Type::MouseToggle: return mouseButton == o.mouseButton;
            case Type::SwitchLayer: return layerName == o.layerName;
            default: return true;
        }
    }
    bool operator!=(const MappedAction& o) const { return !(*this == o); }
};

// 单个按键映射：动作 + 最多 3 个组合子命令（按住主键时依次按下子命令，松开逆序释放）
struct KeyMapping {
    MappedAction action;
    QVector<int> subCommands;

    static constexpr int MAX_SUB_COMMANDS = 3;

    QString describe() const;
    bool operator==(const KeyMapping& o) const {
        return action == o.action && subCommands == o.subCommands;
    }
    bool operator!=(const KeyMapping& o) const { return !(*this == o); }
};

// 操作层（按键映射集合）
class OperationLayer {
public:
    QString name;
    bool hasTriggerButton = false;
    ControllerButton triggerButton = ControllerButton::A;
    QHash<ControllerButton, KeyMapping> buttonMappings;

    OperationLayer() = default;
    explicit OperationLayer(const QString& layerName) : name(layerName) {}

    const KeyMapping* getMapping(ControllerButton b) const {
        const auto it = buttonMappings.constFind(b);
        return it == buttonMappings.constEnd() ? nullptr : &it.value();
    }
    KeyMapping* getMapping(ControllerButton b) {
        auto it = buttonMappings.find(b);
        return it == buttonMappings.end() ? nullptr : &it.value();
    }
};

// 全局设置
struct GlobalSettings {
    float deadzone = 0.15f;          // 摇杆死区
    float lookSensitivity = 0.5f;    // 视角灵敏度
    float cursorSpeed = 1.0f;        // 光标速度（预留）
    float lookSmoothing = 0.5f;      // 视角平滑
    float lookAcceleration = 1.5f;   // 视角加速度曲线
};

// 配置：公共层 + 操作层 + 全局设置
class ControllerProfile {
public:
    OperationLayer commonLayer;
    QVector<OperationLayer> layers;
    GlobalSettings globalSettings;

    static constexpr int MAX_LAYERS = 10;

    OperationLayer* findLayer(const QString& name) {
        if (commonLayer.name == name) return &commonLayer;
        for (OperationLayer& l : layers)
            if (l.name == name) return &l;
        return nullptr;
    }
    const OperationLayer* findLayer(const QString& name) const {
        if (commonLayer.name == name) return &commonLayer;
        for (const OperationLayer& l : layers)
            if (l.name == name) return &l;
        return nullptr;
    }
    // 由触发按键找操作层
    OperationLayer* findLayerByTrigger(ControllerButton b) {
        for (OperationLayer& l : layers)
            if (l.hasTriggerButton && l.triggerButton == b) return &l;
        return nullptr;
    }

    static ControllerProfile createDefault();
};
