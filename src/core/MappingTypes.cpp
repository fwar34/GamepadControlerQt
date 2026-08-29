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

// 【C++ 语法】#include "..."：包含本文件对应的头文件，把 MappingTypes.h 中声明的类与函数定义引入。
#include "MappingTypes.h"

// 【Qt】#include <QStringList>：尖括号形式包含 Qt 库头文件；QStringList 用于 describe() 中收集展示片段。
#include <QStringList>

// ============================================================
// KeyMapping::describe：把一条映射描述为可读文本
// ============================================================
// 格式：主动作 [+ 子命令1] [+ 子命令2] [+ 子命令3]
// 例如：键盘按键"B" + 子命令 Ctrl 会显示为 "B+Ctrl"（组合键）。
// 该文本只用于 UI 展示，与配置文件里的结构化字段无关。
// 【C++ 语法】类外定义成员函数："类名::函数名"；const 与 .h 声明保持一致，表示不修改对象。
// 返回值 QString 为 Qt 字符串（按值返回，内部隐式共享）。
QString KeyMapping::describe() const {      // 实现 const 成员函数：生成该映射的可读描述文本
    QStringList parts;                      // 收集各展示片段的字符串列表
    switch (action.type) {                  // 【C++ 语法】switch：按主动作类型分支处理
        case MappedAction::Type::KeyboardKey:    // 键盘按键：显示键名
            parts << keyCodeToName(action.keyCode);   // 【Qt】<< 追加元素；keyCodeToName() 把键码翻译成键名
            break;                                  // 【C++ 语法】break：结束当前分支，防止贯穿到下一 case
        case MappedAction::Type::MouseClick:        // 鼠标单击：显示鼠标按钮名
            parts << mouseButtonDisplayName(action.mouseButton);   // 鼠标按钮转显示名
            break;                                              // 跳出 switch
        case MappedAction::Type::MouseToggle:       // 鼠标长按锁存：显示"长按+按钮名"
            parts << QStringLiteral("长按%1").arg(mouseButtonDisplayName(action.mouseButton));   // 【Qt】arg() 把 %1 替换为按钮名
            break;                                              // 跳出 switch
        case MappedAction::Type::WheelUp:           // 滚轮上滚
            parts << QStringLiteral("滚轮上滚");    // 追加固定文本"滚轮上滚"
            break;                                  // 跳出 switch
        case MappedAction::Type::WheelDown:         // 滚轮下滚
            parts << QStringLiteral("滚轮下滚");    // 追加固定文本"滚轮下滚"
            break;                                  // 跳出 switch
        case MappedAction::Type::SwitchLayer:       // 切换操作层：显示"切换→层名"
            parts << QStringLiteral("切换→%1").arg(action.layerName);   // 【Qt】arg() 把 %1 替换为目标层名
            break;                                  // 跳出 switch
        case MappedAction::Type::MouseMove:         // 鼠标移动
            parts << QStringLiteral("鼠标移动");    // 追加固定文本"鼠标移动"
            break;                                  // 跳出 switch
        case MappedAction::Type::LookAround:        // 视角控制
            parts << QStringLiteral("视角控制");    // 追加固定文本"视角控制"
            break;                                  // 跳出 switch
        case MappedAction::Type::ToggleMapping:     // 切换映射
            parts << QStringLiteral("切换映射");    // 追加固定文本"切换映射"
            break;                                  // 跳出 switch
        case MappedAction::Type::ToggleOnScreenKeyboard:   // 切换屏幕键盘
            parts << QStringLiteral("切换屏幕键盘");        // 追加固定文本"切换屏幕键盘"
            break;                                         // 跳出 switch
        case MappedAction::Type::ToggleOverlay:     // 切换悬浮窗
            parts << QStringLiteral("切换悬浮窗");  // 追加固定文本"切换悬浮窗"
            break;                                  // 跳出 switch
    }
    // 追加子命令（组合键），同样翻译成可读键名
    for (const int sub : subCommands)       // 【C++ 语法】范围 for（range-based for）：sub 依次取子命令列表中的每个键码
        parts << keyCodeToName(sub);        // 把子命令键码翻译成键名，追加到列表
    return parts.join(QStringLiteral("+")); // 【Qt】join("+")：用"+"把列表拼接成 "B+Ctrl" 形式的字符串返回
}

