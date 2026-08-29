// 【C++ 语法】#pragma once：头文件保护指令，保证本头文件在同一个编译单元中只被包含一次，
// 作用等价于传统的 #ifndef / #define / #endif 防重复包含写法，写起来更简洁。
#pragma once

// 【C++ 语法】#include "..."：双引号形式表示包含项目自定义头文件，预处理器会把其内容原样展开到本行。
#include "InputTypes.h"   // 引入输入类型定义：ControllerButton（手柄按键）、MouseButton（鼠标按键）、AndroidKey（Android 键码）等

// 【Qt】#include <...>：尖括号形式表示包含 Qt 库头文件（Qt 头文件一般不带 .h 后缀）。
#include <QHash>          // Qt 哈希容器：键->值 映射，平均 O(1) 查找
#include <QString>        // Qt 字符串类型：UTF-16 编码、隐式共享、不可变
#include <QStringList>    // Qt 字符串列表：QList<QString> 的便捷别名，支持 << 追加与 join() 拼接
#include <QVector>        // Qt 动态数组容器：连续内存存储，支持随机访问与末尾追加

// =====================================================================
// 映射数据模型
//
// 等效安卓版 MappedAction / KeyMapping / OperationLayer /
// GlobalSettings / ControllerProfile。
//
// 三层结构总览（从高到低）：
//   操作集（OperationSet，如"战斗"/"骑乘"）——
//     最顶层容器，由 1 个公共层 + 最多 10 个操作层组成；
//     切换操作集时，其下所有操作层整体切换（配置互不干扰）。
//   公共层（commonLayer）—— 始终激活，优先度最低，作为兜底映射；
//   操作层（layers）—— 通过公共层的 SwitchLayer 映射按住激活、松开回退，
//     优先度高于公共层。
//  按键查询顺序（getEffectiveMapping）：最后激活的操作层 -> ... -> 公共层。
//  运行时只针对「当前激活的操作集」查询，操作集之间互不影响。
// =====================================================================

// ---------------------------------------------------------------------
// MappedAction —— 映射动作
//
// 对应按键/摇杆在按下时执行的动作类型，等效安卓版 sealed class
// MappedAction。通过静态工厂方法构造，type 决定实际生效的字段。
// ---------------------------------------------------------------------
// 【C++ 语法】struct：C++ 结构体，与 class 的唯一区别是默认访问权限为 public（公有），
// 这里把“动作类型 + 相关字段”聚合成一个可拷贝、可比较的值对象。
struct MappedAction {
    // 【C++ 语法】enum class：C++11 强类型枚举，枚举值必须用 Type:: 前缀访问，
    // 不会与其它枚举/全局名字冲突，也不能隐式转换为整型。
    enum class Type {   // 动作类型枚举：type 字段决定该动作实际生效/使用的字段
        KeyboardKey,            // 键盘按键（keyCode）
        MouseClick,             // 鼠标单击（mouseButton，按下/松开跟随手柄）
        SwitchLayer,            // 切换操作层（layerName，按住激活/松开回退）
        MouseMove,              // 鼠标移动（摇杆动作，无需额外参数）
        LookAround,             // 视角控制（摇杆动作，右摇杆，独立线程节拍处理）
        MouseToggle,            // 鼠标长按锁存（按住期间保持按下，松开不改变状态）
        WheelUp,                // 鼠标滚轮上滚（按下时向上滚动一格）
        WheelDown,              // 鼠标滚轮下滚（按下时向下滚动一格）
        ToggleMapping,          // 切换映射启停（按下时触发）
        ToggleOnScreenKeyboard, // 切换 Windows 屏幕键盘（按下时触发）
        ToggleOverlay           // 切换悬浮窗展开/收起（按下时触发）
    };

    Type type = Type::MouseMove;   // 【C++ 语法】成员默认初始化：声明时直接给出默认值；默认"鼠标移动"（无需额外参数的动作）
    int keyCode = 0;                                  // KeyboardKey 专用
    MouseButton mouseButton = MouseButton::LEFT;      // MouseClick / MouseToggle 专用
    QString layerName;                                // SwitchLayer 专用（目标层 id）

