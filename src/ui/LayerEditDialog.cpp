// ============================================================
// LayerEditDialog.cpp
// 层编辑对话框：配置某个手柄按键的映射
// ------------------------------------------------------------
// 左侧为手柄按键列表，右侧为对应按键的映射编辑表单：
//   动作类型（无/键盘/鼠标点击/鼠标长按/切换层/鼠标移动/视角控制/
//             切换映射/切换屏幕键盘/切换悬浮窗）
//   + 参数（键、鼠标键、目标层等）+ 子命令（组合键，最多 3 个）
//
// 关键设计：
//   - "副本"模式：进入对话框时把目标层复制到 copy_，编辑期间
//     所有改动只写副本，只有点"确定"（accept）才回写真实层；
//     点"取消"则全部丢弃。因此编辑过程中不会污染运行中的映射。
//   - 切换左侧按钮列表项时，会先把当前按钮的表单保存进副本，
//     再加载新按钮的配置（saveFormFor / loadForm 配对）。
//   - 仅"键盘按键"动作允许配置子命令（组合键，实现 Alt+3 等）。
// ============================================================

#include "LayerEditDialog.h"
#include "DarkTitleBar.h"

#include "../gamepad/XInputGamepadSource.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QStringList>
#include <QVBoxLayout>

namespace {

// ------------------------------------------------------------
// 常用键条目：显示名 + Android keyCode
// ------------------------------------------------------------
struct KeyEntry {
    QString name;
    int code;
};

// ------------------------------------------------------------
// buildKeyList：构建全部可选按键列表（显示名 -> Android keyCode）
// ------------------------------------------------------------
// 与 InputTypes.cpp 的 keyCodeToName 保持一致的反向映射，
// 供键盘按键下拉框 / 子命令下拉框使用。
QVector<KeyEntry> buildKeyList() {
    QVector<KeyEntry> keys;
    // 字母 A-Z（AndroidKey::A 起连续）
    for (int i = 0; i < 26; ++i)
        keys.append(KeyEntry{QString(QChar('A' + i)), AndroidKey::A + i});
    // 数字 0-9（AndroidKey::N0 起连续）
    for (int i = 0; i < 10; ++i)
        keys.append(KeyEntry{QString(QChar('0' + i)), AndroidKey::N0 + i});
    // 功能键 F1-F12
    for (int i = 0; i < 12; ++i)
        keys.append(KeyEntry{QStringLiteral("F%1").arg(i + 1), AndroidKey::F1 + i});
    // 小键盘 0-9
    for (int i = 0; i < 10; ++i)
        keys.append(KeyEntry{QStringLiteral("Num%1").arg(i), AndroidKey::NUMPAD_0 + i});

    // 散键（空格/回车/方向/符号/锁键等）
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

// ------------------------------------------------------------
// makeKeyCombo：创建键盘键下拉框
// ------------------------------------------------------------
// withNone=true 时首项为"无"（data=-1），供子命令使用。
QComboBox* makeKeyCombo(bool withNone) {
    QComboBox* combo = new QComboBox;
    if (withNone)
        combo->addItem(QObject::tr("无"), -1);
    for (const KeyEntry& k : buildKeyList())
        combo->addItem(k.name, k.code);
    return combo;
}

// ------------------------------------------------------------
// makeMouseCombo：创建鼠标键下拉框（点击/长按共用）
// ------------------------------------------------------------
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

// ------------------------------------------------------------
// makeLayerCombo：创建目标层下拉框（切换层动作）
// ------------------------------------------------------------
QComboBox* makeLayerCombo(ControllerProfile* profile) {
    QComboBox* combo = new QComboBox;
    combo->addItem(QObject::tr("无"), QString());
    for (const OperationLayer& l : profile->layers)
        combo->addItem(l.name, l.id);
    return combo;
}

// ------------------------------------------------------------
// setComboIndex：按 data 值选中下拉项
// ------------------------------------------------------------
// 兼容 int（键码/鼠标键）与 QString（层名）两种 data 类型。
void setComboIndex(QComboBox* combo, const QVariant& data) {
    const int idx = combo->findData(data);
    if (idx >= 0)
        combo->setCurrentIndex(idx);
}

}  // namespace

// ============================================================
// 构造：初始化副本并搭建界面
// ============================================================
// copy_ = *layer 建立编辑副本；初始选中第一个手柄按钮并加载其配置。
LayerEditDialog::LayerEditDialog(ControllerProfile* profile, OperationLayer* layer,
                                 XInputGamepadSource* gamepad, QWidget* parent)
    : QDialog(parent), profile_(profile), layer_(layer), copy_(*layer) {
    setWindowTitle(tr("编辑层 - %1").arg(layer->name));
    buildUi();

    // 手柄按键实时高亮：按下 -> 左侧列表对应项变色，松开恢复。
    // 显式 QueuedConnection：buttonChanged 从轮询线程发出，保证在 GUI 线程更新控件。
    connect(gamepad, &XInputGamepadSource::buttonChanged,
            this, &LayerEditDialog::onGamepadButton, Qt::QueuedConnection);

    // 默认选中列表第一项（A 键）并加载表单
    buttonList_->setCurrentRow(0);
    if (buttonList_->currentItem())
        loadForm();
}

// ============================================================
// onGamepadButton：手柄按键按下/松开 -> 左侧按钮列表对应项高亮
// ============================================================
// 与主界面操作层激活按钮同样用青绿色（#22958c）提示，
// 方便在编辑映射时按手柄确认「哪个键对应哪个列表项」。
// 只维护 pressedButtons_ 集合，实际样式统一交给 refreshItemStyles()
// 重算（QListWidget::item 的 QSS 会覆盖 setBackground，故列表项文本
// 由 item widget（QLabel）承载，样式直接设置在标签上，可正常高亮背景）。
// 注：本对话框为模态，手柄线程的信号经队列投递到 UI 线程处理。
void LayerEditDialog::onGamepadButton(ControllerButton button, bool isPressed) {
    if (!buttonList_)
        return;
    if (isPressed)
        pressedButtons_.insert(button);
    else
        pressedButtons_.remove(button);
    refreshItemStyles();
}

// ============================================================
// buildUi：构建整个对话框界面
// ============================================================
// 布局：层名编辑行 -> （可选）触发按键提示 -> 左右分栏
//   （左：按键列表 | 右：动作类型 + 参数 + 子命令）-> 确定/取消。
void LayerEditDialog::buildUi() {
    auto* root = new QVBoxLayout(this);

    // ---- 层名称编辑 ----
    auto* nameLayout = new QHBoxLayout;
    nameLayout->addWidget(new QLabel(tr("层名称："), this));
    layerNameEdit_ = new QLineEdit(copy_.name, this);
    nameLayout->addWidget(layerNameEdit_);
    root->addLayout(nameLayout);

    // 触发按键提示（仅操作层有）：扫描公共层中实际配置为
    // 「切换层 -> 本层」的按键，展示真正驱动进入本层的映射；
    // triggerButton 字段仅作无映射时的补充提示。
    const bool isCommon = (layer_ == &profile_->commonLayer);
    if (!isCommon) {
        QStringList triggers;
        for (const ControllerButton b : allControllerButtons()) {
            const KeyMapping* m = profile_->commonLayer.getMapping(b);
            if (m && m->action.type == MappedAction::Type::SwitchLayer
                && m->action.layerName == copy_.id)
                triggers.append(controllerButtonDisplayName(b));
        }
        QString triggerText;
        if (!triggers.isEmpty()) {
            triggerText = tr("触发按键（切换到本层）：%1").arg(triggers.join(", "));
        } else if (copy_.hasTriggerButton) {
            triggerText = tr("触发按键（建议，尚未配置切换映射）：%1")
                              .arg(controllerButtonDisplayName(copy_.triggerButton));
        } else {
            triggerText = tr("触发按键：未配置（请在公共层或其他层为某按键设置“切换层”动作）");
        }
        auto* triggerLabel = new QLabel(triggerText, this);
        triggerLabel->setStyleSheet("color: #d0a060;");
        root->addWidget(triggerLabel);
    }

    auto* hbox = new QHBoxLayout;

    // ---- 左侧：手柄按键列表 ----
    // 每个列表项用一个 QLabel 作为 item widget 承载文本与样式：
    // 直接 setBackground 会被下方 QListWidget::item 的 QSS 覆盖，
    // 改用 item widget 后样式设置在标签上，手柄按下时可正常高亮背景。
    buttonList_ = new QListWidget(this);
    for (const ControllerButton b : allControllerButtons()) {
        QListWidgetItem* item = new QListWidgetItem(buttonList_);
        item->setData(Qt::UserRole, static_cast<int>(b));
        auto* label = new QLabel(
            QStringLiteral("%1   %2").arg(controllerButtonDisplayName(b),
                                          mappingDesc(copy_.getMapping(b))),
            buttonList_);
        label->setTextFormat(Qt::PlainText);
        label->setStyleSheet(itemStyle(false, false));
        item->setSizeHint(label->sizeHint());
        buttonList_->setItemWidget(item, label);
        buttonLabels_.insert(b, label);
    }
    // 切换选中项：先把上一个按钮的表单存进副本，再加载新按钮，
    // 最后刷新全部列表项样式（选中高亮跟随切换）
    connect(buttonList_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem* previous) {
                if (previous)
                    saveFormFor(static_cast<ControllerButton>(previous->data(Qt::UserRole).toInt()));
                if (current)
                    loadForm();
                refreshItemStyles();
            });
    hbox->addWidget(buttonList_, 1);

