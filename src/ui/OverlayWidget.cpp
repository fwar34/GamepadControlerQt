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

// 【C++ 语法】#include "..."：先在本源文件所在目录查找头文件；包含本类对应的头文件（声明了类的全部成员）
#include "OverlayWidget.h"

// 【C++ 语法】相对路径包含：从本文件所在目录向上（../）再进 core 目录查找头文件
#include "../core/InputTypes.h"   // 输入类型定义（ControllerButton、MouseButton、MappedAction 等）
#include "../core/SteamInput.h"   // 手柄映射引擎类定义（查询激活层、映射、公共层等）

// 【C++ 语法】#include <...>：尖括号形式，在编译器标准头文件搜索路径中查找 Qt 头文件
#include <QFont>           // 【Qt】字体类
#include <QFrame>          // 【Qt】框架控件（此处用作分隔线）
#include <QGuiApplication> // 【Qt】GUI 应用类（用于获取屏幕信息）
#include <QScreen>         // 【Qt】屏幕类（获取屏幕可用区域）
#include <QStringList>     // 【Qt】字符串列表（存放按钮名、富文本行等）
#include <QVBoxLayout>     // 【Qt】垂直布局管理器
#include <QHBoxLayout>     // 【Qt】水平布局管理器
#include <QLabel>          // 【Qt】标签控件
#include <QMouseEvent>     // 【Qt】鼠标事件类
#include <QWheelEvent>     // 【Qt】滚轮事件类
#include <QPainter>        // 【Qt】绘图器（在控件上绘制图形）
#include <QPainterPath>    // 【Qt】绘图路径（用于构造圆角矩形路径）
#include <QPaintEvent>     // 【Qt】绘制事件类
#include <QLinearGradient> // 【Qt】线性渐变
#include <QPen>            // 【Qt】画笔（用于描边）
#include <QColor>          // 【Qt】颜色类

// 【C++ 语法】匿名命名空间：namespace { ... } 中的名称仅在当前 .cpp 文件内可见（等价于文件级 static），
// 避免与其它编译单元的符号重名冲突
namespace {

// ============================================================
// StatusDotWidget —— 顶部映射状态圆点（自绘实心圆）
// ============================================================
// 不用 QLabel + QSS：透明无边框窗口（WA_TranslucentBackground）上，
// 样式表的 background-color + border-radius 无法可靠裁剪圆角，
// 圆点会渲染成方形。这里在 paintEvent 里用 QPainter 直接画椭圆，
// 任何环境下都是标准实心圆。
// 【C++ 语法】类定义 + 继承：StatusDotWidget 公开继承自 QWidget
class StatusDotWidget : public QWidget {
public:   // 【C++ 语法】public 访问区段：其后的成员对外公开
    explicit StatusDotWidget(QWidget* parent = nullptr) : QWidget(parent) {   // 【C++ 语法】explicit 禁止隐式转换；冒号后为初始化列表（先调用基类构造函数）
        setFixedSize(12, 12);   // 【Qt】把控件尺寸固定为 12×12 像素
    }
    void setColor(const QColor& c) {   // 【C++ 语法】const 引用形参：不拷贝、只读访问颜色对象
        if (color_ != c) {   // 颜色有变化时才处理
            color_ = c;      // 更新内部颜色成员
            update();        // 【Qt】请求重绘（后续会触发 paintEvent）
        }
    }
protected:   // 【C++ 语法】protected 访问区段：仅本类及派生类可访问
    void paintEvent(QPaintEvent*) override {   // 【Qt】重写绘制事件；参数未使用故省略参数名
        QPainter p(this);   // 【Qt】创建绘图器，绘制目标为当前控件 this
        p.setRenderHint(QPainter::Antialiasing);   // 【Qt】开启抗锯齿，使圆形边缘平滑
        p.setPen(Qt::NoPen);   // 【Qt】不绘制描边，只保留填充
        p.setBrush(color_);    // 【Qt】设置填充画刷为成员颜色
        p.drawEllipse(rect()); // 【Qt】在控件整个矩形区域内画椭圆（正方形矩形即为正圆）
    }
private:   // 【C++ 语法】private 访问区段：其后的成员仅本类内部可访问
    QColor color_ = QColor(QStringLiteral("#2e7d32"));  // 初始：映射运行中（绿）
};

// ============================================================
// themedFont —— 构造带统一渲染策略的雅黑字体
// ============================================================
// 显式 QFont(...) 构造不会继承全局字体策略，这里统一补上
// 抗锯齿 + 无 hinting，避免小字号中文笔画粘连、边缘毛糙。
// ============================================================
// 【C++ 语法】普通函数定义：不属于任何类；QFont::Weight 是枚举类型；weight 形参带默认值（调用时可省略）
QFont themedFont(int pointSize, QFont::Weight weight = QFont::Normal) {   // 按字号/字重生成雅黑字体
    QFont f(QStringLiteral("Microsoft YaHei"), pointSize, weight);   // 【Qt】按 字体名/字号/字重 构造字体对象
    f.setStyleStrategy(QFont::PreferAntialias);   // 【Qt】字体渲染策略：优先抗锯齿
    f.setHintingPreference(QFont::PreferNoHinting);   // 【Qt】不用字体 hinting，避免小字号笔画粘连、边缘毛糙
    return f;   // 返回字体对象（局部对象返回时编译器通常会做拷贝省略/移动优化）
}

}  // namespace

