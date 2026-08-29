#pragma once   // 【C++ 语法】#pragma once：预处理器指令，保证本头文件只被包含编译一次（防止重复包含/重复定义）

// 【C++ 语法】#include <...>：尖括号形式，在编译器标准头文件搜索路径中查找 Qt 头文件
#include <QWidget>   // 【Qt】QWidget：所有界面控件的基类（窗口/控件能力）
#include <QSet>      // 【Qt】QSet：基于哈希的无序集合（元素唯一，用于保存当前按下的手柄按键集合）
#include <QHash>     // 【Qt】QHash：基于哈希的键值映射表（查找快，用于保存 手柄键→鼠标键 的锁存关系）
#include <QPoint>    // 【Qt】QPoint：整数二维坐标点（用于记录按下/拖拽时的位置）

#include "../core/InputTypes.h"   // 包含项目内输入类型定义（ControllerButton、MouseButton、KeyMapping 等）

// 【C++ 语法】class 前向声明：只声明类型名、不提供完整定义；只要类仅以指针形式使用（指针成员/指针参数），
// 就无需包含其完整头文件，从而减少头文件间的编译依赖与耦合
class QVBoxLayout;   // Qt 垂直布局类（前向声明）
class QLabel;        // Qt 标签控件类（前向声明）
class QWheelEvent;   // Qt 滚轮事件类（前向声明）
class SteamInput;    // 项目内手柄映射引擎类（前向声明）

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
//   - 当前操作集名称
//   - 当前激活的操作层名称
//   - 当前按下的手柄按键列表
// 显示内容（展开状态）：
//   - 收起状态的所有内容
//   - 当前层所有已映射的按键及其动作
// =====================================================================
// 【C++ 语法】类定义 + 继承：OverlayWidget 以 public（公有）方式继承自 QWidget，
// 是 QWidget 的派生类，可复用其窗口/控件/事件处理能力
class OverlayWidget : public QWidget {
    Q_OBJECT   // 【Qt】Q_OBJECT 宏：为类生成信号/槽所需的元对象代码；凡包含 signals/slots 的 QObject 派生类都必须声明它（否则编译报错）
public:   // 【C++ 语法】public 访问区段：其后的成员对外公开，外部代码均可调用
    explicit OverlayWidget(QWidget* parent = nullptr);   // 【C++ 语法】explicit 禁止隐式类型转换；构造函数的父窗口指针参数，默认空 = 独立顶层窗口

    void setSteamInput(SteamInput* input) { steamInput_ = input; }   // 【C++ 语法】类体内实现的成员函数默认内联；参数为指针；注入映射引擎指针供刷新列表用
    void setMainWindow(QWidget* window) { mainWindow_ = window; }    // 注入主窗口指针（点击拉前台时使用）

