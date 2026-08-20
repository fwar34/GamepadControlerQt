#pragma once

#include <QMainWindow>

class SteamInput;
class KeyboardMouseMapper;
class XInputGamepadSource;
class QLabel;
class QPushButton;
class QSlider;

// =====================================================================
// 主窗口
// 手柄连接状态、层切换、全局设置、配置保存/重置、层映射编辑入口
// =====================================================================
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(SteamInput* input, KeyboardMouseMapper* mapper, XInputGamepadSource* gamepad,
               QWidget* parent = nullptr);

private slots:
    void onLayerChanged(const QString& activeLayerName);
    void onConnectionChanged(bool connected);
    void onToggleStartStop();
    void onSaveConfig();
    void onResetConfig();
    void onEditCommonLayer();
    void onApplySettings();

private:
    void refreshLayerButtons();
    void editLayer(const QString& layerName);

    SteamInput* input_;
    KeyboardMouseMapper* mapper_;
    XInputGamepadSource* gamepad_;

    QLabel* connectionLabel_ = nullptr;
    QLabel* activeLayerLabel_ = nullptr;
    QVector<QPushButton*> layerButtons_;
    QPushButton* startStopButton_ = nullptr;

    QSlider* deadzoneSlider_ = nullptr;
    QSlider* sensitivitySlider_ = nullptr;
    QSlider* smoothingSlider_ = nullptr;
    QSlider* accelerationSlider_ = nullptr;
};