    // ---- 右侧：映射编辑表单 ----
    auto* form = new QVBoxLayout;

    // 动作类型下拉框：索引与 paramStack_ 页一一对应
    actionTypeCombo_ = new QComboBox(this);
    actionTypeCombo_->addItems({
        tr("无（不映射）"), tr("键盘按键"), tr("鼠标点击"), tr("鼠标长按"),
        tr("鼠标滚轮上滚"), tr("鼠标滚轮下滚"),
        tr("切换层"), tr("鼠标移动"), tr("视角控制"),
        tr("切换映射"), tr("切换屏幕键盘"), tr("切换悬浮窗"),
    });
    // 切换动作类型时刷新参数页
    connect(actionTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LayerEditDialog::updateParamPage);
    form->addWidget(new QLabel(tr("动作类型"), this));
    form->addWidget(actionTypeCombo_);

    // 参数区（QStackedWidget 按动作类型切换页面）：
    //   0 无 | 1 键盘 | 2 鼠标点击 | 3 鼠标长按 | 4 滚轮上 | 5 滚轮下
    //   | 6 切换层 | 7 鼠标移动 | 8 视角控制 | 9 切换映射 | 10 屏幕键盘 | 11 悬浮窗
    paramStack_ = new QStackedWidget(this);
    paramStack_->addWidget(new QWidget(this));                     // 0 无
    keyCombo_ = makeKeyCombo(false);
    paramStack_->addWidget(keyCombo_);                             // 1 键盘按键
    mouseCombo_ = makeMouseCombo();
    paramStack_->addWidget(mouseCombo_);                           // 2 鼠标点击
    mouseToggleCombo_ = makeMouseCombo();
    paramStack_->addWidget(mouseToggleCombo_);                     // 3 鼠标长按
    paramStack_->addWidget(new QLabel(tr("按下时向上滚动一格"), this)); // 4 滚轮上
    paramStack_->addWidget(new QLabel(tr("按下时向下滚动一格"), this)); // 5 滚轮下
    layerCombo_ = makeLayerCombo(profile_);
    paramStack_->addWidget(layerCombo_);                           // 6 切换层
    paramStack_->addWidget(new QLabel(tr("由左摇杆输入驱动"), this)); // 7 鼠标移动
    paramStack_->addWidget(new QLabel(tr("由右摇杆输入驱动"), this)); // 8 视角控制
    paramStack_->addWidget(new QLabel(tr("按下时切换映射启停"), this)); // 9 切换映射
    paramStack_->addWidget(new QLabel(tr("按下时切换屏幕键盘"), this)); // 10 切换屏幕键盘
    paramStack_->addWidget(new QLabel(tr("按下时切换悬浮窗"), this)); // 11 切换悬浮窗
    form->addWidget(paramStack_);

