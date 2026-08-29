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

// 【C++ 语法】#include 预处理指令：把双引号包裹的 "InputTypes.h" 内容原样插入本文件，
// 以便使用其中声明的枚举、函数与类型。双引号表示优先在源文件所在目录查找该头文件。
#include "InputTypes.h"

// 【C++ 语法】#include 预处理指令：引入 Qt 的 QHash（哈希表容器）头文件，
// 供 mouseButtonFromName、layerDisplayName 中做键值映射查找。尖括号表示在编译器的包含目录中查找。
#include <QHash>

// ============================================================
// controllerButtonName：手柄按钮 -> 序列化名（大写英文）
// ============================================================
// 该名称写入配置文件与悬浮窗过滤逻辑，须与安卓版保持一致的枚举名
//（如 A/B/X/Y、LB/RB、LT/RT、L3/R3、MENU/OPTIONS、DPAD_* 等）。
// 【C++ 语法】函数定义：返回类型 QString（Qt 字符串类）；函数名 controllerButtonName；
// 形参 b 按值接收一个 ControllerButton 枚举；函数体由一对花括号 {} 包裹。
QString controllerButtonName(ControllerButton b) { // 定义"手柄按钮 -> 序列化名"转换函数，函数体开始
    switch (b) { // 【C++ 语法】switch 多分支语句：根据 b 的枚举值跳转到对应 case 分支执行
        // 【C++ 语法】case 标签：匹配枚举值 ControllerButton::A；QStringLiteral() 是 Qt 宏，
        // 编译期生成 QString 字面量（避免运行时重复分配内存）；return 立即返回结果并结束函数。
        case ControllerButton::A: return QStringLiteral("A"); // 枚举 A 序列化为字符串 "A"
        case ControllerButton::B: return QStringLiteral("B"); // 枚举 B 序列化为字符串 "B"
        case ControllerButton::X: return QStringLiteral("X"); // 枚举 X 序列化为字符串 "X"
        case ControllerButton::Y: return QStringLiteral("Y"); // 枚举 Y 序列化为字符串 "Y"
        case ControllerButton::LEFT_SHOULDER: return QStringLiteral("LB"); // 左肩键(LB) 序列化为 "LB"
        case ControllerButton::RIGHT_SHOULDER: return QStringLiteral("RB"); // 右肩键(RB) 序列化为 "RB"
        case ControllerButton::LEFT_TRIGGER_CLICK: return QStringLiteral("LT"); // 左扳机(LT) 序列化为 "LT"
        case ControllerButton::RIGHT_TRIGGER_CLICK: return QStringLiteral("RT"); // 右扳机(RT) 序列化为 "RT"
        case ControllerButton::LEFT_STICK_CLICK: return QStringLiteral("L3"); // 左摇杆按下(L3) 序列化为 "L3"
        case ControllerButton::RIGHT_STICK_CLICK: return QStringLiteral("R3"); // 右摇杆按下(R3) 序列化为 "R3"
        case ControllerButton::MENU: return QStringLiteral("MENU"); // 菜单键(MENU) 序列化为 "MENU"
        case ControllerButton::OPTIONS: return QStringLiteral("OPTIONS"); // 视图键(OPTIONS) 序列化为 "OPTIONS"
        case ControllerButton::GUIDE: return QStringLiteral("GUIDE"); // Home 键(GUIDE) 序列化为 "GUIDE"
        case ControllerButton::DPAD_UP: return QStringLiteral("DPAD_UP"); // 方向键上 序列化为 "DPAD_UP"
        case ControllerButton::DPAD_DOWN: return QStringLiteral("DPAD_DOWN"); // 方向键下 序列化为 "DPAD_DOWN"
        case ControllerButton::DPAD_LEFT: return QStringLiteral("DPAD_LEFT"); // 方向键左 序列化为 "DPAD_LEFT"
        case ControllerButton::DPAD_RIGHT: return QStringLiteral("DPAD_RIGHT"); // 方向键右 序列化为 "DPAD_RIGHT"
        case ControllerButton::TOUCHPAD_CLICK: return QStringLiteral("TOUCHPAD_CLICK"); // 触摸板点击 序列化为 "TOUCHPAD_CLICK"
    } // switch 语句结束（花括号闭合）
    return QString(); // 【C++ 语法】return 语句：返回默认构造的空字符串 QString()；switch 未覆盖全部枚举值时的兜底
} // 函数定义结束（花括号闭合）

