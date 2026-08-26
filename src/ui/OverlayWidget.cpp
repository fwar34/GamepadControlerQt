// ============================================================
// OverlayWidget.cpp
// 悬浮信息窗（当前层 + 按下的手柄按键 + 展开映射列表）
// ------------------------------------------------------------
// 无边框、置顶、半透明的悬浮小窗，帮助玩家在手柄切换层时
// 随时看到当前层与当前按住的按键，无需切回主窗口。
//
// 交互：
//   - 左键点击：将主窗口拉到前台
//   - 左键拖拽：移动悬浮窗位置
//   - 右键点击：展开/收起当前层已映射按键列表
// ============================================================

#include "OverlayWidget.h"

#include "../core/InputTypes.h"
#include "../core/SteamInput.h"

#include <QFont>
#include <QFrame>
#include <QGuiApplication>
#include <QScreen>
#include <QStringList>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QLinearGradient>
#include <QPen>
#include <QColor>

namespace {

// ============================================================
// StatusDotWidget —— 顶部映射状态圆点（自绘实心圆）
// ============================================================
// 不用 QLabel + QSS：透明无边框窗口（WA_TranslucentBackground）上，
// 样式表的 background-color + border-radius 无法可靠裁剪圆角，
// 圆点会渲染成方形。这里在 paintEvent 里用 QPainter 直接画椭圆，
// 任何环境下都是标准实心圆。
class StatusDotWidget : public QWidget {
public:
    explicit StatusDotWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(12, 12);
    }
    void setColor(const QColor& c) {
        if (color_ != c) {
            color_ = c;
            update();
        }
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(color_);
        p.drawEllipse(rect());
    }
private:
    QColor color_ = QColor(QStringLiteral("#2e7d32"));  // 初始：映射运行中（绿）
};

}  // namespace

// ============================================================
// 构造：搭建悬浮窗外观与初始位置
// ============================================================
OverlayWidget::OverlayWidget(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setObjectName(QStringLiteral("overlayRoot"));

    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(12, 10, 12, 10);
    layout_->setSpacing(5);

    // 顶部行：映射状态圆点 + 当前层名称
    statusDot_ = new StatusDotWidget(this);
    layerLabel_ = new QLabel(tr("当前层: Common"), this);
    baseLayerFont_ = QFont("DengXian", 11, QFont::Bold);   // 粗体用等线，避免雅黑合成粗体发虚
    layerLabel_->setFont(baseLayerFont_);
    layerLabel_->setStyleSheet("color: #e8eaee;");
    auto* headerRow = new QHBoxLayout;
    headerRow->setSpacing(7);
    headerRow->addWidget(statusDot_);
    headerRow->addWidget(layerLabel_);
    headerRow->addStretch();
    layout_->addLayout(headerRow);

    // 当前操作集名称（切换操作集时更新，收起/展开都显示）
    setLabel_ = new QLabel(tr("操作集: 默认操作集"), this);
    setLabel_->setFont(QFont("DengXian", 9, QFont::Normal));
    setLabel_->setStyleSheet("color: #7fc9c4;");
    layout_->addWidget(setLabel_);

    // 头部与内容之间的分隔线
    auto* separator = new QFrame(this);
    separator->setObjectName(QStringLiteral("headerSeparator"));
    separator->setFixedHeight(1);
    layout_->addWidget(separator);

    // 按下的手柄按键
    buttonsLabel_ = new QLabel(tr("按下按键: 无"), this);
    // 换用雅黑 UI + 非粗体：雅黑粗体字形下缘超出度量导致「无」字截断
    baseButtonsFont_ = QFont("Microsoft YaHei UI", 10, QFont::Normal);
    buttonsLabel_->setFont(baseButtonsFont_);
    // 强制 label 高度大于字体行高，避免字形下缘被裁切
    buttonsLabel_->setMinimumHeight(buttonsLabel_->fontMetrics().height() + 4);
    buttonsLabel_->setStyleSheet("color: #d9a25e;");
    layout_->addWidget(buttonsLabel_);

    // MouseToggle 锁存提示（默认隐藏；有锁存时橙色高亮，提示如何解除）
    toggleLabel_ = new QLabel(this);
    toggleLabel_->setFont(QFont("DengXian", 10, QFont::Bold));
    toggleLabel_->setStyleSheet("color: #ffb54d;");
    toggleLabel_->setWordWrap(true);   // 多个锁存键时超宽自动换行兜底
    toggleLabel_->hide();
    layout_->addWidget(toggleLabel_);

    // 展开时的映射列表（默认隐藏）
    mappingsLabel_ = new QLabel(this);
    baseMappingsFont_ = QFont("Microsoft YaHei", 9);
    mappingsLabel_->setFont(baseMappingsFont_);
    mappingsLabel_->setStyleSheet("color: #c9cdd4;");
    mappingsLabel_->setTextFormat(Qt::RichText);
    mappingsLabel_->setWordWrap(true);
    mappingsLabel_->hide();
    layout_->addWidget(mappingsLabel_);

    // 分隔线样式（背景/边框改由 paintEvent 手动绘制，确保透明窗口下可见）
    setStyleSheet(R"(
        #headerSeparator {
            background: rgba(255, 255, 255, 70);
            border: none;
        }
    )");

    // 默认位置：屏幕右上角
    const QRect screenRect = QGuiApplication::primaryScreen()->availableGeometry();
    adjustSize();
    move(screenRect.topRight() - QPoint(width() + 10, 10));
}

