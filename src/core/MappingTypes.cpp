// ============================================================
// MappingTypes.cpp
// 映射数据结构的实现：描述生成 与 默认配置创建
// ------------------------------------------------------------
// 本文件包含两个部分：
//   1. KeyMapping::describe()：把一条映射翻译成人类可读的文本，
//      用于编辑对话框按钮列表/悬浮窗展示（如 "B+Ctrl"）。
//   2. ControllerProfile::createDefault()：生成一份开箱即用的默认
//      配置——一个公共层 + 10 个操作层（只带 WoW 预设显示名，
//      不预设任何按键映射，由用户编辑）。
//
// 分层模型回顾（详见 MappingTypes.h）：
//   - 公共层(Common)：始终处于激活兜底，是配置 SwitchLayer
//     （层切换）映射的常用位置（操作层内亦可设置切换层）。
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
        case MappedAction::Type::WheelUp:
            parts << QStringLiteral("滚轮上滚");
            break;
        case MappedAction::Type::WheelDown:
            parts << QStringLiteral("滚轮下滚");
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
        case MappedAction::Type::ToggleMapping:
            parts << QStringLiteral("切换映射");
            break;
        case MappedAction::Type::ToggleOnScreenKeyboard:
            parts << QStringLiteral("切换屏幕键盘");
            break;
        case MappedAction::Type::ToggleOverlay:
            parts << QStringLiteral("切换悬浮窗");
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
//   - 默认操作集「默认操作集」（Set1）：含公共层 + 10 个空操作层。
//   - 公共层（Common）：绑定常用键（空格/左右键/ESC 等）+ 视角控制，
//     不预设层切换映射，由用户通过 UI 或配置文件自行设置；
//   - 10 个操作层：各自带显示名，具体键位映射留空，由用户编辑。
ControllerProfile ControllerProfile::createDefault() {
    ControllerProfile p;
    p.operationSets.append(OperationSet::createEmpty(QStringLiteral("Set1"),
                                                     QStringLiteral("默认操作集")));
    p.activeOperationSetId = QStringLiteral("Set1");
    OperationSet& set = *p.activeSet();

    // ---- 公共层默认映射 ----
    // 基础常用键：供所有层共享，操作层没有映射的键会回退到这里
    set.commonLayer.buttonMappings.insert(
        ControllerButton::A, KeyMapping{MappedAction::keyboardKey(AndroidKey::SPACE), {}});
    set.commonLayer.buttonMappings.insert(
        ControllerButton::B, KeyMapping{MappedAction::mouseClick(MouseButton::RIGHT), {}});
    set.commonLayer.buttonMappings.insert(
        ControllerButton::X, KeyMapping{MappedAction::mouseClick(MouseButton::LEFT), {}});
    set.commonLayer.buttonMappings.insert(
        ControllerButton::Y, KeyMapping{MappedAction::keyboardKey(AndroidKey::I), {}});
    set.commonLayer.buttonMappings.insert(
        ControllerButton::MENU, KeyMapping{MappedAction::keyboardKey(AndroidKey::ESCAPE), {}});
    set.commonLayer.buttonMappings.insert(
        ControllerButton::OPTIONS, KeyMapping{MappedAction::keyboardKey(AndroidKey::M), {}});
    // 右摇杆按压 -> 视角控制
    set.commonLayer.buttonMappings.insert(
        ControllerButton::RIGHT_STICK_CLICK, KeyMapping{MappedAction::lookAround(), {}});

    return p;
}
