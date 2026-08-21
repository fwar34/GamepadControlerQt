#pragma once

#include <QWidget>
#include <QSet>
#include <QPoint>

#include "../core/InputTypes.h"

class QVBoxLayout;
class QLabel;

// =====================================================================
// OverlayWidget —— 悬浮层信息窗口
//
// 独立顶层窗口（parent 传 nullptr，主窗口最小化时不会跟随隐藏），
// 无边框、始终置顶、半透明圆角背景，支持鼠标拖动。
//
// 显示内容：
//   - 当前激活的操作层名称（随 SteamInput::layerChanged 更新）
//   - 当前按下的手柄按键列表（随 SteamInput::buttonMapped 更新，
//     过滤掉用于层切换的触发按键）
// =====================================================================
class OverlayWidget : public QWidget {
    Q_OBJECT
public:
    explicit OverlayWidget(QWidget* parent = nullptr);

    // 设置显示的层名称
    void setLayerName(const QString& name);
    // 设置当前按下的手柄按键列表（会按层过滤触发按键）
    void setHeldButtons(const QSet<ControllerButton>& buttons);

protected:
    // ---- 拖动支持：按下记录偏移，移动时平移窗口 ----
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QLabel* layerLabel_ = nullptr;      // 层名称标签
    QLabel* buttonsLabel_ = nullptr;    // 按键列表标签
    QVBoxLayout* layout_ = nullptr;
    bool dragging_ = false;             // 是否正在拖动
    QPoint dragPos_;                    // 按下点相对窗口的偏移
};
