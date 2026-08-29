// 【C++ 语法】#pragma once：编译器预处理指令，保证本头文件在单个编译单元中只被包含一次，作用等价于 #ifndef/#define 宏守卫。
#pragma once // 防止头文件被重复包含的预处理指令

// 【C++ 语法】#include <...>：尖括号形式，表示在编译器标准头文件搜索路径中查找；预处理时将该头文件内容原样文本包含到本文件。
// 【Qt】QMainWindow：Qt Widgets 提供的主窗口类（带菜单栏、工具栏、状态栏、中央控件区等）。
#include <QMainWindow> // 引入 QMainWindow 声明，本类将公有继承它

// 【Qt】QHash<K, V>：Qt 提供的哈希表容器（键值对），类似 std::unordered_map。
#include <QHash> // 引入哈希表容器，用于保存「手柄键 -> 鼠标键」的 MouseToggle 锁存映射

// 【C++ 语法】双引号形式的 #include：优先在包含者所在目录查找头文件（用于项目内部头文件）。
#include "OverlayWidget.h" // 引入悬浮信息窗类声明（主窗口需要与其联动）

// 【C++ 语法】前置声明（forward declaration）：只声明类名而不包含类定义，可减小编译依赖、避免头文件循环包含；本文件只用这些类的指针/引用，无需完整定义。
class SteamInput;            // 前置声明：映射引擎类（构造时注入指针）
class KeyboardMouseMapper;   // 前置声明：键鼠执行器类
class XInputGamepadSource;   // 前置声明：手柄轮询源类
class QEvent;                // 前置声明：Qt 事件基类
class QLabel;                // 前置声明：文本标签控件
class QPushButton;           // 前置声明：按钮控件
class QComboBox;             // 前置声明：下拉框控件
class QCheckBox;             // 前置声明：复选框控件
class QSlider;               // 前置声明：滑块控件
class QTimer;                // 前置声明：定时器
class QMenu;                 // 前置声明：菜单
class QSystemTrayIcon;       // 前置声明：系统托盘图标

// =====================================================================
// MainWindow —— 主窗口
//
// 功能：
//   - 顶部：启停映射按钮、手柄连接状态、当前激活层标签
//   - 操作集区：切换 / 添加 / 复制 / 重命名 / 删除操作集
//   - 中部：2 列 x 5 行操作层按钮（点击编辑该层）、
//     公共层编辑入口、全局设置滑块（死区/灵敏度/平滑/加速度）
//   - 底部：保存配置、重置默认
//   - 右上角：悬浮层信息窗口（OverlayWidget，独立顶层窗口）
//
// 操作集（OperationSet）：最顶层容器，一组完整的映射配置
//   （1 公共层 + 最多 10 操作层）。切换操作集时其下所有层整体切换，
//   各操作集之间配置互不干扰。所有操作集操作都在主窗口左侧完成。
//
// 滑块与 GlobalSettings 的换算：
//   死区 0-50 -> /100；灵敏度 10-200 -> /100；
//   平滑 0-100 -> /100；加速度 100-300 -> /100
// =====================================================================
// 【C++ 语法】类定义；「: public QMainWindow」表示公有继承，子类拥有基类全部公有接口并可覆写其虚函数。
class MainWindow : public QMainWindow { // 主窗口类定义：公有继承 QMainWindow
    // 【Qt】Q_OBJECT 宏：Qt 元对象系统入口，为类生成信号槽/moc 元数据；凡声明信号槽或用 connect/tr 的 QObject 子类必须包含。
    Q_OBJECT // 启用 Qt 信号槽机制的宏（须在类内出现，且该类最终继承 QObject）
public: // 访问说明符：以下成员为公有，可从类外部访问
    // 【C++ 语法】构造函数：与类同名的特殊成员函数，在对象创建时自动调用，可带默认参数（此处 parent 默认 nullptr）。
    MainWindow(SteamInput* input, KeyboardMouseMapper* mapper, XInputGamepadSource* gamepad,
               QWidget* parent = nullptr); // 构造函数声明：注入引擎/执行器/手柄源指针；parent 默认 nullptr 表示无父窗口
    // 【C++ 语法】析构函数（~类名）：对象销毁时自动调用；override 表示覆写基类的虚析构函数。
    ~MainWindow() override; // 析构函数声明：退出时保存悬浮窗位置并自动保存配置