// ============================================================
// 构造：搭建悬浮窗外观与初始位置
// ============================================================
// 【C++ 语法】成员函数定义：类名::函数名；冒号后为初始化列表（调用基类构造函数），parent 是父窗口指针
OverlayWidget::OverlayWidget(QWidget* parent) : QWidget(parent) {
    // 【Qt】setWindowFlags：设置窗口标志；Qt::FramelessWindowHint=无边框、
    // Qt::WindowStaysOnTopHint=始终置顶、Qt::Tool=工具窗口（不占用任务栏）
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    // 【Qt】setAttribute + WA_TranslucentBackground：让窗口背景透明（圆角/半透明需配合自绘）
    setAttribute(Qt::WA_TranslucentBackground);
    setObjectName(QStringLiteral("overlayRoot"));   // 【Qt】设置对象名，供样式表选择器 #overlayRoot 引用

    layout_ = new QVBoxLayout(this);   // 【C++ 语法】new 在堆上创建垂直布局，父对象为 this（Qt 会随父对象自动销毁）
    layout_->setContentsMargins(12, 10, 12, 10);   // 【Qt】设置布局内容边距：左/上/右/下
    layout_->setSpacing(5);   // 【Qt】设置布局中子项之间的间距

    // 顶部行：映射状态圆点 + 当前层名称
    statusDot_ = new StatusDotWidget(this);   // 创建自绘状态圆点控件
    layerLabel_ = new QLabel(tr("当前层: Common"), this);   // 【Qt】创建标签；tr() 标记可翻译文本，便于国际化
    baseLayerFont_ = themedFont(11, QFont::Bold);   // 生成 11 号加粗雅黑，作为层名标签的基础字体
    layerLabel_->setFont(baseLayerFont_);   // 【Qt】把字体应用到标签
    layerLabel_->setStyleSheet("color: #e8eaee;");   // 【Qt】QSS 内联样式：设置文字颜色为浅白
    auto* headerRow = new QHBoxLayout;   // 【C++ 语法】auto 自动推导指针类型；创建水平布局
    headerRow->setSpacing(7);   // 水平行内子项间距
    headerRow->addWidget(statusDot_);   // 把状态圆点加入水平行
    headerRow->addWidget(layerLabel_);  // 把层名标签加入水平行
    headerRow->addStretch();   // 【Qt】添加弹簧占位，把前面控件推到左侧
    layout_->addLayout(headerRow);   // 把水平行作为子布局嵌入主垂直布局

    // 当前操作集名称（切换操作集时更新，收起/展开都显示）
    setLabel_ = new QLabel(tr("操作集: 默认操作集"), this);   // 创建操作集名称标签
    setLabel_->setFont(themedFont(9));   // 应用 9 号雅黑字体
    setLabel_->setStyleSheet("color: #7fc9c4;");   // 设置青绿色文字
    layout_->addWidget(setLabel_);   // 加入主垂直布局

    // 头部与内容之间的分隔线
    auto* separator = new QFrame(this);   // 【Qt】创建 QFrame 控件（此处用作分隔线）
    separator->setObjectName(QStringLiteral("headerSeparator"));   // 设置对象名，供样式表选中
    separator->setFixedHeight(1);   // 固定高度为 1 像素
    layout_->addWidget(separator);   // 加入主垂直布局

    // 按下的手柄按键（非粗体：粗体字形下缘超出度量导致「无」字截断）
    buttonsLabel_ = new QLabel(tr("按下按键: 无"), this);   // 创建"按下按键"标签
    baseButtonsFont_ = themedFont(10);   // 生成 10 号雅黑，作为按键标签的基础字体
    buttonsLabel_->setFont(baseButtonsFont_);   // 应用字体
    // 强制 label 高度大于字体行高，避免字形下缘被裁切
    buttonsLabel_->setMinimumHeight(buttonsLabel_->fontMetrics().height() + 4);   // 【Qt】最小高度 = 字体行高 + 4 像素
    buttonsLabel_->setStyleSheet("color: #d9a25e;");   // 设置橙色文字
    layout_->addWidget(buttonsLabel_);   // 加入主垂直布局

    // MouseToggle 锁存提示（默认隐藏；有锁存时橙色高亮，提示如何解除）
    toggleLabel_ = new QLabel(this);   // 创建锁存提示标签
    toggleLabel_->setFont(themedFont(10, QFont::Bold));   // 应用 10 号加粗字体
    toggleLabel_->setStyleSheet("color: #ffb54d;");   // 设置橙色文字
    // 【Qt】setWordWrap(true)：启用自动换行
    toggleLabel_->setWordWrap(true);   // 多个锁存键时超宽自动换行兜底
    toggleLabel_->hide();   // 【Qt】默认隐藏
    layout_->addWidget(toggleLabel_);   // 加入主垂直布局

    // 展开时的映射列表（默认隐藏）
    mappingsLabel_ = new QLabel(this);   // 创建映射列表标签
    baseMappingsFont_ = themedFont(9);   // 生成 9 号雅黑，作为映射列表标签的基础字体
    mappingsLabel_->setFont(baseMappingsFont_);   // 应用字体
    mappingsLabel_->setStyleSheet("color: #c9cdd4;");   // 设置浅灰文字
    mappingsLabel_->setTextFormat(Qt::RichText);   // 【Qt】文本格式设为富文本（支持 HTML 标签渲染）
    mappingsLabel_->setWordWrap(true);   // 启用自动换行
    mappingsLabel_->hide();   // 默认隐藏
    layout_->addWidget(mappingsLabel_);   // 加入主垂直布局

    // 分隔线样式（背景/边框改由 paintEvent 手动绘制，确保透明窗口下可见）
    // 【C++ 语法】R"(...)"：原始字符串字面量，内部字符不做转义，适合直接书写多行 QSS 样式文本
    setStyleSheet(R"(
        #headerSeparator {
            background: rgba(255, 255, 255, 70);
            border: none;
        }
    )");   // 【Qt】整窗样式表：给分隔线设置半透明白背景、无边框

    // 默认位置：屏幕右上角
    const QRect screenRect = QGuiApplication::primaryScreen()->availableGeometry();   // 【Qt】获取主屏可用区域（不含任务栏等）
    adjustSize();   // 【Qt】根据内容自适应调整窗口尺寸
    move(screenRect.topRight() - QPoint(width() + 10, 10));   // 【Qt】把窗口移到右上角内侧（留 10px 边距）
}

