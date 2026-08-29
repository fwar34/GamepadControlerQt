// 【C++ 语法】#pragma once：头文件包含守卫指令，确保本头文件在同一个翻译单元中只被编译一次，
// 等效于 #ifndef / #define / #endif 的组合，写法更简洁。
#pragma once // 防止头文件被重复包含

// 【C++ 语法】#include "xxx.h"：以双引号包含自定义头文件（编译器优先搜索当前源文件所在目录），
// 引入映射引擎所需的全部核心数据结构类型（ControllerProfile、OperationLayer、KeyMapping 等）。
#include "MappingTypes.h" // 引入自定义的核心数据类型定义

// 【C++ 语法】#include <xxx>：以尖括号包含系统/编译环境提供的头文件；以下引入 Qt 框架容器与基类。
#include <QHash>    // Qt 哈希表容器：键->值的快速查找（记录"哪个按键激活了哪个层"）
#include <QObject>  // Qt 对象基类：提供信号/槽（signals/slots）、元对象、父子对象树等机制
#include <QSet>     // Qt 集合容器：存放不重复元素（记录当前物理按下的手柄按键）
#include <QString>  // Qt 字符串类：UTF-16 编码、跨平台，与所有 Qt API 无缝配合
#include <QVector>  // Qt 动态数组容器（等效 std::vector）：用于维护"激活层栈"

// =====================================================================
// SteamInput —— 映射引擎（等效安卓版 SteamInput）
//
// 职责：
//   - 维护当前激活的操作层栈（公共层始终激活、优先级最低）
//   - 按键查询：按「最后激活的操作层 -> ... -> 公共层」顺序查找有效映射
//   - 分发按钮/摇杆输入，并负责层切换动作的运行时处理
//
// 数据流：
//   XInputGamepadSource（手柄读取）-> handleButtonEvent/handleStickInput
//   -> buttonMapped/stickMapped 信号 -> KeyboardMouseMapper（键鼠注入）
// =====================================================================
// 【C++ 语法】class 类名 : public 基类：声明继承关系；public 继承表示"is-a"关系，
// 派生类拥有基类的所有公共/保护成员。
// 【Qt】继承 QObject 是使用 Q_OBJECT、信号/槽（signals/slots）、Qt 对象树的先决条件。
class SteamInput : public QObject { // 声明映射引擎类，公开继承 QObject
    Q_OBJECT // 【Qt】Q_OBJECT 宏：为类注入元对象系统，启用信号/槽、属性、tr() 翻译等机制
public: // 【C++ 语法】public 访问说明符：其下方成员为对外公开接口，任何代码均可访问
    // 【C++ 语法】explicit：禁止构造函数被隐式调用，避免意外的隐式类型转换。
    // 【C++ 语法】QObject* parent = nullptr：指针参数带默认实参 nullptr，用于建立 Qt 父子
    // 对象树（父对象析构时自动销毁子对象，防止内存泄漏）。
    explicit SteamInput(QObject* parent = nullptr); // 构造函数，parent 为父对象指针（可为空）

    // 当前配置（操作集列表 + 当前激活操作集 + 全局设置）
    ControllerProfile profile; // 公开成员对象：程序运行时的全部配置数据

    // 整体替换配置（启动时加载配置文件后调用），同时清空所有激活层
    // 【C++ 语法】const ControllerProfile&：常量引用传参，既避免拷贝大对象又保证不修改实参，
    // 是 C++ 传递"只读大对象"的惯用做法。
    void loadProfile(const ControllerProfile& newProfile); // 整体替换配置并清空已激活层

    // 仅更新全局设置（不重置已激活层），并通知映射器同步
    // （用于界面滑块实时调整死区/灵敏度等，避免打断进行中的层切换）
    // 【C++ 语法】const GlobalSettings&：同上，常量引用传参（只读、不拷贝）。
    void setGlobalSettings(const GlobalSettings& settings); // 仅更新全局设置（界面滑块实时调整用）

    // ---- 操作集管理 ----
    // 切换当前操作集（按 id）：清空已激活层栈（不同操作集的层指针不再有效），
    // 更新激活操作集并发出 profileChanged / operationSetChanged。
    // id 无效或与当前相同返回 false（不产生任何副作用）。
    // 【C++ 语法】bool 返回类型：用布尔值表示操作成功与否（true=成功，false=失败）。
    bool switchOperationSet(const QString& setId); // 按 id 切换操作集，失败返回 false
    // 操作集结构变化后通知（新增/删除/复制/重命名后调用，此时调用方
    // 已把 activeOperationSetId 指向目标集）：发出 profileChanged +
    // operationSetChanged，供界面/悬浮窗同步。调用前必须先 deactivateAllLayers，
    // 避免 QVector 扩容使已激活层指针失效。
    void notifyOperationSetChanged(); // 操作集结构变化后的统一广播通知（无返回值）

