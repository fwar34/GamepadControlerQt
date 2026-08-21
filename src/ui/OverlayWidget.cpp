// ============================================================
// OverlayWidget.cpp
// 悬浮信息窗（当前层 + 按下的手柄按键）
// ------------------------------------------------------------
// 无边框、置顶、半透明的悬浮小窗，帮助玩家在手柄切换层时
// 随时看到当前层与当前按住的按键，无需切回主窗口。
//
// 关键设计：
//   - 窗口标志 Qt::Tool + Qt::WindowStaysOnTopHint：作为工具窗
//     始终置顶显示；不挂在主窗口下，主窗口最小化/关闭也不影响它。
//   - 通过鼠标左键拖拽可自由移动位置（默认停在屏幕右上角）。
//   - setHeldButtons 收到的按键集合已经过 MainWindow 过滤
//     （排除了"层切换"触发按键），这里只负责纯展示。
// ============================================================

#include "OverlayWidget.h"

#include "../core/InputTypes.h"

#include <QFont>
#include <QPalette>
#include <QGuiApplication>
#include <QScreen>
#include <QVBoxLayout>
#include <QLabel>
#include <QMouseEvent>

// ============================================================
// 构造：搭建悬浮窗外观与初始位置
// ============================================================
OverlayWidget::OverlayWidget(QWidget* parent) : QWidget(parent) {
    // 窗口样式：无边框、始终置顶、Qt::Tool（不出现在任务栏）
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    // 背景允许半透明（配合 QSS 的 rgba 背景色）
    setAttribute(Qt::WA_TranslucentBackground);

    // 垂直布局：层名标签 + 按键标签
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(10, 8, 10, 8);
    layout_->setSpacing(4);

    // 当前层名称（大号加粗，醒目）
    layerLabel_ = new QLabel(tr("当前层: Common"), this);
    layerLabel_->setFont(QFont("Microsoft YaHei", 12, QFont::Bold));
    layerLabel_->setStyleSheet("color: #ffffff;");
    layout_->addWidget(layerLabel_);

    // 按下的手柄按键（小号灰色次要信息）
    buttonsLabel_ = new QLabel(tr("按下按键: 无"), this);
    buttonsLabel_->setFont(QFont("Microsoft YaHei", 10));
    buttonsLabel_->setStyleSheet("color: #a0a0a0;");
    layout_->addWidget(buttonsLabel_);

    // 整体背景：半透明黑色圆角卡片
    setStyleSheet(R"(
        QWidget {
            background-color: rgba(0, 0, 0, 180);
            border-radius: 6px;
        }
    )");

    // 默认位置：屏幕右上角（留出 10px 边距）
    const QRect screenRect = QGuiApplication::primaryScreen()->availableGeometry();
    adjustSize();
    move(screenRect.topRight() - QPoint(width() + 10, 10));
}

// ============================================================
// setLayerName：更新显示的当前层
// ============================================================
// 用 layerDisplayName 附带中文别名（如 "Layer1 战斗"），
// 文本变化后 adjustSize 让圆角卡片贴合内容。
void OverlayWidget::setLayerName(const QString& name) {
    layerLabel_->setText(tr("当前层: %1").arg(layerDisplayName(name)));
    adjustSize();
}

// ============================================================
// setHeldButtons：更新显示的按下按键
// ============================================================
// 传入的是"当前有效映射不为 SwitchLayer"的按住按键集合，
// 这里只负责把按钮显示名拼接展示；空集合显示"无"。
void OverlayWidget::setHeldButtons(const QSet<ControllerButton>& buttons) {
    if (buttons.isEmpty()) {
        buttonsLabel_->setText(tr("按下按键: 无"));
        return;
    }

    QStringList buttonNames;
    for (ControllerButton btn : buttons) {
        buttonNames.append(controllerButtonDisplayName(btn));
    }
    buttonsLabel_->setText(tr("按下按键: %1").arg(buttonNames.join(", ")));
    adjustSize();
}

// ============================================================
// 拖拽实现：左键按住拖动悬浮窗
// ============================================================
// 记录按下时鼠标全局坐标与窗口左上角的偏移，移动时按偏移跟手。
void OverlayWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        dragPos_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void OverlayWidget::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_ && event->buttons() & Qt::LeftButton) {
        move(event->globalPosition().toPoint() - dragPos_);
        event->accept();
    }
}

void OverlayWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
        event->accept();
    }
}
