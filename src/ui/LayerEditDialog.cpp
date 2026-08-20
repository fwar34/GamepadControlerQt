#include "LayerEditDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

// 常用键：显示名 + Android keyCode
struct KeyEntry {
    QString name;
    int code;
};

QVector<KeyEntry> buildKeyList() {
    QVector<KeyEntry> keys;
    // 字母 A-Z
    for (int i = 0; i < 26; ++i)
        keys.append(KeyEntry{QString(QChar('A' + i)), AndroidKey::A + i});
    // 数字 0-9
    for (int i = 0; i < 10; ++i)
        keys.append(KeyEntry{QString(QChar('0' + i)), AndroidKey::N0 + i});
    // 功能键 F1-F12
    for (int i = 0; i < 12; ++i)
        keys.append(KeyEntry{QStringLiteral("F%1").arg(i + 1), AndroidKey::F1 + i});
    // 小键盘 0-9
    for (int i = 0; i < 10; ++i)
        keys.append(KeyEntry{QStringLiteral("Num%1").arg(i), AndroidKey::NUMPAD_0 + i});

    const QVector<KeyEntry> extra = {
        {QStringLiteral("Space"), AndroidKey::SPACE},
        {QStringLiteral("Enter"), AndroidKey::ENTER},
        {QStringLiteral("Tab"), AndroidKey::TAB},
        {QStringLiteral("Esc"), AndroidKey::ESCAPE},
        {QStringLiteral("Backspace"), AndroidKey::DEL},
        {QStringLiteral("Insert"), AndroidKey::INSERT},
        {QStringLiteral("Home"), AndroidKey::HOME},
        {QStringLiteral("End"), AndroidKey::MOVE_END},
        {QStringLiteral("PageUp"), AndroidKey::PAGE_UP},
        {QStringLiteral("PageDown"), AndroidKey::PAGE_DOWN},
        {QStringLiteral("Shift"), AndroidKey::SHIFT_LEFT},
        {QStringLiteral("Ctrl"), AndroidKey::CTRL_LEFT},
        {QStringLiteral("Alt"), AndroidKey::ALT_LEFT},
        {QStringLiteral("↑"), AndroidKey::DPAD_UP},
        {QStringLiteral("↓"), AndroidKey::DPAD_DOWN},
        {QStringLiteral("←"), AndroidKey::DPAD_LEFT},
        {QStringLiteral("→"), AndroidKey::DPAD_RIGHT},
        {QStringLiteral("-"), AndroidKey::MINUS},
        {QStringLiteral("="), AndroidKey::EQUALS},
        {QStringLiteral("["), AndroidKey::LEFT_BRACKET},
        {QStringLiteral("]"), AndroidKey::RIGHT_BRACKET},
        {QStringLiteral("\\"), AndroidKey::BACKSLASH},
        {QStringLiteral(";"), AndroidKey::SEMICOLON},
        {QStringLiteral("'"), AndroidKey::APOSTROPHE},
        {QStringLiteral(","), AndroidKey::COMMA},
        {QStringLiteral("."), AndroidKey::PERIOD},
        {QStringLiteral("/"), AndroidKey::SLASH},
        {QStringLiteral("`"), AndroidKey::GRAVE},
        {QStringLiteral("CapsLock"), AndroidKey::CAPS_LOCK},
        {QStringLiteral("NumLock"), AndroidKey::NUM_LOCK},
        {QStringLiteral("ScrollLock"), AndroidKey::SCROLL_LOCK},
    };
    keys += extra;
    return keys;
}

QComboBox* makeKeyCombo(bool withNone) {
    QComboBox* combo = new QComboBox;
    if (withNone)
        combo->addItem(QObject::tr("无"), -1);
    for (const KeyEntry& k : buildKeyList())
        combo->addItem(k.name, k.code);
    return combo;
}

QComboBox* makeMouseCombo() {
    QComboBox* combo = new QComboBox;
    const MouseButton buttons[] = {
        MouseButton::LEFT, MouseButton::RIGHT, MouseButton::MIDDLE,
        MouseButton::FORWARD, MouseButton::BACK,
    };
    for (const MouseButton b : buttons)
        combo->addItem(mouseButtonDisplayName(b), static_cast<int>(b));
    return combo;
}