    // 【Qt】slots：Qt 槽函数声明关键字，槽函数可被信号触发；private 限定其仅可在类内部连接使用。
private slots: // 私有槽区：这些成员函数可被信号通过 connect 调用
    // 【Qt】const QString&：常量引用传参，避免拷贝字符串。
    void onLayerChanged(const QString& activeLayerName); // 槽：当前层变化 -> 更新标签与悬浮窗
    void onConnectionChanged(bool connected); // 槽：手柄连接状态变化 -> 更新状态标签
    void onToggleStartStop(); // 槽：切换映射启动/停止
    void onSaveConfig(); // 槽：保存配置到磁盘
    void onResetConfig(); // 槽：重置为默认配置
    void onEditCommonLayer(); // 槽：打开公共层编辑对话框
    void onApplySettings(); // 槽：把滑块值写回引擎全局设置
    void onShowHelp(); // 槽：打开使用说明对话框
    void onCheckForeground(); // 槽：定时检查前台窗口，切换时释放按键
    // ---- 操作集管理 ----
    void onSetComboChanged(int index);   // 下拉框选择变化 -> 切换操作集
    void onAddSet();                     // 添加新操作集并切换到它
    void onCopySet();                    // 复制当前操作集（可直接改名）
    void onRenameSet();                  // 重命名当前操作集
    void onDeleteSet();                  // 删除当前操作集（至少保留一个）
    // 【Qt】事件处理函数：覆写基类的 closeEvent/changeEvent，以拦截窗口关闭与状态变化事件。
    void closeEvent(QCloseEvent* event) override; // 覆写：自定义关闭行为（退出或最小化到托盘）
    void changeEvent(QEvent* event) override; // 覆写：监听窗口状态变化（最小化时隐藏到托盘）
    // 托盘「退出」统一入口：保存悬浮窗位置后退出程序
    void exitApplication(); // 槽：托盘「退出」入口，保存位置后退出事件循环

private: // 访问说明符：以下成员仅供本类内部使用
    // 根据当前 profile 重建层按钮文本（含显示名）
    void refreshLayerButtons(); // 刷新所有层按钮的勾选状态与文本
    // 重建操作集下拉框（按当前激活集选中，refreshingSets_ 防递归）
    void refreshSetCombo(); // 重建操作集下拉框并选中当前激活集
    // 打开指定层（id）的编辑对话框
    void editLayer(const QString& layerName); // 打开指定层（用层 id）的编辑对话框
    // 同步启停按钮文字与状态色（mappingActive=true 映射运行中）
    void applyStartStopState(bool mappingActive); // 同步启停按钮文字与颜色

    // 【C++ 语法】原始指针成员变量：仅保存外部传入对象的地址（不拥有、不负责释放），均为运行时依赖的核心对象。
    SteamInput* input_; // 指向映射引擎（构造时注入）
    KeyboardMouseMapper* mapper_; // 指向键鼠执行器
    XInputGamepadSource* gamepad_; // 指向手柄轮询源
    // 【C++ 语法】= nullptr：C++11 起允许的类内成员初始化，表示指针初始为空，避免未初始化野指针。
    OverlayWidget* overlay_ = nullptr; // 悬浮信息窗指针（独立顶层窗口，初始为空）

    QLabel* connectionLabel_ = nullptr; // 手柄连接状态标签
    QLabel* activeLayerLabel_ = nullptr; // 当前激活层标签
    // 【Qt】QVector<T>：Qt 动态数组容器（类似 std::vector），元素类型为按钮指针。
    QVector<QPushButton*> layerButtons_; // 各操作层按钮指针的集合（用于批量刷新）
    QPushButton* startStopButton_ = nullptr; // 启停映射按钮
    // 操作集下拉框（itemData 存操作集 id）；refreshingSets_ 防止程序化刷新触发切换
    QComboBox* setCombo_ = nullptr; // 操作集下拉框指针
    bool refreshingSets_ = false; // 下拉框程序化刷新标志（防止刷新误触发切换逻辑）
    // MouseToggle 锁存集合（手柄键 -> 鼠标键），用于主窗口边框变色提示
    // 【Qt】QHash<ControllerButton, MouseButton>：键为手柄键、值为鼠标键的哈希表。
    QHash<ControllerButton, MouseButton> toggledButtons_; // 锁存映射表：手柄键 -> 鼠标键
    bool toggleActive_ = false;   // 是否有 MouseToggle 处于锁存（边框是否高亮）

    QSlider* deadzoneSlider_ = nullptr; // 死区滑块
    QSlider* sensitivitySlider_ = nullptr; // 灵敏度滑块
    QSlider* smoothingSlider_ = nullptr; // 平滑滑块
    QSlider* accelerationSlider_ = nullptr; // 加速滑块
    QCheckBox* invertLookXCheck_ = nullptr; // 右摇杆 X 轴反转复选框
    QCheckBox* invertLookYCheck_ = nullptr; // 右摇杆 Y 轴反转复选框
    QCheckBox* releaseOnFgCheck_ = nullptr; // 切换窗口时释放按键复选框
    QCheckBox* confirmOnCloseCheck_ = nullptr; // 关闭时退出程序复选框

    // 【Qt】QTimer 定时器指针；void* 为无类型指针（C++ 通用指针），用于存放平台相关的窗口句柄。
    QTimer* foregroundTimer_ = nullptr; // 前台窗口监控定时器（每 200ms 检查一次）
    void* lastForegroundHwnd_ = nullptr; // 上一次记录的前台窗口句柄
    // 【Qt】QMenu / QAction：菜单与菜单项类。
    QSystemTrayIcon* trayIcon_ = nullptr; // 系统托盘图标
    QMenu* trayMenu_ = nullptr; // 托盘右键菜单
    QAction* trayMappingAction_ = nullptr;  // 托盘菜单「激活映射」项
}; // 类定义结束（类定义必须以分号结束）