    // 【C++ 语法】static 静态成员函数：无需对象实例即可用 MappedAction::keyboardKey() 调用，
    // 把“创建对象 + 填字段”封装成静态工厂方法；返回类型 MappedAction 为按值返回。
    // 构造「键盘按键」动作
    static MappedAction keyboardKey(int code) {   // code：Android 键码（如 SPACE=62）
        MappedAction a;                            // 在栈上创建局部对象 a
        a.type = Type::KeyboardKey;                // 设置动作类型为"键盘按键"
        a.keyCode = code;                          // 写入目标键码
        return a;                                  // 【C++ 语法】按值返回对象；C++11 起可能触发移动语义/返回值优化
    }
    // 构造「鼠标单击」动作
    static MappedAction mouseClick(MouseButton b) {  // b：要模拟的鼠标按键
        MappedAction a;                              // 创建局部对象
        a.type = Type::MouseClick;                   // 设置动作类型为"鼠标单击"
        a.mouseButton = b;                           // 记录鼠标按键
        return a;                                    // 返回构造好的对象
    }
    // 构造「鼠标长按锁存」动作
    static MappedAction mouseToggle(MouseButton b) {  // b：要锁存按下的鼠标按键
        MappedAction a;                               // 创建局部对象
        a.type = Type::MouseToggle;                   // 设置动作类型为"鼠标长按锁存"
        a.mouseButton = b;                            // 记录鼠标按键
        return a;                                     // 返回构造好的对象
    }
    // 构造「鼠标滚轮上滚」动作
    static MappedAction wheelUp() {       // 无参数：滚轮上滚无需额外字段
        MappedAction a;                   // 创建局部对象
        a.type = Type::WheelUp;           // 设置动作类型为"滚轮上滚"
        return a;                         // 返回构造好的对象
    }
    // 构造「鼠标滚轮下滚」动作
    static MappedAction wheelDown() {     // 无参数工厂
        MappedAction a;                   // 创建局部对象
        a.type = Type::WheelDown;         // 设置动作类型为"滚轮下滚"
        return a;                         // 返回构造好的对象
    }
    // 构造「切换操作层」动作（target 为目标层 id，如 "Layer1"）
    // 【C++ 语法】const QString&：常量左值引用，只读引用调用方传入的字符串对象，避免按值拷贝的开销。
    static MappedAction switchLayer(const QString& name) {   // name：目标操作层 id
        MappedAction a;                                      // 创建局部对象
        a.type = Type::SwitchLayer;                          // 设置动作类型为"切换操作层"
        a.layerName = name;                                  // 记录目标层 id
        return a;                                            // 返回构造好的对象
    }
    // 构造「鼠标移动」动作
    static MappedAction mouseMove() {     // 无参数工厂
        MappedAction a;                   // 创建局部对象
        a.type = Type::MouseMove;         // 设置动作类型为"鼠标移动"
        return a;                         // 返回构造好的对象
    }
    // 构造「视角控制」动作
    static MappedAction lookAround() {    // 无参数工厂
        MappedAction a;                   // 创建局部对象
        a.type = Type::LookAround;        // 设置动作类型为"视角控制"
        return a;                         // 返回构造好的对象
    }
    // 构造「切换映射启停」动作
    static MappedAction toggleMapping() { // 无参数工厂
        MappedAction a;                   // 创建局部对象
        a.type = Type::ToggleMapping;     // 设置动作类型为"切换映射启停"
        return a;                         // 返回构造好的对象
    }
    // 构造「切换屏幕键盘」动作
    static MappedAction toggleOnScreenKeyboard() {  // 无参数工厂
        MappedAction a;                             // 创建局部对象
        a.type = Type::ToggleOnScreenKeyboard;      // 设置动作类型为"切换屏幕键盘"
        return a;                                   // 返回构造好的对象
    }
    // 构造「切换悬浮窗」动作
    static MappedAction toggleOverlay() {  // 无参数工厂
        MappedAction a;                    // 创建局部对象
        a.type = Type::ToggleOverlay;      // 设置动作类型为"切换悬浮窗"
        return a;                          // 返回构造好的对象
    }