// ============================================================
// paintEvent：手动绘制圆角渐变背景 + 描边
// ============================================================
// WA_TranslucentBackground 的透明顶层窗口上样式表背景可能无法
// 可靠渲染，改为手动绘制，确保任何桌面/游戏环境下背景都可见。
// 【Qt】paintEvent 是 QWidget 的保护虚函数，窗口需要重绘时由事件系统自动调用
void OverlayWidget::paintEvent(QPaintEvent*) {   // 参数为绘制事件对象（此处未使用，故省略参数名）
    QPainter p(this);   // 【Qt】创建绘图器，绘制目标为当前控件 this
    p.setRenderHint(QPainter::Antialiasing);   // 【Qt】开启抗锯齿
    const QRectF rect = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);   // 【Qt】取控件矩形并内缩 0.5 像素，使描边落在像素边界内
    QPainterPath path;   // 【Qt】创建绘制路径对象
    path.addRoundedRect(rect, 10, 10);   // 在路径中添加圆角矩形（x/y 方向圆角半径均为 10 像素）
    QLinearGradient grad(0, 0, 0, height());   // 【Qt】线性渐变：起点 (0,0) 终点 (0,height())，即从上到下渐变
    grad.setColorAt(0.0, QColor(52, 54, 58));   // 渐变起点（顶部）颜色：深灰
    grad.setColorAt(1.0, QColor(24, 25, 28));   // 渐变终点（底部）颜色：更深灰
    p.fillPath(path, grad);   // 用渐变填充圆角矩形路径
    // 有 MouseToggle 锁存时边框高亮橙色（提示有按键被锁存），否则保持原白边
    const bool hasToggle = !toggledButtons_.isEmpty();   // 判断当前是否存在锁存的鼠标键
    p.setPen(QPen(hasToggle ? QColor(255, 181, 77, 235) : QColor(255, 255, 255, 120),   // 【C++ 语法】三目运算：锁存时橙色、否则白色
                  hasToggle ? 2 : 1));   // 三目运算：锁存时线宽 2、否则线宽 1
    p.drawPath(path);   // 沿路径绘制描边
}