// ============================================================
// controllerButtonFromName：序列化名 -> 手柄按钮（解析用）
// ============================================================
// 线性遍历全部按钮逐一比对，成功时写入 out 并返回 true；
// 失败返回 false（例如配置里出现了未知按钮名）。
// 【C++ 语法】函数定义：返回类型 bool（布尔值）；参数 name 是 const QString&（对 QString 的常引用，
// 只读访问且避免拷贝）；参数 out 是 ControllerButton*（指针，用于把结果写回调用者提供的变量）。
bool controllerButtonFromName(const QString& name, ControllerButton* out) { // 定义"序列化名 -> 手柄按钮"解析函数，函数体开始
    for (const ControllerButton b : allControllerButtons()) { // 【C++ 语法】基于范围的 for 循环：遍历 allControllerButtons() 返回的按钮集合，b 是每次迭代的当前元素（const 修饰为只读）
        if (controllerButtonName(b) == name) { // 【C++ 语法】if 条件判断：把按钮 b 序列化为名字并与目标 name 比较是否相等（==）
            *out = b; // 【C++ 语法】*out 为解引用指针，把匹配到的按钮通过赋值写入调用者的变量
            return true; // 找到匹配，返回 true 表示解析成功并结束函数
        } // if 语句结束（花括号闭合）
    } // for 循环结束（花括号闭合）
    return false; // 遍历完所有按钮仍未匹配，返回 false 表示解析失败
} // 函数定义结束（花括号闭合）

// ============================================================
// controllerButtonDisplayName：手柄按钮 -> 中文展示名
// ============================================================
// 仅用于 UI 显示（编辑对话框按钮列表、悬浮窗按键提示），
// 不参与配置序列化，因此可以放心使用中文。
// 【C++ 语法】函数定义：返回 QString，参数 b 按值接收 ControllerButton 枚举；
// 内部同样是 switch 多分支映射，将每个枚举值映射为中文显示名。
QString controllerButtonDisplayName(ControllerButton b) { // 定义"手柄按钮 -> 中文展示名"转换函数，函数体开始
    switch (b) { // 【C++ 语法】switch 多分支语句：根据 b 的枚举值选择对应分支
        case ControllerButton::A: return QStringLiteral("A键"); // 枚举 A 显示为 "A键"
        case ControllerButton::B: return QStringLiteral("B键"); // 枚举 B 显示为 "B键"
        case ControllerButton::X: return QStringLiteral("X键"); // 枚举 X 显示为 "X键"
        case ControllerButton::Y: return QStringLiteral("Y键"); // 枚举 Y 显示为 "Y键"
        case ControllerButton::LEFT_SHOULDER: return QStringLiteral("LB肩键"); // 左肩键显示为 "LB肩键"
        case ControllerButton::RIGHT_SHOULDER: return QStringLiteral("RB肩键"); // 右肩键显示为 "RB肩键"
        case ControllerButton::LEFT_TRIGGER_CLICK: return QStringLiteral("LT扳机"); // 左扳机显示为 "LT扳机"
        case ControllerButton::RIGHT_TRIGGER_CLICK: return QStringLiteral("RT扳机"); // 右扳机显示为 "RT扳机"
        case ControllerButton::LEFT_STICK_CLICK: return QStringLiteral("L3摇杆按下"); // 左摇杆按下显示为 "L3摇杆按下"
        case ControllerButton::RIGHT_STICK_CLICK: return QStringLiteral("R3摇杆按下"); // 右摇杆按下显示为 "R3摇杆按下"
        case ControllerButton::MENU: return QStringLiteral("菜单键"); // 菜单键显示为 "菜单键"
        case ControllerButton::OPTIONS: return QStringLiteral("视图键"); // 视图键显示为 "视图键"
        case ControllerButton::GUIDE: return QStringLiteral("Home键"); // Home 键显示为 "Home键"
        case ControllerButton::DPAD_UP: return QStringLiteral("方向键上"); // 方向键上显示为 "方向键上"
        case ControllerButton::DPAD_DOWN: return QStringLiteral("方向键下"); // 方向键下显示为 "方向键下"
        case ControllerButton::DPAD_LEFT: return QStringLiteral("方向键左"); // 方向键左显示为 "方向键左"
        case ControllerButton::DPAD_RIGHT: return QStringLiteral("方向键右"); // 方向键右显示为 "方向键右"
        case ControllerButton::TOUCHPAD_CLICK: return QStringLiteral("触控板点击"); // 触摸板点击显示为 "触控板点击"
    } // switch 语句结束（花括号闭合）
    return QString(); // 未匹配任何枚举值时返回空字符串（兜底）
} // 函数定义结束（花括号闭合）