    // 【C++ 语法】operator== 运算符重载：定义 MappedAction 的相等比较（供容器查找、对象比较使用）；
    // 参数 const MappedAction& 为常量引用（避免拷贝）；末尾 const 表示该成员函数不修改对象。
    bool operator==(const MappedAction& o) const {
        if (type != o.type) return false;                  // 【C++ 语法】if 条件判断；类型不同直接返回"不相等"
        switch (type) {                                    // 类型相同再按类型逐一比较相关字段
            case Type::KeyboardKey: return keyCode == o.keyCode;             // 键盘按键：比较键码
            case Type::MouseClick:                                             // 鼠标单击（不写 break，贯穿到下一 case 共用比较）
            case Type::MouseToggle: return mouseButton == o.mouseButton;       // 鼠标长按：比较鼠标按钮
            case Type::SwitchLayer: return layerName == o.layerName;           // 层切换：比较目标层名
            default: return true;  // MouseMove/LookAround/ToggleMapping/ToggleOnScreenKeyboard/ToggleOverlay
        }
    }
    bool operator!=(const MappedAction& o) const { return !(*this == o); }  // 不等比较：对 == 结果取反
};

// ---------------------------------------------------------------------
// KeyMapping —— 单个按键映射
//
// 由 1 个主动作（action）+ 最多 3 个子命令（subCommands）组成：
//  按下主键时：先注入主动作，再依次按下各子命令；
//  松开时：逆序释放子命令，再释放主动作。
// 典型用途：将手柄按键映射为 "Alt+3" 之类的组合键，
// 其中 action=KeyboardKey(3)，subCommands=[ALT_LEFT]。
// ---------------------------------------------------------------------
struct KeyMapping {            // 单个按键的映射结构体
    MappedAction action;       // 主动作：按下主键时注入
    QVector<int> subCommands;   // Android KeyCode 列表，最多 MAX_SUB_COMMANDS 个

    // 【C++ 语法】static constexpr：编译期常量成员，无需实例即可用 KeyMapping::MAX_SUB_COMMANDS 访问。
    static constexpr int MAX_SUB_COMMANDS = 3;   // 子命令（组合键）数量上限

    // 生成可读描述字符串，如 "W+ALT"
    // 【C++ 语法】const 成员函数声明：末尾 const 表示不修改对象；该函数实现在 MappingTypes.cpp。
    QString describe() const;
    bool operator==(const KeyMapping& o) const {       // 相等比较运算符重载：主动作与子命令列表都相等才相等
        return action == o.action && subCommands == o.subCommands;   // 【C++ 语法】&& 逻辑与：两个条件同时成立
    }
    bool operator!=(const KeyMapping& o) const { return !(*this == o); }  // 不等比较：对 == 结果取反
};

// ---------------------------------------------------------------------
// OperationLayer —— 操作层（一组按键映射）
//
//  - id：唯一标识符，固定不变（如 "Layer1"、"Common"）。
//    层切换、按钮查找等运行时逻辑都基于 id，与显示名称解耦。
//  - name：显示名称，用户可在编辑对话框中自由修改，
//    不影响任何运行时逻辑（运行时只认 id）。
//  - hasTriggerButton / triggerButton：仅用于 UI 展示说明（如编辑对话框
//    中标注"按住 LB 激活本层"），不参与运行时层切换判定。
//    实际层切换完全由公共层的 SwitchLayer 映射驱动。
// ---------------------------------------------------------------------
// 【C++ 语法】class：C++ 类，与 struct 的区别是默认访问权限为 private（私有），
// 因此需要显式写 public: 开放对外接口。
class OperationLayer {
public:                            // 【C++ 语法】访问控制符：以下成员对外公有可访问
    QString id; // 唯一标识符，固定不变（如"Layer1"）
    QString name; // 显示名称，用户可修改
    bool hasTriggerButton = false;                    // 是否有"按住某键激活本层"的展示按钮（仅 UI 展示用）
    ControllerButton triggerButton = ControllerButton::A;   // 触发按钮（仅 UI 展示用，默认 A）
    QHash<ControllerButton, KeyMapping> buttonMappings;   // 按钮 -> 映射

    // 【C++ 语法】= default：显式要求编译器生成默认构造函数（保持默认行为，且不阻止其它构造函数重载）。
    OperationLayer() = default;
    // 【C++ 语法】explicit：禁止隐式转换（避免 QString 被隐式转成 OperationLayer）；
    // 冒号后为成员初始化列表 : id(layerName), name(layerName)，直接初始化成员（比函数体内赋值更高效）。
    explicit OperationLayer(const QString& layerName) : id(layerName), name(layerName) {}

