// ============================================================
// MainWindow.cpp
// 主窗口：状态栏 + 层切换/编辑 + 全局设置 + 悬浮窗联动
// ------------------------------------------------------------
// 主窗口是用户操作入口，负责：
//   - 展示/编辑操作层（点击按钮打开对应层的编辑对话框）
//   - 调整全局设置（死区/灵敏度/平滑/加速）并实时写回引擎
//   - 保存/重置配置
//   - 创建悬浮信息窗（OverlayWidget）并驱动其显示层名与按下按键
//
// 关键设计：
//   - 层按钮用 layer.id 作 objectName，便于重命名后仍能正确定位
//     （id 唯一固定，name 仅显示）。
//   - 悬浮窗按键展示会过滤掉"层切换"触发按键（SwitchLayer），
//     避免按住方向键切层时误显示为普通按键。
//   - 全局设置滑块值改变即实时写回引擎（onApplySettings），
//     无需额外"应用"按钮。
// ============================================================

// 【C++ 语法】双引号 #include：优先在本文件所在目录查找头文件，预处理时将该头文件内容文本包含进来。
#include "MainWindow.h" // 包含主窗口类头文件（类声明与成员变量）

#include "OverlayWidget.h" // 悬浮信息窗类（独立顶层窗口）
#include "LayerEditDialog.h" // 层编辑对话框类
#include "HelpDialog.h" // 使用说明对话框类
#include "DarkTitleBar.h" // 深色标题栏辅助函数（DWM 设置）

#include "../core/ConfigManager.h" // 配置读写管理类（../ 表示上级目录 core）
#include "../core/InputTypes.h" // 核心数据类型（ControllerButton/MouseButton/GlobalSettings 等）
#include "../core/KeyboardMouseMapper.h" // 键鼠执行器类（负责实际注入）
#include "../core/SteamInput.h" // 映射引擎类
#include "../gamepad/XInputGamepadSource.h" // XInput 手柄轮询源类

#include <QApplication> // Qt：应用级类（quit 退出事件循环等）
#include <QGuiApplication> // Qt：GUI 应用基类（primaryScreen 等）
#include <QScreen> // Qt：屏幕信息（可用区域等）
#include <QGridLayout> // Qt：网格布局
#include <QGroupBox> // Qt：分组框控件
#include <QHBoxLayout> // Qt：水平布局
#include <QLabel> // Qt：文本标签控件
#include <QMessageBox> // Qt：消息/提问对话框
#include <QPushButton> // Qt：按钮控件
#include <QComboBox> // Qt：下拉框控件
#include <QCheckBox> // Qt：复选框控件
#include <QCloseEvent> // Qt：关闭事件类
#include <QInputDialog> // Qt：输入对话框（用于操作集命名）
#include <QLineEdit> // Qt：单行输入框（QInputDialog 参数类型）
#include <QMenu> // Qt：菜单
#include <QProcess> // Qt：外部进程启动（启动屏幕键盘 osk）
#include <QSlider> // Qt：滑块控件
#include <QStatusBar> // Qt：状态栏
#include <QStyle> // Qt：样式对象（unpolish/polish 刷新样式）
#include <QSystemTrayIcon> // Qt：系统托盘图标
#include <QTimer> // Qt：定时器
#include <QVBoxLayout> // Qt：垂直布局

// 【C++ 语法】条件编译：#ifdef 判断宏 Q_OS_WIN（Qt 在 Windows 平台会自动定义）是否定义，为真才编译其后的代码，直到 #endif。
#ifdef Q_OS_WIN // 仅 Windows 平台编译此段（涉及窗口句柄的代码）
#include <windows.h> // Windows API 头文件（GetForegroundWindow 等）
#endif // 条件编译结束