// ============================================================
// allControllerButtons：返回所有可映射的手柄按钮集合
// ============================================================
// 供 UI 遍历按钮列表、以及 controllerButtonFromName 遍历比对使用。
// GUIDE / TOUCHPAD_CLICK 等按键即使 XInput 无对应物理位，也保留在
// 枚举中，兼容安卓配置文件。
// 【C++ 语法】函数定义：返回类型 QVector<ControllerButton>（Qt 的动态数组容器，元素类型为 ControllerButton 枚举）。
QVector<ControllerButton> allControllerButtons() { // 定义"返回全部手柄按钮集合"的函数，函数体开始
    return { // 【C++ 语法】return 返回花括号初始化列表 {}：编译器用列表里的元素构造 QVector 容器
        ControllerButton::A, // 加入按钮 A
        ControllerButton::B, // 加入按钮 B
        ControllerButton::X, // 加入按钮 X
        ControllerButton::Y, // 加入按钮 Y
        ControllerButton::LEFT_SHOULDER, // 加入左肩键
        ControllerButton::RIGHT_SHOULDER, // 加入右肩键
        ControllerButton::LEFT_TRIGGER_CLICK, // 加入左扳机
        ControllerButton::RIGHT_TRIGGER_CLICK, // 加入右扳机
        ControllerButton::LEFT_STICK_CLICK, // 加入左摇杆按下
        ControllerButton::RIGHT_STICK_CLICK, // 加入右摇杆按下
        ControllerButton::MENU, // 加入菜单键
        ControllerButton::OPTIONS, // 加入视图键
        ControllerButton::DPAD_UP, // 加入方向键上
        ControllerButton::DPAD_DOWN, // 加入方向键下
        ControllerButton::DPAD_LEFT, // 加入方向键左
        ControllerButton::DPAD_RIGHT, // 加入方向键右
        ControllerButton::GUIDE, // 加入 Home 键（兼容安卓配置）
        ControllerButton::TOUCHPAD_CLICK, // 加入触摸板点击（兼容安卓配置）
    }; // 初始化列表结束，分号结束 return 语句
} // 函数定义结束（花括号闭合）

// ============================================================
// mouseButtonName：鼠标键 -> 序列化名（大写）
// ============================================================
// 与安卓版枚举名保持一致（大写），保证配置文件兼容。
// 【C++ 语法】函数定义：返回 QString，参数 b 按值接收 MouseButton 枚举；
// switch 把每个鼠标键枚举映射为对应的大写序列化名。
QString mouseButtonName(MouseButton b) { // 定义"鼠标键 -> 序列化名"转换函数，函数体开始
    switch (b) { // 【C++ 语法】switch 多分支语句：根据 b 的枚举值选择对应分支
        case MouseButton::LEFT: return QStringLiteral("LEFT"); // 左键序列化为 "LEFT"
        case MouseButton::RIGHT: return QStringLiteral("RIGHT"); // 右键序列化为 "RIGHT"
        case MouseButton::MIDDLE: return QStringLiteral("MIDDLE"); // 中键序列化为 "MIDDLE"
        case MouseButton::FORWARD: return QStringLiteral("FORWARD"); // 前进键序列化为 "FORWARD"
        case MouseButton::BACK: return QStringLiteral("BACK"); // 后退键序列化为 "BACK"
    } // switch 语句结束（花括号闭合）
    return QString(); // 未匹配任何枚举值时返回空字符串（兜底）
} // 函数定义结束（花括号闭合）