    // 子命令（组合键）：仅键盘按键动作生效，最多 3 个
    form->addWidget(new QLabel(tr("子命令（组合键，最多 3 个；仅在键盘按键时生效）"), this));
    auto* subRow = new QHBoxLayout;
    for (int i = 0; i < 3; ++i) {
        subCombos_[i] = makeKeyCombo(true);
        subRow->addWidget(subCombos_[i], 1);
    }
    form->addLayout(subRow);
    form->addStretch(1);

    // 参数下拉框变更即保存并刷新左侧列表（键盘键/鼠标键/目标层/子命令），
    // 保证右侧任何改动都即时同步到左侧按钮描述，无需等重新打开对话框。
    // loading_ 置位时（loadForm 填充控件）跳过，避免初始化期递归保存。
    auto saveCurrent = [this]() {
        if (!loading_)
            saveFormFor(currentButton());
    };
    connect(keyCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, saveCurrent);
    connect(mouseCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, saveCurrent);
    connect(mouseToggleCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, saveCurrent);
    connect(layerCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, saveCurrent);
    for (int i = 0; i < 3; ++i)
        connect(subCombos_[i], QOverload<int>::of(&QComboBox::currentIndexChanged), this, saveCurrent);

    hbox->addLayout(form, 1);
    root->addLayout(hbox, 1);

