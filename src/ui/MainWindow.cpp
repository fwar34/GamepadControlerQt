// ============================================================
// MainWindow.cpp
// 主窗口：状态栏 + 层切换/编辑 + 全局设置 + 悬浮窗联动
// ------------------------------------------------------------
// 主窗口是用户操作入口，负责：
//   - 展示/切换/编辑操作层（点击按钮激活层，右键弹出编辑菜单）
//   - 调整全局设置（死区/灵敏度/平滑/加速）并实时写回引擎
//   - 保存/重置配置
//   - 创建悬浮信息窗（OverlayWidget）并驱动其显示层名与按下按键
//
// 关键设计：
//   - 层按钮用 layer.id 作 objectName，便于重命名后仍能正确定位
//     （id 唯一固定，name 仅显示）。
//   - 悬浮窗按键展示会过滤掉"层切换"触发按键（SwitchLayer），
//     避免按住方向键切层时误显示为普通按键。
//   - 全局设置滑块值改变即实时写回引擎（onApplySettings），
//     无需额外"应用"按钮。
// ============================================================

#include "MainWindow.h"

#include "OverlayWidget.h"
#include "LayerEditDialog.h"

#include "../core/ConfigManager.h"
#include "../core/InputTypes.h"
#include "../core/KeyboardMouseMapper.h"
#include "../core/SteamInput.h"
#include "../gamepad/XInputGamepadSource.h"

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QCheckBox>
#include <QSlider>
#include <QStatusBar>
#include <QVBoxLayout>

