#pragma once

#include <QWidget>
#include <QSet>
#include <QPoint>

#include "../core/InputTypes.h"

class QVBoxLayout;
class QLabel;
class QWheelEvent;
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
    // 同步映射运行状态（true=运行中，圆点变绿；false=已停止，变灰）
    void setMappingState(bool connected, bool mappingActive);
    // 切换展开/收起状态（由手柄 ToggleOverlay 动作触发）
    void toggleExpanded();
    // 刷新展开状态下的映射列表（供外部信号连接）
    void refreshMappingsIfExpanded();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

public:
    // 应用滚轮缩放系数（含启动时恢复上次大小）
    void applyScale(qreal scale);
    // 当前缩放系数
    qreal scale() const { return scale_; }

private:
    // 刷新展开状态下的映射列表
    void refreshMappings();
    // 将当前 scale_ 应用到各控件并重排窗口
    void applyCurrentScale();

    SteamInput* steamInput_ = nullptr;
    QWidget* mainWindow_ = nullptr;

    QLabel* layerLabel_ = nullptr;
    QLabel* buttonsLabel_ = nullptr;
    QLabel* mappingsLabel_ = nullptr;    // 展开时显示映射列表
    QLabel* statusDot_ = nullptr;        // 顶部映射状态圆点（绿=运行/灰=停止）
    QVBoxLayout* layout_ = nullptr;
    bool dragging_ = false;
    bool dragMoved_ = false;             // 拖拽过程中是否实际移动过
    QPoint dragPos_;
    QPoint pressPos_;                    // 按下时的全局坐标（用于判断是否真正点击）
    bool expanded_ = false;              // 是否展开
    QString currentLayerName_;           // 当前层名（用于刷新映射列表）
    qreal scale_ = 1.0;                  // 滚轮缩放系数（0.5 ~ 2.0）
    QFont baseLayerFont_;
    QFont baseButtonsFont_;
    QFont baseMappingsFont_;
    QString currentDotColor_ = QStringLiteral("#2e7d32");   // 当前圆点颜色（供缩放重建样式）
};