// ============================================================
// setLayerName：更新显示的当前层
// ============================================================
void OverlayWidget::setLayerName(const QString& name) {   // 参数：新层名称（const 引用，避免拷贝）
    currentLayerName_ = name;   // 记录当前层名，供刷新映射列表时使用
    layerLabel_->setText(tr("当前层: %1").arg(name));   // 【Qt】%1 占位符由 arg(name) 替换后显示
    if (expanded_)   // 【C++ 语法】无花括号的 if：只有紧接的一条语句属于其分支
        refreshMappings();   // 展开状态下刷新映射列表
    adjustSize();   // 重新自适应尺寸
}

// ============================================================
// setOperationSet：更新显示的当前操作集
// ============================================================
void OverlayWidget::setOperationSet(const QString& name) {   // 参数：新操作集名称
    currentSetName_ = name;   // 记录当前操作集名
    setLabel_->setText(tr("操作集: %1").arg(name));   // 更新操作集标签文本
    if (expanded_)   // 展开时同步刷新
        refreshMappings();   // 刷新映射列表
    adjustSize();   // 重新自适应尺寸
}

// ============================================================
// setHeldButtons：更新显示的按下按键
// ============================================================
// 记录当前按下的按键集合（heldButtons_，供展开映射列表高亮对应行），
// 并更新顶部"按下按键"文本；展开状态下同步刷新映射列表里的高亮。
void OverlayWidget::setHeldButtons(const QSet<ControllerButton>& buttons) {   // 参数：当前按下的手柄按键集合
    heldButtons_ = buttons;   // 保存按下按键集合
    if (buttons.isEmpty()) {   // 没有按键按下时
        buttonsLabel_->setText(tr("按下按键: 无"));   // 显示"无"
    } else {   // 有按键按下时
        QStringList buttonNames;   // 准备按钮显示名列表
        for (ControllerButton btn : buttons)   // 【C++ 语法】基于范围的 for 循环（C++11）：遍历集合中的每个元素
            buttonNames.append(controllerButtonDisplayName(btn));   // 逐个追加按钮的显示名
        buttonsLabel_->setText(tr("按下按键: %1").arg(buttonNames.join(", ")));   // 多个按钮名用逗号拼接后显示
    }
    if (expanded_)   // 展开状态下
        refreshMappings();   // 同步刷新映射列表中按下按键的高亮
    adjustSize();   // 重新自适应尺寸
}