    // ---- 层管理 ----
    // 激活指定层（name 为层 id）；重复激活同一层会被忽略（见实现）
    // 【C++ 语法】函数重载：同名函数因参数类型不同（QString / OperationLayer*）构成重载，
    // 编译器根据实参类型自动选择调用哪个版本。
    void activateLayer(const QString& name);   // 按层 id（字符串）激活层
    void activateLayer(OperationLayer* layer); // 按层对象指针激活层
    // 停用指定层
    void deactivateLayer(const QString& name);   // 按层 id（字符串）停用层
    void deactivateLayer(OperationLayer* layer); // 按层对象指针停用层
    // 停用所有操作层，回到公共层
    void deactivateAllLayers(); // 停用全部操作层，回到公共层
    // 指定层当前是否激活
    // 【C++ 语法】末尾 const：const 成员函数承诺不修改对象状态，可对 const 对象调用。
    bool isLayerActive(const QString& name) const; // 判断指定层当前是否激活（只读查询）
    // 当前激活层 id（未激活任何操作层时为 "Common"）
    // 【C++ 语法】类内直接定义函数体即"内联函数"（隐式 inline）；末尾 const 表示只读查询。
    QString activeLayerName() const { return activeLayerName_; } // 内联只读函数：返回当前激活层名

    // ---- 查询 ----
    // 查询某按钮在当前层栈下的有效映射：
    //   从最后激活的操作层开始，逐层回退到公共层，返回第一个命中
    // 【C++ 语法】返回 const KeyMapping*：返回指向"只读映射对象"的指针；用指针而非值返回，
    // 既避免拷贝，又能用 nullptr 表达"无映射"这一状态。
    const KeyMapping* getEffectiveMapping(ControllerButton button) const; // 查询按钮有效映射（只读）
    // 当前激活层列表（公共层不在其中）
    // 【C++ 语法】QVector<const OperationLayer*>：存放"指向常对象的指针"的动态数组，只读遍历。
    QVector<const OperationLayer*> getActiveLayers() const; // 返回当前激活层列表（按激活顺序）
    // 当前物理按下的手柄按键集合
    QSet<ControllerButton> heldButtons() const { return heldButtons_; } // 内联只读：返回按下的按键集合

    // ---- 输入入口（由手柄读取源调用） ----
    // 按钮按下/松开事件；SwitchLayer 动作在此处理（按住激活/松开回退），
    // 其余动作通过 buttonMapped 信号广播给映射器
    // 【C++ 语法】ControllerButton 按值传参：枚举/小尺寸类型按值传递更高效。
    void handleButtonEvent(ControllerButton button, bool isPressed); // 按钮事件入口（按下/松开）
    // 摇杆输入（x,y 已归一化到 [-1,1]，未应用死区）
    // 【C++ 语法】float 按值传参：坐标数值按值传递。
    void handleStickInput(ControllerStick stick, float x, float y); // 摇杆输入入口（坐标已归一化）

// 【Qt】signals 关键字：声明信号区，其下声明的函数为"信号"——只声明不实现；
// 信号是 QObject 的发射点，用 emit 触发，可被任意槽或其他信号连接。
signals: // 信号区开始
    // 按钮命中映射（isPressed=true 按下，false 松开；mapping 为查询到的映射）
    void buttonMapped(ControllerButton button, bool isPressed, const KeyMapping& mapping); // 按钮映射信号
    // 摇杆输入（已应用死区）
    void stickMapped(ControllerStick stick, float x, float y); // 摇杆输入信号（已应用死区）
    // 当前激活层变化（未激活任何操作层时为 "Common"）
    void layerChanged(const QString& activeLayerName); // 激活层变化信号（携带新层名）
    // 当前操作集变化（name 为显示名）
    void operationSetChanged(const QString& name); // 操作集变化信号（携带显示名）
    // 配置被整体替换（loadProfile 或 setGlobalSettings 后触发）
    void profileChanged(); // 配置整体替换信号
    // 切换映射启停请求（由 ToggleMapping 动作触发）
    void toggleMappingRequested(); // 切换映射启停请求信号
    // 切换屏幕键盘请求（由 ToggleOnScreenKeyboard 动作触发）
    void toggleOnScreenKeyboardRequested(); // 切换屏幕键盘请求信号
    // 切换悬浮窗请求（由 ToggleOverlay 动作触发）
    void toggleOverlayRequested(); // 切换悬浮窗请求信号

private: // 【C++ 语法】private 访问说明符：其下方成员为私有实现细节，仅类内部可访问
    // 根据 activeLayers_ 重新计算 activeLayerName_ 并发出 layerChanged
    void updateActiveLayerName(); // 私有工具函数：重算当前激活层名并广播变化

    // 已激活操作层（按下顺序，后加入的优先级更高）
    // 【C++ 语法】QVector<OperationLayer*>：存放"指向 OperationLayer 的指针"的动态数组。
    // 成员用指针而非对象，因层对象由 profile 持有，这里只维护"激活顺序"引用。
    QVector<OperationLayer*> activeLayers_; // 激活层栈（后加入的优先级更高）
    // 记录「哪个按键激活了哪个层」，松开该按键时停用对应层并 return
    // 【C++ 语法】QHash<Key, Value>：哈希表（键值对容器），平均 O(1) 查找；
    // 键为手柄按键，值为其激活的层指针。
    QHash<ControllerButton, OperationLayer*> buttonTriggeredLayers_; // 按键->激活层的映射表
    // 当前物理按下的手柄按键集合
    QSet<ControllerButton> heldButtons_; // 当前物理按下的手柄按键集合（自动去重）
    // 【C++ 语法】QStringLiteral("Common")：编译期构造 QString 字面量，运行时零开销；
    // 该写法为"类内默认成员初始化"（C++11 起支持）。
    QString activeLayerName_ = QStringLiteral("Common"); // 当前激活层名，默认 "Common"（公共层）
}; // 【C++ 语法】类定义必须以分号结束（与函数定义不同）