// ============================================================
// 构造：搭建主窗口 UI 与信号连接
// ============================================================
// 【C++ 语法】成员函数类外定义：用「类名::函数名」限定所属类；后面的「: QMainWindow(parent), input_(input), ...」为初始化列表。
MainWindow::MainWindow(SteamInput* input, KeyboardMouseMapper* mapper, XInputGamepadSource* gamepad,
                       QWidget* parent) // 构造函数定义（声明在头文件）：形参为三个核心对象指针与父窗口指针
    : QMainWindow(parent), input_(input), mapper_(mapper), gamepad_(gamepad) { // 初始化列表：在进入函数体前初始化基类与三个成员指针
    setWindowTitle(tr("Gamepad 控制器 - Windows 本机版")); // 设置窗口标题（tr 登记为可翻译文本）

    // ---- 悬浮信息窗 ----
    // 注意：parent 传 nullptr，使其成为独立顶层窗口，
    // 主窗口最小化时悬浮窗不会跟随隐藏。
    // 【C++ 语法】new：在堆上动态创建对象并返回指向它的指针；对象需用 delete 释放。
    overlay_ = new OverlayWidget(nullptr); // 创建悬浮窗（无父窗口 -> 独立顶层窗口）
    overlay_->setSteamInput(input_); // 把映射引擎指针传给悬浮窗（供其查询映射信息）
    // 【C++ 语法】this：指向当前对象（主窗口）自身的指针。
    overlay_->setMainWindow(this); // 把主窗口指针传给悬浮窗（用于联动交互）
    overlay_->setMappingState(true, true);   // 初始映射已启动（圆点绿色）
    // 恢复上次保存的悬浮窗位置（-1 表示未保存过，使用默认位置）
    // 【C++ 语法】const 引用（const T&）：只读绑定到已存在的对象，避免拷贝；这里 gs0 是全局设置的别名。
    const GlobalSettings& gs0 = input_->profile.globalSettings; // 引用全局设置（只读），供恢复位置/缩放使用
    // 【C++ 语法】if 条件语句：表达式为真才执行其后的语句块；&& 为逻辑与（两侧都为真才为真）。
    if (gs0.overlayX >= 0 && gs0.overlayY >= 0) // 若保存过悬浮窗坐标（非 -1）才恢复位置
        overlay_->move(gs0.overlayX, gs0.overlayY); // 移动悬浮窗到保存的位置
    // 恢复上次保存的悬浮窗缩放大小
    overlay_->applyScale(gs0.overlayScale); // 应用悬浮窗缩放比例
    overlay_->show(); // 显示悬浮窗
    // 层变化 -> 悬浮窗更新层名
    // 【Qt】connect(发送者, 信号, 接收者, 槽)：建立信号槽连接；&类::信号 为取成员函数指针（新式语法，编译期检查）。
    connect(input_, &SteamInput::layerChanged, overlay_, &OverlayWidget::setLayerName); // 层变化信号 -> 悬浮窗更新层名
    // 层变化 -> 主界面刷新当前层标签与激活层按钮颜色
    connect(input_, &SteamInput::layerChanged, this, &MainWindow::onLayerChanged); // 层变化信号 -> 主窗口更新标签与按钮
    // 操作集变化 -> 刷新下拉框 + 悬浮窗操作集名
    // 【C++ 语法】lambda 表达式（匿名函数）：[捕获列表](形参){函数体}；[this] 捕获当前对象指针，可在 lambda 内访问成员函数。
    connect(input_, &SteamInput::operationSetChanged, this, [this](const QString& name) { // 操作集变化信号 -> lambda 同时刷新下拉框与悬浮窗
        refreshSetCombo(); // 重建操作集下拉框
        if (overlay_) // 悬浮窗存在时……
            overlay_->setOperationSet(name); // 把新操作集名写入悬浮窗
    }); // lambda 结束
    overlay_->setOperationSet(input_->profile.activeOperationSetName());   // 初始操作集名
    // MouseToggle 锁存状态 -> 悬浮窗橙色提示（含再按一次解除的方法）
    connect(mapper_, &KeyboardMouseMapper::mouseToggleChanged, // 键鼠执行器的 MouseToggle 锁存信号……
            overlay_, &OverlayWidget::setMouseToggleState); // 连接到悬浮窗的锁存状态槽
    // MouseToggle 锁存状态 -> 主窗口内容区边框变橙提示（锁存/解除时刷新）
    connect(mapper_, &KeyboardMouseMapper::mouseToggleChanged, this, // 锁存信号也接到主窗口自身……
            [this](ControllerButton button, MouseButton mb, bool active) { // lambda 参数：手柄键、鼠标键、是否锁存
        if (active) // 锁存开始时……
            // 【Qt】QHash::insert(key, value)：向哈希表插入或更新键值对。
            toggledButtons_.insert(button, mb); // 记录该手柄键对应的鼠标键锁存
        else // 锁存解除时……
            toggledButtons_.remove(button); // 从哈希表移除该键的锁存记录
        // 【C++ 语法】const bool：只读布尔变量；! 为逻辑非运算符。
        const bool hasToggle = !toggledButtons_.isEmpty(); // 是否存在任意锁存按键
        if (hasToggle == toggleActive_) // 锁存状态未变化时直接返回（避免重复刷新样式）
            return; // 提前结束 lambda
        toggleActive_ = hasToggle; // 记录新锁存状态
        // 【C++ 语法】auto：自动类型推导；auto* 表示推导出的类型是指针。
        auto* central = centralWidget(); // 获取主窗口中央控件（内容区）指针
        // 【Qt】setProperty：设置动态属性，可与样式表选择器（如 [toggleActive="true"]）配合控制外观。
        central->setProperty("toggleActive", toggleActive_); // 设置动态属性，驱动样式表边框变色
        // 【Qt】QStyle::unpolish/polish：先解除再重新应用样式，使动态属性改动立即生效。
        central->style()->unpolish(central); // 解除原样式（使样式表重算）
        central->style()->polish(central); // 重新应用样式
        central->update(); // 触发控件重绘
    }); // lambda 结束
    // 配置变更 -> 悬浮窗刷新映射列表
    connect(input_, &SteamInput::profileChanged, overlay_, &OverlayWidget::refreshMappingsIfExpanded); // 配置变更信号 -> 悬浮窗刷新映射列表（仅展开时）
    // 按键映射事件 -> 悬浮窗更新按下按键（过滤层切换触发按键）
    connect(input_, &SteamInput::buttonMapped, this, [this]() { // 按键映射事件 -> 主窗口 lambda 收集当前按住的键
        // 【Qt】QSet<T>：无序集合容器，元素唯一且无重复。
        QSet<ControllerButton> filtered; // 存放过滤后的按键集合
        const auto& held = input_->heldButtons(); // 引用当前按住的全部手柄键（只读）
        // 【C++ 语法】范围 for（range-based for）：for (元素类型 变量 : 容器) 依次遍历容器中每个元素。
        for (ControllerButton btn : held) { // 遍历每个按住的按键
            const auto* mapping = input_->getEffectiveMapping(btn); // 查询该键当前生效的映射（可能为 nullptr）
            // 【C++ 语法】&& 短路求值：mapping 非空才继续判断其后的条件；!= 为不等比较。
            if (mapping && mapping->action.type != MappedAction::Type::SwitchLayer // 若映射存在且动作不是「切换层」……
                && mapping->action.type != MappedAction::Type::ToggleMapping // 且不是「切换映射启停」
                && mapping->action.type != MappedAction::Type::ToggleOnScreenKeyboard // 且不是「切换屏幕键盘」
                && mapping->action.type != MappedAction::Type::ToggleOverlay) { // 且不是「切换悬浮窗」时才保留
                filtered.insert(btn); // 把普通按键加入展示集合
            } // if 结束
        } // for 结束
        overlay_->setHeldButtons(filtered); // 把过滤后的按键集合交给悬浮窗显示
    }); // lambda 结束
    // 初始更新按键状态（过滤掉层切换和系统切换触发按键）
    QSet<ControllerButton> filtered; // 再次声明按键集合（本次为启动时的初始状态）
    const auto& initialHeld = input_->heldButtons(); // 引用初始按住的按键
    for (ControllerButton btn : initialHeld) { // 遍历初始按住的按键
        const auto* mapping = input_->getEffectiveMapping(btn); // 查询初始映射
        if (mapping && mapping->action.type != MappedAction::Type::SwitchLayer // 过滤条件：非切层
            && mapping->action.type != MappedAction::Type::ToggleMapping // 非切映射
            && mapping->action.type != MappedAction::Type::ToggleOnScreenKeyboard // 非切屏幕键盘
            && mapping->action.type != MappedAction::Type::ToggleOverlay) { // 非切悬浮窗
            filtered.insert(btn); // 保留该键
        } // if 结束
    } // for 结束
    overlay_->setHeldButtons(filtered); // 交给悬浮窗显示初始按键状态

    // ---- 前台窗口监控：切换窗口时自动释放所有按键 ----
    // 【Qt】new QTimer(this)：创建定时器，父对象为主窗口（由 Qt 父子机制在父对象销毁时自动销毁）。
    foregroundTimer_ = new QTimer(this); // 创建前台监控定时器
    connect(foregroundTimer_, &QTimer::timeout, this, &MainWindow::onCheckForeground); // 定时器超时信号 -> 检查前台窗口
    foregroundTimer_->start(200); // 每 200 毫秒触发一次

    // ---- 系统托盘图标（始终显示，最小化后通过托盘恢复） ----
    trayIcon_ = new QSystemTrayIcon(windowIcon(), this); // 创建托盘图标（使用主窗口图标，父对象为主窗口）
    trayIcon_->setToolTip(windowTitle()); // 设置鼠标悬停提示为窗口标题
    // 右键菜单
    trayMenu_ = new QMenu(this); // 创建托盘右键菜单
    // 【Qt】QAction：菜单/工具栏的动作项；addAction 创建并把它添加到菜单。
    QAction* showAction = trayMenu_->addAction(tr("显示主界面")); // 添加「显示主界面」菜单项
    connect(showAction, &QAction::triggered, this, [this]() { // 菜单项被触发 -> lambda 恢复并聚焦主窗口
        showNormal(); // 从最小化恢复为普通窗口
        raise(); // 把窗口置顶
        activateWindow(); // 激活窗口（获得焦点）
    }); // lambda 结束
    trayMappingAction_ = trayMenu_->addAction(tr("激活映射")); // 添加「激活映射」菜单项并保存指针
    trayMappingAction_->setCheckable(true); // 设为可勾选（可复选状态）
    trayMappingAction_->setChecked(mapper_->isRunning()); // 初始勾选状态与映射是否运行一致
    connect(trayMappingAction_, &QAction::triggered, this, [this](bool checked) { // 勾选状态变化 -> lambda（参数 checked 为新勾选状态）
        if (checked && !mapper_->isRunning()) { // 勾选且当前未运行 -> 启动映射
            mapper_->start(); // 启动键鼠执行器
            gamepad_->start(); // 启动手柄轮询
            applyStartStopState(true); // 同步启停按钮状态
            // 【Qt】statusBar()->showMessage()：在状态栏临时显示一条消息。
            statusBar()->showMessage(tr("已启动映射")); // 状态栏提示已启动
        } else if (!checked && mapper_->isRunning()) { // 取消勾选且当前运行 -> 停止映射
            // 只停止键鼠注入，保持手柄轮询运行：
            // 否则"切换映射"键无法被读取，停止后无法再次开启映射
            mapper_->stop(); // 仅停止键鼠执行器
            applyStartStopState(false); // 同步按钮为停止状态
            statusBar()->showMessage(tr("已停止：释放所有按键")); // 状态栏提示已停止
        } // if-else 结束
    }); // lambda 结束
    trayMenu_->addSeparator(); // 菜单中添加分隔线
    QAction* quitAction = trayMenu_->addAction(tr("退出")); // 添加「退出」菜单项
    connect(quitAction, &QAction::triggered, this, &MainWindow::exitApplication); // 触发 -> 统一退出入口
    trayIcon_->setContextMenu(trayMenu_); // 把菜单设为托盘图标的右键菜单
    // 双击托盘图标 → 显示主窗口
    // 【Qt】枚举参数：QSystemTrayIcon::ActivationReason 表示托盘被激活的原因（单击/双击等）。
    connect(trayIcon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) { // 托盘图标激活信号 -> lambda（参数为激活原因）
        if (reason == QSystemTrayIcon::DoubleClick) { // 双击托盘图标时……
            showNormal(); // 恢复窗口
            raise(); // 置顶
            activateWindow(); // 激活
        } // if 结束
    }); // lambda 结束
    trayIcon_->show(); // 显示托盘图标

    auto* central = new QWidget(this); // 创建中央控件（主窗口内容根，父对象为主窗口）
    // 【Qt】setObjectName：给控件设置对象名，供样式表选择器（#centralRoot）定位；QStringLiteral 为编译期构造 QString 的宏。
    central->setObjectName(QStringLiteral("centralRoot")); // 设置对象名 centralRoot（样式表按此定位）
    // 【Qt】QVBoxLayout：垂直布局，子控件自上而下排列；构造参数作为其宿主控件。
    auto* root = new QVBoxLayout(central); // 中央控件采用垂直布局

    // ---- 顶部：状态 + 启停 ----
    // 【Qt】QHBoxLayout：水平布局，子控件从左到右排列。
    auto* topBar = new QHBoxLayout; // 顶部水平布局
    startStopButton_ = new QPushButton(this); // 创建启停按钮
    connect(startStopButton_, &QPushButton::clicked, this, &MainWindow::onToggleStartStop); // 点击 -> 切换映射启停
    applyStartStopState(true);   // 初始映射已启动
    // 【Qt】addWidget：把控件加入布局，交由布局管理其位置与大小。
    topBar->addWidget(startStopButton_); // 把启停按钮加入顶部布局

    connectionLabel_ = new QLabel(tr("手柄：未连接"), this); // 创建连接状态标签（初始显示未连接）
    topBar->addWidget(connectionLabel_); // 加入顶部布局

    activeLayerLabel_ = new QLabel(tr("当前层：Common"), this); // 创建当前层标签
    topBar->addWidget(activeLayerLabel_); // 加入顶部布局
    // 【Qt】addStretch：加入伸缩因子，把两侧控件推开、中间留弹性空白。
    topBar->addStretch(1); // 加入弹性空白，把后续控件推到右侧

    auto* helpButton = new QPushButton(tr("使用说明"), this); // 创建「使用说明」按钮
    connect(helpButton, &QPushButton::clicked, this, &MainWindow::onShowHelp); // 点击 -> 打开说明对话框
    topBar->addWidget(helpButton); // 加入顶部布局
    // 【Qt】addLayout：把子布局加入父布局。
    root->addLayout(topBar); // 把顶部布局加入根布局

    // ---- 中部：层按钮 + 设置 ----
    auto* mid = new QHBoxLayout; // 中部水平布局（左侧层编辑区 + 右侧设置区）

    // 层编辑区：操作集管理 + 每个操作层一个按钮（点击打开该层的编辑对话框）
    // 【Qt】QGroupBox：带标题边框的分组框，用于把相关控件分组显示。
    auto* layerGroup = new QGroupBox(tr("操作集与操作层（点击层按钮编辑）"), this); // 创建层编辑分组框
    auto* layerLayout = new QVBoxLayout(layerGroup); // 分组框内使用垂直布局

    // ---- 操作集管理栏：切换 + 添加 + 复制 + 重命名 + 删除 ----
    auto* setBar = new QHBoxLayout; // 操作集管理水平栏
    setBar->addWidget(new QLabel(tr("操作集："), layerGroup)); // 添加「操作集：」标题标签
    setCombo_ = new QComboBox(layerGroup); // 创建操作集下拉框
    setCombo_->setMinimumWidth(110); // 设置下拉框最小宽度
    // 【Qt】setToolTip：设置鼠标悬停时的提示文字。
    setCombo_->setToolTip(tr("切换操作集：其下所有操作层整体切换，各操作集互不影响")); // 设置下拉框悬停提示
    // 【C++ 语法】QOverload<int>::of(成员函数指针)：Qt5 用于消除重载信号歧义的语法，明确选择带 int 参数的 currentIndexChanged 重载。
    connect(setCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), // 连接下拉框选项变化信号（显式选择 int 重载）
            this, &MainWindow::onSetComboChanged); // 连接到操作集切换槽
    // 【Qt】addWidget(widget, stretch)：stretch 为拉伸因子，数值越大占用的多余空间越多。
    setBar->addWidget(setCombo_, 1); // 把下拉框加入管理栏并允许其拉伸占满
    auto* addSetBtn = new QPushButton(tr("添加"), layerGroup); // 「添加」操作集按钮
    connect(addSetBtn, &QPushButton::clicked, this, &MainWindow::onAddSet); // 点击 -> 添加操作集
    setBar->addWidget(addSetBtn); // 加入管理栏
    auto* copySetBtn = new QPushButton(tr("复制"), layerGroup); // 「复制」操作集按钮
    connect(copySetBtn, &QPushButton::clicked, this, &MainWindow::onCopySet); // 点击 -> 复制操作集
    setBar->addWidget(copySetBtn); // 加入管理栏
    auto* renameSetBtn = new QPushButton(tr("重命名"), layerGroup); // 「重命名」操作集按钮
    connect(renameSetBtn, &QPushButton::clicked, this, &MainWindow::onRenameSet); // 点击 -> 重命名操作集
    setBar->addWidget(renameSetBtn); // 加入管理栏
    auto* delSetBtn = new QPushButton(tr("删除"), layerGroup); // 「删除」操作集按钮
    connect(delSetBtn, &QPushButton::clicked, this, &MainWindow::onDeleteSet); // 点击 -> 删除操作集
    setBar->addWidget(delSetBtn); // 加入管理栏
    layerLayout->addLayout(setBar); // 把操作集管理栏加入层编辑布局
    refreshSetCombo(); // 初始填充操作集下拉框

    const auto& layers = input_->profile.layers(); // 引用当前操作集的层列表（只读）
    // 【Qt】QGridLayout：网格布局，按 (行, 列) 放置控件。
    auto* grid = new QGridLayout; // 层按钮使用网格布局
    grid->setSpacing(6); // 网格间距 6 像素
    // 【C++ 语法】const int：只读整型常量。
    const int cols = 2; // 每行显示 2 个层按钮
    // 【C++ 语法】传统 for 循环：for(初始化; 条件; 步进)；++i 为前置自增（先自增再取用）。
    for (int i = 0; i < layers.size(); ++i) { // 为每个操作层创建一个按钮
        // 【C++ 语法】operator[] 下标访问：按索引访问 QVector 元素；QString 为 Qt 的字符串类。
        const QString layerId = layers[i].id; // 取该层固定 id（用于定位）
        const OperationLayer* layer = input_->profile.findLayer(layerId); // 按 id 查找层对象（可能为 nullptr）
        // 【C++ 语法】三目运算符 条件 ? 值A : 值B：条件为真取 A，否则取 B。
        auto* btn = new QPushButton(layer ? layer->name : layerDisplayName(layerId), layerGroup); // 创建按钮，文本为层显示名（找不到层则用 id 的显示名）
        btn->setObjectName(layerId);   // 以 id 为对象名，重命名后仍可定位
        btn->setToolTip(tr("点击编辑该层")); // 设置按钮悬停提示
        // 左键：打开编辑对话框
        // 【C++ 语法】lambda 捕获 [this, layerId]：this 按引用（指针）、layerId 按值捕获（值捕获避免循环变量被复用导致错误）。
        connect(btn, &QPushButton::clicked, this, [this, layerId]() { // 点击层按钮 -> lambda 打开该层编辑
            editLayer(layerId); // 打开编辑对话框
        }); // lambda 结束
        // 【C++ 语法】/ 为整除取商，% 为取余：由索引算出所在行与列。
        grid->addWidget(btn, i / cols, i % cols); // 按 (行=索引/2, 列=索引%2) 放入网格
        // 【Qt】QVector::append：在末尾追加元素。
        layerButtons_.append(btn); // 记录按钮指针，供批量刷新
    } // for 结束
    layerLayout->addLayout(grid); // 把层按钮网格加入层编辑布局
    layerLayout->addStretch(1); // 底部留弹性空白

    auto* editCommonBtn = new QPushButton(tr("编辑公共层…"), layerGroup); // 「编辑公共层」按钮
    editCommonBtn->setObjectName(QStringLiteral("editCommonBtn")); // 设置对象名，供 findChild 定位
    connect(editCommonBtn, &QPushButton::clicked, this, &MainWindow::onEditCommonLayer); // 点击 -> 编辑公共层
    layerLayout->addWidget(editCommonBtn); // 加入层编辑布局
    mid->addWidget(layerGroup, 1); // 把层编辑区加入中部布局（拉伸因子 1）

    // 全局设置区：四个滑块（死区/灵敏度/平滑/加速）
    auto* settingsGroup = new QGroupBox(tr("全局设置"), this); // 创建全局设置分组框
    auto* settingsLayout = new QVBoxLayout(settingsGroup); // 设置区垂直布局

    // 本地工具函数：创建一行"标题 + 滑块 + 数值标签"并保存指针
    // 【C++ 语法】lambda 赋给 auto 变量：形参列表在下一行延续；[settingsLayout] 按值捕获布局指针，供 lambda 内部使用。
    auto addSetting = [settingsLayout](const QString& title, const QString& tip, // 定义局部 lambda：创建一行设置（标题/提示/范围/初值/输出指针）
                                       int min, int max, int value, // 形参：滑块最小/最大值与当前值
                                       QSlider** outSlider, QLabel** outValue) { // 【C++ 语法】二级指针 QSlider**：指向指针的指针，用于在函数内修改外部的指针变量（输出参数）。
        auto* row = new QHBoxLayout; // 每行使用水平布局
        auto* label = new QLabel(title); // 创建标题标签
        label->setMinimumWidth(80); // 标题最小宽度
        label->setToolTip(tip); // 悬停提示
        row->addWidget(label); // 加入该行
        // 【Qt】QSlider(Qt::Horizontal)：构造水平方向的滑块。
        auto* slider = new QSlider(Qt::Horizontal); // 创建水平滑块
        slider->setRange(min, max); // 设置滑块数值范围
        slider->setValue(value); // 设置当前值
        slider->setToolTip(tip); // 悬停提示
        row->addWidget(slider, 1); // 滑块占满剩余宽度（拉伸因子 1）
        // 【Qt】QString::number(int)：静态方法，把数字转换为字符串。
        auto* valueLabel = new QLabel(QString::number(value)); // 创建数值显示标签
        valueLabel->setMinimumWidth(36); // 数值标签最小宽度
        // 【C++ 语法】位或 | 组合枚举标志：把「右对齐」与「垂直居中」两个标志合并为一个值。
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter); // 文字右对齐且垂直居中
        row->addWidget(valueLabel); // 加入该行
        settingsLayout->addLayout(row); // 该行加入设置布局
        *outSlider = slider; // 通过二级指针把滑块指针写回外部变量
        *outValue = valueLabel; // 通过二级指针把数值标签指针写回外部变量
    }; // 【C++ 语法】};：lambda 定义结束（lambda 表达式以分号结束语句）。

    // 从配置初始化滑块（配置文件里是 0~1 浮点，UI 用整数 0~100 等）
    const GlobalSettings& gs = input_->profile.globalSettings; // 引用全局设置（只读）
    QLabel* deadzoneValue = nullptr; // 死区数值标签指针（初始为空）
    QLabel* sensitivityValue = nullptr; // 灵敏度数值标签指针
    QLabel* smoothingValue = nullptr; // 平滑数值标签指针
    QLabel* accelerationValue = nullptr; // 加速数值标签指针
    addSetting(tr("摇杆死区"), // 添加死区滑块：标题为「摇杆死区」
               tr("摇杆推力低于此阈值时视为零输入，避免手柄漂移导致误触发。\n值越大需要推得越深才会有响应。"), // 死区提示文字（\n 为字符串中的换行转义）
               // 【C++ 语法】qRound()：Qt 的四舍五入取整函数；浮点 0~1 转成 UI 整数 0~100。
               0, 50, qRound(gs.deadzone * 100), // 范围 0~50，当前值 = 死区浮点值 × 100 取整
               // 【C++ 语法】& 取地址运算符：取成员变量地址，传给二级指针参数。
               &deadzoneSlider_, &deadzoneValue); // 输出到死区滑块/数值标签成员
    addSetting(tr("视角灵敏度"), // 添加灵敏度滑块
               tr("右摇杆控制鼠标移动的速度倍率。\n值越大相同推力下鼠标移动越快。"), // 灵敏度提示文字
               10, 200, qRound(gs.lookSensitivity * 100), // 范围 10~200，按配置换算初始值
               &sensitivitySlider_, &sensitivityValue); // 输出到灵敏度滑块/数值标签
    addSetting(tr("视角平滑"), // 添加平滑滑块
               tr("对右摇杆输入做时间轴上的平滑处理，减少抖动。\n值越大响应越平滑但延迟越高，设为 0 为无平滑。"), // 平滑提示文字
               0, 100, qRound(gs.lookSmoothing * 100), // 范围 0~100
               &smoothingSlider_, &smoothingValue); // 输出到平滑滑块/数值标签
    addSetting(tr("视角加速"), // 添加加速滑块
               tr("右摇杆推力与鼠标速度的非线性映射指数。\n100 为线性（无加速），值越大轻推越慢、重推越快。"), // 加速提示文字
               100, 300, qRound(gs.lookAcceleration * 100), // 范围 100~300
               &accelerationSlider_, &accelerationValue); // 输出到加速滑块/数值标签

    invertLookXCheck_ = new QCheckBox(tr("右摇杆 X 轴反转"), settingsGroup); // 创建「右摇杆 X 轴反转」复选框
    invertLookXCheck_->setChecked(gs.invertLookX); // 初始勾选状态来自配置
    invertLookXCheck_->setToolTip(tr("反转右摇杆左右方向的鼠标移动")); // 悬停提示
    settingsLayout->addWidget(invertLookXCheck_); // 加入设置布局

    invertLookYCheck_ = new QCheckBox(tr("右摇杆 Y 轴反转"), settingsGroup); // 创建「右摇杆 Y 轴反转」复选框
    invertLookYCheck_->setChecked(gs.invertLookY); // 初始勾选状态来自配置
    invertLookYCheck_->setToolTip(tr("反转右摇杆上下方向的鼠标移动")); // 悬停提示
    settingsLayout->addWidget(invertLookYCheck_); // 加入设置布局

    releaseOnFgCheck_ = new QCheckBox(tr("切换窗口时释放按键"), settingsGroup); // 创建「切换窗口时释放按键」复选框
    releaseOnFgCheck_->setChecked(gs.releaseOnForegroundChange); // 初始勾选状态来自配置
    releaseOnFgCheck_->setToolTip(tr("离开前台窗口时自动释放所有已注入的按键，防止按键卡死")); // 悬停提示
    settingsLayout->addWidget(releaseOnFgCheck_); // 加入设置布局

    confirmOnCloseCheck_ = new QCheckBox(tr("关闭时退出程序"), settingsGroup); // 创建「关闭时退出程序」复选框
    confirmOnCloseCheck_->setChecked(gs.confirmOnClose); // 初始勾选状态来自配置
    confirmOnCloseCheck_->setToolTip(tr("勾选时点击关闭按钮直接退出程序；不勾选时点击关闭按钮最小化到系统托盘（无确认弹窗）")); // 悬停提示
    settingsLayout->addWidget(confirmOnCloseCheck_); // 加入设置布局

    // 数值标签随滑块更新，并实时写回引擎（lambda 捕获引用）
    // 【C++ 语法】lambda 值捕获局部指针变量，使 lambda 内部可用这些标签指针更新数值。
    auto updateValues = [deadzoneValue, sensitivityValue, smoothingValue, accelerationValue, // 定义更新数值标签的 lambda（捕获各标签指针）
                         this]() { // 捕获 this，以便调用成员函数 onApplySettings
        if (deadzoneValue) deadzoneValue->setText(QString::number(deadzoneSlider_->value())); // 指针非空时把死区滑块值写入标签
        if (sensitivityValue) sensitivityValue->setText(QString::number(sensitivitySlider_->value())); // 更新灵敏度数值标签
        if (smoothingValue) smoothingValue->setText(QString::number(smoothingSlider_->value())); // 更新平滑数值标签
        if (accelerationValue) accelerationValue->setText(QString::number(accelerationSlider_->value())); // 更新加速数值标签
        onApplySettings(); // 实时把最新值写回引擎
    }; // lambda 结束
    // 【Qt】valueChanged 信号：滑块值变化时发射；此处第四个参数为 std::function 可调用对象（lambda）。
    connect(deadzoneSlider_, &QSlider::valueChanged, this, updateValues); // 死区滑块变化 -> 更新数值并写回
    connect(sensitivitySlider_, &QSlider::valueChanged, this, updateValues); // 灵敏度滑块变化 -> 更新数值并写回
    connect(smoothingSlider_, &QSlider::valueChanged, this, updateValues); // 平滑滑块变化 -> 更新数值并写回
    connect(accelerationSlider_, &QSlider::valueChanged, this, updateValues); // 加速滑块变化 -> 更新数值并写回
    // 【Qt】toggled 信号：复选框勾选状态切换时发射。
    connect(invertLookXCheck_, &QCheckBox::toggled, this, [this]() { onApplySettings(); }); // X 反转复选框变化 -> 立即写回
    connect(invertLookYCheck_, &QCheckBox::toggled, this, [this]() { onApplySettings(); }); // Y 反转复选框变化 -> 立即写回
    connect(releaseOnFgCheck_, &QCheckBox::toggled, this, [this]() { onApplySettings(); }); // 释放按键复选框变化 -> 立即写回
    connect(confirmOnCloseCheck_, &QCheckBox::toggled, this, [this]() { onApplySettings(); }); // 关闭行为复选框变化 -> 立即写回

    settingsLayout->addStretch(1); // 设置区底部弹性空白
    mid->addWidget(settingsGroup, 0); // 设置区加入中部布局（拉伸因子 0，保持固定宽度）
    root->addLayout(mid, 1); // 中部布局加入根布局并占满高度（拉伸因子 1）

    // ---- 底部：配置操作 ----
    auto* bottomBar = new QHBoxLayout; // 底部水平布局
    auto* saveBtn = new QPushButton(tr("保存配置"), this); // 「保存配置」按钮
    connect(saveBtn, &QPushButton::clicked, this, &MainWindow::onSaveConfig); // 点击 -> 保存配置
    bottomBar->addWidget(saveBtn); // 加入底部布局

    auto* resetBtn = new QPushButton(tr("重置默认"), this); // 「重置默认」按钮
    connect(resetBtn, &QPushButton::clicked, this, &MainWindow::onResetConfig); // 点击 -> 重置默认配置
    bottomBar->addWidget(resetBtn); // 加入底部布局
    bottomBar->addStretch(1); // 底部弹性空白
    root->addLayout(bottomBar); // 底部布局加入根布局

    // 【Qt】setCentralWidget：把指定控件设为主窗口中央部件（占满中间区域）。
    setCentralWidget(central); // 把中央控件设为主窗口内容
    // ---- 深色主题（仅作用于主窗口子树，不影响悬浮窗/编辑对话框） ----
    // 【Qt】setStyleSheet：设置 Qt 样式表（类似 CSS）定义控件外观；R"(...)" 为 C++11 原始字符串，内部换行与引号无需转义。
    // 注意：下方 R"(...)" 内部全部是样式表字符串内容（CSS 语法），为保证代码逻辑与样式字符串完全不变，未在字符串内部添加任何注释。
    setStyleSheet(R"(
        #centralRoot {
            background-color: #2b2d31;
            border: 2px solid transparent;
        }
        /* MouseToggle 锁存时主窗口内容区边框变橙色（提示有按键被锁存） */
        #centralRoot[toggleActive="true"] {
            border: 2px solid #ffb54d;
        }
        QWidget {
            color: #d5d9df;
            font-family: "Microsoft YaHei";
            font-size: 12px;
        }
        QGroupBox {
            background-color: #33363b;
            border: 1px solid #40434a;
            border-radius: 8px;
            margin-top: 12px;
            color: #dfe3e8;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 10px;
            padding: 2px 6px;
            color: #7fc9c4;
            background-color: transparent;
            font-weight: bold;
            font-family: "Microsoft YaHei";   /* 与整体字体统一为微软雅黑 */
        }
        QPushButton {
            background-color: #3d4147;
            color: #e8eaee;
            border: 1px solid #4a4e55;
            border-radius: 6px;
            padding: 5px 14px;
        }
        QPushButton:hover {
            background-color: #474b52;
            border-color: #7fc9c4;
        }
        QPushButton:pressed {
            background-color: #2f3237;
        }
        QPushButton:focus {
            outline: none;
        }
        /* 操作集下拉框（与层编辑对话框同款深色样式） */
        QComboBox {
            background-color: #33363b;
            color: #e8eaee;
            border: 1px solid #4a4e55;
            border-radius: 5px;
            padding: 3px 8px;
        }
        QComboBox:hover {
            border-color: #7fc9c4;
        }
        QComboBox::drop-down {
            border: none;
            background: transparent;
            width: 22px;
        }
        QComboBox::down-arrow {
            image: url(:/icons/down-arrow.png);
            width: 10px;
            height: 6px;
        }
        QComboBox QAbstractItemView {
            background-color: #33363b;
            color: #e8eaee;
            border: 1px solid #4a4e55;
            selection-background-color: #22958c;
            selection-color: #ffffff;
            outline: none;
        }
        QSlider::groove:horizontal {
            height: 4px;
            background: #4a4e55;
            border-radius: 2px;
        }
        QSlider::sub-page:horizontal {
            background: #7fc9c4;
            border-radius: 2px;
        }
        QSlider::add-page:horizontal {
            background: #4a4e55;
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            width: 14px;
            height: 14px;
            margin: -5px 0;
            border-radius: 7px;
            background: #7fc9c4;
            border: 1px solid #a0e8e2;
        }
        QSlider::handle:horizontal:hover {
            background: #9adfda;
        }
        QCheckBox {
            spacing: 6px;
            color: #d5d9df;
        }
        QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border-radius: 4px;
            border: 1px solid #555a62;
            background: #2b2d31;
        }
        QCheckBox::indicator:hover {
            border-color: #7fc9c4;
        }
        QCheckBox::indicator:checked {
            background: #7fc9c4;
            border-color: #7fc9c4;
        }
        QStatusBar {
            background: #26282c;
            color: #aeb4bd;
        }
        QStatusBar::item {
            border: none;
        }
    )"); // 原始字符串结束，并把样式表应用到主窗口
    // 【Qt】tr("...%1").arg(值)：QString 格式化，%1 占位符被 arg 参数替换。
    statusBar()->showMessage(tr("配置文件：%1").arg(ConfigManager::configFilePath())); // 状态栏显示配置文件路径

    // ---- 初始状态同步 ----
    onLayerChanged(input_->activeLayerName()); // 用当前激活层名初始化层标签
    refreshLayerButtons(); // 初始化层按钮状态
    onConnectionChanged(gamepad_->isConnected()); // 用当前手柄连接状态初始化标签

    // 连接手柄连接状态变化信号（注意：需在 gamepad 指针有效时连接）
    connect(gamepad_, &XInputGamepadSource::connectedChanged, // 手柄源连接状态变化信号……
            this, &MainWindow::onConnectionChanged); // 连接到主窗口的连接状态槽

    // ---- 手柄动作触发的系统操作 ----
    // 切换映射启停
    connect(input_, &SteamInput::toggleMappingRequested, this, [this]() { // 手柄触发「切换映射」请求 -> lambda 调用启停
        onToggleStartStop(); // 执行启停切换
    }); // lambda 结束
    // 切换 Windows 屏幕键盘
    connect(input_, &SteamInput::toggleOnScreenKeyboardRequested, this, []() { // 手柄触发「切换屏幕键盘」请求 -> lambda（无捕获）
        // 【Qt】QProcess::startDetached：以独立进程方式启动外部程序（此处为 Windows 屏幕键盘 osk.exe）。
        QProcess::startDetached(QStringLiteral("osk")); // 启动系统屏幕键盘
    }); // lambda 结束
    // 切换悬浮窗展开/收起
    connect(input_, &SteamInput::toggleOverlayRequested, this, [this]() { // 手柄触发「切换悬浮窗」请求 -> lambda
        if (overlay_) // 悬浮窗存在时……
            overlay_->toggleExpanded(); // 展开/收起悬浮窗
    }); // lambda 结束

    // ---- 标题栏深色化（DWM 沉浸式深色标题栏，与深色主题统一） ----
