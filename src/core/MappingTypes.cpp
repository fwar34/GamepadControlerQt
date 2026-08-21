// ============================================================
// MappingTypes.cpp
// 映射数据结构的实现：描述生成 与 默认配置创建
// ------------------------------------------------------------
// 本文件包含两个部分：
//   1. KeyMapping::describe()：把一条映射翻译成人类可读的文本，
//      用于编辑对话框按钮列表/悬浮窗展示（如 "B+Ctrl"）。
//   2. ControllerProfile::createDefault()：生成一份开箱即用的默认
//      配置——一个公共层 + 10 个由方向键/肩键/摇杆按压等触发的
//      操作层（与安卓版 WoW 动作集预设保持一致）。
//
// 分层模型回顾（详见 MappingTypes.h）：
//   - 公共层(Common)：始终处于激活兜底，也是唯一允许定义
//     SwitchLayer（层切换）映射的层。
//   - 操作层(Layer1~10)：由公共层里的 SwitchLayer 映射"按住激活/
//     松开回退"临时叠加，查询顺序为激活层栈顶 -> ... -> 公共层。
// ============================================================

#include "MappingTypes.h"

#include <QStringList>

// ============================================================
// KeyMapping::describe：把一条映射描述为可读文本
// ============================================================
// 格式：主动作 [+ 子命令1] [+ 子命令2] [+ 子命令3]
// 例如：键盘按键"B" + 子命令 Ctrl 会显示为 "B+Ctrl"（组合键）。
// 该文本只用于 UI 展示，与配置文件里的结构化字段无关。
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
    // 追加子命令（组合键），同样翻译成可读键名
    for (const int sub : subCommands)
        parts << keyCodeToName(sub);
    return parts.join(QStringLiteral("+"));
}

// ============================================================
// ControllerProfile::createDefault：创建默认配置
// ============================================================
// 返回一份全新的默认 ControllerProfile：
//   - 公共层（Common）：绑定常用键（空格/左右键/ESC 等）+ 视角控制，
//     以及 10 个"层切换"映射；
//   - 10 个操作层：各自带显示名与触发按键信息（triggerButton），
//     具体键位映射留空，由用户通过 UI 编辑。
ControllerProfile ControllerProfile::createDefault() {
    ControllerProfile p;
    // 公共层：id 与 name 均为 "Common"（id 唯一固定，name 可改）
    p.commonLayer.id = QStringLiteral("Common");
    p.commonLayer.name = QStringLiteral("Common");

    // ---- 公共层默认映射 ----
    // 基础常用键：供所有层共享，操作层没有映射的键会回退到这里
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
    // 右摇杆按压 -> 视角控制
    p.commonLayer.buttonMappings.insert(
        ControllerButton::RIGHT_STICK_CLICK, KeyMapping{MappedAction::lookAround(), {}});

    // ---- 层切换按键映射 ----
    // 公共层为每个操作层绑定一个"按住激活"的 SwitchLayer 映射：
    // 按住对应手柄按键即激活该层，松开自动回退公共层。
    // 注意：OperationLayer 里的 triggerButton 仅用于 UI 显示，
    // 实际切换动作完全由这里公共层的 SwitchLayer 映射完成。
    struct Trigger {
        const char* name;          // 层名（同时作为默认 id）
        ControllerButton button;   // 触发按键
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
        layer.name = layerDisplayName(layer.id);   // 显示名带中文别名（如 "Layer1 战斗"）
        layer.hasTriggerButton = true;
        layer.triggerButton = t.button;
        p.layers.append(layer);
    }

    return p;
}