// ============================================================
// paintEvent：手动绘制圆角渐变背景 + 描边
// ============================================================
// WA_TranslucentBackground 的透明顶层窗口上样式表背景可能无法
// 可靠渲染，改为手动绘制，确保任何桌面/游戏环境下背景都可见。
void OverlayWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF rect = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(rect, 10, 10);
    QLinearGradient grad(0, 0, 0, height());
    grad.setColorAt(0.0, QColor(52, 54, 58));
    grad.setColorAt(1.0, QColor(24, 25, 28));
    p.fillPath(path, grad);
    // 有 MouseToggle 锁存时边框高亮橙色（提示有按键被锁存），否则保持原白边
    const bool hasToggle = !toggledButtons_.isEmpty();
    p.setPen(QPen(hasToggle ? QColor(255, 181, 77, 235) : QColor(255, 255, 255, 120),
                  hasToggle ? 2 : 1));
    p.drawPath(path);
}

// ============================================================
// setLayerName：更新显示的当前层
// ============================================================
void OverlayWidget::setLayerName(const QString& name) {
    currentLayerName_ = name;
    layerLabel_->setText(tr("当前层: %1").arg(name));
    if (expanded_)
        refreshMappings();
    adjustSize();
}

// ============================================================
// setOperationSet：更新显示的当前操作集
// ============================================================
void OverlayWidget::setOperationSet(const QString& name) {
    currentSetName_ = name;
    setLabel_->setText(tr("操作集: %1").arg(name));
    if (expanded_)
        refreshMappings();
    adjustSize();
}

// ============================================================
// setHeldButtons：更新显示的按下按键
// ============================================================
// 记录当前按下的按键集合（heldButtons_，供展开映射列表高亮对应行），
// 并更新顶部"按下按键"文本；展开状态下同步刷新映射列表里的高亮。
void OverlayWidget::setHeldButtons(const QSet<ControllerButton>& buttons) {
    heldButtons_ = buttons;
    if (buttons.isEmpty()) {
        buttonsLabel_->setText(tr("按下按键: 无"));
    } else {
        QStringList buttonNames;
        for (ControllerButton btn : buttons)
            buttonNames.append(controllerButtonDisplayName(btn));
        buttonsLabel_->setText(tr("按下按键: %1").arg(buttonNames.join(", ")));
    }
    if (expanded_)
        refreshMappings();   // 同步刷新映射列表中按下按键的高亮
    adjustSize();
}

// ============================================================
// setMouseToggleState / refreshToggleLabel：MouseToggle 锁存提示
// ============================================================
// 手柄键被映射为 mouseToggle 时，按一次会锁存鼠标键（松开不释放）。
// 为避免玩家误按后困惑（鼠标键一直按住），在悬浮窗用橙色高亮显示
// 当前锁存的「手柄键→鼠标键」，并提示再按一次解除。
void OverlayWidget::setMouseToggleState(ControllerButton button, MouseButton mb, bool active) {
    if (active)
        toggledButtons_.insert(button, mb);
    else
        toggledButtons_.remove(button);
    refreshToggleLabel();
}