    // 查询某按钮的映射；不存在返回 nullptr（const 版本）
    // 【C++ 语法】const 成员函数重载：返回 const 指针（只读访问），与下面非 const 版本构成重载。
    const KeyMapping* getMapping(ControllerButton b) const {   // 按按钮查询映射，返回只读指针
        const auto it = buttonMappings.constFind(b);           // 【C++ 语法】auto 自动推导类型；【Qt】constFind 返回只读迭代器
        return it == buttonMappings.constEnd() ? nullptr : &it.value();   // 【C++ 语法】三目运算符 ?:；找不到返回空指针，否则返回指向值的地址
    }
    KeyMapping* getMapping(ControllerButton b) {               // 【C++ 语法】函数重载：同名不同 const 限定；返回可修改指针
        auto it = buttonMappings.find(b);                      // find 返回可修改（mutable）迭代器
        return it == buttonMappings.end() ? nullptr : &it.value();   // 找不到返回空指针，否则返回可修改指针
    }
};

// ---------------------------------------------------------------------
// GlobalSettings —— 全局设置
// 数值含义见 MainWindow 滑块的换算关系：
//  - deadzone        摇杆死区 0~1（滑块 0~50 → /100）
//  - lookSensitivity 视角灵敏度（滑块 10~200 → /100）
//  - cursorSpeed     光标速度，当前固定 1.0（预留）
//  - lookSmoothing   视角平滑系数（滑块 0~100 → /100，
//                    决定时间常数 tau = smoothing * 0.048s）
//  - lookAcceleration 视角加速度曲线指数（滑块 100~300 → /100）
// ---------------------------------------------------------------------
// 【C++ 语法】struct：全部成员默认公有，用成员默认初始化直接给出各设置的初始值。
struct GlobalSettings {
    float deadzone = 0.15f;          // 摇杆死区
    float lookSensitivity = 0.5f;    // 视角灵敏度
    float cursorSpeed = 1.0f;        // 光标速度（预留）
    float lookSmoothing = 0.5f;      // 视角平滑
    float lookAcceleration = 1.5f;   // 视角加速度曲线
    bool invertLookX = false;        // 右摇杆 X 轴反转
    bool invertLookY = false;        // 右摇杆 Y 轴反转
    int overlayX = -1;               // 悬浮窗 X 坐标（-1 表示未设置，使用默认位置）
    int overlayY = -1;               // 悬浮窗 Y 坐标（-1 表示未设置，使用默认位置）
    double overlayScale = 1.0;       // 悬浮窗缩放系数（滚轮调整，0.5 ~ 2.0）
    int mainWindowX = -1;            // 主窗口 X 坐标（-1 表示未设置，使用默认位置）
    int mainWindowY = -1;            // 主窗口 Y 坐标（-1 表示未设置，使用默认位置）
    bool releaseOnForegroundChange = true;  // 切换前台窗口时释放所有按键
    bool confirmOnClose = true;             // 关闭时弹出确认对话框（退出/最小化）
};

// 单个操作集内最多操作层数
// 【C++ 语法】constexpr：编译期常量，可用于数组大小等需要常量表达式的场合。
constexpr int kMaxLayersPerSet = 10;   // kMax 前缀表示常量，值为 10

// ---------------------------------------------------------------------
// OperationSet —— 操作集（最顶层容器）
//
// 一组完整的映射配置：1 个公共层 + 最多 kMaxLayersPerSet 个操作层。
//  - id：唯一标识符，固定不变（如 "Set1"），运行时定位用；
//  - name：显示名称，用户可自由修改。
// 切换操作集 = 整体切换其下所有层；运行时层查询只针对当前激活操作集。
// ---------------------------------------------------------------------
class OperationSet {
public:                                // 以下成员对外公有
    QString id;                          // 唯一标识符（"Set1"、"Set2"...）
    QString name;                        // 显示名称，可自定义
    OperationLayer commonLayer;          // 本操作集的公共层
    QVector<OperationLayer> layers;      // 本操作集的操作层

    OperationSet() = default;            // 使用编译器生成的默认构造函数

