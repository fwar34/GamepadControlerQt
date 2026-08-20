#include "OverlayWidget.h"

#include "../core/InputTypes.h"

#include <QFont>
#include <QPalette>
#include <QGuiApplication>
#include <QScreen>
#include <QVBoxLayout>
#include <QLabel>
#include <QMouseEvent>

OverlayWidget::OverlayWidget(QWidget* parent) : QWidget(parent) {
    // 窗口样式：无边框、始终置顶、背景半透明
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    
    // 布局
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(10, 8, 10, 8);
    layout_->setSpacing(4);
    
    // 层名称标签
    layerLabel_ = new QLabel(tr("当前层: Common"), this);
    layerLabel_->setFont(QFont("Microsoft YaHei", 12, QFont::Bold));
    layerLabel_->setStyleSheet("color: #ffffff;");
    layout_->addWidget(layerLabel_);
    
    // 按键标签
    buttonsLabel_ = new QLabel(tr("按下按键: 无"), this);
    buttonsLabel_->setFont(QFont("Microsoft YaHei", 10));
    buttonsLabel_->setStyleSheet("color: #a0a0a0;");
    layout_->addWidget(buttonsLabel_);
    
    // 整体背景
    setStyleSheet(R"(
        QWidget {
            background-color: rgba(0, 0, 0, 180);
            border-radius: 6px;
        }
    )");
    
    // 默认位置：屏幕右上角
    const QRect screenRect = QGuiApplication::primaryScreen()->availableGeometry();
    adjustSize();
    move(screenRect.topRight() - QPoint(width() + 10, 10));
}

void OverlayWidget::setLayerName(const QString& name) {
    layerLabel_->setText(tr("当前层: %1").arg(layerDisplayName(name)));
    adjustSize();
}

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
