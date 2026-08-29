#pragma once   // 【C++ 语法】预处理指令：#pragma once 保证本头文件只被包含一次（等价于传统 include guard）

#include <QDialog>   // 【C++ 语法】#include：包含 Qt 对话框基类头文件
#include <QHash>     // 【Qt】包含 QHash（哈希表容器）头文件，用于"按键 -> 标签"映射
#include <QListWidget>  // 【Qt】包含列表控件头文件（左侧手柄按键列表）
#include <QSet>      // 【Qt】包含 QSet（集合容器）头文件（保存当前按下的手柄按键）
#include <QStackedWidget>  // 【Qt】包含堆叠页面控件头文件（按动作类型切换参数面板）
#include <QComboBox> // 【Qt】包含下拉框控件头文件
#include <QLineEdit> // 【Qt】包含单行输入框控件头文件

#include "../core/MappingTypes.h"   // 【C++ 语法】相对路径包含核心映射类型头文件（ControllerProfile/OperationLayer/KeyMapping 等定义）

// 【C++ 语法】前置声明：只声明"类存在"而不包含完整头文件，适用于仅以指针引用的类型，可加快编译、避免循环包含
class QLabel;   // 前置声明 QLabel 类
class QPushButton;  // 前置声明 QPushButton 类
class XInputGamepadSource;   // 前置声明手柄源类（本类只存其指针）

// =====================================================================
// LayerEditDialog —— 操作层编辑对话框
//
// 编辑指定操作层的按键映射（按钮 -> 动作 + 子命令）以及层显示名称。
//
// 设计要点：
//   - 构造时复制一份 OperationLayer（copy_），所有编辑都在副本上进行，
//     点击「确定」（accept）时才整体写回原始层 *layer_ = copy_，
//     避免中途取消产生半修改状态。
//   - 左侧按钮列表（buttonList_）选择要编辑的手柄按钮；
//     右侧根据动作类型（actionTypeCombo_）显示不同参数面板
//     （paramStack_：键盘键 / 鼠标键 / 目标层 / 无参数）。
//   - loading_ 标志防止初始化时信号触发 saveFormFor 产生递归。
//   - gamepad：用于实时显示手柄按键按下状态（对应按钮列表项高亮），
//     便于调试映射是否正确命中。
// =====================================================================
// 【C++ 语法】类定义：class LayerEditDialog 公有继承 : public QDialog（is-a 关系，获得对话框全部能力）
class LayerEditDialog : public QDialog {
    // 【Qt】Q_OBJECT 宏：必须出现在含 signals/slots 的类中；让 moc 元对象编译器生成信号槽机制所需的元数据，是 connect 能工作的前提
    Q_OBJECT
public:   // 【C++ 语法】访问控制关键字：public 成员对外公开，任何代码都可调用
    // profile：用于选择 SwitchLayer 目标层；layer：要编辑的操作层；gamepad：手柄源（实时按键高亮）
    // 【C++ 语法】构造函数声明：explicit 禁止隐式类型转换（只能显式构造）；参数为指针类型（*）；
    //   parent 带默认参数 = nullptr，调用时可省略该参数
    explicit LayerEditDialog(ControllerProfile* profile, OperationLayer* layer,
                             XInputGamepadSource* gamepad, QWidget* parent = nullptr);   // 构造函数声明结束

private slots:   // 【Qt】slots 关键字：标记以下为槽函数（可被信号 connect 连接）；private 限定只能在类内部连接
    // 将当前选中按钮的映射加载到界面
    void loadForm();   // 【C++ 语法】成员函数声明：返回 void，无参数
    // 将界面当前内容写回 copy_
    void saveFormFor(ControllerButton button);   // 保存指定按键的表单内容到副本
    // 动作类型切换时切换参数面板
    void updateParamPage(int typeIndex);   // 参数：动作类型索引 int
    // 保存当前按钮并整体写回原始层
    // 【C++ 语法】override：覆盖基类 QDialog 的虚函数 accept()（虚函数允许派生类重写）
    void accept() override;   // 重写确定行为：回写副本后关闭
    // 手柄按键按下/松开 -> 左侧按钮列表对应项高亮（松开恢复）
    void onGamepadButton(ControllerButton button, bool isPressed);   // 手柄按键变化槽：按键 + 是否按下

private:   // 【C++ 语法】访问控制关键字：private 成员仅类内部（含成员函数）可访问
    // 【C++ 语法】成员变量声明：* 表示指针成员；= nullptr 为类内初始化（默认空指针，C++11 起支持）
    ControllerProfile* profile_ = nullptr;   // 指向配置文件（用于查找层列表/公共层）
    OperationLayer* layer_ = nullptr;   // 指向要编辑的真实操作层
    // 【C++ 语法】成员对象（非指针）：copy_ 是 OperationLayer 类型的完整对象（值语义）
    OperationLayer copy_;   // 编辑副本，确定时才写回

    // 【C++ 语法】成员变量声明：指针成员带 = nullptr 类内初始化；容器（QHash/QSet）与数组（subCombos_[3]）成员也在此声明
    bool loading_ = false;  // 防止初始化期间信号递归
    QLineEdit* layerNameEdit_ = nullptr;   // 层显示名称输入框
    QListWidget* buttonList_ = nullptr;    // 左侧按钮列表
    QHash<ControllerButton, QLabel*> buttonLabels_;  // 列表项标签（item widget，手柄按下高亮用）
    QSet<ControllerButton> pressedButtons_;          // 当前按下的手柄键集合
    QStackedWidget* paramStack_ = nullptr; // 动作参数面板
    QComboBox* actionTypeCombo_ = nullptr; // 动作类型（含"无"）
    QComboBox* keyCombo_ = nullptr;        // 键盘键（KeyboardKey）
    QComboBox* mouseCombo_ = nullptr;      // 鼠标键（MouseClick）
    QComboBox* mouseToggleCombo_ = nullptr; // 鼠标键（MouseToggle）
    QComboBox* layerCombo_ = nullptr;      // 目标层（SwitchLayer）
    QComboBox* subCombos_[3] = {};         // 子命令组合键（最多 3 个）

    void buildUi();   // 【C++ 语法】私有成员函数声明：void 无返回值；构建整个界面（仅类内部调用）
    ControllerButton currentButton() const;  // 【C++ 语法】const 成员函数：末尾 const 表示不修改对象状态；返回当前选中按键
    // 生成映射的描述文本（左侧按钮列表项用，SwitchLayer 解析为目标层显示名）
    QString mappingDesc(const KeyMapping* m) const;   // 【C++ 语法】const 成员函数；参数为指向映射的常量指针
    // 按副本刷新左侧指定按钮列表项的文本（右侧改动后即时同步）
    void updateButtonListItem(ControllerButton button);
    // 生成列表项标签样式（selected=当前选中，pressed=手柄正按下）
    QString itemStyle(bool selected, bool pressed) const;   // 【C++ 语法】const 成员函数，两个 bool 参数
    // 刷新所有列表项标签样式（选中/按下状态）
    void refreshItemStyles();   // 【C++ 语法】成员函数声明结束
};