void OverlayWidget::refreshToggleLabel() {
    if (toggledButtons_.isEmpty()) {
        toggleLabel_->hide();
    } else {
        QStringList parts;
        for (auto it = toggledButtons_.cbegin(); it != toggledButtons_.cend(); ++it)
            parts << tr("%1→%2").arg(controllerButtonDisplayName(it.key()),
                                     mouseButtonDisplayName(it.value()));
        toggleLabel_->setText(tr("⚠ 已锁存：%1\n（再按一次解除）").arg(parts.join("，")));
        toggleLabel_->show();
    }
    adjustSize();
    update();   // 边框颜色跟随锁存状态刷新
}

// ============================================================
// setMappingState：同步映射运行状态（顶部圆点颜色）
// ============================================================
// 与主窗口启停按钮同色：手柄未连接时红色，连接+运行绿色，连接+停止灰色。
void OverlayWidget::setMappingState(bool connected, bool mappingActive) {
    if (!statusDot_) return;
    currentDotColor_ = !connected ? QStringLiteral("#c62828")
                                  : (mappingActive ? QStringLiteral("#2e7d32")
                                                   : QStringLiteral("#9e9e9e"));
    const int dot = qRound(12 * scale_);
    statusDot_->setFixedSize(dot, dot);
    static_cast<StatusDotWidget*>(statusDot_)->setColor(QColor(currentDotColor_));
}

// ============================================================
// toggleExpanded：切换展开/收起状态（手柄触发）
// ============================================================
void OverlayWidget::toggleExpanded() {
    expanded_ = !expanded_;
    if (expanded_) {
        refreshMappings();
        mappingsLabel_->show();
    } else {
        mappingsLabel_->hide();
    }
    adjustSize();
}

// ============================================================
// refreshMappingsIfExpanded：配置变更时刷新展开状态下的映射列表
// ============================================================
void OverlayWidget::refreshMappingsIfExpanded() {
    if (expanded_)
        refreshMappings();
}

// ============================================================
// refreshMappings：展开时刷新当前层已映射的按键列表
// ============================================================
// 展示对象为「当前激活的操作层」自身的映射（栈顶；未激活任何
// 操作层时为公共层），而不是整个层栈的有效映射，避免：
//   - 未激活操作层时只显示公共层兜底、看不到操作层配置；
//   - 激活操作层时混入公共层兜底键导致列表混杂。
void OverlayWidget::refreshMappings() {
    if (!steamInput_ || !mappingsLabel_) return;

    // 定位当前层：最后激活的操作层（栈顶）或公共层。
    // 公共层取当前激活操作集的公共层（profile.commonLayer()）。
    const QVector<const OperationLayer*> active = steamInput_->getActiveLayers();
    const OperationLayer* layer = active.isEmpty()
                                      ? steamInput_->profile.commonLayer()
                                      : active.last();

    QStringList lines;
    if (layer) {
        // 遍历当前层内所有已映射的手柄按钮
        for (const ControllerButton btn : allControllerButtons()) {
            const KeyMapping* m = layer->getMapping(btn);
            if (!m) continue;

            QString desc;
            if (m->action.type == MappedAction::Type::SwitchLayer) {
                // SwitchLayer：将 layer id 解析为显示名
                const OperationLayer* target = steamInput_->profile.findLayer(m->action.layerName);
                desc = QStringLiteral("切换→%1").arg(target ? target->name : m->action.layerName);
            } else {
                desc = m->describe();
            }
            // 按下高亮：正在按下的按键对应行的按钮名变橙色加粗（颜色区别于
            // 常驻青色），松开后恢复，与"按下按键"栏同源（heldButtons_）。
            const bool held = heldButtons_.contains(btn);
            lines << QStringLiteral(
                "<span style='color:%1;font-weight:%2;'>%3</span>"
                "<span style='color:#8f949d;'> → </span>"
                "<span style='color:#c9cdd4;'>%4</span>").arg(
                held ? QStringLiteral("#ffb54d") : QStringLiteral("#7fc9c4"),
                held ? QStringLiteral("bold") : QStringLiteral("600"),
                controllerButtonDisplayName(btn), desc.toHtmlEscaped());
        }
    }
    // 摇杆映射
    lines << QStringLiteral("<span style='color:#7fc9c4;font-weight:600;'>左摇杆</span>"
                            "<span style='color:#8f949d;'> → </span>"
                            "<span style='color:#c9cdd4;'>WASD 移动</span>");
    lines << QStringLiteral("<span style='color:#7fc9c4;font-weight:600;'>右摇杆</span>"
                            "<span style='color:#8f949d;'> → </span>"
                            "<span style='color:#c9cdd4;'>视角控制</span>");

    mappingsLabel_->setText(lines.isEmpty() ? tr("（无映射）") : lines.join("<br>"));
    adjustSize();
}