// ============================================================
// setMouseToggleState / refreshToggleLabel：MouseToggle 锁存提示
// ============================================================
// 手柄键被映射为 mouseToggle 时，按一次会锁存鼠标键（松开不释放）。
// 为避免玩家误按后困惑（鼠标键一直按住），在悬浮窗用橙色高亮显示
// 当前锁存的「手柄键→鼠标键」，并提示再按一次解除。
void OverlayWidget::setMouseToggleState(ControllerButton button, MouseButton mb, bool active) {   // 参数：手柄键/鼠标键/是否锁存
    if (active)   // 锁存生效时
        toggledButtons_.insert(button, mb);   // 【Qt】QHash::insert 插入键值对：手柄键→鼠标键
    else   // 解除锁存时
        toggledButtons_.remove(button);   // 移除该手柄键的锁存记录
    refreshToggleLabel();   // 刷新锁存提示文本
}

void OverlayWidget::refreshToggleLabel() {   // 根据 toggledButtons_ 刷新锁存提示
    if (toggledButtons_.isEmpty()) {   // 没有锁存时
        toggleLabel_->hide();   // 隐藏提示标签
    } else {   // 有锁存时
        QStringList parts;   // 准备提示片段列表
        for (auto it = toggledButtons_.cbegin(); it != toggledButtons_.cend(); ++it)   // 【C++ 语法】传统 for 循环遍历哈希表；cbegin/cend 取常量迭代器，++it 前置自增
            parts << tr("%1→%2").arg(controllerButtonDisplayName(it.key()),   // 【C++ 语法】"<<" 向 QStringList 追加元素；组装"手柄键→鼠标键"文本
                                     mouseButtonDisplayName(it.value()));   // it.value() 取得该键对应的鼠标键显示名
        toggleLabel_->setText(tr("⚠ 已锁存：%1\n（再按一次解除）").arg(parts.join("，")));   // 拼接完整提示文本（\n 为换行符）
        toggleLabel_->show();   // 显示提示标签
    }
    adjustSize();   // 重新自适应尺寸
    // 【Qt】update()：请求整个控件重绘，触发 paintEvent，让边框颜色即时刷新
    update();   // 边框颜色跟随锁存状态刷新
}

// ============================================================
// setMappingState：同步映射运行状态（顶部圆点颜色）
// ============================================================
// 与主窗口启停按钮同色：手柄未连接时红色，连接+运行绿色，连接+停止灰色。
void OverlayWidget::setMappingState(bool connected, bool mappingActive) {   // 参数：是否已连接/是否运行中
    if (!statusDot_) return;   // 圆点控件尚未创建则直接返回（空指针保护）
    currentDotColor_ = !connected ? QStringLiteral("#c62828")   // 【C++ 语法】嵌套三目运算：未连接 → 红
                                  : (mappingActive ? QStringLiteral("#2e7d32")   // 已连接且运行中 → 绿
                                                   : QStringLiteral("#9e9e9e"));   // 已连接但已停止 → 灰
    const int dot = qRound(12 * scale_);   // 【Qt】qRound 四舍五入取整；按缩放系数计算圆点直径
    statusDot_->setFixedSize(dot, dot);   // 设置圆点控件尺寸
    static_cast<StatusDotWidget*>(statusDot_)->setColor(QColor(currentDotColor_));   // 【C++ 语法】static_cast 静态向下转型为子类指针后再调用子类方法
}

