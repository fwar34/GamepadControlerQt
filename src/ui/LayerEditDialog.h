#pragma once

#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QComboBox>
#include <QLineEdit>

#include "../core/MappingTypes.h"

class QLabel;
class QPushButton;

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
// =====================================================================
class LayerEditDialog : public QDialog {
    Q_OBJECT
public:
    // profile：用于选择 SwitchLayer 目标层；layer：要编辑的操作层
    explicit LayerEditDialog(ControllerProfile* profile, OperationLayer* layer, QWidget* parent = nullptr);

private slots:
    // 将当前选中按钮的映射加载到界面
    void loadForm();
    // 将界面当前内容写回 copy_
    void saveFormFor(ControllerButton button);
    // 动作类型切换时切换参数面板
    void updateParamPage(int typeIndex);
    // 保存当前按钮并整体写回原始层
    void accept() override;

private:
    ControllerProfile* profile_ = nullptr;
    OperationLayer* layer_ = nullptr;
    OperationLayer copy_;   // 编辑副本，确定时才写回

    bool loading_ = false;  // 防止初始化期间信号递归
    QLineEdit* layerNameEdit_ = nullptr;   // 层显示名称输入框
    QListWidget* buttonList_ = nullptr;    // 左侧按钮列表
    QStackedWidget* paramStack_ = nullptr; // 动作参数面板
    QComboBox* actionTypeCombo_ = nullptr; // 动作类型（含"无"）
    QComboBox* keyCombo_ = nullptr;        // 键盘键（KeyboardKey）
    QComboBox* mouseCombo_ = nullptr;      // 鼠标键（MouseClick）
    QComboBox* mouseToggleCombo_ = nullptr; // 鼠标键（MouseToggle）
    QComboBox* layerCombo_ = nullptr;      // 目标层（SwitchLayer）
    QComboBox* subCombos_[3] = {};         // 子命令组合键（最多 3 个）

    void buildUi();
    ControllerButton currentButton() const;  // 当前选中按钮
    // 生成映射的描述文本（左侧按钮列表项用，SwitchLayer 解析为目标层显示名）
    QString mappingDesc(const KeyMapping* m) const;
    // 按副本刷新左侧指定按钮列表项的文本（右侧改动后即时同步）
    void updateButtonListItem(ControllerButton button);
};