QComboBox* makeLayerCombo(ControllerProfile* profile) {
    QComboBox* combo = new QComboBox;
    combo->addItem(QObject::tr("无"), QString());
    for (const OperationLayer& l : profile->layers)
        combo->addItem(layerDisplayName(l.name), l.name);
    return combo;
}

// 按 data 值选中下拉项（int 或 QString 均可）
void setComboIndex(QComboBox* combo, const QVariant& data) {
    const int idx = combo->findData(data);
    if (idx >= 0)
        combo->setCurrentIndex(idx);
}

}  // namespace

LayerEditDialog::LayerEditDialog(ControllerProfile* profile, OperationLayer* layer, QWidget* parent)
    : QDialog(parent), profile_(profile), layer_(layer), copy_(*layer) {
    setWindowTitle(tr("编辑层 - %1").arg(layerDisplayName(layer->name)));
    buildUi();

    buttonList_->setCurrentRow(0);
    if (buttonList_->currentItem())
        loadForm();
}

void LayerEditDialog::buildUi() {
    auto* root = new QVBoxLayout(this);

    // 层名称编辑
    auto* nameLayout = new QHBoxLayout;
    nameLayout->addWidget(new QLabel(tr("层名称："), this));
    layerNameEdit_ = new QLineEdit(copy_.name, this);
    nameLayout->addWidget(layerNameEdit_);
    root->addLayout(nameLayout);

    if (copy_.hasTriggerButton) {
        root->addWidget(new QLabel(
            tr("触发按键（仅显示用，实际切换由公共层的“切换层”映射完成）：%1")
                .arg(controllerButtonDisplayName(copy_.triggerButton)),
            this));
    }

    auto* hbox = new QHBoxLayout;

    // 左侧：按钮列表
    buttonList_ = new QListWidget(this);
    for (const ControllerButton b : allControllerButtons()) {
        const KeyMapping* m = copy_.getMapping(b);
        const QString desc = m ? m->describe() : QStringLiteral("—");
        QListWidgetItem* item = new QListWidgetItem(
            QStringLiteral("%1   %2").arg(controllerButtonDisplayName(b), desc), buttonList_);
        item->setData(Qt::UserRole, static_cast<int>(b));
    }
    connect(buttonList_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem* previous) {
                if (previous)
                    saveFormFor(static_cast<ControllerButton>(previous->data(Qt::UserRole).toInt()));
                if (current)
                    loadForm();
            });
    hbox->addWidget(buttonList_, 1);

    // 右侧：编辑表单
    auto* form = new QVBoxLayout;

    actionTypeCombo_ = new QComboBox(this);
    actionTypeCombo_->addItems({
        tr("无（不映射）"), tr("键盘按键"), tr("鼠标点击"), tr("鼠标长按"),
        tr("切换层"), tr("鼠标移动"), tr("视角控制"),
    });
    connect(actionTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LayerEditDialog::updateParamPage);
    form->addWidget(new QLabel(tr("动作类型"), this));
    form->addWidget(actionTypeCombo_);

    paramStack_ = new QStackedWidget(this);
    paramStack_->addWidget(new QWidget(this));                     // 0 无
    keyCombo_ = makeKeyCombo(false);
    paramStack_->addWidget(keyCombo_);                             // 1 键盘按键
    mouseCombo_ = makeMouseCombo();
    paramStack_->addWidget(mouseCombo_);                           // 2 鼠标点击
    paramStack_->addWidget(mouseCombo_);                           // 3 鼠标长按
    layerCombo_ = makeLayerCombo(profile_);
    paramStack_->addWidget(layerCombo_);                           // 4 切换层
    paramStack_->addWidget(new QLabel(tr("由左摇杆输入驱动"), this)); // 5 鼠标移动
    paramStack_->addWidget(new QLabel(tr("由右摇杆输入驱动"), this)); // 6 视角控制
    form->addWidget(paramStack_);

    form->addWidget(new QLabel(tr("子命令（组合键，最多 3 个；仅在键盘按键时生效）"), this));
    auto* subRow = new QHBoxLayout;
    for (int i = 0; i < 3; ++i) {
        subCombos_[i] = makeKeyCombo(true);
        subRow->addWidget(subCombos_[i], 1);
    }
    form->addLayout(subRow);
    form->addStretch(1);

    hbox->addLayout(form, 1);
    root->addLayout(hbox, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &LayerEditDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &LayerEditDialog::reject);
    root->addWidget(buttons);

    resize(620, 480);
}

