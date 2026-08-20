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
#include <QCursor>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
#include <QVBoxLayout>

MainWindow::MainWindow(SteamInput* input, KeyboardMouseMapper* mapper, XInputGamepadSource* gamepad,
                       QWidget* parent)
    : QMainWindow(parent), input_(input), mapper_(mapper), gamepad_(gamepad) {
    setWindowTitle(tr("Gamepad 控制器 - Windows 本机版"));
    
    // 创建悬浮层信息窗口
    // 注意：parent 传 nullptr，使其成为独立顶层窗口，
    // 主窗口最小化时悬浮窗不会跟随隐藏
    overlay_ = new OverlayWidget(nullptr);
    overlay_->show();
    // 连接层变化信号到悬浮窗口
    connect(input_, &SteamInput::layerChanged, overlay_, &OverlayWidget::setLayerName);
    // 连接按键变化信号到悬浮窗口（过滤掉层切换触发按键）
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

    // 层切换区
    auto* layerGroup = new QGroupBox(tr("操作层（点击切换，右键编辑）"), this);
    auto* layerLayout = new QVBoxLayout(layerGroup);

    const auto& layers = input_->profile.layers;
    auto* grid = new QGridLayout;
    grid->setSpacing(6);
    const int cols = 2;
    for (int i = 0; i < layers.size(); ++i) {
        const QString layerId = layers[i].id;
        const OperationLayer* layer = input_->profile.findLayer(layerId);
        auto* btn = new QPushButton(layer ? layer->name : layerDisplayName(layerId), layerGroup);
        btn->setObjectName(layerId);
        btn->setCheckable(true);
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        btn->setToolTip(tr("点击切换层，右键编辑"));
        connect(btn, &QPushButton::clicked, this, [this, layerId](bool checked) {
            if (checked)
                input_->activateLayer(layerId);
            else
                input_->deactivateLayer(layerId);
        });
        connect(btn, &QPushButton::customContextMenuRequested, this, [this, layerId](const QPoint&) {
            QMenu menu(this);
            QAction* edit = menu.addAction(tr("编辑该层…"));
            QAction* act = menu.exec(QCursor::pos());
            if (act == edit)
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

    // 全局设置区
    auto* settingsGroup = new QGroupBox(tr("全局设置"), this);
    auto* settingsLayout = new QVBoxLayout(settingsGroup);

    auto addSetting = [settingsLayout](const QString& title, int min, int max, int value,
                                       QSlider** outSlider, QLabel** outValue) {
        auto* row = new QHBoxLayout;
        auto* label = new QLabel(title);
        label->setMinimumWidth(80);
        row->addWidget(label);
        auto* slider = new QSlider(Qt::Horizontal);
        slider->setRange(min, max);
        slider->setValue(value);
        row->addWidget(slider, 1);
        auto* valueLabel = new QLabel(QString::number(value));
        valueLabel->setMinimumWidth(36);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(valueLabel);
        settingsLayout->addLayout(row);
        *outSlider = slider;
        *outValue = valueLabel;
    };

    const GlobalSettings& gs = input_->profile.globalSettings;
    QLabel* deadzoneValue = nullptr;
    QLabel* sensitivityValue = nullptr;
    QLabel* smoothingValue = nullptr;
    QLabel* accelerationValue = nullptr;
    addSetting(tr("摇杆死区"), 0, 50, qRound(gs.deadzone * 100),
               &deadzoneSlider_, &deadzoneValue);
    addSetting(tr("视角灵敏度"), 10, 200, qRound(gs.lookSensitivity * 100),
               &sensitivitySlider_, &sensitivityValue);
    addSetting(tr("视角平滑"), 0, 100, qRound(gs.lookSmoothing * 100),
               &smoothingSlider_, &smoothingValue);
    addSetting(tr("视角加速"), 100, 300, qRound(gs.lookAcceleration * 100),
               &accelerationSlider_, &accelerationValue);

    // 数值标签随滑块更新（本地 lambda 收集引用）
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

    // 初始状态
    onLayerChanged(input_->activeLayerName());
    refreshLayerButtons();
    onConnectionChanged(gamepad_->isConnected());
    
    // 连接手柄连接状态变化信号
    connect(gamepad_, &XInputGamepadSource::connectedChanged,
            this, &MainWindow::onConnectionChanged);
}

// ---------------------------------------------------------------

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

void MainWindow::onConnectionChanged(bool connected) {
    connectionLabel_->setText(connected ? tr("手柄：已连接") : tr("手柄：未连接"));
    connectionLabel_->setStyleSheet(connected ? QStringLiteral("color: #2e7d32; font-weight: bold;")
                                              : QStringLiteral("color: #b71c1c; font-weight: bold;"));
}

void MainWindow::refreshLayerButtons() {
    for (QPushButton* btn : layerButtons_) {
        const QString layerId = btn->objectName();
        const bool active = input_->isLayerActive(layerId);
        btn->setChecked(active);
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

void MainWindow::onSaveConfig() {
    const bool ok = ConfigManager::save(input_->profile);
    statusBar()->showMessage(ok ? tr("配置已保存到 %1").arg(ConfigManager::configFilePath())
                                : tr("保存配置失败"));
}

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
    statusBar()->showMessage(tr("已重置为默认配置"));
}

void MainWindow::onEditCommonLayer() {
    editLayer(QStringLiteral("Common"));
}

void MainWindow::editLayer(const QString& layerName) {
    OperationLayer* layer = input_->profile.findLayer(layerName);
    if (!layer)
        return;
    LayerEditDialog dlg(&input_->profile, layer, this);
    dlg.exec();
    refreshLayerButtons();
}

void MainWindow::onApplySettings() {
    GlobalSettings s;
    s.deadzone = static_cast<float>(deadzoneSlider_->value()) / 100.0f;
    s.lookSensitivity = static_cast<float>(sensitivitySlider_->value()) / 100.0f;
    s.cursorSpeed = 1.0f;
    s.lookSmoothing = static_cast<float>(smoothingSlider_->value()) / 100.0f;
    s.lookAcceleration = static_cast<float>(accelerationSlider_->value()) / 100.0f;
    input_->setGlobalSettings(s);
}

MainWindow::~MainWindow() {
    if (overlay_) {
        overlay_->close();
        delete overlay_;
    }
}