// ============================================================
// ControllerProfile::createDefault：创建默认配置
// ============================================================
// 返回一份全新的默认 ControllerProfile：
//   - 默认操作集「默认操作集」（Set1）：含公共层 + 10 个空操作层。
//   - 公共层（Common）：绑定常用键（空格/左右键/ESC 等）+ 视角控制，
//     不预设层切换映射，由用户通过 UI 或配置文件自行设置；
//   - 10 个操作层：各自带显示名，具体键位映射留空，由用户编辑。
// 【C++ 语法】返回值类型为类类型 ControllerProfile：按值返回整个配置对象。
ControllerProfile ControllerProfile::createDefault() {   // 实现静态工厂方法：生成默认配置
    ControllerProfile p;                                 // 创建空配置对象
    p.operationSets.append(OperationSet::createEmpty(QStringLiteral("Set1"),
                                                     QStringLiteral("默认操作集")));
    // 【Qt】QStringLiteral：编译期把字符串字面量预转为 QString，避免运行时转换开销；
    // append() 追加：创建 id="Set1"/显示名"默认操作集"的空操作集（含公共层 + 10 个空操作层）。
    p.activeOperationSetId = QStringLiteral("Set1");     // 把默认激活操作集 id 设为 "Set1"
    OperationSet& set = *p.activeSet();                  // 【C++ 语法】引用 &：对 activeSet() 返回的指针解引用并取别名，之后直接操作 set

    // ---- 公共层默认映射 ----
    // 基础常用键：供所有层共享，操作层没有映射的键会回退到这里
    // 【C++ 语法】花括号初始化 {}：KeyMapping{主动作, 子命令列表} 聚合初始化；子命令传 {} 表示空列表（QVector 默认构造）。
    set.commonLayer.buttonMappings.insert(               // 【Qt】QHash::insert(键, 值)：向公共层映射表插入 按钮->映射
        ControllerButton::A, KeyMapping{MappedAction::keyboardKey(AndroidKey::SPACE), {}});   // A 键 -> 键盘"空格"
    set.commonLayer.buttonMappings.insert(               // 继续插入公共层映射
        ControllerButton::B, KeyMapping{MappedAction::mouseClick(MouseButton::RIGHT), {}});   // B 键 -> 鼠标右键单击
    set.commonLayer.buttonMappings.insert(               // 继续插入公共层映射
        ControllerButton::X, KeyMapping{MappedAction::mouseClick(MouseButton::LEFT), {}});    // X 键 -> 鼠标左键单击
    set.commonLayer.buttonMappings.insert(               // 继续插入公共层映射
        ControllerButton::Y, KeyMapping{MappedAction::keyboardKey(AndroidKey::I), {}});       // Y 键 -> 键盘"I"
    set.commonLayer.buttonMappings.insert(               // 继续插入公共层映射
        ControllerButton::MENU, KeyMapping{MappedAction::keyboardKey(AndroidKey::ESCAPE), {}});   // MENU 键 -> 键盘"ESC"
    set.commonLayer.buttonMappings.insert(               // 继续插入公共层映射
        ControllerButton::OPTIONS, KeyMapping{MappedAction::keyboardKey(AndroidKey::M), {}});     // OPTIONS 键 -> 键盘"M"
    // 右摇杆按压 -> 视角控制
    set.commonLayer.buttonMappings.insert(               // 继续插入公共层映射
        ControllerButton::RIGHT_STICK_CLICK, KeyMapping{MappedAction::lookAround(), {}});    // 右摇杆按压 -> 视角控制（配合鼠标移动实现视角转向）

    return p;                                            // 返回生成的默认配置
}