ControllerButton LayerEditDialog::currentButton() const {
    if (QListWidgetItem* item = buttonList_->currentItem())
        return static_cast<ControllerButton>(item->data(Qt::UserRole).toInt());
    return ControllerButton::A;
}

void LayerEditDialog::loadForm() {
    loading_ = true;
    const ControllerButton b = currentButton();
    const KeyMapping* m = copy_.getMapping(b);

    if (!m) {
        actionTypeCombo_->setCurrentIndex(0);
    } else {
        switch (m->action.type) {
            case MappedAction::Type::KeyboardKey:
                actionTypeCombo_->setCurrentIndex(1);
                setComboIndex(keyCombo_, m->action.keyCode);
                break;
            case MappedAction::Type::MouseClick:
                actionTypeCombo_->setCurrentIndex(2);
                setComboIndex(mouseCombo_, static_cast<int>(m->action.mouseButton));
                break;
            case MappedAction::Type::MouseToggle:
                actionTypeCombo_->setCurrentIndex(3);
                setComboIndex(mouseCombo_, static_cast<int>(m->action.mouseButton));
                break;
            case MappedAction::Type::SwitchLayer:
                actionTypeCombo_->setCurrentIndex(4);
                setComboIndex(layerCombo_, m->action.layerName);
                break;
            case MappedAction::Type::MouseMove:
                actionTypeCombo_->setCurrentIndex(5);
                break;
            case MappedAction::Type::LookAround:
                actionTypeCombo_->setCurrentIndex(6);
                break;
        }
    }

    for (int i = 0; i < 3; ++i) {
        subCombos_[i]->setCurrentIndex(0);
        if (m && i < m->subCommands.size())
            setComboIndex(subCombos_[i], m->subCommands[i]);
    }

    loading_ = false;
    updateParamPage(actionTypeCombo_->currentIndex());
}

void LayerEditDialog::saveFormFor(ControllerButton button) {
    if (buttonList_ == nullptr)
        return;

    const int typeIdx = actionTypeCombo_->currentIndex();
    if (typeIdx <= 0) {  // 无（不映射）
        copy_.buttonMappings.remove(button);
        return;
    }

    KeyMapping m;
    switch (typeIdx) {
        case 1:  // 键盘按键
            m.action = MappedAction::keyboardKey(keyCombo_->currentData().toInt());
            break;
        case 2:  // 鼠标点击
            m.action = MappedAction::mouseClick(
                static_cast<MouseButton>(mouseCombo_->currentData().toInt()));
            break;
        case 3:  // 鼠标长按
            m.action = MappedAction::mouseToggle(
                static_cast<MouseButton>(mouseCombo_->currentData().toInt()));
            break;
        case 4: {  // 切换层
            const QString layerName = layerCombo_->currentData().toString();
            if (layerName.isEmpty()) {
                copy_.buttonMappings.remove(button);
                return;
            }
            m.action = MappedAction::switchLayer(layerName);
            break;
        }
        case 5:  // 鼠标移动
            m.action = MappedAction::mouseMove();
            break;
        case 6:  // 视角控制
            m.action = MappedAction::lookAround();
            break;
        default:
            return;
    }

    for (int i = 0; i < 3; ++i) {
        const int sub = subCombos_[i]->currentData().toInt();
        if (sub >= 0 && sub != m.action.keyCode && !m.subCommands.contains(sub))
            m.subCommands.append(sub);
    }
    copy_.buttonMappings.insert(button, m);
}

void LayerEditDialog::updateParamPage(int typeIndex) {
    paramStack_->setCurrentIndex(typeIndex);
    if (loading_)
        return;
    saveFormFor(currentButton());
}

void LayerEditDialog::accept() {
    saveFormFor(currentButton());
    // 更新层名称
    copy_.name = layerNameEdit_->text().trimmed();
    *layer_ = copy_;
    QDialog::accept();
}