// ============================================================
// toggleExpanded：切换展开/收起状态（手柄触发）
// ============================================================
void OverlayWidget::toggleExpanded() {   // 翻转展开/收起状态
    expanded_ = !expanded_;   // 【C++ 语法】! 逻辑取反，翻转布尔值
    if (expanded_) {   // 展开时
        refreshMappings();   // 刷新映射列表
        mappingsLabel_->show();   // 显示映射列表
    } else {   // 收起时
        mappingsLabel_->hide();   // 隐藏映射列表
    }
    adjustSize();   // 重新自适应尺寸
}

// ============================================================
// refreshMappingsIfExpanded：配置变更时刷新展开状态下的映射列表
// ============================================================
void OverlayWidget::refreshMappingsIfExpanded() {   // 供外部信号连接的回调
    if (expanded_)   // 展开状态下
        refreshMappings();   // 才刷新映射列表
}

// ============================================================
// refreshMappings：展开时刷新当前层已映射的按键列表
// ============================================================
// 展示对象为「当前激活的操作层」自身的映射（栈顶；未激活任何
// 操作层时为公共层），而不是整个层栈的有效映射，避免：
//   - 未激活操作层时只显示公共层兜底、看不到操作层配置；
//   - 激活操作层时混入公共层兜底键导致列表混杂。
void OverlayWidget::refreshMappings() {   // 重新生成映射列表富文本
    if (!steamInput_ || !mappingsLabel_) return;   // 引擎或标签未就绪则返回（空指针保护）

    // 定位当前层：最后激活的操作层（栈顶）或公共层。
    // 公共层取当前激活操作集的公共层（profile.commonLayer()）。
    const QVector<const OperationLayer*> active = steamInput_->getActiveLayers();   // 获取当前激活的操作层栈（栈顶在末尾）
    const OperationLayer* layer = active.isEmpty()   // 【C++ 语法】三目运算 + 常量指针：栈为空则取公共层
                                      ? steamInput_->profile.commonLayer()
                                      : active.last();   // 否则取栈顶（最后激活）的操作层

    QStringList lines;   // 收集要显示的富文本行
    if (layer) {   // 层指针非空才处理
        // 遍历当前层内所有已映射的手柄按钮
        for (const ControllerButton btn : allControllerButtons()) {   // 【C++ 语法】基于范围的 for：遍历所有手柄按钮枚举值
            const KeyMapping* m = layer->getMapping(btn);   // 查询该按钮在当前层的映射（未映射时返回空指针）
            if (!m) continue;   // 【C++ 语法】if 单语句 + continue：未映射则跳过本轮循环

            QString desc;   // 该按钮的动作描述文本
            if (m->action.type == MappedAction::Type::SwitchLayer) {   // 动作类型为"切换层"时
                // SwitchLayer：将 layer id 解析为显示名
                const OperationLayer* target = steamInput_->profile.findLayer(m->action.layerName);   // 按层名查找目标层（可能为空）
                desc = QStringLiteral("切换→%1").arg(target ? target->name : m->action.layerName);   // 【C++ 语法】三目运算：找到则显示层名，否则回退显示原始层 id
            } else {   // 其它动作类型
                desc = m->describe();   // 调用映射对象的 describe() 生成动作描述
            }
            // 按下高亮：正在按下的按键对应行的按钮名变橙色加粗（颜色区别于
            // 常驻青色），松开后恢复，与"按下按键"栏同源（heldButtons_）。
            const bool held = heldButtons_.contains(btn);   // 判断该按钮当前是否处于按下状态
            lines << QStringLiteral(   // 【C++ 语法】"<<" 向 QStringList 追加一行富文本
                "<span style='color:%1;font-weight:%2;'>%3</span>"   // 【Qt】HTML 片段：按钮名（颜色/字重随按下状态变化）
                "<span style='color:#8f949d;'> → </span>"   // 灰色箭头分隔符（相邻字符串字面量会自动拼接）
                "<span style='color:#c9cdd4;'>%4</span>").arg(   // 动作描述（浅灰）
                held ? QStringLiteral("#ffb54d") : QStringLiteral("#7fc9c4"),   // 三目运算：按下=橙色、未按下=青色
                held ? QStringLiteral("bold") : QStringLiteral("600"),   // 三目运算：按下=bold 加粗、未按下=字重 600
                controllerButtonDisplayName(btn), desc.toHtmlEscaped());   // 参数：按钮显示名 + HTML 转义后的动作描述
        }
    }
    // 摇杆映射
    lines << QStringLiteral("<span style='color:#7fc9c4;font-weight:600;'>左摇杆</span>"   // 固定显示行：左摇杆（青色加粗）
                            "<span style='color:#8f949d;'> → </span>"   // 灰色箭头
                            "<span style='color:#c9cdd4;'>WASD 移动</span>");   // 动作说明：WASD 移动
    lines << QStringLiteral("<span style='color:#7fc9c4;font-weight:600;'>右摇杆</span>"   // 固定显示行：右摇杆
                            "<span style='color:#8f949d;'> → </span>"   // 灰色箭头
                            "<span style='color:#c9cdd4;'>视角控制</span>");   // 动作说明：视角控制

    mappingsLabel_->setText(lines.isEmpty() ? tr("（无映射）") : lines.join("<br>"));   // 【Qt】列表为空则显示"（无映射）"，否则用 <br> 换行拼接
    adjustSize();   // 重新自适应尺寸
}