// ============================================================
// 构造：搭建主窗口 UI 与信号连接
// ============================================================
MainWindow::MainWindow(SteamInput* input, KeyboardMouseMapper* mapper, XInputGamepadSource* gamepad,
                       QWidget* parent)
    : QMainWindow(parent), input_(input), mapper_(mapper), gamepad_(gamepad) {
    setWindowTitle(tr("Gamepad 控制器 - Windows 本机版"));

    // ---- 悬浮信息窗 ----
    // 注意：parent 传 nullptr，使其成为独立顶层窗口，
    // 主窗口最小化时悬浮窗不会跟随隐藏。
    overlay_ = new OverlayWidget(nullptr);
    overlay_->show();
    // 层变化 -> 悬浮窗更新层名
    connect(input_, &SteamInput::layerChanged, overlay_, &OverlayWidget::setLayerName);
    // 按键映射事件 -> 悬浮窗更新按下按键（过滤层切换触发按键）
    connect(input_, &SteamInput::buttonMapped, this, [this]() {
        QSet<ControllerButton> filtered;
        const auto& held = input_->heldButtons();
        for (ControllerButton btn : held) {
            const auto* mapping = input_->getEffectiveMapping(btn);
            if (mapping && mapping->action.type != MappedAction::Type::SwitchLayer) {
                filtered.insert(btn);
            }
        }
        overlay_->setHeldButtons(filtered);
    });
    // 初始更新按键状态（过滤掉层切换触发按键）
    QSet<ControllerButton> filtered;
    const auto& initialHeld = input_->heldButtons();
    for (ControllerButton btn : initialHeld) {
        const auto* mapping = input_->getEffectiveMapping(btn);
        if (mapping && mapping->action.type != MappedAction::Type::SwitchLayer) {
            filtered.insert(btn);
        }
    }
    overlay_->setHeldButtons(filtered);

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);

    // ---- 顶部：状态 + 启停 ----
    auto* topBar = new QHBoxLayout;
    startStopButton_ = new QPushButton(tr("停止映射"), this);
    connect(startStopButton_, &QPushButton::clicked, this, &MainWindow::onToggleStartStop);
    topBar->addWidget(startStopButton_);

    connectionLabel_ = new QLabel(tr("手柄：未连接"), this);
    topBar->addWidget(connectionLabel_);

    activeLayerLabel_ = new QLabel(tr("当前层：Common"), this);
    topBar->addWidget(activeLayerLabel_);
    topBar->addStretch(1);
    root->addLayout(topBar);

    // ---- 中部：层按钮 + 设置 ----
    auto* mid = new QHBoxLayout;

    // 层切换区：每个操作层一个可点击按钮（点击激活/取消，右键编辑）
    auto* layerGroup = new QGroupBox(tr("操作层（点击编辑）"), this);
    auto* layerLayout = new QVBoxLayout(layerGroup);

    const auto& layers = input_->profile.layers;
    auto* grid = new QGridLayout;
    grid->setSpacing(6);
    const int cols = 2;
    for (int i = 0; i < layers.size(); ++i) {
        const QString layerId = layers[i].id;
        const OperationLayer* layer = input_->profile.findLayer(layerId);
        auto* btn = new QPushButton(layer ? layer->name : layerDisplayName(layerId), layerGroup);
        btn->setObjectName(layerId);   // 以 id 为对象名，重命名后仍可定位
        btn->setToolTip(tr("点击编辑该层"));
        // 左键：打开编辑对话框
        connect(btn, &QPushButton::clicked, this, [this, layerId]() {
            editLayer(layerId);
        });
        grid->addWidget(btn, i / cols, i % cols);
        layerButtons_.append(btn);
    }
    layerLayout->addLayout(grid);
    layerLayout->addStretch(1);

    auto* editCommonBtn = new QPushButton(tr("编辑公共层…"), layerGroup);
    editCommonBtn->setObjectName(QStringLiteral("editCommonBtn"));
    connect(editCommonBtn, &QPushButton::clicked, this, &MainWindow::onEditCommonLayer);
    layerLayout->addWidget(editCommonBtn);
    mid->addWidget(layerGroup, 1);

    // 全局设置区：四个滑块（死区/灵敏度/平滑/加速）
    auto* settingsGroup = new QGroupBox(tr("全局设置"), this);
    auto* settingsLayout = new QVBoxLayout(settingsGroup);

    // 本地工具函数：创建一行"标题 + 滑块 + 数值标签"并保存指针
    auto addSetting = [settingsLayout](const QString& title, const QString& tip,
                                       int min, int max, int value,
                                       QSlider** outSlider, QLabel** outValue) {
        auto* row = new QHBoxLayout;
        auto* label = new QLabel(title);
        label->setMinimumWidth(80);
        label->setToolTip(tip);
        row->addWidget(label);
        auto* slider = new QSlider(Qt::Horizontal);
        slider->setRange(min, max);
        slider->setValue(value);
        slider->setToolTip(tip);
        row->addWidget(slider, 1);
        auto* valueLabel = new QLabel(QString::number(value));
        valueLabel->setMinimumWidth(36);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(valueLabel);
        settingsLayout->addLayout(row);
        *outSlider = slider;
        *outValue = valueLabel;
    };

    // 从配置初始化滑块（配置文件里是 0~1 浮点，UI 用整数 0~100 等）
    const GlobalSettings& gs = input_->profile.globalSettings;
    QLabel* deadzoneValue = nullptr;
    QLabel* sensitivityValue = nullptr;
    QLabel* smoothingValue = nullptr;
    QLabel* accelerationValue = nullptr;
    addSetting(tr("摇杆死区"),
               tr("摇杆推力低于此阈值时视为零输入，避免手柄漂移导致误触发。\n值越大需要推得越深才会有响应。"),
               0, 50, qRound(gs.deadzone * 100),
               &deadzoneSlider_, &deadzoneValue);
    addSetting(tr("视角灵敏度"),
               tr("右摇杆控制鼠标移动的速度倍率。\n值越大相同推力下鼠标移动越快。"),
               10, 200, qRound(gs.lookSensitivity * 100),
               &sensitivitySlider_, &sensitivityValue);
    addSetting(tr("视角平滑"),
               tr("对右摇杆输入做时间轴上的平滑处理，减少抖动。\n值越大响应越平滑但延迟越高，设为 0 为无平滑。"),
               0, 100, qRound(gs.lookSmoothing * 100),
               &smoothingSlider_, &smoothingValue);
    addSetting(tr("视角加速"),
               tr("右摇杆推力与鼠标速度的非线性映射指数。\n100 为线性（无加速），值越大轻推越慢、重推越快。"),
               100, 300, qRound(gs.lookAcceleration * 100),
               &accelerationSlider_, &accelerationValue);

    invertLookXCheck_ = new QCheckBox(tr("右摇杆 X 轴反转"), settingsGroup);
    invertLookXCheck_->setChecked(gs.invertLookX);
    invertLookXCheck_->setToolTip(tr("反转右摇杆左右方向的鼠标移动"));
    settingsLayout->addWidget(invertLookXCheck_);

    invertLookYCheck_ = new QCheckBox(tr("右摇杆 Y 轴反转"), settingsGroup);
    invertLookYCheck_->setChecked(gs.invertLookY);
    invertLookYCheck_->setToolTip(tr("反转右摇杆上下方向的鼠标移动"));
    settingsLayout->addWidget(invertLookYCheck_);

    // 数值标签随滑块更新，并实时写回引擎（lambda 捕获引用）
    auto updateValues = [deadzoneValue, sensitivityValue, smoothingValue, accelerationValue,
                         this]() {
        if (deadzoneValue) deadzoneValue->setText(QString::number(deadzoneSlider_->value()));
        if (sensitivityValue) sensitivityValue->setText(QString::number(sensitivitySlider_->value()));
        if (smoothingValue) smoothingValue->setText(QString::number(smoothingSlider_->value()));
        if (accelerationValue) accelerationValue->setText(QString::number(accelerationSlider_->value()));
        onApplySettings();
    };
    connect(deadzoneSlider_, &QSlider::valueChanged, this, updateValues);
    connect(sensitivitySlider_, &QSlider::valueChanged, this, updateValues);
    connect(smoothingSlider_, &QSlider::valueChanged, this, updateValues);
    connect(accelerationSlider_, &QSlider::valueChanged, this, updateValues);
    connect(invertLookXCheck_, &QCheckBox::toggled, this, [this]() { onApplySettings(); });
    connect(invertLookYCheck_, &QCheckBox::toggled, this, [this]() { onApplySettings(); });

    settingsLayout->addStretch(1);
    mid->addWidget(settingsGroup, 0);
    root->addLayout(mid, 1);

    // ---- 底部：配置操作 ----
    auto* bottomBar = new QHBoxLayout;
    auto* saveBtn = new QPushButton(tr("保存配置"), this);
    connect(saveBtn, &QPushButton::clicked, this, &MainWindow::onSaveConfig);
    bottomBar->addWidget(saveBtn);

    auto* resetBtn = new QPushButton(tr("重置默认"), this);
    connect(resetBtn, &QPushButton::clicked, this, &MainWindow::onResetConfig);
    bottomBar->addWidget(resetBtn);
    bottomBar->addStretch(1);
    root->addLayout(bottomBar);

    setCentralWidget(central);
    statusBar()->showMessage(tr("配置文件：%1").arg(ConfigManager::configFilePath()));

    // ---- 初始状态同步 ----
    onLayerChanged(input_->activeLayerName());
    refreshLayerButtons();
    onConnectionChanged(gamepad_->isConnected());

    // 连接手柄连接状态变化信号（注意：需在 gamepad 指针有效时连接）
    connect(gamepad_, &XInputGamepadSource::connectedChanged,
            this, &MainWindow::onConnectionChanged);
}

