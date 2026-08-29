// 【C++ 语法】#pragma once：头文件守卫的一种写法，保证本头文件在同一个编译单元中只被包含一次，
// 避免因重复包含导致的类型/函数重复定义错误（等价于 #ifndef/#define/#endif 宏守卫）。
#pragma once

// 【C++ 语法】#include 预处理指令：引入 Qt 的 QHash（哈希表容器）头文件，
// 本文件为 ControllerButton、MouseButton 提供 qHash 重载，并用 QHash 做名字映射。尖括号表示在编译器的包含目录中查找。
#include <QHash>
// 【C++ 语法】#include 预处理指令：引入 Qt 的 QString（Unicode 字符串）头文件，
// 本文件中大量函数签名使用 QString 作为返回类型或参数类型。
#include <QString>
// 【C++ 语法】#include 预处理指令：引入 Qt 的 QVector（动态数组容器）头文件，
// 本文件中 allControllerButtons() 的返回类型为 QVector<ControllerButton>。
#include <QVector>
// 【C++ 语法】#include 预处理指令：引入 C++ 标准库 <cmath>（数学函数库）头文件，
// 供 Vector2::magnitude() 中调用 std::sqrt() 计算平方根使用。
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
// 【C++ 语法】namespace（命名空间）声明：把一系列常量放在 AndroidKey 名字空间中，
// 外部必须用 AndroidKey::A 的形式访问，避免名字冲突；花括号包裹命名空间主体，末尾无需分号。
namespace AndroidKey {
    // 字母 A-Z: 29..54
    // 【C++ 语法】constexpr 常量表达式：编译期即可求值的常量；constexpr int A = 29, B = 30, ... 是一条声明语句，
    // 用逗号分隔可在一行内定义多个同类型变量。
    constexpr int A = 29, B = 30, C = 31, D = 32, E = 33, F = 34, G = 35, H = 36, // 字母 A-H 的安卓键码：A=29、B=30、C=31、D=32、E=33、F=34、G=35、H=36
                  I = 37, J = 38, K = 39, L = 40, M = 41, N = 42, O = 43, P = 44, // 字母 I-P 的安卓键码：I=37、J=38、K=39、L=40、M=41、N=42、O=43、P=44
                  Q = 45, R = 46, S = 47, T = 48, U = 49, V = 50, W = 51, X = 52, // 字母 Q-X 的安卓键码：Q=45、R=46、S=47、T=48、U=49、V=50、W=51、X=52
                  Y = 53, Z = 54; // 【C++ 语法】上一条 constexpr 声明语句的续行（通过缩进对齐），定义 Y=53、Z=54，分号结束整条声明
    // 数字 0-9: 7..16
    constexpr int N0 = 7, N1 = 8, N2 = 9, N3 = 10, N4 = 11, // 数字 0-4 的安卓键码：N0=7、N1=8、N2=9、N3=10、N4=11
                  N5 = 12, N6 = 13, N7 = 14, N8 = 15, N9 = 16; // 数字 5-9 的安卓键码：N5=12、N6=13、N7=14、N8=15、N9=16
    // 功能键 F1-F12: 131..142
    constexpr int F1 = 131, F2 = 132, F3 = 133, F4 = 134, F5 = 135, F6 = 136, // 功能键 F1-F6 的安卓键码：F1=131、F2=132、F3=133、F4=134、F5=135、F6=136
                  F7 = 137, F8 = 138, F9 = 139, F10 = 140, F11 = 141, F12 = 142; // 功能键 F7-F12 的安卓键码：F7=137、F8=138、F9=139、F10=140、F11=141、F12=142
    // 修饰键
    constexpr int SHIFT_LEFT = 59, SHIFT_RIGHT = 60, // 左右 Shift 的安卓键码：SHIFT_LEFT=59、SHIFT_RIGHT=60
                  CTRL_LEFT = 113, CTRL_RIGHT = 114, // 左右 Ctrl 的安卓键码：CTRL_LEFT=113、CTRL_RIGHT=114
                  ALT_LEFT = 57, ALT_RIGHT = 58; // 左右 Alt 的安卓键码：ALT_LEFT=57、ALT_RIGHT=58
    // 特殊键
    constexpr int SPACE = 62, ENTER = 66, TAB = 61, ESCAPE = 111, BACK = 4, // 空格=62、回车=66、制表=61、退出=111、返回=4
                  DEL = 67, INSERT = 124, HOME = 123, PAGE_UP = 92, // 删除=67、插入=124、Home=123、向上翻页=92
                  PAGE_DOWN = 93, MOVE_END = 122; // 向下翻页=93、行尾(End)=122
    // 方向键
    constexpr int DPAD_UP = 19, DPAD_DOWN = 20, DPAD_LEFT = 21, DPAD_RIGHT = 22; // 方向键上=19、下=20、左=21、右=22
    // 符号键
    constexpr int MINUS = 69, EQUALS = 70, LEFT_BRACKET = 71, RIGHT_BRACKET = 72, // 减号=69、等号=70、左方括号=71、右方括号=72
                  BACKSLASH = 73, SEMICOLON = 74, APOSTROPHE = 75, COMMA = 55, // 反斜杠=73、分号=74、撇号=75、逗号=55
                  PERIOD = 56, SLASH = 76, GRAVE = 68; // 句号=56、斜杠=76、反引号=68
    // 锁键
    constexpr int CAPS_LOCK = 115, NUM_LOCK = 143, SCROLL_LOCK = 116; // 大写锁定=115、数字锁定=143、滚动锁定=116
    // 小键盘 0-9: 144..153
    constexpr int NUMPAD_0 = 144, NUMPAD_1 = 145, NUMPAD_2 = 146, NUMPAD_3 = 147, // 小键盘 0-3 的安卓键码：144~147
                  NUMPAD_4 = 148, NUMPAD_5 = 149, NUMPAD_6 = 150, NUMPAD_7 = 151, // 小键盘 4-7 的安卓键码：148~151
                  NUMPAD_8 = 152, NUMPAD_9 = 153; // 小键盘 8-9 的安卓键码：NUMPAD_8=152、NUMPAD_9=153
} // 【C++ 语法】namespace AndroidKey 声明结束：其内部常量需通过 AndroidKey:: 作用域限定符访问

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
// 【C++ 语法】enum class（带作用域的枚举类型）：枚举值不会泄漏到外层作用域，
// 必须用 ControllerButton::A 的形式访问；与普通 enum 不同，它不会隐式转换为整数（类型更安全）。
enum class ControllerButton { // 定义"手柄物理按键"枚举类型，花括号包裹枚举值列表，末尾分号结束类型定义
    A, B, X, Y, // 四个主按键 A/B/X/Y
    LEFT_SHOULDER, RIGHT_SHOULDER, // 左肩键(LB)、右肩键(RB)
    LEFT_TRIGGER_CLICK, RIGHT_TRIGGER_CLICK, // 左扳机(LT)、右扳机(RT)
    LEFT_STICK_CLICK, RIGHT_STICK_CLICK, // 左摇杆按下(L3)、右摇杆按下(R3)
    MENU, OPTIONS, GUIDE, // 菜单键(MENU)、视图键(OPTIONS)、Home 键(GUIDE)
    DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT, // 方向键：上/下/左/右
    TOUCHPAD_CLICK // 触摸板点击（XInput 无对应物理位，仅用于兼容安卓配置）
}; // 【C++ 语法】枚举类型定义结束，末尾的分号是类型定义必需的