    // 创建一个全新的空操作集：空公共层 + kMaxLayersPerSet 个空操作层（默认名）
    // 【C++ 语法】static 静态成员函数（工厂方法）：类名直接调用，返回新构造的对象。
    static OperationSet createEmpty(const QString& setId, const QString& setName) {  // 入参：操作集 id 与显示名
        OperationSet set;                            // 创建局部操作集对象
        set.id = setId;                              // 设置 id
        set.name = setName;                          // 设置显示名
        set.commonLayer = OperationLayer(QStringLiteral("Common"));   // 用显式构造创建公共层（id/name 均为 "Common"）
        set.commonLayer.name = QStringLiteral("Common");              // 再次确认公共层显示名为 "Common"
        const char* layerIds[kMaxLayersPerSet] = {   // 【C++ 语法】C 风格字符串数组（const char*）：存放 10 个默认层 id
            "Layer1", "Layer2", "Layer3", "Layer4", "Layer5",
            "Layer6", "Layer7", "Layer8", "Layer9", "Layer10",
        };
        for (int i = 0; i < kMaxLayersPerSet; ++i) {  // 【C++ 语法】经典 for 循环：i 从 0 到 9 遍历全部层
            OperationLayer layer(QString::fromLatin1(layerIds[i]));   // 【Qt】fromLatin1：把 8 位拉丁-1 字符串转成 QString 构造层
            layer.name = layerDisplayName(layer.id);                  // 用显示名函数为层取名（本地化显示名）
            set.layers.append(layer);                                 // 【Qt】append()：把层追加到操作层列表末尾
        }
        return set;                              // 返回构造好的操作集
    }
};

// ---------------------------------------------------------------------
// ControllerProfile —— 完整配置（整个配置文件对应一个 ControllerProfile）
//
// 结构：operationSets（操作集列表）+ activeOperationSetId（当前激活操作集）
//      + globalSettings（全局设置）。
// 配置通过 ControllerConfig 序列化为 JSON（version=2）持久化。
// ---------------------------------------------------------------------
class ControllerProfile {         // 顶层配置类：整个配置文件的对应物
public:
    QVector<OperationSet> operationSets;   // 操作集列表（至少 1 个）
    QString activeOperationSetId;          // 当前激活的操作集 id
    GlobalSettings globalSettings;         // 全局设置

    static constexpr int MAX_LAYERS = kMaxLayersPerSet;   // 单个操作集内最多操作层数

    // 当前激活的操作集（无有效激活集时返回 nullptr）
    OperationSet* activeSet() {                       // 非 const 版本：返回可修改指针
        for (OperationSet& s : operationSets)         // 【C++ 语法】范围 for：遍历每个操作集（s 为可修改引用）
            if (s.id == activeOperationSetId) return &s;   // 找到 id 匹配的操作集，返回其地址
        return operationSets.isEmpty() ? nullptr : &operationSets.first();   // 【C++ 语法】三目运算符；未找到则回退到第一个操作集
    }
    const OperationSet* activeSet() const {           // 【C++ 语法】const 成员函数重载：返回 const 指针（只读）
        for (const OperationSet& s : operationSets)   // 范围 for：只读遍历
            if (s.id == activeOperationSetId) return &s;   // 找到匹配则返回其地址
        return operationSets.isEmpty() ? nullptr : &operationSets.first();   // 未找到则回退到第一个操作集
    }

    // 当前激活操作集的公共层 / 操作层（快捷访问）
    OperationLayer* commonLayer() { return activeSet() ? &activeSet()->commonLayer : nullptr; }   // 【C++ 语法】三目；有激活集则取公共层地址，否则返回空指针
    const OperationLayer* commonLayer() const {       // const 版本：返回只读指针
        return activeSet() ? &activeSet()->commonLayer : nullptr;   // 三目：同上逻辑
    }
    // 无激活集（operationSets 为空）时返回空引用，避免解引用空指针。
    // 空容器用成员变量 emptyLayers_（而非 static 局部变量）：避免跨调用
    // 共享可变状态及多线程同时访问的可写引用风险。
    QVector<OperationLayer>& layers() {               // 【C++ 语法】返回引用：让调用方能直接修改返回的容器
        OperationSet* set = activeSet();              // 获取当前激活操作集
        return set ? set->layers : emptyLayers_;      // 三目：无激活集时返回空容器引用
    }
    const QVector<OperationLayer>& layers() const {   // const 版本：返回只读引用
        const OperationSet* set = activeSet();        // 获取当前激活操作集（只读）
        return set ? set->layers : emptyLayers_;      // 三目：无激活集时返回空容器引用
    }