#ifdef Q_OS_WIN // 仅 Windows 平台编译
    enableDarkTitleBar(this); // 调用深色标题栏辅助函数（DWM 设置）
#endif // 条件编译结束

    // 恢复上次保存的主窗口位置（-1 表示未保存过，使用默认位置）
    if (gs0.mainWindowX >= 0 && gs0.mainWindowY >= 0) // 若保存过主窗口坐标……
        move(gs0.mainWindowX, gs0.mainWindowY); // 移动到保存的位置
} // 构造函数结束

// ---------------------------------------------------------------

// ============================================================
// onLayerChanged：当前层变化 -> 更新顶部标签与悬浮窗
// ============================================================
// 层名取 layer.name（显示名），并刷新所有层按钮的勾选/文本状态。
// 【C++ 语法】成员函数定义（类外）：类名::函数名 指明所属类；参数为常量字符串引用（只读，不拷贝）。
void MainWindow::onLayerChanged(const QString& activeLayerName) { // 槽函数定义：当前层变化时更新标签与悬浮窗
    if (activeLayerLabel_) { // 标签存在时……
        const OperationLayer* layer = input_->profile.findLayer(activeLayerName); // 按层名（id）查找层对象
        const QString displayName = layer ? layer->name : activeLayerName; // 显示名：层存在用其 name，否则用原始名称
        activeLayerLabel_->setText(tr("当前层：%1").arg(displayName)); // 更新当前层标签文字
        if (overlay_) // 悬浮窗存在时……
            overlay_->setLayerName(displayName); // 同步悬浮窗层名
    } // if 结束
    refreshLayerButtons(); // 刷新层按钮状态
} // 函数结束