// ---------------------------------------------------------------------
// ControllerStick —— 摇杆
//  - LEFT_STICK：默认映射 WASD 8 方向移动
//  - RIGHT_STICK：默认映射视角控制（LookAround），由独立线程按固定节拍处理
//  - DPAD_AS_STICK：预留，将方向键模拟为摇杆
// ---------------------------------------------------------------------
// 【C++ 语法】enum class（带作用域的枚举类型）：定义"摇杆"枚举，需用 ControllerStick:: 前缀访问枚举值。
enum class ControllerStick { // 定义"摇杆"枚举类型，花括号包裹枚举值列表，末尾分号结束类型定义
    LEFT_STICK, RIGHT_STICK, DPAD_AS_STICK // 左摇杆、右摇杆、预留的方向键模拟摇杆
}; // 【C++ 语法】枚举类型定义结束，末尾的分号是类型定义必需的

// 鼠标按键（名称用大写，与安卓版枚举名一致，保证配置文件兼容）
// 【C++ 语法】enum class（带作用域的枚举类型）：定义"鼠标按键"枚举，需用 MouseButton:: 前缀访问枚举值。
enum class MouseButton { // 定义"鼠标按键"枚举类型，花括号包裹枚举值列表，末尾分号结束类型定义
    LEFT, RIGHT, MIDDLE, FORWARD, BACK // 左键、右键、中键、前进键、后退键
}; // 【C++ 语法】枚举类型定义结束，末尾的分号是类型定义必需的