// ============================================================
// 鼠标事件
// ============================================================
// 左键点击：将主窗口拉到前台
// 左键拖拽：移动悬浮窗
// 右键点击：展开/收起映射列表
// 【Qt】mousePressEvent 是 QWidget 的保护虚函数，鼠标按下时由事件系统自动回调
void OverlayWidget::mousePressEvent(QMouseEvent* event) {   // 参数：鼠标事件对象
    if (event->button() == Qt::LeftButton) {   // 【Qt】判断按下的按钮是否为左键
        dragging_ = true;   // 进入拖拽状态
        dragMoved_ = false;   // 复位"实际移动过"标记
        pressPos_ = event->globalPosition().toPoint();   // 【Qt】记录按下时的全局坐标（用于判断拖拽距离）
        dragPos_ = event->globalPosition().toPoint() - frameGeometry().topLeft();   // 记录按下点相对窗口左上角的偏移
        event->accept();   // 【Qt】标记事件已处理（阻止继续传播给父级）
    } else if (event->button() == Qt::RightButton) {   // 右键按下时
        expanded_ = !expanded_;   // 翻转展开状态
        if (expanded_) {   // 展开时
            refreshMappings();   // 刷新映射列表
            mappingsLabel_->show();   // 显示映射列表
        } else {   // 收起时
            mappingsLabel_->hide();   // 隐藏映射列表
        }
        adjustSize();   // 重新自适应尺寸
        event->accept();   // 标记事件已处理
    }
}

// 【Qt】mouseMoveEvent 是 QWidget 的保护虚函数，鼠标移动时自动回调
void OverlayWidget::mouseMoveEvent(QMouseEvent* event) {   // 参数：鼠标事件对象
    if (dragging_ && event->buttons() & Qt::LeftButton) {   // 正在拖拽 且 左键仍按住时（& 按位与用于检测按键状态）
        const QPoint delta = event->globalPosition().toPoint() - pressPos_;   // 计算当前点相对按下点的位移
        if (!dragMoved_ && (delta.x() * delta.x() + delta.y() * delta.y()) > 25)   // 位移平方和 > 25（约 5 像素）才认定为真实移动
            dragMoved_ = true;   // 标记已实际移动
        if (dragMoved_)   // 确实移动过才跟随移动
            move(event->globalPosition().toPoint() - dragPos_);   // 【Qt】move() 把窗口移动到目标位置
        event->accept();   // 标记事件已处理
    }
}