// ============================================================
// onConnectionChanged：手柄连接状态变化 -> 更新状态标签
// ============================================================
void MainWindow::onConnectionChanged(bool connected) { // 槽函数：手柄连接状态变化
    connectionLabel_->setText(connected ? tr("手柄：已连接") : tr("手柄：未连接")); // 根据连接状态设置标签文字（三目运算）
    connectionLabel_->setStyleSheet(connected ? QStringLiteral("color: #66bb6a;") // 已连接 -> 标签文字绿色
                                              : QStringLiteral("color: #ef5350;")); // 未连接 -> 标签文字红色
    // 手柄连接状态变化时，同步启停按钮与悬浮窗圆点的颜色（未连接 -> 红）
    if (mapper_) // 执行器存在时……
        applyStartStopState(mapper_->isRunning()); // 按映射是否运行同步按钮状态
} // 函数结束

// ============================================================
// refreshLayerButtons：刷新所有层按钮的勾选状态与文本
// ============================================================
// 通过 objectName（= layer.id）查找对应层：勾选状态表示当前是否激活，
// 文本始终显示最新层名（支持改名后即时刷新）。
void MainWindow::refreshLayerButtons() { // 刷新层按钮：勾选状态 + 文本
    for (QPushButton* btn : layerButtons_) { // 范围 for：遍历所有层按钮指针
        const QString layerId = btn->objectName(); // 取按钮对象名（即层 id）
        const bool active = input_->isLayerActive(layerId); // 查询该层当前是否激活
        // 【Qt】setStyleSheet：为按钮单独设置样式表；激活时用青绿色高亮背景。
        btn->setStyleSheet(active ? QStringLiteral("background: #22958c; color: white; font-weight: bold; font-family: \"Microsoft YaHei\"; border-radius: 6px;") // 激活 -> 设置高亮样式
                                  : QString()); // 未激活 -> 清除样式（恢复默认）
        // 更新按钮文本为当前层名称
        // 【C++ 语法】if 内声明变量：在条件中定义并初始化指针，若指针非空则进入分支（变量作用域限于该 if 语句）。
        if (const OperationLayer* layer = input_->profile.findLayer(layerId)) { // 若查到该层……
            btn->setText(layer->name); // 按钮文本更新为层显示名
        } // if 结束
    } // for 结束
    // 更新公共层编辑按钮文本
    // 【Qt】findChild<T>(name)：在子控件树中按对象名查找指定类型的控件，找不到返回 nullptr。
    if (auto* editCommonBtn = findChild<QPushButton*>(QStringLiteral("editCommonBtn"))) { // 按对象名查找公共层编辑按钮
        const OperationLayer* commonLayer = input_->profile.findLayer(QStringLiteral("Common")); // 查找公共层对象
        editCommonBtn->setText(tr("编辑公共层：%1…").arg(commonLayer ? commonLayer->name : tr("公共层"))); // 更新按钮文字为公共层显示名
    } // if 结束
} // 函数结束

