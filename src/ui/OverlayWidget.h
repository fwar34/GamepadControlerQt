#pragma once

#include <QWidget>
#include <QSet>
#include <QPoint>

#include "../core/InputTypes.h"

class QVBoxLayout;
class QLabel;
class SteamInput;

// =====================================================================
// OverlayWidget —— 悬浮层信息窗口
//
// 独立顶层窗口（parent 传 nullptr，主窗口最小化时不会跟随隐藏），
// 无边框、始终置顶、半透明圆角背景。
//
// 交互：
//   - 左键点击：将主窗口拉到前台
//   - 左键拖拽：移动悬浮窗位置
//   - 右键点击：展开/收起当前层已映射按键列表
//
// 显示内容（收起状态）：
//   - 当前激活的操作层名称
//   - 当前按下的手柄按键列表
// 显示内容（展开状态）：
//   - 收起状态的所有内容
//   - 当前层所有已映射的按键及其动作
// =====================================================================
class OverlayWidget : public QWidget {
    Q_OBJECT
public:
    explicit OverlayWidget(QWidget* parent = nullptr);

    void setSteamInput(SteamInput* input) { steamInput_ = input; }
    void setMainWindow(QWidget* window) { mainWindow_ = window; }

    // 设置显示的层名称
    void setLayerName(const QString& name);
    // 设置当前按下的手柄按键列表（会按层过滤触发按键）
    void setHeldButtons(const QSet<ControllerButton>& buttons);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    // 刷新展开状态下的映射列表
    void refreshMappings();

    SteamInput* steamInput_ = nullptr;
    QWidget* mainWindow_ = nullptr;

    QLabel* layerLabel_ = nullptr;
    QLabel* buttonsLabel_ = nullptr;
    QLabel* mappingsLabel_ = nullptr;    // 展开时显示映射列表
    QVBoxLayout* layout_ = nullptr;
    bool dragging_ = false;
    QPoint dragPos_;
    bool expanded_ = false;              // 是否展开
    QString currentLayerName_;           // 当前层名（用于刷新映射列表）
};