    // 确定 / 取消
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &LayerEditDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &LayerEditDialog::reject);
    root->addWidget(buttons);

    // ---- 深色主题（与主窗口一致的灰底 + 青绿强调） ----
    setStyleSheet(R"(
        QDialog {
            background-color: #2b2d31;
        }
        QLabel {
            color: #d5d9df;
            background-color: transparent;
        }
        QLineEdit {
            background-color: #33363b;
            color: #e8eaee;
            border: 1px solid #4a4e55;
            border-radius: 5px;
            padding: 4px 8px;
            selection-background-color: #7fc9c4;
            selection-color: #1c1e22;
        }
        QLineEdit:focus {
            border-color: #7fc9c4;
        }
        QListWidget {
            background-color: #2b2d31;
            color: #e8eaee;
            border: 1px solid #4a4e55;
            border-radius: 6px;
            outline: none;
        }
        /* 列表项样式由 item widget（QLabel）承载，不再在此设置 ::item 规则，
           否则会覆盖标签背景导致手柄按下高亮失效 */
        QComboBox {
            background-color: #33363b;
            color: #e8eaee;
            border: 1px solid #4a4e55;
            border-radius: 5px;
            padding: 3px 8px;
        }
        QComboBox:hover {
            border-color: #7fc9c4;
        }
        QComboBox::drop-down {
            border: none;
            background: transparent;
            width: 22px;
        }
        QComboBox::down-arrow {
            image: url(:/icons/down-arrow.png);
            width: 10px;
            height: 6px;
        }
        QComboBox QAbstractItemView {
            background-color: #33363b;
            color: #e8eaee;
            border: 1px solid #4a4e55;
            selection-background-color: #22958c;
            selection-color: #ffffff;
        }
        QPushButton {
            background-color: #3d4147;
            color: #e8eaee;
            border: 1px solid #4a4e55;
            border-radius: 6px;
            padding: 5px 14px;
        }
        QPushButton:hover {
            background-color: #474b52;
            border-color: #7fc9c4;
        }
        QPushButton:pressed {
            background-color: #2f3237;
        }
        QDialogButtonBox {
            background-color: transparent;
        }
    )");

    // ---- 标题栏深色化，与主窗口一致 ----
    enableDarkTitleBar(this);

    resize(620, 480);
}

// ============================================================
// currentButton：返回左侧当前选中的手柄按钮
// ============================================================
ControllerButton LayerEditDialog::currentButton() const {
    if (QListWidgetItem* item = buttonList_->currentItem())
        return static_cast<ControllerButton>(item->data(Qt::UserRole).toInt());
    return ControllerButton::A;
}