// ============================================================
// mouseButtonFromName：鼠标键名 -> 枚举（解析用）
// ============================================================
// 兼容大写（安卓格式）与小写：统一转大写后查表。
// 【C++ 语法】函数定义：返回 bool；参数 name 是 const QString&（常引用，只读避免拷贝），
// 参数 out 是 MouseButton*（指针，用于把解析结果写回调用者的变量）。
bool mouseButtonFromName(const QString& name, MouseButton* out) { // 定义"鼠标键名 -> 枚举"解析函数，函数体开始
    const QString key = name.toUpper(); // 【C++ 语法】const 声明只读常量；name.toUpper() 是 QString 的成员函数，返回全大写的新字符串（兼容大小写输入）
    const QHash<QString, MouseButton> map = { // 【C++ 语法】QHash 是 Qt 的哈希表容器（键值对）；const 使其只读；花括号初始化列表一次性填充映射内容
        {QStringLiteral("LEFT"), MouseButton::LEFT}, // 键 "LEFT" 映射到枚举 MouseButton::LEFT
        {QStringLiteral("RIGHT"), MouseButton::RIGHT}, // 键 "RIGHT" 映射到枚举 MouseButton::RIGHT
        {QStringLiteral("MIDDLE"), MouseButton::MIDDLE}, // 键 "MIDDLE" 映射到枚举 MouseButton::MIDDLE
        {QStringLiteral("FORWARD"), MouseButton::FORWARD}, // 键 "FORWARD" 映射到枚举 MouseButton::FORWARD
        {QStringLiteral("BACK"), MouseButton::BACK}, // 键 "BACK" 映射到枚举 MouseButton::BACK
    }; // QHash 初始化列表结束，分号结束声明语句
    const auto it = map.constFind(key); // 【C++ 语法】auto 让编译器自动推导迭代器类型；map.constFind() 返回只读迭代器查找键 key，找不到时返回 constEnd()
    if (it != map.constEnd()) { // 【C++ 语法】if 判断：迭代器是否未指向末尾，即键 key 是否在表中存在
        *out = it.value(); // 【C++ 语法】it.value() 取出迭代器所指键对应的值（枚举），*out 解引用指针把结果写回调用者
        return true; // 查表命中，返回 true 表示解析成功
    } // if 语句结束（花括号闭合）
    return false; // 未命中（未知的鼠标键名），返回 false 表示解析失败
} // 函数定义结束（花括号闭合）

// ============================================================
// mouseButtonDisplayName：鼠标键 -> 中文展示名
// ============================================================
// 【C++ 语法】函数定义：返回 QString，参数 b 按值接收 MouseButton 枚举；
// switch 把每个鼠标键枚举映射为中文显示名（仅用于界面显示）。
QString mouseButtonDisplayName(MouseButton b) { // 定义"鼠标键 -> 中文展示名"转换函数，函数体开始
    switch (b) { // 【C++ 语法】switch 多分支语句：根据 b 的枚举值选择对应分支
        case MouseButton::LEFT: return QStringLiteral("鼠标左键"); // 左键显示为 "鼠标左键"
        case MouseButton::RIGHT: return QStringLiteral("鼠标右键"); // 右键显示为 "鼠标右键"
        case MouseButton::MIDDLE: return QStringLiteral("鼠标中键"); // 中键显示为 "鼠标中键"
        case MouseButton::FORWARD: return QStringLiteral("鼠标前进键"); // 前进键显示为 "鼠标前进键"
        case MouseButton::BACK: return QStringLiteral("鼠标后退键"); // 后退键显示为 "鼠标后退键"
    } // switch 语句结束（花括号闭合）
    return QString(); // 未匹配任何枚举值时返回空字符串（兜底）
} // 函数定义结束（花括号闭合）

