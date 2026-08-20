#pragma once

#include "../core/MappingTypes.h"

#include <QDialog>

class QComboBox;
class QListWidget;
class QStackedWidget;

// =====================================================================
// 操作层编辑对话框
// 编辑某个操作层（或公共层）的按键映射。
// 在副本上编辑，确认时写回原 OperationLayer（取消则不改动）。
// =====================================================================
class LayerEditDialog : public QDialog {
    Q_OBJECT
public:
    LayerEditDialog(ControllerProfile* profile, OperationLayer* layer, QWidget* parent = nullptr);

private:
    void buildUi();
    ControllerButton currentButton() const;
    void loadForm();
    void saveFormFor(ControllerButton button);
    void updateParamPage(int typeIndex);
    void accept() override;

    ControllerProfile* profile_;
    OperationLayer* layer_;   // 原始层（accept 时写回）
    OperationLayer copy_;     // 编辑副本

    QListWidget* buttonList_ = nullptr;
    QComboBox* actionTypeCombo_ = nullptr;
    QStackedWidget* paramStack_ = nullptr;
    QComboBox* keyCombo_ = nullptr;
    QComboBox* mouseCombo_ = nullptr;
    QComboBox* layerCombo_ = nullptr;
    QComboBox* subCombos_[3] = {nullptr, nullptr, nullptr};

    bool loading_ = false;
};