// ============================================================
// 鼠标事件
// ============================================================
// 左键点击：将主窗口拉到前台
// 左键拖拽：移动悬浮窗
// 右键点击：展开/收起映射列表
void OverlayWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        dragMoved_ = false;
        pressPos_ = event->globalPosition().toPoint();
        dragPos_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    } else if (event->button() == Qt::RightButton) {
        expanded_ = !expanded_;
        if (expanded_) {
            refreshMappings();
            mappingsLabel_->show();
        } else {
            mappingsLabel_->hide();
        }
        adjustSize();
        event->accept();
    }
}

void OverlayWidget::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_ && event->buttons() & Qt::LeftButton) {
        const QPoint delta = event->globalPosition().toPoint() - pressPos_;
        if (!dragMoved_ && (delta.x() * delta.x() + delta.y() * delta.y()) > 25)
            dragMoved_ = true;
        if (dragMoved_)
            move(event->globalPosition().toPoint() - dragPos_);
        event->accept();
    }
}

void OverlayWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // 没有实际移动过 → 视为点击，将主窗口拉到前台
        if (!dragMoved_ && mainWindow_) {
            mainWindow_->showNormal();
            mainWindow_->raise();
            mainWindow_->activateWindow();
        }
        dragging_ = false;
        dragMoved_ = false;
        event->accept();
    }
}

// ============================================================
// 滚轮缩放悬浮窗大小
// ============================================================
// 向上滚动放大、向下滚动缩小，缩放系数限制在 [0.5, 2.0]。
// 字体、状态圆点随缩放比例一起调整，窗口由 adjustSize 自动重排。
void OverlayWidget::wheelEvent(QWheelEvent* event) {
    const int delta = event->angleDelta().y();
    if (delta != 0) {
        const qreal step = 0.1;
        scale_ += (delta > 0 ? step : -step);
        scale_ = qBound(0.5, scale_, 2.0);
        applyCurrentScale();
    }
    event->accept();
}

// ============================================================
// applyScale：应用外部传入的缩放系数（启动恢复上次大小）
// ============================================================
void OverlayWidget::applyScale(qreal scale) {
    scale_ = qBound(0.5, scale, 2.0);
    applyCurrentScale();
}

// ============================================================
// applyCurrentScale：把当前 scale_ 应用到各控件并重排窗口
// ============================================================
void OverlayWidget::applyCurrentScale() {
    const auto scaleFont = [this](const QFont& base) {
        QFont f = base;
        f.setPointSizeF(qMax(1.0, base.pointSizeF() * scale_));
        return f;
    };
    layerLabel_->setFont(scaleFont(baseLayerFont_));
    setLabel_->setFont(scaleFont(baseLayerFont_));
    buttonsLabel_->setFont(scaleFont(baseButtonsFont_));
    mappingsLabel_->setFont(scaleFont(baseMappingsFont_));
    // 更新「按下按键」最小高度，防止缩放后字形下缘被裁切
    buttonsLabel_->setMinimumHeight(buttonsLabel_->fontMetrics().height() + 4);
    // 状态圆点随缩放调整尺寸（自绘控件内部按当前尺寸画圆）
    const int dot = qRound(12 * scale_);
    statusDot_->setFixedSize(dot, dot);
    static_cast<StatusDotWidget*>(statusDot_)->setColor(QColor(currentDotColor_));
    adjustSize();
}