    // 设置显示的层名称
    void setLayerName(const QString& name);   // 更新悬浮窗显示的当前层名称
    // 设置当前操作集名称（切换操作集时更新）
    void setOperationSet(const QString& name);   // 更新悬浮窗显示的操作集名称
    // 设置当前按下的手柄按键列表（会按层过滤触发按键）
    void setHeldButtons(const QSet<ControllerButton>& buttons);   // 更新"按下按键"显示，并同步映射列表高亮
    // 同步映射运行状态（true=运行中，圆点变绿；false=已停止，变灰）
    void setMappingState(bool connected, bool mappingActive);   // 按连接/运行状态更新顶部圆点颜色
    // 切换展开/收起状态（由手柄 ToggleOverlay 动作触发）
    void toggleExpanded();   // 展开/收起映射列表
    // 刷新展开状态下的映射列表（供外部信号连接）
    void refreshMappingsIfExpanded();   // 仅在展开状态下刷新映射列表
    // 更新 MouseToggle 锁存状态提示（active=true 锁存 / false 解除）
    void setMouseToggleState(ControllerButton button, MouseButton mb, bool active);   // 记录/解除鼠标键锁存并刷新提示

protected:   // 【C++ 语法】protected 访问区段：仅本类及派生类可访问；Qt 的事件回调函数正是 protected 虚函数
    // 【C++ 语法】override：明确表示重写基类虚函数，编译器会校验基类确有相同签名，防止写错方法名
    void mousePressEvent(QMouseEvent* event) override;   // 重写：鼠标按下事件回调
    void mouseMoveEvent(QMouseEvent* event) override;    // 重写：鼠标移动事件回调
    void mouseReleaseEvent(QMouseEvent* event) override; // 重写：鼠标释放事件回调
    void paintEvent(QPaintEvent* event) override;        // 重写：绘制事件回调
    void wheelEvent(QWheelEvent* event) override;        // 重写：滚轮事件回调

public:   // 【C++ 语法】再次进入 public 区段：开放供外部调用的公开接口
    // 应用滚轮缩放系数（含启动时恢复上次大小）
    void applyScale(qreal scale);   // 设置缩放系数并应用到各控件
    // 当前缩放系数
    qreal scale() const { return scale_; }   // 【C++ 语法】const 成员函数：承诺不修改成员状态；返回当前缩放系数

private:   // 【C++ 语法】private 访问区段：其后的成员仅本类内部可见
    // 刷新展开状态下的映射列表
    void refreshMappings();   // 重新生成当前层的映射列表富文本
    // 将当前 scale_ 应用到各控件并重排窗口
    void applyCurrentScale();   // 按缩放系数更新各控件尺寸/字体并重排窗口
    // 根据 toggledButtons_ 刷新锁存提示文本与可见性
    void refreshToggleLabel();   // 刷新 MouseToggle 锁存提示的文本与显隐

    // 【C++ 语法】指针成员 + 声明处默认初始化（C++11 起支持在类内声明处直接给默认值；nullptr 是空指针常量）
    SteamInput* steamInput_ = nullptr;   // 手柄映射引擎指针（默认空）
    QWidget* mainWindow_ = nullptr;      // 主窗口指针（默认空）

    QLabel* layerLabel_ = nullptr;       // 当前层名称标签
    QLabel* setLabel_ = nullptr;         // 当前操作集名称
    QLabel* buttonsLabel_ = nullptr;     // "按下按键" 标签
    QLabel* toggleLabel_ = nullptr;      // MouseToggle 锁存提示（橙色警示，无锁存时隐藏）
    QLabel* mappingsLabel_ = nullptr;    // 展开时显示映射列表
    QWidget* statusDot_ = nullptr;       // 顶部映射状态圆点（自绘实心圆，绿=运行/灰=停止）
    QVBoxLayout* layout_ = nullptr;      // 主垂直布局
    bool dragging_ = false;              // 是否正在拖拽
    bool dragMoved_ = false;             // 拖拽过程中是否实际移动过
    QPoint dragPos_;                     // 拖拽时鼠标相对窗口左上角的偏移
    QPoint pressPos_;                    // 按下时的全局坐标（用于判断是否真正点击）
    bool expanded_ = false;              // 是否展开
    QString currentLayerName_;           // 当前层名（用于刷新映射列表）
    QString currentSetName_;             // 当前操作集名（用于刷新映射列表）
    qreal scale_ = 1.0;                  // 滚轮缩放系数（0.5 ~ 2.0）
    QFont baseLayerFont_;                // 层名标签的基础字体（缩放基准）
    QFont baseButtonsFont_;              // "按下按键"标签的基础字体
    QFont baseMappingsFont_;             // 映射列表标签的基础字体
    QString currentDotColor_ = QStringLiteral("#2e7d32");   // 当前圆点颜色（供缩放重建样式）
    QSet<ControllerButton> heldButtons_;                    // 当前按下的手柄按键（映射列表高亮用）
    QHash<ControllerButton, MouseButton> toggledButtons_;   // 当前锁存的鼠标键（手柄键 -> 鼠标键）
};