// ---------------------------------------------------------------

// ============================================================
// onLayerChanged：当前层变化 -> 更新顶部标签与悬浮窗
// ============================================================
// 层名取 layer.name（显示名），并刷新所有层按钮的勾选/文本状态。
void MainWindow::onLayerChanged(const QString& activeLayerName) {
    if (activeLayerLabel_) {
        const OperationLayer* layer = input_->profile.findLayer(activeLayerName);
        const QString displayName = layer ? layer->name : activeLayerName;
        activeLayerLabel_->setText(tr("当前层：%1").arg(displayName));
        if (overlay_)
            overlay_->setLayerName(displayName);
    }
    refreshLayerButtons();
}

// ============================================================
// onConnectionChanged：手柄连接状态变化 -> 更新状态标签
// ============================================================
void MainWindow::onConnectionChanged(bool connected) {
    connectionLabel_->setText(connected ? tr("手柄：已连接") : tr("手柄：未连接"));
    connectionLabel_->setStyleSheet(connected ? QStringLiteral("color: #2e7d32; font-weight: bold;")
                                              : QStringLiteral("color: #b71c1c; font-weight: bold;"));
}

// ============================================================
// refreshLayerButtons：刷新所有层按钮的勾选状态与文本
// ============================================================
// 通过 objectName（= layer.id）查找对应层：勾选状态表示当前是否激活，
// 文本始终显示最新层名（支持改名后即时刷新）。
void MainWindow::refreshLayerButtons() {
    for (QPushButton* btn : layerButtons_) {
        const QString layerId = btn->objectName();
        const bool active = input_->isLayerActive(layerId);
        btn->setStyleSheet(active ? QStringLiteral("background: #2196f3; color: white;")
                                  : QString());
        // 更新按钮文本为当前层名称
        if (const OperationLayer* layer = input_->profile.findLayer(layerId)) {
            btn->setText(layer->name);
        }
    }
    // 更新公共层编辑按钮文本
    if (auto* editCommonBtn = findChild<QPushButton*>(QStringLiteral("editCommonBtn"))) {
        const OperationLayer* commonLayer = input_->profile.findLayer(QStringLiteral("Common"));
        editCommonBtn->setText(tr("编辑公共层：%1…").arg(commonLayer ? commonLayer->name : tr("公共层")));
    }
}