// ============================================================
// refreshSetCombo：重建操作集下拉框
// ============================================================
// 按操作集显示名填充下拉框（itemData 存 id），并选中当前激活集。
// refreshingSets_ 置位期间不响应 currentIndexChanged（防止程序化
// 刷新误触发 onSetComboChanged 切换操作集）。
void MainWindow::refreshSetCombo() { // 重建操作集下拉框
    if (!setCombo_) return; // 下拉框未创建时直接返回
    refreshingSets_ = true; // 置位程序化刷新标志（抑制切换信号）
    // 【Qt】QComboBox::clear()：清空下拉框所有条目。
    setCombo_->clear(); // 清空下拉框
    for (const OperationSet& set : input_->profile.operationSets) // 范围 for：遍历所有操作集（常量引用避免拷贝）
        // 【Qt】addItem(显示文本, 用户数据)：向下拉框添加条目并附带数据（此处存操作集 id）。
        setCombo_->addItem(set.name, set.id); // 添加条目：显示名 + id（存 itemData）
    // 【Qt】findData：按数据值查找对应条目索引，未找到返回 -1。
    const int idx = setCombo_->findData(input_->profile.activeOperationSetId); // 查找当前激活操作集对应的下拉框索引
    if (idx >= 0) // 索引有效时……
        setCombo_->setCurrentIndex(idx); // 选中激活操作集条目
    refreshingSets_ = false; // 清除刷新标志
} // 函数结束