// 【Qt】mouseReleaseEvent 是 QWidget 的保护虚函数，鼠标释放时自动回调
void OverlayWidget::mouseReleaseEvent(QMouseEvent* event) {   // 参数：鼠标事件对象
    if (event->button() == Qt::LeftButton) {   // 释放的是左键时
        // 没有实际移动过 → 视为点击，将主窗口拉到前台
        if (!dragMoved_ && mainWindow_) {   // 未移动过 且 主窗口指针有效时
            mainWindow_->showNormal();   // 【Qt】恢复主窗口到正常大小（取消最小化）
            mainWindow_->raise();   // 【Qt】把主窗口提到所有窗口最前
            mainWindow_->activateWindow();   // 【Qt】激活主窗口（获取焦点）
        }
        dragging_ = false;   // 结束拖拽状态
        dragMoved_ = false;   // 复位移动标记
        event->accept();   // 标记事件已处理
    }
}

// ============================================================
// 滚轮缩放悬浮窗大小
// ============================================================
// 向上滚动放大、向下滚动缩小，缩放系数限制在 [0.5, 2.0]。
// 字体、状态圆点随缩放比例一起调整，窗口由 adjustSize 自动重排。
// 【Qt】wheelEvent 是 QWidget 的保护虚函数，滚轮滚动时自动回调
void OverlayWidget::wheelEvent(QWheelEvent* event) {   // 参数：滚轮事件对象
    const int delta = event->angleDelta().y();   // 【Qt】angleDelta() 返回滚轮角度增量，取垂直分量 y（上滚为正）
    if (delta != 0) {   // 有滚动量才处理
        const qreal step = 0.1;   // 每次滚动的缩放步长
        scale_ += (delta > 0 ? step : -step);   // 三目运算：上滚加步长、下滚减步长
        scale_ = qBound(0.5, scale_, 2.0);   // 【Qt】qBound 把缩放系数限制在 [0.5, 2.0] 区间内
        applyCurrentScale();   // 应用当前缩放系数
    }
    event->accept();   // 标记事件已处理
}

// ============================================================
// applyScale：应用外部传入的缩放系数（启动恢复上次大小）
// ============================================================
void OverlayWidget::applyScale(qreal scale) {   // 参数：目标缩放系数
    scale_ = qBound(0.5, scale, 2.0);   // 同样限制在 [0.5, 2.0] 区间
    applyCurrentScale();   // 应用缩放
}

// ============================================================
// applyCurrentScale：把当前 scale_ 应用到各控件并重排窗口
// ============================================================
void OverlayWidget::applyCurrentScale() {   // 应用当前缩放系数到各控件
    // 【C++ 语法】lambda 表达式：[this] 捕获列表捕获 this 以访问成员 scale_；
    // 形参 base 为基础字体，函数体返回按比例缩放后的新字体
    const auto scaleFont = [this](const QFont& base) {   // 生成"按缩放系数放大字体"的局部函数对象
        QFont f = base;   // 复制一份基础字体
        f.setPointSizeF(qMax(1.0, base.pointSizeF() * scale_));   // 【Qt】字号 = 基础字号 × 缩放系数，且最小为 1.0（qMax 取较大值）
        return f;   // 返回缩放后的字体
    };
    layerLabel_->setFont(scaleFont(baseLayerFont_));   // 应用缩放后的层名字体
    setLabel_->setFont(scaleFont(baseLayerFont_));   // 应用缩放后的操作集字体
    buttonsLabel_->setFont(scaleFont(baseButtonsFont_));   // 应用缩放后的按键字体
    mappingsLabel_->setFont(scaleFont(baseMappingsFont_));   // 应用缩放后的映射列表字体
    // 更新「按下按键」最小高度，防止缩放后字形下缘被裁切
    buttonsLabel_->setMinimumHeight(buttonsLabel_->fontMetrics().height() + 4);   // 重算最小高度
    // 状态圆点随缩放调整尺寸（自绘控件内部按当前尺寸画圆）
    const int dot = qRound(12 * scale_);   // 按缩放系数计算圆点直径
    statusDot_->setFixedSize(dot, dot);   // 设置圆点尺寸
    static_cast<StatusDotWidget*>(statusDot_)->setColor(QColor(currentDotColor_));   // 向下转型为子类指针后更新圆点颜色
    adjustSize();   // 重新自适应尺寸
}