// ============================================================
// onToggleStartStop：启动/停止映射开关
// ============================================================
// 停止时同时停掉手柄轮询与映射器（look 线程），并释放所有按键；
// 启动时重新拉起两者（XInput 轮询 / 注入）。
void MainWindow::onToggleStartStop() {
    if (mapper_->isRunning()) {
        gamepad_->stop();
        mapper_->stop();
        startStopButton_->setText(tr("启动映射"));
        statusBar()->showMessage(tr("已停止：释放所有按键"));
    } else {
        mapper_->start();
        gamepad_->start();
        startStopButton_->setText(tr("停止映射"));
        statusBar()->showMessage(tr("已启动映射"));
    }
}

// ============================================================
// onSaveConfig：保存当前配置到磁盘
// ============================================================
void MainWindow::onSaveConfig() {
    const bool ok = ConfigManager::save(input_->profile);
    statusBar()->showMessage(ok ? tr("配置已保存到 %1").arg(ConfigManager::configFilePath())
                                : tr("保存配置失败"));
}

// ============================================================
// onResetConfig：重置为默认配置
// ============================================================
// 二次确认后：保存默认配置 -> 重新加载到引擎 -> 同步滑块到默认值。
void MainWindow::onResetConfig() {
    const auto ret = QMessageBox::question(
        this, tr("重置配置"), tr("确定要恢复默认配置吗？当前修改将丢失。"),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;
    const ControllerProfile def = ControllerProfile::createDefault();
    ConfigManager::save(def);
    input_->loadProfile(def);
    // 同步滑块到新默认值（滑块变更会自动写回 profile）
    deadzoneSlider_->setValue(qRound(def.globalSettings.deadzone * 100));
    sensitivitySlider_->setValue(qRound(def.globalSettings.lookSensitivity * 100));
    smoothingSlider_->setValue(qRound(def.globalSettings.lookSmoothing * 100));
    accelerationSlider_->setValue(qRound(def.globalSettings.lookAcceleration * 100));
    invertLookXCheck_->setChecked(def.globalSettings.invertLookX);
    invertLookYCheck_->setChecked(def.globalSettings.invertLookY);
    statusBar()->showMessage(tr("已重置为默认配置"));
}

// ============================================================
// onEditCommonLayer：编辑公共层入口
// ============================================================
void MainWindow::onEditCommonLayer() {
    editLayer(QStringLiteral("Common"));
}

// ============================================================
// editLayer：打开指定层的编辑对话框
// ============================================================
// 按层名（实为 id）定位层对象，打开模态编辑对话框，
// 关闭后刷新按钮文本（层名可能被修改）。
void MainWindow::editLayer(const QString& layerName) {
    OperationLayer* layer = input_->profile.findLayer(layerName);
    if (!layer)
        return;
    LayerEditDialog dlg(&input_->profile, layer, this);
    dlg.exec();
    refreshLayerButtons();
}

// ============================================================
// onApplySettings：把滑块值写回引擎的全局设置
// ============================================================
// UI 用整数（如 0~100），引擎内部用 0~1 浮点，这里做换算。
// cursorSpeed 固定为 1.0（本机版未开放光标速度调节）。
void MainWindow::onApplySettings() {
    GlobalSettings s;
    s.deadzone = static_cast<float>(deadzoneSlider_->value()) / 100.0f;
    s.lookSensitivity = static_cast<float>(sensitivitySlider_->value()) / 100.0f;
    s.cursorSpeed = 1.0f;
    s.lookSmoothing = static_cast<float>(smoothingSlider_->value()) / 100.0f;
    s.lookAcceleration = static_cast<float>(accelerationSlider_->value()) / 100.0f;
    s.invertLookX = invertLookXCheck_->isChecked();
    s.invertLookY = invertLookYCheck_->isChecked();
    input_->setGlobalSettings(s);
}

// ============================================================
// 析构：关闭并释放悬浮窗
// ============================================================
MainWindow::~MainWindow() {
    if (overlay_) {
        overlay_->close();
        delete overlay_;
    }
}
