#include "MappingTypes.h"

#include <QStringList>

QString KeyMapping::describe() const {
    QStringList parts;
    switch (action.type) {
        case MappedAction::Type::KeyboardKey:
            parts << keyCodeToName(action.keyCode);
            break;
        case MappedAction::Type::MouseClick:
            parts << mouseButtonDisplayName(action.mouseButton);
            break;
        case MappedAction::Type::MouseToggle:
            parts << QStringLiteral("长按%1").arg(mouseButtonDisplayName(action.mouseButton));
            break;
        case MappedAction::Type::SwitchLayer:
            parts << QStringLiteral("切换→%1").arg(action.layerName);
            break;
        case MappedAction::Type::MouseMove:
            parts << QStringLiteral("鼠标移动");
            break;
        case MappedAction::Type::LookAround:
            parts << QStringLiteral("视角控制");
            break;
    }
    for (const int sub : subCommands)
        parts << keyCodeToName(sub);
    return parts.join(QStringLiteral("+"));
}

ControllerProfile ControllerProfile::createDefault() {
    ControllerProfile p;
    p.commonLayer.id = QStringLiteral("Common");
    p.commonLayer.name = QStringLiteral("Common");

    // 公共层默认映射
    p.commonLayer.buttonMappings.insert(
        ControllerButton::A, KeyMapping{MappedAction::keyboardKey(AndroidKey::SPACE), {}});
    p.commonLayer.buttonMappings.insert(
        ControllerButton::B, KeyMapping{MappedAction::mouseClick(MouseButton::RIGHT), {}});
    p.commonLayer.buttonMappings.insert(
        ControllerButton::X, KeyMapping{MappedAction::mouseClick(MouseButton::LEFT), {}});
    p.commonLayer.buttonMappings.insert(
        ControllerButton::Y, KeyMapping{MappedAction::keyboardKey(AndroidKey::I), {}});
    p.commonLayer.buttonMappings.insert(
        ControllerButton::MENU, KeyMapping{MappedAction::keyboardKey(AndroidKey::ESCAPE), {}});
    p.commonLayer.buttonMappings.insert(
        ControllerButton::OPTIONS, KeyMapping{MappedAction::keyboardKey(AndroidKey::M), {}});
    p.commonLayer.buttonMappings.insert(
        ControllerButton::RIGHT_STICK_CLICK, KeyMapping{MappedAction::lookAround(), {}});

    // 层切换按键映射（按住激活对应层，松开回公共层），与 triggerButton 保持一致
    struct Trigger {
        const char* name;
        ControllerButton button;
    };
    const Trigger triggers[MAX_LAYERS] = {
        {"Layer1", ControllerButton::DPAD_UP},
        {"Layer2", ControllerButton::DPAD_DOWN},
        {"Layer3", ControllerButton::DPAD_LEFT},
        {"Layer4", ControllerButton::DPAD_RIGHT},
        {"Layer5", ControllerButton::LEFT_SHOULDER},
        {"Layer6", ControllerButton::RIGHT_SHOULDER},
        {"Layer7", ControllerButton::LEFT_STICK_CLICK},
        {"Layer8", ControllerButton::TOUCHPAD_CLICK},
        {"Layer9", ControllerButton::LEFT_TRIGGER_CLICK},
        {"Layer10", ControllerButton::RIGHT_TRIGGER_CLICK},
    };

    for (const Trigger& t : triggers) {
        // 公共层：绑定层切换动作
        p.commonLayer.buttonMappings.insert(
            t.button, KeyMapping{MappedAction::switchLayer(QString::fromLatin1(t.name)), {}});
        // 操作层：triggerButton 仅用于 UI 显示，实际切换由公共层的 SwitchLayer 映射完成
        OperationLayer layer(QString::fromLatin1(t.name));
        layer.name = layerDisplayName(layer.id);
        layer.hasTriggerButton = true;
        layer.triggerButton = t.button;
        p.layers.append(layer);
    }

    return p;
}