// ============================================================
// loadForm：把副本中"当前按钮"的映射加载进表单控件
// ============================================================
// loading_ 置位可抑制 updateParamPage 的连带保存动作，避免循环。
void LayerEditDialog::loadForm() {
    loading_ = true;
    const ControllerButton b = currentButton();
    const KeyMapping* m = copy_.getMapping(b);

    // 无映射 -> 动作类型选"无"；有映射 -> 按类型填充控件
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
                setComboIndex(mouseToggleCombo_, static_cast<int>(m->action.mouseButton));
                break;
            case MappedAction::Type::WheelUp:
                actionTypeCombo_->setCurrentIndex(4);
                break;
            case MappedAction::Type::WheelDown:
                actionTypeCombo_->setCurrentIndex(5);
                break;
            case MappedAction::Type::SwitchLayer:
                actionTypeCombo_->setCurrentIndex(6);
                // 先按 id 匹配；匹配不到则按 name 匹配（兼容旧配置）
                {
                    const int idx = layerCombo_->findData(m->action.layerName);
                    if (idx >= 0) {
                        layerCombo_->setCurrentIndex(idx);
                    } else {
                        for (int i = 0; i < layerCombo_->count(); ++i) {
                            const QString layerId = layerCombo_->itemData(i).toString();
                            const OperationLayer* l = profile_->findLayer(layerId);
                            if (l && l->name == m->action.layerName) {
                                layerCombo_->setCurrentIndex(i);
                                break;
                            }
                        }
                    }
                }
                break;
            case MappedAction::Type::MouseMove:
                actionTypeCombo_->setCurrentIndex(7);
                break;
            case MappedAction::Type::LookAround:
                actionTypeCombo_->setCurrentIndex(8);
                break;
            case MappedAction::Type::ToggleMapping:
                actionTypeCombo_->setCurrentIndex(9);
                break;
            case MappedAction::Type::ToggleOnScreenKeyboard:
                actionTypeCombo_->setCurrentIndex(10);
                break;
            case MappedAction::Type::ToggleOverlay:
                actionTypeCombo_->setCurrentIndex(11);
                break;
        }
    }

    // 子命令：逐个填充（无子命令时保持"无"）
    for (int i = 0; i < 3; ++i) {
        subCombos_[i]->setCurrentIndex(0);
        if (m && i < m->subCommands.size())
            setComboIndex(subCombos_[i], m->subCommands[i]);
    }

    loading_ = false;
    updateParamPage(actionTypeCombo_->currentIndex());
}

// ============================================================
// saveFormFor：把表单当前内容保存进副本的指定按钮映射
// ============================================================
// 动作类型为"无"时移除该按钮映射（等价于清除配置）。
// 仅键盘按键动作收集子命令，且过滤掉与主动作相同或重复的键。
void LayerEditDialog::saveFormFor(ControllerButton button) {
    if (buttonList_ == nullptr)
        return;

    const int typeIdx = actionTypeCombo_->currentIndex();
    if (typeIdx <= 0) {  // 无（不映射）：移除现有映射
        copy_.buttonMappings.remove(button);
        updateButtonListItem(button);
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
                static_cast<MouseButton>(mouseToggleCombo_->currentData().toInt()));
            break;
        case 4:  // 鼠标滚轮上滚
            m.action = MappedAction::wheelUp();
            break;
        case 5:  // 鼠标滚轮下滚
            m.action = MappedAction::wheelDown();
            break;
        case 6: {  // 切换层：目标层为空则视为清除
            const QString layerName = layerCombo_->currentData().toString();
            if (layerName.isEmpty()) {
                copy_.buttonMappings.remove(button);
                updateButtonListItem(button);
                return;
            }
            m.action = MappedAction::switchLayer(layerName);
            break;
        }
        case 7:  // 鼠标移动
            m.action = MappedAction::mouseMove();
            break;
        case 8:  // 视角控制
            m.action = MappedAction::lookAround();
            break;
        case 9:  // 切换映射
            m.action = MappedAction::toggleMapping();
            break;
        case 10:  // 切换屏幕键盘
            m.action = MappedAction::toggleOnScreenKeyboard();
            break;
        case 11:  // 切换悬浮窗
            m.action = MappedAction::toggleOverlay();
            break;
        default:
            return;
    }

    // 收集子命令：跳过"无"、与主动作相同的键、已存在的重复键
    for (int i = 0; i < 3; ++i) {
        const int sub = subCombos_[i]->currentData().toInt();
        if (sub >= 0 && sub != m.action.keyCode && !m.subCommands.contains(sub))
            m.subCommands.append(sub);
    }
    copy_.buttonMappings.insert(button, m);
    updateButtonListItem(button);
}