// ============================================================
// keyCodeToName：Android KeyCode -> 展示名
// ============================================================
// 用于 KeyMapping::describe()（编辑对话框/按钮列表里的映射描述）。
// 先处理有规律的区间（字母/数字/F1-F12/小键盘），再处理散键。
// 【C++ 语法】函数定义：返回 QString，参数 keyCode 按值接收 int（Android 键码数值）。
QString keyCodeToName(int keyCode) { // 定义"Android 键码 -> 可读名称"转换函数，函数体开始
    // 字母 A-Z：Android KeyCode 29~54 连续对应 ASCII 'A'~'Z'
    if (keyCode >= 29 && keyCode <= 54) // 【C++ 语法】if 条件：逻辑与 && 同时满足"大于等于29"和"小于等于54"两个边界判断
        return QString(QChar('A' + (keyCode - 29))); // 【C++ 语法】QChar 为 Qt 字符类；'A'+偏移量得到目标字母字符，再用 QString(...) 构造字符串返回
    // 数字 0-9：Android KeyCode 7~16 连续对应 '0'~'9'
    if (keyCode >= 7 && keyCode <= 16) // 判断键码是否落在数字键 0-9 的连续区间（7~16）
        return QString(QChar('0' + (keyCode - 7))); // '0' 加上偏移量得到对应数字字符，构造成 QString 返回
    // 功能键 F1-F12：Android KeyCode 131~142 连续
    if (keyCode >= 131 && keyCode <= 142) // 判断键码是否落在功能键 F1-F12 的连续区间（131~142）
        return QStringLiteral("F%1").arg(keyCode - 131 + 1); // 【C++ 语法】QString 的 arg() 方法：把 "%1" 占位符替换为参数，生成 "F1"~"F12"
    // 小键盘 0-9：Android KeyCode 144~153 连续
    if (keyCode >= 144 && keyCode <= 153) // 判断键码是否落在小键盘 0-9 的连续区间（144~153）
        return QStringLiteral("Num%1").arg(keyCode - 144); // 用 arg() 把 "%1" 替换为小键盘数字，生成 "Num0"~"Num9"

    // 其余散键逐一映射为易读名称
    switch (keyCode) { // 【C++ 语法】switch 多分支语句：对无法用区间公式处理的散键键码逐一匹配
        case AndroidKey::SPACE: return QStringLiteral("Space"); // 空格键显示为 "Space"
        case AndroidKey::ENTER: return QStringLiteral("Enter"); // 回车键显示为 "Enter"
        case AndroidKey::TAB: return QStringLiteral("Tab"); // 制表键显示为 "Tab"
        case AndroidKey::ESCAPE: return QStringLiteral("Esc"); // 退出键显示为 "Esc"
        case AndroidKey::BACK: return QStringLiteral("Back"); // 返回键显示为 "Back"
        case AndroidKey::DEL: return QStringLiteral("Backspace"); // 删除键显示为 "Backspace"
        case AndroidKey::INSERT: return QStringLiteral("Insert"); // 插入键显示为 "Insert"
        case AndroidKey::HOME: return QStringLiteral("Home"); // Home 键显示为 "Home"
        case AndroidKey::PAGE_UP: return QStringLiteral("PageUp"); // 向上翻页显示为 "PageUp"
        case AndroidKey::PAGE_DOWN: return QStringLiteral("PageDown"); // 向下翻页显示为 "PageDown"
        case AndroidKey::MOVE_END: return QStringLiteral("End"); // 行尾键显示为 "End"
        case AndroidKey::SHIFT_LEFT: // 【C++ 语法】case 标签不加 return：代码会"穿透"（fall-through）继续执行下一 case 的语句
        case AndroidKey::SHIFT_RIGHT: return QStringLiteral("Shift"); // 左右 Shift 统一显示为 "Shift"
        case AndroidKey::CTRL_LEFT: // 左 Ctrl 分支穿透到下一行
        case AndroidKey::CTRL_RIGHT: return QStringLiteral("Ctrl"); // 左右 Ctrl 统一显示为 "Ctrl"
        case AndroidKey::ALT_LEFT: // 左 Alt 分支穿透到下一行
        case AndroidKey::ALT_RIGHT: return QStringLiteral("Alt"); // 左右 Alt 统一显示为 "Alt"
        case AndroidKey::DPAD_UP: return QStringLiteral("↑"); // 方向键上显示为上箭头符号 "↑"
        case AndroidKey::DPAD_DOWN: return QStringLiteral("↓"); // 方向键下显示为下箭头符号 "↓"
        case AndroidKey::DPAD_LEFT: return QStringLiteral("←"); // 方向键左显示为左箭头符号 "←"
        case AndroidKey::DPAD_RIGHT: return QStringLiteral("→"); // 方向键右显示为右箭头符号 "→"
        case AndroidKey::MINUS: return QStringLiteral("-"); // 减号键显示为 "-"
        case AndroidKey::EQUALS: return QStringLiteral("="); // 等号键显示为 "="
        case AndroidKey::LEFT_BRACKET: return QStringLiteral("["); // 左方括号显示为 "["
        case AndroidKey::RIGHT_BRACKET: return QStringLiteral("]"); // 右方括号显示为 "]"
        case AndroidKey::BACKSLASH: return QStringLiteral("\\"); // 反斜杠键显示为 "\\"
        case AndroidKey::SEMICOLON: return QStringLiteral(";"); // 分号键显示为 ";"
        case AndroidKey::APOSTROPHE: return QStringLiteral("'"); // 撇号键显示为 "'"
        case AndroidKey::COMMA: return QStringLiteral(","); // 逗号键显示为 ","
        case AndroidKey::PERIOD: return QStringLiteral("."); // 句号键显示为 "."
        case AndroidKey::SLASH: return QStringLiteral("/"); // 斜杠键显示为 "/"
        case AndroidKey::GRAVE: return QStringLiteral("`"); // 反引号键显示为 "`"
        case AndroidKey::CAPS_LOCK: return QStringLiteral("CapsLock"); // 大写锁定键显示为 "CapsLock"
        case AndroidKey::NUM_LOCK: return QStringLiteral("NumLock"); // 数字锁定键显示为 "NumLock"
        case AndroidKey::SCROLL_LOCK: return QStringLiteral("ScrollLock"); // 滚动锁定键显示为 "ScrollLock"
        default: // 【C++ 语法】default 分支：switch 中没有任何 case 匹配时执行的兜底分支
            return QStringLiteral("Key(%1)").arg(keyCode); // 未识别的键码显示为 "Key(数字)"，arg() 替换 "%1" 占位符
    } // switch 语句结束（花括号闭合）
} // 函数定义结束（花括号闭合）