// ============================================================
// onSetComboChanged：下拉框选择变化 -> 切换操作集
// ============================================================
// 调用引擎的 switchOperationSet：清空层栈 + 更新激活集，
// 并通过 operationSetChanged / layerChanged 信号联动悬浮窗与层按钮。
// 【C++ 语法】形参不命名（int）：该参数在函数体中不使用，省略名字以避免未使用警告。
void MainWindow::onSetComboChanged(int) { // 槽函数：下拉框选择变化（用 currentData 取 id，形参 index 用不到）
    if (refreshingSets_ || !setCombo_) return; // 程序化刷新中或下拉框为空时直接返回
    // 【Qt】currentData()：取当前选中条目的用户数据（QVariant），.toString() 转为字符串。
    const QString id = setCombo_->currentData().toString(); // 取当前选中操作集的 id
    if (id.isEmpty() || id == input_->profile.activeOperationSetId) // id 为空或就是当前激活集则跳过
        return; // 直接返回
    if (input_->switchOperationSet(id)) { // 调用引擎切换操作集（成功返回 true）
        refreshLayerButtons(); // 刷新层按钮
        statusBar()->showMessage( // 状态栏显示切换结果……
            tr("已切换到操作集：%1").arg(input_->profile.activeOperationSetName())); // 显示新操作集名称
    } // if 结束
} // 函数结束

// ============================================================
// onAddSet：添加新操作集并切换到它
// ============================================================
// 新操作集为全新空配置（空公共层 + 10 个空操作层），默认名"操作集 N"。
// 先清空层栈（防止 QVector 扩容使已激活层指针失效），再追加并切换。
void MainWindow::onAddSet() { // 添加新操作集
    const QString newId = input_->profile.uniqueOperationSetId(); // 生成唯一 id
    int n = input_->profile.operationSets.size() + 1; // 默认编号 = 现有操作集数量 + 1
    QString name = tr("操作集 %1").arg(n); // 生成默认名称「操作集 N」
    // 避免与已有操作集重名
    // 【C++ 语法】while 循环：条件为真反复执行循环体；这里用 break 手动跳出（直到找到不重复的名字）。
    while (true) { // 无限循环：直到名字不重复才退出
        bool dup = false; // 标记名字是否重复
        for (const OperationSet& s : input_->profile.operationSets) // 遍历所有已有操作集
            if (s.name == name) { dup = true; break; } // 若名字重复则置位并跳出内层 for
        if (!dup) break; // 无重名则跳出 while
        name = tr("操作集 %1").arg(++n); // 否则编号 +1 再试（++n 先自增后使用）
    } // while 结束
    // 【C++ 语法】静态成员函数调用：类名::函数名 直接调用，无需对象实例。
    OperationSet set = OperationSet::createEmpty(newId, name); // 创建全新空操作集（空公共层 + 10 空层）
    input_->deactivateAllLayers(); // 先清空层栈（防止 QVector 扩容使已激活层指针失效）
    input_->profile.operationSets.append(set); // 把新操作集追加到列表
    input_->profile.activeOperationSetId = newId; // 设为当前激活操作集
    input_->notifyOperationSetChanged(); // 通知操作集变化（触发界面刷新）
    refreshSetCombo(); // 重建下拉框
    refreshLayerButtons(); // 刷新层按钮
    statusBar()->showMessage(tr("已添加操作集：%1").arg(name)); // 状态栏提示
} // 函数结束

// ============================================================
// onCopySet：复制当前操作集（可直接改名）
// ============================================================
// 弹出命名对话框（默认"xxx - 副本"），确定后生成完整副本并切换到它。
// 副本保留层 id（Layer1..Layer10），因此层内 SwitchLayer 引用依然有效。
void MainWindow::onCopySet() { // 复制当前操作集
    const OperationSet* src = input_->profile.activeSet(); // 取当前激活操作集指针
    if (!src) return; // 为空则返回
    const QString defaultName = tr("%1 - 副本").arg(src->name); // 默认副本名「xxx - 副本」
    bool ok = false; // 记录用户是否点击确定
    // 【Qt】QInputDialog::getText：静态方法弹出文本输入对话框，返回输入内容；&ok 接收确认结果。
    const QString newName = QInputDialog::getText( // 弹出命名对话框
        this, tr("复制操作集"), tr("新操作集名称："), QLineEdit::Normal, // 参数：父窗口、标题、提示、输入模式（Normal 普通文本）
        defaultName, &ok).trimmed(); // 默认文本 + 确定标志；trimmed() 去除首尾空白
    if (!ok || newName.isEmpty()) return; // 取消或名为空则返回
    const QString newId = input_->profile.uniqueOperationSetId(); // 生成新唯一 id
    // 【C++ 语法】解引用 *：*src 取得指针指向的对象；此处为拷贝构造（复制整个操作集）。
    OperationSet copy = *src; // 深拷贝当前操作集
    copy.id = newId; // 替换为新 id
    copy.name = newName; // 替换为新名字
    input_->deactivateAllLayers(); // 先清空层栈再追加（防悬垂指针）
    input_->profile.operationSets.append(copy); // 追加副本
    input_->profile.activeOperationSetId = newId; // 切换到副本
    input_->notifyOperationSetChanged(); // 通知操作集变化
    refreshSetCombo(); // 重建下拉框
    refreshLayerButtons(); // 刷新层按钮
    statusBar()->showMessage(tr("已复制操作集：%1").arg(newName)); // 状态栏提示
} // 函数结束

// ============================================================
// onRenameSet：重命名当前操作集
// ============================================================
// 仅修改显示名（id 不变，运行时定位不受影响）。
void MainWindow::onRenameSet() { // 重命名当前操作集
    OperationSet* set = input_->profile.activeSet(); // 取当前激活操作集指针（非常量，因要改名）
    if (!set) return; // 为空则返回
    bool ok = false; // 记录确认结果
    const QString newName = QInputDialog::getText( // 弹出重命名对话框
        this, tr("重命名操作集"), tr("操作集名称："), QLineEdit::Normal, // 对话框参数
        set->name, &ok).trimmed(); // 预填当前名字
    if (!ok || newName.isEmpty() || newName == set->name) return; // 取消/为空/名字未变则返回
    set->name = newName; // 直接修改操作集的显示名（id 不变）
    refreshSetCombo(); // 重建下拉框显示新名字
    input_->notifyOperationSetChanged(); // 通知操作集变化
    statusBar()->showMessage(tr("操作集已重命名为：%1").arg(newName)); // 状态栏提示
} // 函数结束