// ============================================================
// mappingDesc：生成一条映射的描述文本（左侧按钮列表项用）
// ============================================================
QString LayerEditDialog::mappingDesc(const KeyMapping* m) const {
    if (!m)
        return QStringLiteral("—");
    QString desc = m->describe();
    // SwitchLayer：将 layer id 解析为显示名
    if (m->action.type == MappedAction::Type::SwitchLayer) {
        const OperationLayer* target = profile_->findLayer(m->action.layerName);
        if (target)
            desc = QStringLiteral("切换→%1").arg(target->name);
    }
    return desc;
}

// ============================================================
// updateButtonListItem：按副本刷新左侧指定按钮列表项的文本
// ============================================================
// 右侧表单改动写回副本后调用，保证左侧列表与右侧动作即时一致，
// 无需等重新打开对话框才看到最新描述。文本直接更新到对应 item widget 标签上。
void LayerEditDialog::updateButtonListItem(ControllerButton button) {
    if (!buttonList_) return;
    auto it = buttonLabels_.constFind(button);
    if (it != buttonLabels_.cend() && it.value()) {
        it.value()->setText(QStringLiteral("%1   %2").arg(
            controllerButtonDisplayName(button), mappingDesc(copy_.getMapping(button))));
    }
}

// ============================================================
// itemStyle：生成单个列表项标签的样式
// ============================================================
// 高亮态（当前选中 或 手柄正按下）用与主界面操作层激活按钮一致的
// 青绿色背景 + 白字加粗；普通态透明背景 + 浅灰文字。
QString LayerEditDialog::itemStyle(bool selected, bool pressed) const {
    if (selected || pressed)
        return QStringLiteral(
            "QLabel { background-color: #22958c; color: #ffffff; font-weight: bold;"
            " border-radius: 4px; padding: 5px 8px; }");
    return QStringLiteral(
        "QLabel { background-color: transparent; color: #e8eaee;"
        " border-radius: 4px; padding: 5px 8px; }");
}

// ============================================================
// refreshItemStyles：按当前选中项与按下集合刷新全部列表项样式
// ============================================================
// 在选中切换、手柄按键按下/松开时调用，保证每个列表项背景/文字
// 与最新状态一致（item widget 的 QSS 直接生效，不再被 QListWidget 覆盖）。
void LayerEditDialog::refreshItemStyles() {
    if (!buttonList_) return;
    const int selectedId = buttonList_->currentItem()
                               ? buttonList_->currentItem()->data(Qt::UserRole).toInt()
                               : -1;
    for (auto it = buttonLabels_.cbegin(); it != buttonLabels_.cend(); ++it) {
        const ControllerButton b = it.key();
        QLabel* label = it.value();
        if (!label) continue;
        label->setStyleSheet(itemStyle(static_cast<int>(b) == selectedId,
                                       pressedButtons_.contains(b)));
    }
}

// ============================================================
// updateParamPage：切换参数页（并保存当前按钮表单）
// ============================================================
// 由动作类型下拉框触发；若是用户操作（非 loading 加载中），
// 则先把当前按钮表单写入副本，保证类型切换不丢数据。
void LayerEditDialog::updateParamPage(int typeIndex) {
    paramStack_->setCurrentIndex(typeIndex);
    if (loading_)
        return;
    saveFormFor(currentButton());
}

// ============================================================
// accept：确定并回写
// ============================================================
// 1) 保存当前按钮表单；2) 把编辑框里的新层名写入副本；
// 3) 整个副本回写真实层（layer_）并关闭对话框。
void LayerEditDialog::accept() {
    saveFormFor(currentButton());
    // 更新层名称
    copy_.name = layerNameEdit_->text().trimmed();
    *layer_ = copy_;
    QDialog::accept();
}