// ============================================================
// layerDisplayName：层名 -> 带预设中文别名的展示名
// ============================================================
// 与安卓版 WoW 动作集预设（WoWActionSets.LAYER_NAMES）保持一致，
// 例如 "Layer1" 显示为 "Layer1 战斗"。非预设层名原样返回。
// 【C++ 语法】函数定义：返回 QString，参数 layerName 是 const QString&（常引用，只读避免拷贝）。
QString layerDisplayName(const QString& layerName) { // 定义"层名 -> 带中文别名展示名"的函数，函数体开始
    static const QHash<QString, QString> names = { // 【C++ 语法】static const 局部静态常量：只初始化一次，函数多次调用共享同一份数据；
        // 类型为 QHash<QString,QString>（字符串键 -> 字符串值），花括号初始化列表填充内容。
        {QStringLiteral("Layer1"), QStringLiteral("战斗")}, // 层 Layer1 对应中文别名 "战斗"
        {QStringLiteral("Layer2"), QStringLiteral("骑乘")}, // 层 Layer2 对应中文别名 "骑乘"
        {QStringLiteral("Layer3"), QStringLiteral("瞄准")}, // 层 Layer3 对应中文别名 "瞄准"
        {QStringLiteral("Layer4"), QStringLiteral("拾取")}, // 层 Layer4 对应中文别名 "拾取"
        {QStringLiteral("Layer5"), QStringLiteral("潜行")}, // 层 Layer5 对应中文别名 "潜行"
        {QStringLiteral("Layer6"), QStringLiteral("钓鱼")}, // 层 Layer6 对应中文别名 "钓鱼"
        {QStringLiteral("Layer7"), QStringLiteral("对战")}, // 层 Layer7 对应中文别名 "对战"
        {QStringLiteral("Layer8"), QStringLiteral("团本")}, // 层 Layer8 对应中文别名 "团本"
        {QStringLiteral("Layer9"), QStringLiteral("旅行")}, // 层 Layer9 对应中文别名 "旅行"
        {QStringLiteral("Layer10"), QStringLiteral("自定义")}, // 层 Layer10 对应中文别名 "自定义"
    }; // QHash 初始化列表结束，分号结束声明语句
    const auto it = names.constFind(layerName); // 【C++ 语法】auto 自动推导迭代器类型；names.constFind() 返回只读迭代器查找层名，找不到返回 constEnd()
    if (it != names.constEnd()) // 判断迭代器是否未指向末尾，即层名是否在预设表中
        return layerName + QStringLiteral(" ") + it.value(); // 【C++ 语法】QString 支持 + 运算符拼接：层名 + 空格 + 中文别名，组成 "Layer1 战斗"
    return layerName; // 非预设层名：原样返回层名本身
} // 函数定义结束（花括号闭合）