// ============================================================
// onDeleteSet：删除当前操作集
// ============================================================
// 至少保留一个操作集；删除后切换到剩余的第一个。
// 先清空层栈再删除（防止删除后已激活层指针悬垂）。
void MainWindow::onDeleteSet() { // 删除当前操作集
    if (input_->profile.operationSets.size() <= 1) { // 至少保留一个操作集
        // 【Qt】QMessageBox::information：静态方法弹出信息提示框。
        QMessageBox::information(this, tr("删除操作集"), tr("至少需要保留一个操作集。")); // 提示用户不能全删
        return; // 返回
    } // if 结束
    const OperationSet* set = input_->profile.activeSet(); // 取当前激活操作集
    if (!set) return; // 为空则返回
    const QString name = set->name; // 记录要删除的操作集名（用于提示）
    // 【Qt】QMessageBox::question：弹出是/否确认对话框，返回用户选择。
    const auto ret = QMessageBox::question( // 弹出删除确认对话框
        this, tr("删除操作集"), tr("确定删除操作集「%1」吗？其下所有层映射将丢失。").arg(name), // 确认文案：提示删除后映射丢失
        QMessageBox::Yes | QMessageBox::No); // 提供「是 / 否」两个按钮
    if (ret != QMessageBox::Yes) // 未点「是」则不删除
        return; // 返回
    const QString removingId = input_->profile.activeOperationSetId; // 记录要删除的操作集 id
    input_->deactivateAllLayers(); // 先清空层栈（防删除后悬垂指针）
    for (int i = 0; i < input_->profile.operationSets.size(); ++i) { // 遍历操作集找要删除的那一个
        if (input_->profile.operationSets[i].id == removingId) { // 找到匹配 id 的操作集……
            // 【Qt】QVector::removeAt：移除指定索引处的元素。
            input_->profile.operationSets.removeAt(i); // 从列表移除该操作集
            break; // 跳出循环
        } // if 结束
    } // for 结束
    if (input_->profile.operationSets.isEmpty()) // 若删除后为空（防御性检查）……
        input_->profile.operationSets.append(OperationSet::createEmpty( // 补一个默认操作集
            QStringLiteral("Set1"), QStringLiteral("默认操作集"))); // 默认 id=Set1，名字=默认操作集
    // 【Qt】QVector::first()：返回容器首元素。
    input_->profile.activeOperationSetId = input_->profile.operationSets.first().id; // 切换到剩余的第一个操作集
    input_->notifyOperationSetChanged(); // 通知操作集变化
    refreshSetCombo(); // 重建下拉框
    refreshLayerButtons(); // 刷新层按钮
    statusBar()->showMessage(tr("已删除操作集：%1").arg(name)); // 状态栏提示
} // 函数结束

// ============================================================
// onToggleStartStop：启动/停止映射开关
// ============================================================
// 停止时只停映射器（look 线程 + 断开注入信号），并释放所有按键；
// 保持手柄轮询运行，这样"切换映射"键仍能被读取，可再次开启映射。
// 启动时拉起注入（XInput 轮询始终运行，start 幂等）。
void MainWindow::onToggleStartStop() { // 启动/停止映射开关
    if (mapper_->isRunning()) { // 当前映射运行中 -> 停止
        mapper_->stop(); // 停止键鼠执行器
        applyStartStopState(false); // 同步按钮为停止状态
        statusBar()->showMessage(tr("已停止：释放所有按键")); // 状态栏提示
    } else { // 当前未运行 -> 启动
        mapper_->start(); // 启动键鼠执行器
        gamepad_->start(); // 确保手柄轮询运行
        applyStartStopState(true); // 同步按钮为运行状态
        statusBar()->showMessage(tr("已启动映射")); // 状态栏提示
    } // if-else 结束
} // 函数结束

// ============================================================
// applyStartStopState：同步启停按钮文字与状态色
// ============================================================
// 手柄未连接：红底白字；映射运行中（已连接）：绿底白字「停止映射」；
// 映射已停止（已连接）：灰底深字「启动映射」。悬浮窗圆点同步同色。
void MainWindow::applyStartStopState(bool mappingActive) { // 同步启停按钮文字与颜色
    if (!startStopButton_) return; // 按钮未创建则返回
    const bool connected = gamepad_ && gamepad_->isConnected(); // 判断手柄是否已连接（注意 && 短路保护空指针）
    if (overlay_) // 悬浮窗存在时……
        overlay_->setMappingState(connected, mappingActive);   // 悬浮窗圆点同步状态
    // 【C++ 语法】一行声明多个同类型变量：bg（背景色）、fg（前景色）。
    QString bg, fg; // 背景色与前景色变量
    if (!connected) { // 未连接 -> 红色
        bg = QStringLiteral("#c62828");   // 红：手柄未连接
        fg = QStringLiteral("#ffffff"); // 前景白色
    } else if (mappingActive) { // 已连接且映射运行 -> 绿色
        bg = QStringLiteral("#2e7d32");   // 绿：映射运行中
        fg = QStringLiteral("#ffffff"); // 前景白色
    } else { // 已连接但映射停止 -> 灰色
        bg = QStringLiteral("#9e9e9e");   // 灰：映射已停止
        fg = QStringLiteral("#212121"); // 前景深色
    } // if-else 结束
    startStopButton_->setText(mappingActive ? tr("停止映射") : tr("启动映射")); // 按钮文字随状态切换
    startStopButton_->setStyleSheet( // 设置按钮样式表……
        QStringLiteral("QPushButton { background-color: %1; color: %2;" // 样式模板：背景色 %1、前景色 %2
                       " font-weight: bold; font-family: \"Microsoft YaHei\";" // 粗体 + 微软雅黑字体
                       " border-radius: 6px; padding: 4px 12px; }") // 圆角与内边距
            // 【Qt】QString::arg(值1, 值2)：按顺序替换字符串中的 %1、%2 占位符。
            .arg(bg, fg)); // 把背景色/前景色填入模板
} // 函数结束

// ============================================================
// onSaveConfig：保存当前配置到磁盘
// ============================================================
void MainWindow::onSaveConfig() { // 保存配置
    const bool ok = ConfigManager::save(input_->profile); // 调用配置管理器保存当前 profile
    statusBar()->showMessage(ok ? tr("配置已保存到 %1").arg(ConfigManager::configFilePath()) // 保存成功 -> 提示路径
                                : tr("保存配置失败")); // 保存失败 -> 提示失败
} // 函数结束

// ============================================================
// onResetConfig：重置为默认配置
// ============================================================
// 二次确认后：保存默认配置 -> 重新加载到引擎 -> 同步滑块到默认值。
void MainWindow::onResetConfig() { // 重置默认配置
    const auto ret = QMessageBox::question( // 弹出重置确认对话框
        this, tr("重置配置"), tr("确定要恢复默认配置吗？当前修改将丢失。"), // 确认文案
        QMessageBox::Yes | QMessageBox::No); // 提供是/否按钮
    if (ret != QMessageBox::Yes) // 未确认则返回
        return; // 返回
    const ControllerProfile def = ControllerProfile::createDefault(); // 创建默认配置
    ConfigManager::save(def); // 把默认配置写入磁盘
    input_->loadProfile(def); // 重新加载到引擎
    // 重置操作集下拉框与悬浮窗操作集名（默认只有"默认操作集"）
    refreshSetCombo(); // 重建下拉框
    if (overlay_) // 悬浮窗存在时……
        overlay_->setOperationSet(input_->profile.activeOperationSetName()); // 同步悬浮窗操作集名
    // 同步滑块到新默认值（滑块变更会自动写回 profile）
    deadzoneSlider_->setValue(qRound(def.globalSettings.deadzone * 100)); // 死区滑块复位
    sensitivitySlider_->setValue(qRound(def.globalSettings.lookSensitivity * 100)); // 灵敏度滑块复位
    smoothingSlider_->setValue(qRound(def.globalSettings.lookSmoothing * 100)); // 平滑滑块复位
    accelerationSlider_->setValue(qRound(def.globalSettings.lookAcceleration * 100)); // 加速滑块复位
    invertLookXCheck_->setChecked(def.globalSettings.invertLookX); // X 反转复选框复位
    invertLookYCheck_->setChecked(def.globalSettings.invertLookY); // Y 反转复选框复位
    releaseOnFgCheck_->setChecked(def.globalSettings.releaseOnForegroundChange); // 释放按键复选框复位
    confirmOnCloseCheck_->setChecked(def.globalSettings.confirmOnClose); // 关闭行为复选框复位
    // 重置悬浮窗到默认位置
    if (overlay_) { // 悬浮窗存在时……
        // 【Qt】QGuiApplication::primaryScreen()：获取主屏幕；availableGeometry() 返回去掉任务栏后的可用区域（QRect）。
        const QRect screenRect = QGuiApplication::primaryScreen()->availableGeometry(); // 获取主屏幕可用区域
        overlay_->adjustSize(); // 让悬浮窗按内容调整大小
        // 【Qt】QRect::topRight()：右上角坐标；QPoint 为二维整型坐标点，运算符 - 已被重载用于坐标运算。
        overlay_->move(screenRect.topRight() - QPoint(overlay_->width() + 10, 10)); // 把悬浮窗移到屏幕右上角（留 10px 边距）
    } // if 结束
    statusBar()->showMessage(tr("已重置为默认配置")); // 状态栏提示
} // 函数结束

// ============================================================
// onEditCommonLayer：编辑公共层入口
// ============================================================
void MainWindow::onEditCommonLayer() { // 编辑公共层入口
    editLayer(QStringLiteral("Common")); // 公共层的 id 固定为 "Common"
} // 函数结束

// ============================================================
// editLayer：打开指定层的编辑对话框
// ============================================================
// 按层名（实为 id）定位层对象，打开模态编辑对话框，
// 关闭后刷新按钮文本（层名可能被修改）。
void MainWindow::editLayer(const QString& layerName) { // 打开指定层的编辑对话框
    OperationLayer* layer = input_->profile.findLayer(layerName); // 按层名（id）查找层对象
    if (!layer) // 层不存在则……
        return; // 返回
    // 【C++ 语法】栈上对象：直接声明（非 new），离开作用域自动析构；& 取地址以传指针参数。
    LayerEditDialog dlg(&input_->profile, layer, gamepad_, this); // 创建层编辑对话框（栈对象，传配置/层/手柄源/父窗口）
    // 【Qt】exec()：以模态方式运行对话框，阻塞直至用户关闭，返回关闭结果。
    dlg.exec(); // 模态运行对话框（等待用户编辑完成）
    refreshLayerButtons(); // 关闭后刷新层按钮（层名可能已改）
    // 【Qt】emit：发射信号关键字（可省略，仅为可读性），通知配置已变更。
    emit input_->profileChanged(); // 手动发射配置变更信号（通知悬浮窗等刷新）
    // 刷新悬浮窗层名（层名可能被修改）
    onLayerChanged(input_->activeLayerName()); // 重新同步当前层标签与悬浮窗
} // 函数结束