// QHash/QSet 需要 qHash 重载（Qt 5.15 对 enum 没有默认实现）
// 【C++ 语法】inline（内联）函数定义：建议编译器把函数体直接展开到调用处以减少函数调用开销；
// 返回类型 uint（无符号整型）；函数名 qHash 是 Qt 哈希函数的自定义重载点；
// 参数 key 为枚举值，seed 为哈希种子（带默认参数 = 0）；noexcept 承诺该函数不会抛出异常。
inline uint qHash(ControllerButton key, uint seed = 0) noexcept { // 为 ControllerButton 提供哈希函数，函数体开始
    return ::qHash(static_cast<int>(key), seed); // 【C++ 语法】:: 为全局作用域限定符，调用全局函数 qHash；
    // static_cast<int> 把枚举显式转换为 int（enum class 不允许隐式转换），转换后再按整数计算哈希
} // 函数定义结束（花括号闭合）
// 【C++ 语法】inline 内联函数定义：为 MouseButton 提供哈希函数，签名与上面一致。
inline uint qHash(MouseButton key, uint seed = 0) noexcept { // 为 MouseButton 提供哈希函数，函数体开始
    return ::qHash(static_cast<int>(key), seed); // 把 MouseButton 显式转为 int 后调用全局 qHash 计算哈希
} // 函数定义结束（花括号闭合）

// ---------------------------------------------------------------------
// Vector2 —— 二维向量（摇杆输入）
// 摇杆原始输入为 x/y ∈ [-1,1]，经过死区处理后用于移动/视角控制。
// ---------------------------------------------------------------------
// 【C++ 语法】struct（结构体）：与 class 相似，但默认所有成员为 public（公开可访问），
// 可同时包含数据成员（变量）和成员函数（方法）。
struct Vector2 { // 定义二维向量结构体，花括号包裹成员，末尾分号结束类型定义
    float x = 0.f; // 【C++ 语法】非静态数据成员 x，类型 float（单精度浮点）；= 0.f 是类内初始化器（C++11 起支持），默认值为 0
    float y = 0.f; // 非静态数据成员 y，默认值为 0

    // 向量长度
    // 【C++ 语法】成员函数定义：返回类型 float；函数名 magnitude；const 修饰符表示该成员函数
    // 承诺不修改对象的状态，因此可对 const 对象调用。
    float magnitude() const { // 计算向量长度（模长）的成员函数，函数体开始
        return std::sqrt(x * x + y * y); // 【C++ 语法】std::sqrt 来自 <cmath>，计算平方根；x*x+y*y 先算平方和再开方得到长度
    } // 成员函数结束（花括号闭合）

    // 归一化：长度不足 1e-6 视为零向量
    // 【C++ 语法】成员函数定义：返回类型 Vector2（按值返回一个新向量对象，而非引用）；
    // const 修饰符保证不修改当前对象；normalized 函数名。
    Vector2 normalized() const { // 返回单位向量的成员函数，函数体开始
        const float m = magnitude(); // 【C++ 语法】const 声明只读局部变量；调用成员函数 magnitude() 得到当前向量长度
        if (m < 1e-6f) return Vector2{0.f, 0.f}; // 【C++ 语法】if 判断长度是否过小；1e-6f 为科学计数法浮点常量；Vector2{...} 用花括号初始化构造并返回零向量
        return Vector2{x / m, y / m}; // 各分量除以长度得到单位向量（长度为 1）并返回
    } // 成员函数结束（花括号闭合）