    // 当前激活操作集的显示名（无激活集时返回空串）
    QString activeOperationSetName() const {          // const 成员函数
        const OperationSet* s = activeSet();          // 获取当前激活操作集
        return s ? s->name : QString();               // 三目：无激活集时返回空字符串
    }

    // 按 id 设置当前激活操作集；无效 id 返回 false
    bool setActiveOperationSet(const QString& id) {   // 入参：要激活的操作集 id
        for (const OperationSet& s : operationSets)   // 遍历所有操作集
            if (s.id == id) { activeOperationSetId = id; return true; }   // 找到匹配则记录 id 并返回成功
        return false;                                 // 未找到返回失败
    }

    // 生成一个不与现有操作集重复的新 id（"Set1"、"Set2"...）
    QString uniqueOperationSetId() const {            // const 成员函数
        int max = 0;                                  // 记录现有操作集编号的最大值
        for (const OperationSet& s : operationSets) { // 遍历所有操作集
            bool ok = false;                          // toInt 转换是否成功的标志
            const int n = s.id.mid(3).toInt(&ok);     // 【Qt】mid(3) 去掉"Set"前缀取数字部分；toInt(&ok) 转成 int
            if (ok && n > max) max = n;               // 转换成功且编号更大则更新 max
        }
        return QStringLiteral("Set%1").arg(max + 1);  // 【Qt】arg() 格式化：把 %1 替换为 (max+1)，得到新 id
    }

    // 按 id 查找层（仅当前激活操作集内，含公共层）；不存在返回 nullptr
    OperationLayer* findLayer(const QString& id) {    // 非 const 版本：返回可修改层指针
        OperationSet* set = activeSet();              // 获取当前激活操作集
        if (!set) return nullptr;                     // 无激活集直接返回空指针
        if (set->commonLayer.id == id) return &set->commonLayer;   // 先查公共层，命中则返回其地址
        for (OperationLayer& l : set->layers)         // 再遍历操作层
            if (l.id == id) return &l;                // 找到 id 匹配的层，返回其地址
        return nullptr;                               // 都没找到返回空指针
    }
    const OperationLayer* findLayer(const QString& id) const {  // 【C++ 语法】const 成员函数重载：返回只读指针
        const OperationSet* set = activeSet();        // 获取当前激活操作集（只读）
        if (!set) return nullptr;                     // 无激活集返回空指针
        if (set->commonLayer.id == id) return &set->commonLayer;   // 先查公共层
        for (const OperationLayer& l : set->layers)   // 再遍历操作层
            if (l.id == id) return &l;                // 命中返回地址
        return nullptr;                               // 未找到返回空指针
    }
    // 由触发按键找操作层（当前激活操作集内，仅供 UI 展示，不参与运行时切换）
    OperationLayer* findLayerByTrigger(ControllerButton b) {   // 按触发按钮查找层，返回可修改指针
        OperationSet* set = activeSet();              // 获取当前激活操作集
        if (!set) return nullptr;                     // 无激活集返回空指针
        for (OperationLayer& l : set->layers)         // 遍历操作层
            if (l.hasTriggerButton && l.triggerButton == b) return &l;   // 同时满足"有触发按钮"且"按钮匹配"才返回该层
        return nullptr;                               // 没找到返回空指针
    }

    // 生成默认配置（WoW 预设：1 个"默认操作集"，含公共层 + 10 个操作层）
    static ControllerProfile createDefault();         // 【C++ 语法】static 工厂方法声明：实现在 MappingTypes.cpp

private:                                              // 【C++ 语法】访问控制符：以下成员私有，仅类内部可访问
    // 无激活集时 layers() 返回的空容器（每实例独立，非共享静态变量）
    QVector<OperationLayer> emptyLayers_;             // 空层容器成员（下划线后缀是私有成员的常见命名约定）
};
