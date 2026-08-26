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

#include <QApplication>
#include <QEvent>

#include "core/ConfigManager.h"
#include "core/InputInjector.h"
#include "core/KeyboardMouseMapper.h"
#include "core/SteamInput.h"
#include "gamepad/XInputGamepadSource.h"
#include "ui/DarkTitleBar.h"
#include "ui/MainWindow.h"

namespace {

// ============================================================
// DarkDialogFilter —— 全局对话框深色标题栏过滤器
// ============================================================
// 任何顶层窗口（QDialog 等）显示时自动套用深色标题栏，
// 与 MainWindow / LayerEditDialog / HelpDialog 保持一致。
// QInputDialog / QMessageBox 等原生对话框没有自己调用
// enableDarkTitleBar，标题栏会残留系统浅色，这里统一补上。
// enableDarkTitleBar 幂等，对已设置的窗口重复调用无副作用。
// ============================================================
class DarkDialogFilter : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (ev->type() == QEvent::Show) {
            if (auto* w = qobject_cast<QWidget*>(obj); w && w->isWindow())
                enableDarkTitleBar(w);
        }
        return QObject::eventFilter(obj, ev);
    }
};

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    // 应用元信息（用于配置文件/设置路径等；本程序配置走 exe 同目录）
    QApplication::setApplicationName(QStringLiteral("GamepadControllerQt"));
    QApplication::setOrganizationName(QStringLiteral("SteamLike"));

    // ---- 全局深色主题（限定在 QDialog 内） ----
    // 统一原生对话框（QInputDialog / QMessageBox 等）的深色风格：
    // 复制/重命名/删除操作集、重置确认等弹窗都走这套样式。
    // 用「QDialog ...」后代选择器限定作用域，避免影响主窗口/悬浮窗
    // 各自的局部样式表（局部 QSS 优先于全局）。
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
    )");

    // 全局深色标题栏过滤器（生命周期随 main 退出）
    DarkDialogFilter darkFilter;
    app.installEventFilter(&darkFilter);

    // ---- 1. 映射引擎 + 配置 ----
    // 加载配置文件（无配置/损坏则自动生成默认配置）
    SteamInput input;
    input.loadProfile(ConfigManager::load());

    // ---- 2. 键鼠注入 + 映射执行器 ----
    // createWindowsInputInjector() 返回基于 SendInput 的注入器实例
    // （工厂函数，返回原始指针，退出时需手动 delete）
    InputInjector* injector = createWindowsInputInjector();
    KeyboardMouseMapper mapper(&input, injector);
    mapper.start();

    // ---- 3. 手柄读取 ----
    // Windows XInput 实现（替代 QtGamepad），独立线程轮询
    XInputGamepadSource gamepad;
    gamepad.start();

    // ---- 4. 手柄输入 -> 映射引擎 ----
    // 信号从轮询线程发出，用 DirectConnection 确保即时传递（低延迟）
    QObject::connect(&gamepad, &XInputGamepadSource::buttonChanged,
                     &input, &SteamInput::handleButtonEvent, Qt::DirectConnection);
    QObject::connect(&gamepad, &XInputGamepadSource::stickChanged,
                     &input, &SteamInput::handleStickInput, Qt::DirectConnection);
    // 手柄断开时释放全部注入状态，避免 MouseToggle 保持的鼠标键卡死
    QObject::connect(&gamepad, &XInputGamepadSource::connectedChanged,
                     &mapper, [&mapper](bool connected) {
                         if (!connected) mapper.releaseAllInputs();
                     }, Qt::DirectConnection);

    // ---- 5. 主窗口 ----
    MainWindow window(&input, &mapper, &gamepad);
    window.show();

    const int rc = app.exec();   // 进入 Qt 事件循环

    // ---- 6. 退出清理 ----
    // 停止 look 线程并释放所有残留的注入（松开所有按键/鼠标键）
    mapper.stop();
    injector->releaseAll();
    delete injector;

    return rc;
}
