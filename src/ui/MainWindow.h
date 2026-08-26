#pragma once

#include <QMainWindow>
#include <QHash>

#include "OverlayWidget.h"

class SteamInput;
class KeyboardMouseMapper;
class XInputGamepadSource;
class QEvent;
class QLabel;
class QPushButton;
class QComboBox;
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
//   - 操作集区：切换 / 添加 / 复制 / 重命名 / 删除操作集
//   - 中部：2 列 x 5 行操作层按钮（点击编辑该层）、
//     公共层编辑入口、全局设置滑块（死区/灵敏度/平滑/加速度）
//   - 底部：保存配置、重置默认
//   - 右上角：悬浮层信息窗口（OverlayWidget，独立顶层窗口）
//
// 操作集（OperationSet）：最顶层容器，一组完整的映射配置
//   （1 公共层 + 最多 10 操作层）。切换操作集时其下所有层整体切换，
//   各操作集之间配置互不干扰。所有操作集操作都在主窗口左侧完成。
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
    void onShowHelp();
    void onCheckForeground();
    // ---- 操作集管理 ----
    void onSetComboChanged(int index);   // 下拉框选择变化 -> 切换操作集
    void onAddSet();                     // 添加新操作集并切换到它
    void onCopySet();                    // 复制当前操作集（可直接改名）
    void onRenameSet();                  // 重命名当前操作集
    void onDeleteSet();                  // 删除当前操作集（至少保留一个）
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    // 托盘「退出」统一入口：保存悬浮窗位置后退出程序
    void exitApplication();

private:
    // 根据当前 profile 重建层按钮文本（含显示名）
    void refreshLayerButtons();
    // 重建操作集下拉框（按当前激活集选中，refreshingSets_ 防递归）
    void refreshSetCombo();
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
    // 操作集下拉框（itemData 存操作集 id）；refreshingSets_ 防止程序化刷新触发切换
    QComboBox* setCombo_ = nullptr;
    bool refreshingSets_ = false;
    // MouseToggle 锁存集合（手柄键 -> 鼠标键），用于主窗口边框变色提示
    QHash<ControllerButton, MouseButton> toggledButtons_;
    bool toggleActive_ = false;   // 是否有 MouseToggle 处于锁存（边框是否高亮）

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