// ============================================================
// onApplySettings：把滑块值写回引擎的全局设置
// ============================================================
// UI 用整数（如 0~100），引擎内部用 0~1 浮点，这里做换算。
// cursorSpeed 固定为 1.0（本机版未开放光标速度调节）。
void MainWindow::onApplySettings() { // 把滑块值写回引擎全局设置
    GlobalSettings s; // 新建一个全局设置对象
    // 【C++ 语法】static_cast<float>(表达式)：C++ 显式类型转换（编译期检查），此处把 int 转为 float。
    s.deadzone = static_cast<float>(deadzoneSlider_->value()) / 100.0f; // 死区：整数 /100 转回 0~1 浮点
    s.lookSensitivity = static_cast<float>(sensitivitySlider_->value()) / 100.0f; // 灵敏度换算
    s.cursorSpeed = 1.0f; // 光标速度固定 1.0（本机版未开放调节）
    s.lookSmoothing = static_cast<float>(smoothingSlider_->value()) / 100.0f; // 平滑换算
    s.lookAcceleration = static_cast<float>(accelerationSlider_->value()) / 100.0f; // 加速换算
    // 【Qt】isChecked()：返回复选框是否勾选（布尔值）。
    s.invertLookX = invertLookXCheck_->isChecked(); // X 反转标志
    s.invertLookY = invertLookYCheck_->isChecked(); // Y 反转标志
    s.releaseOnForegroundChange = releaseOnFgCheck_->isChecked(); // 前台切换释放标志
    s.confirmOnClose = confirmOnCloseCheck_->isChecked(); // 关闭行为标志
    input_->setGlobalSettings(s); // 把新设置写回引擎
} // 函数结束

// ============================================================
// onShowHelp：打开使用说明对话框
// ============================================================
void MainWindow::onShowHelp() { // 打开使用说明对话框
    HelpDialog dlg(this); // 创建帮助对话框（栈对象，父窗口为主窗口）
    dlg.exec(); // 模态运行
} // 函数结束

// ============================================================
// onCheckForeground：检测前台窗口变化，切换时释放所有按键
// ============================================================
// 每 200ms 检查一次当前前台窗口句柄。若与上次记录不同（用户切换了窗口），
// 释放所有已注入的按键/鼠标键，避免按键卡死在目标游戏中。
void MainWindow::onCheckForeground() { // 检查前台窗口变化
#ifdef Q_OS_WIN // 仅 Windows 平台编译
    // 【C++ 语法】Windows API 调用：HWND 为窗口句柄类型；GetForegroundWindow() 返回当前前台窗口句柄。
    HWND hwnd = GetForegroundWindow(); // 获取当前前台窗口句柄
    if (lastForegroundHwnd_ != nullptr && hwnd != lastForegroundHwnd_ // 若记录过旧句柄且当前句柄不同（发生了窗口切换）……
        && input_->profile.globalSettings.releaseOnForegroundChange) { // 且开启了「切换窗口释放按键」设置
        mapper_->releaseAllInputs(); // 释放所有已注入的按键/鼠标键，防止卡死
    } // if 结束
    lastForegroundHwnd_ = hwnd; // 记录本次前台窗口句柄供下次比较
#else // 非 Windows 平台……
    // 【Qt】Q_UNUSED(变量)：标记参数未使用，避免编译器警告（非 Windows 编译时空函数体）。
    Q_UNUSED(this); // 标记 this 未使用以消除警告
#endif // 条件编译结束
} // 函数结束

// ============================================================
// closeEvent：关闭窗口（无二次确认弹窗）
// ============================================================
// 按设置「关闭时退出程序」决定行为：
//   - 勾选（默认）：直接退出，先关闭悬浮窗、停止映射与手柄轮询；
//   - 未勾选：最小化到系统托盘（窗口隐藏，保留托盘图标）。
// 【Qt】覆写事件处理函数：窗口收到关闭请求时 Qt 会调用它，可在其中决定接受或忽略该事件。
void MainWindow::closeEvent(QCloseEvent* event) { // 覆写 closeEvent：自定义关闭行为
    if (!input_->profile.globalSettings.confirmOnClose) { // 未勾选「关闭时退出程序」-> 最小化到托盘
        hide(); // 隐藏主窗口
        event->ignore(); // 忽略关闭事件（窗口不真正关闭）
        return; // 返回
    } // if 结束
    // 悬浮窗位置在析构中统一保存（这里仅关闭不删除，避免丢失坐标）
    if (overlay_) overlay_->close(); // 关闭悬浮窗（不删除对象）
    mapper_->stop(); // 停止键鼠执行器
    gamepad_->stop(); // 停止手柄轮询
    event->accept(); // 接受关闭事件（窗口正常关闭）
} // 函数结束

// ============================================================
// exitApplication：托盘「退出」统一入口
// ============================================================
// 保存悬浮窗位置后直接退出事件循环（绕过关闭确认/最小化逻辑，
// 确保「关闭时最小化到托盘」设置下托盘退出仍能真正退出程序），
// 配置落盘由 ~MainWindow 完成。
void MainWindow::exitApplication() { // 托盘「退出」统一入口
    if (overlay_) { // 悬浮窗存在时……
        // 【Qt】QPoint：二维整型坐标；pos() 返回控件当前位置。
        const QPoint pos = overlay_->pos(); // 取悬浮窗当前位置
        input_->profile.globalSettings.overlayX = pos.x(); // 保存 X 坐标到配置
        input_->profile.globalSettings.overlayY = pos.y(); // 保存 Y 坐标到配置
        input_->profile.globalSettings.overlayScale = overlay_->scale(); // 保存缩放比例到配置
    } // if 结束
    // 保存主窗口位置
    const QPoint wpos = pos(); // 取主窗口当前位置
    input_->profile.globalSettings.mainWindowX = wpos.x(); // 保存 X 坐标
    input_->profile.globalSettings.mainWindowY = wpos.y(); // 保存 Y 坐标
    // 【Qt】QApplication::quit()：退出 Qt 事件循环，程序正常结束。
    QApplication::quit(); // 退出事件循环（配置落盘由析构完成）
} // 函数结束

// ============================================================
// changeEvent：最小化时隐藏到托盘（任务栏不显示图标）
// ============================================================
// 点击右上角最小化按钮后，窗口从任务栏消失，仅保留右下角托盘图标；
// 通过托盘菜单「显示主界面」或双击托盘图标恢复。
void MainWindow::changeEvent(QEvent* event) { // 覆写 changeEvent：监听窗口状态变化
    // 【C++ 语法】显式调用基类实现：QMainWindow::函数名 指明调用父类的同名函数。
    QMainWindow::changeEvent(event); // 先调用基类处理（保证默认行为）
    if (event->type() == QEvent::WindowStateChange && isMinimized()) { // 若是窗口状态变化事件且窗口已最小化……
        // 延迟到最小化动画结束后隐藏，避免任务栏图标闪烁
        // 【Qt】QTimer::singleShot(毫秒, 上下文, lambda)：一次性定时器，0 毫秒后执行 lambda（在下一事件循环空闲时）。
        QTimer::singleShot(0, this, [this]() { hide(); }); // 下一事件循环隐藏窗口（等动画结束）
    } // if 结束
} // 函数结束

// ============================================================
// 析构：保存悬浮窗位置、自动保存配置、关闭并释放悬浮窗
// ============================================================
// 【C++ 语法】析构函数定义：对象销毁时自动调用，用于清理资源。
MainWindow::~MainWindow() { // 析构：保存悬浮窗位置、自动保存配置、释放悬浮窗
    if (overlay_) { // 悬浮窗存在时……
        const QPoint pos = overlay_->pos(); // 取悬浮窗位置
        input_->profile.globalSettings.overlayX = pos.x(); // 保存 X 坐标
        input_->profile.globalSettings.overlayY = pos.y(); // 保存 Y 坐标
        input_->profile.globalSettings.overlayScale = overlay_->scale(); // 保存缩放
        overlay_->close(); // 关闭悬浮窗
        // 【C++ 语法】delete 指针：释放 new 分配的堆对象并调用其析构函数。
        delete overlay_; // 释放悬浮窗对象（悬浮窗是 new 创建的，须手动 delete）
    } // if 结束
    // 保存主窗口位置
    const QPoint wpos = pos(); // 取主窗口位置
    input_->profile.globalSettings.mainWindowX = wpos.x(); // 保存 X 坐标
    input_->profile.globalSettings.mainWindowY = wpos.y(); // 保存 Y 坐标
    ConfigManager::save(input_->profile); // 自动保存配置到磁盘
} // 析构函数结束
