#pragma once

#include <QWidget>
#include <QSet>
#include <QPoint>

#include "../core/InputTypes.h"

class QVBoxLayout;
class QLabel;

// =====================================================================
// 悬浮层信息窗口
// 无边框、始终置顶、可拖动，实时显示当前激活的操作层名称和按下的手柄按键
// =====================================================================
class OverlayWidget : public QWidget {
    Q_OBJECT
public:
    explicit OverlayWidget(QWidget* parent = nullptr);
    
    // 设置显示的层名称
    void setLayerName(const QString& name);
    // 设置当前按下的手柄按键列表
    void setHeldButtons(const QSet<ControllerButton>& buttons);
    
protected:
    // 拖动相关
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    
private:
    QLabel* layerLabel_ = nullptr;
    QLabel* buttonsLabel_ = nullptr;
    QVBoxLayout* layout_ = nullptr;
    bool dragging_ = false;
    QPoint dragPos_;
};