    // 死区缩放：(mag - deadzone) / (1 - deadzone)
    // 输入小于死区返回零向量，超过死区后按比例线性放大，
    // 保证摇杆推到底（mag=1）时输出仍为满幅（1）。
    // 【C++ 语法】成员函数定义：返回类型 Vector2；参数 deadzone 按值接收 float（死区大小）；
    // const 修饰符保证不修改当前对象；withDeadzone 函数名。
    Vector2 withDeadzone(float deadzone) const { // 应用死区缩放后返回向量的成员函数，函数体开始
        const float m = magnitude(); // 先计算当前向量长度并保存到 const 局部变量
        if (m <= deadzone) return Vector2{0.f, 0.f}; // 若长度不超过死区，视为无效输入，返回零向量
        const float scale = (m - deadzone) / (1.f - deadzone); // 【C++ 语法】计算缩放系数：把长度区间 (deadzone,1] 线性映射到 (0,1]；/ 为除法运算符
        return Vector2{x * scale, y * scale}; // 各分量乘以缩放系数后返回（保持方向、按比例调整幅度）
    } // 成员函数结束（花括号闭合）
}; // 【C++ 语法】结构体类型定义结束，末尾的分号是类型定义必需的

// ---------------------------------------------------------------------
// 辅助函数
// 提供三种命名空间：
//  1. controllerButtonName/FromName —— 配置文件中使用的内部名（英文枚举名）
//  2. controllerButtonDisplayName —— 界面显示名（中文，如"A键"）
//  3. keyCodeToName —— Android KeyCode 的可读名称
// ---------------------------------------------------------------------

// 统一按钮枚举 -> 内部名（"A"、"DPAD_UP"），用于配置文件读写
// 【C++ 语法】函数声明（只有签名、没有函数体，以分号结尾）：告诉编译器该函数存在且具有此签名，
// 具体实现在 InputTypes.cpp 中；返回 QString，参数 b 按值接收 ControllerButton 枚举。
QString controllerButtonName(ControllerButton b); // 声明"手柄按钮 -> 内部名"转换函数
// 内部名 -> 按钮枚举；解析失败返回 false
// 【C++ 语法】函数声明：返回 bool；参数 name 为 const QString&（常引用，只读且避免拷贝）；
// 参数 out 为 ControllerButton*（输出指针，函数把结果写入该指针指向的变量）。
bool controllerButtonFromName(const QString& name, ControllerButton* out); // 声明"内部名 -> 手柄按钮"解析函数
// 统一按钮枚举 -> 显示名（"A键"、"方向键上"），用于界面
// 【C++ 语法】函数声明：返回 QString，参数 b 按值接收 ControllerButton 枚举。
QString controllerButtonDisplayName(ControllerButton b); // 声明"手柄按钮 -> 中文显示名"转换函数
// 所有按钮（按固定显示顺序，用于编辑界面遍历）
// 【C++ 语法】函数声明：返回类型 QVector<ControllerButton>（Qt 动态数组容器，元素为手柄按钮枚举）。
QVector<ControllerButton> allControllerButtons(); // 声明"返回全部手柄按钮集合"的函数

// 鼠标按键 -> 内部名（"LEFT"），用于配置文件
// 【C++ 语法】函数声明：返回 QString，参数 b 按值接收 MouseButton 枚举。
QString mouseButtonName(MouseButton b); // 声明"鼠标按键 -> 内部名"转换函数
// 内部名 -> 鼠标按键；解析失败返回 false（兼容大小写）
// 【C++ 语法】函数声明：返回 bool；参数 name 为 const QString&（常引用）；
// 参数 out 为 MouseButton*（输出指针，把结果写回调用者的变量）。
bool mouseButtonFromName(const QString& name, MouseButton* out); // 声明"内部名 -> 鼠标按键"解析函数
// 鼠标按键 -> 显示名（"左键"），用于界面
// 【C++ 语法】函数声明：返回 QString，参数 b 按值接收 MouseButton 枚举。
QString mouseButtonDisplayName(MouseButton b); // 声明"鼠标按键 -> 中文显示名"转换函数

// Android KeyCode -> 可读名称（与安卓版 keyCodeToName 一致）
// 【C++ 语法】函数声明：返回 QString，参数 keyCode 按值接收 int（Android 键码数值）。
QString keyCodeToName(int keyCode); // 声明"Android 键码 -> 可读名称"转换函数

// 操作层显示名（Layer1->"Layer1 战斗"等，WoW 预设；未知层名原样返回）
// 【C++ 语法】函数声明：返回 QString，参数 layerName 为 const QString&（常引用，只读避免拷贝）。
QString layerDisplayName(const QString& layerName); // 声明"操作层名 -> 带中文别名显示名"转换函数
