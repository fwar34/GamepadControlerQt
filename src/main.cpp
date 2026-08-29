// ============================================================
// main.cpp
// 程序入口：组装各个模块并启动事件循环
// ------------------------------------------------------------
// 数据流总览（Windows 本机版，无跨机器桥接）：
//
//   XInputGamepadSource (手柄读取, 独立线程轮询 125Hz)
//        |  buttonChanged / stickChanged / connectedChanged
//        v
//   SteamInput (映射引擎：层管理、按键/摇杆分发)
//        |  buttonMapped / layerChanged
//        v
//   KeyboardMouseMapper (键鼠映射执行器, 独立 look 线程)
//        |  WindowsInputInjector (SendInput 注入)
//        v
//   真实键盘 / 鼠标 / 鼠标指针
//
// 生命周期：所有对象创建于 main() 栈上，随事件循环退出一起析构。
// ============================================================

#include <QApplication>   // 【Qt】包含 QApplication 类头文件（Qt Widgets 程序的必需应用类）
#include <QEvent>   // 【Qt】包含 QEvent 事件类头文件（事件过滤器判断事件类型需要）
#include <QFont>   // 【Qt】包含 QFont 字体类头文件（全局字体设置需要）

#include "core/ConfigManager.h"   // 【C++ 语法】包含本工程自定义头文件：配置管理器（负责配置读写）
#include "core/InputInjector.h"   // 包含注入器接口头文件（Windows SendInput 注入实现）
#include "core/KeyboardMouseMapper.h"   // 包含键鼠映射执行器头文件
#include "core/SteamInput.h"   // 包含映射引擎头文件
#include "gamepad/XInputGamepadSource.h"   // 包含手柄读取源头文件（XInput 实现）
#include "ui/DarkTitleBar.h"   // 包含深色标题栏工具函数头文件
#include "ui/MainWindow.h"   // 包含主窗口头文件

namespace {   // 【C++ 语法】匿名命名空间：内部名称仅在本编译单元可见（相当于文件内 static），避免与其它文件符号冲突

// ============================================================
// DarkDialogFilter —— 全局对话框深色标题栏过滤器
// ============================================================
// 任何顶层窗口（QDialog 等）显示时自动套用深色标题栏，
// 与 MainWindow / LayerEditDialog / HelpDialog 保持一致。
// QInputDialog / QMessageBox 等原生对话框没有自己调用
// enableDarkTitleBar，标题栏会残留系统浅色，这里统一补上。
// enableDarkTitleBar 幂等，对已设置的窗口重复调用无副作用。
// ============================================================
class DarkDialogFilter : public QObject {   // 【C++ 语法】类定义：公有继承 QObject（Qt 基类，提供事件过滤与对象树能力）
public:   // 【C++ 语法】访问控制：以下成员为公有
    using QObject::QObject;   // 【C++ 语法】using 继承构造：把 QObject 的所有构造函数引入本类，本类可用同样的参数方式构造

protected:   // 【C++ 语法】访问控制：以下成员为受保护（仅本类及派生类可访问）
    bool eventFilter(QObject* obj, QEvent* ev) override {   // 【Qt】重写事件过滤器虚函数：所有发给被监视对象的事件都会先到达这里；【C++ 语法】override 表示重写基类虚函数
        if (ev->type() == QEvent::Show) {   // 【Qt】判断事件类型是否为“窗口显示”事件（QEvent::Show）
            if (auto* w = qobject_cast<QWidget*>(obj); w && w->isWindow())   // 【C++ 语法】C++17 的 if 初始化语句：声明指针并立即判断；【Qt】qobject_cast 把 QObject* 安全转型为 QWidget*；isWindow() 判断是否为顶层窗口
                enableDarkTitleBar(w);   // 【Windows API】对顶层窗口调用工具函数设置深色标题栏
        }   // 【C++ 语法】结束内层 if 代码块
        return QObject::eventFilter(obj, ev);   // 【Qt】把事件继续交给基类处理（不吞掉事件），保证原有事件分发流程不受影响
    }   // 【C++ 语法】结束 eventFilter 函数体
};   // 【C++ 语法】结束类定义（类声明末尾必须有分号）

}  // namespace   // 【C++ 语法】结束匿名命名空间

