#pragma once

#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QComboBox>
#include <QLineEdit>

#include "../core/MappingTypes.h"

class QLabel;
class QPushButton;

class LayerEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit LayerEditDialog(ControllerProfile* profile, OperationLayer* layer, QWidget* parent = nullptr);
    
private slots:
    void loadForm();
    void saveFormFor(ControllerButton button);
    void updateParamPage(int typeIndex);
    void accept() override;
    
private:
    ControllerProfile* profile_ = nullptr;
    OperationLayer* layer_ = nullptr;
    OperationLayer copy_;
    
    bool loading_ = false;
    QLineEdit* layerNameEdit_ = nullptr;
    QListWidget* buttonList_ = nullptr;
    QStackedWidget* paramStack_ = nullptr;
    QComboBox* actionTypeCombo_ = nullptr;
    QComboBox* keyCombo_ = nullptr;
    QComboBox* mouseCombo_ = nullptr;
    QComboBox* layerCombo_ = nullptr;
    QComboBox* subCombos_[3] = {};
    
    void buildUi();
    ControllerButton currentButton() const;
};
