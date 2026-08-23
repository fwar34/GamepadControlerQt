#pragma once

#include <QMainWindow>

#include "OverlayWidget.h"

class SteamInput;
class KeyboardMouseMapper;
class XInputGamepadSource;
class QEvent;
class QLabel;
class QPushButton;
class QCheckBox;
class QSlider;
class QTimer;
class QMenu;
class QSystemTrayIcon;

// =====================================================================
// MainWindow —— 主窗口
//
// 功能：
//   - 顶部：启停映射按钮、手柄连接状态、当前激活层标签
//   - 中部：2 列 x 5 行操作层按钮（点击切换 / 右键编辑该层）、
//     公共层编辑入口、全局设置滑块（死区/灵敏度/平滑/加速度）
//   - 底部：保存配置、重置默认
//   - 右上角：悬浮层信息窗口（OverlayWidget，独立顶层窗口）
//
// 滑块与 GlobalSettings 的换算：
//   死区 0-50 -> /100；灵敏度 10-200 -> /100；
//   平滑 0-100 -> /100；加速度 100-300 -> /100
// =====================================================================
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(SteamInput* input, KeyboardMouseMapper* mapper, XInputGamepadSource* gamepad,
               QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onLayerChanged(const QString& activeLayerName);
    void onConnectionChanged(bool connected);
    void onToggleStartStop();
    void onSaveConfig();
    void onResetConfig();
    void onEditCommonLayer();
    void onApplySettings();
    void onCheckForeground();
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    // 根据当前 profile 重建层按钮文本（含显示名）
    void refreshLayerButtons();
    // 打开指定层（id）的编辑对话框
    void editLayer(const QString& layerName);
    // 同步启停按钮文字与状态色（mappingActive=true 映射运行中）
    void applyStartStopState(bool mappingActive);

    SteamInput* input_;
    KeyboardMouseMapper* mapper_;
    XInputGamepadSource* gamepad_;
    OverlayWidget* overlay_ = nullptr;

    QLabel* connectionLabel_ = nullptr;
    QLabel* activeLayerLabel_ = nullptr;
    QVector<QPushButton*> layerButtons_;
    QPushButton* startStopButton_ = nullptr;

    QSlider* deadzoneSlider_ = nullptr;
    QSlider* sensitivitySlider_ = nullptr;
    QSlider* smoothingSlider_ = nullptr;
    QSlider* accelerationSlider_ = nullptr;
    QCheckBox* invertLookXCheck_ = nullptr;
    QCheckBox* invertLookYCheck_ = nullptr;
    QCheckBox* releaseOnFgCheck_ = nullptr;
    QCheckBox* confirmOnCloseCheck_ = nullptr;

    QTimer* foregroundTimer_ = nullptr;
    void* lastForegroundHwnd_ = nullptr;
    QSystemTrayIcon* trayIcon_ = nullptr;
    QMenu* trayMenu_ = nullptr;
    QAction* trayMappingAction_ = nullptr;  // 托盘菜单「激活映射」项
};