int main(int argc, char* argv[]) {   // 【C++ 语法】主函数入口：argc 为命令行参数个数，argv 为参数字符串数组；程序从这里开始执行
    QApplication app(argc, argv);   // 【Qt】创建 QApplication 应用对象（管理 GUI 资源与事件循环），必须最先创建、整个程序仅一个
    // 应用元信息（用于配置文件/设置路径等；本程序配置走 exe 同目录）
    QApplication::setApplicationName(QStringLiteral("GamepadControllerQt"));   // 【Qt】设置应用名称（用于 QSettings 等存储路径；本程序配置存于 exe 同目录）
    QApplication::setOrganizationName(QStringLiteral("SteamLike"));   // 【Qt】设置组织名称（与应用名一起构成配置/设置项的归属路径）

    // ---- 全局默认字体：微软雅黑 ----
    // 保留系统默认字号，只统一字体族，使未显式指定字体的控件/对话框一致。
    // 字体渲染：Qt6 在 Windows 上用 FreeType，默认 hinting（PreferDefaultHinting）
    // 会让雅黑在小字号下笔画粘连、边缘毛糙。实测改为 PreferNoHinting +
    // 抗锯齿后文字平滑干净（若嫌发虚可改用 PreferVerticalHinting）。
    QFont appFont = app.font();   // 【Qt】取出应用当前默认字体作为修改起点（保留系统默认字号）
    appFont.setFamily(QStringLiteral("Microsoft YaHei"));   // 【Qt】把字体族设置为微软雅黑，统一界面字体
    appFont.setStyleStrategy(QFont::PreferAntialias);   // 【Qt】设置字体渲染策略为优先抗锯齿，使文字边缘平滑
    appFont.setHintingPreference(QFont::PreferNoHinting);   // 【Qt】关闭字体 hinting（像素微调），避免雅黑在小字号下笔画粘连毛糙
    app.setFont(appFont);   // 【Qt】把修改后的字体设为应用全局默认字体，未显式设置字体的控件都会继承它

    // ---- 全局深色主题（限定在 QDialog 内） ----
    // 统一原生对话框（QInputDialog / QMessageBox 等）的深色风格：
    // 复制/重命名/删除操作集、重置确认等弹窗都走这套样式。
    // 用「QDialog ...」后代选择器限定作用域，避免影响主窗口/悬浮窗
    // 各自的局部样式表（局部 QSS 优先于全局）。
    // 【Qt】app.setStyleSheet 为应用设置全局样式表 QSS（内容见下方，字符串内部不插入注释）
    app.setStyleSheet(R"(
        QDialog {
            background-color: #2b2d31;
        }
        QDialog QLabel {
            color: #d5d9df;
            background-color: transparent;
        }
        QDialog QLineEdit {
            background-color: #33363b;
            color: #e8eaee;
            border: 1px solid #4a4e55;
            border-radius: 5px;
            padding: 4px 8px;
            selection-background-color: #7fc9c4;
            selection-color: #1c1e22;
        }
        QDialog QLineEdit:focus {
            border-color: #7fc9c4;
        }
        QDialog QPushButton {
            background-color: #3d4147;
            color: #e8eaee;
            border: 1px solid #4a4e55;
            border-radius: 6px;
            padding: 5px 14px;
            min-width: 60px;
        }
        QDialog QPushButton:hover {
            background-color: #474b52;
            border-color: #7fc9c4;
        }
        QDialog QPushButton:pressed {
            background-color: #2f3237;
        }
        QDialog QPushButton:focus {
            outline: none;
        }
    )");   // 【C++ 语法】结束原始字符串字面量，并结束 setStyleSheet 语句

    // 全局深色标题栏过滤器（生命周期随 main 退出）
    DarkDialogFilter darkFilter;   // 【C++ 语法】在栈上创建过滤器对象（作用域为 main，随 main 结束自动析构）
    app.installEventFilter(&darkFilter);   // 【Qt】把过滤器安装到应用对象上：应用接收的所有事件都会先经过过滤器再正常分发

    // ---- 1. 映射引擎 + 配置 ----
    // 加载配置文件（无配置/损坏则自动生成默认配置）
    SteamInput input;   // 【C++ 语法】在栈上创建映射引擎对象（栈对象，离开作用域自动析构）
    input.loadProfile(ConfigManager::load());   // 【Qt】调用 ConfigManager 的静态方法 load() 读取配置，并把返回的临时结果加载进映射引擎

    // ---- 2. 键鼠注入 + 映射执行器 ----
    // createWindowsInputInjector() 返回基于 SendInput 的注入器实例
    // （工厂函数，返回原始指针，退出时需手动 delete）
    InputInjector* injector = createWindowsInputInjector();   // 【C++ 语法】声明 InputInjector 指针变量；工厂函数返回新建的注入器原始指针（需手动 delete）
    KeyboardMouseMapper mapper(&input, injector);   // 【C++ 语法】在栈上创建 mapper 对象，构造参数为映射引擎与注入器的指针（mapper 使用但不拥有它们）
    mapper.start();   // 启动映射执行器（内部开启独立 look 线程，按固定 8ms 周期处理鼠标移动）

    // ---- 3. 手柄读取 ----
    // Windows XInput 实现（替代 QtGamepad），独立线程轮询
    XInputGamepadSource gamepad;   // 【C++ 语法】在栈上创建手柄读取源对象
    gamepad.start();   // 启动手柄轮询（内部开启独立线程，以 125Hz 频率读取 XInput 状态）

    // ---- 4. 手柄输入 -> 映射引擎 ----
    // 信号从轮询线程发出，用 DirectConnection 确保即时传递（低延迟）
    QObject::connect(&gamepad, &XInputGamepadSource::buttonChanged,   // 【Qt】连接信号与槽：手柄“按键变化”信号
                     &input, &SteamInput::handleButtonEvent, Qt::DirectConnection);   // 连接到映射引擎的按键处理槽；DirectConnection 表示信号发出时在当前线程立即同步执行，保证低延迟
    QObject::connect(&gamepad, &XInputGamepadSource::stickChanged,   // 【Qt】连接信号与槽：手柄“摇杆变化”信号
                     &input, &SteamInput::handleStickInput, Qt::DirectConnection);   // 连接到映射引擎的摇杆处理槽；同样使用 DirectConnection 保持低延迟
    // 手柄断开时释放全部注入状态，避免 MouseToggle 保持的鼠标键卡死
    QObject::connect(&gamepad, &XInputGamepadSource::connectedChanged,   // 【Qt】连接信号与槽：手柄“连接状态变化”信号
                     &mapper, [&mapper](bool connected) {   // 【C++ 语法】lambda 匿名函数作为槽：按引用捕获 mapper，参数 connected 表示手柄是否仍连接
                         if (!connected) mapper.releaseAllInputs();   // 【C++ 语法】if 单语句：断开时调用 releaseAllInputs 释放所有已注入的按键/鼠标键，防止卡键
                     }, Qt::DirectConnection);   // 【C++ 语法】lambda 结束并作为参数传入 connect，同时指定 DirectConnection 连接方式

    // ---- 5. 主窗口 ----
    MainWindow window(&input, &mapper, &gamepad);   // 【C++ 语法】在栈上创建主窗口对象，构造参数为上述三个核心对象的指针
    window.show();   // 【Qt】显示主窗口（show() 使窗口变为可见）

    // 【Qt】app.exec() 进入事件循环并阻塞运行，直到所有窗口关闭才返回退出码
    const int rc = app.exec();   // 进入 Qt 事件循环

    // ---- 6. 退出清理 ----
    // 停止 look 线程并释放所有残留的注入（松开所有按键/鼠标键）
    mapper.stop();   // 停止映射执行器及其 look 线程（等待线程安全结束）
    injector->releaseAll();   // 释放注入器中所有残留的注入状态（松开所有按键/鼠标键），避免程序退出后卡键
    delete injector;   // 【C++ 语法】delete 释放工厂函数 new 出来的注入器对象（工厂返回的原始指针需手动删除）

    return rc;   // 【C++ 语法】把事件循环的退出码作为程序返回值返回给操作系统
}   // 【C++ 语法】结束 main 函数体
