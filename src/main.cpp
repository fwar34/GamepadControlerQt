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

#include "core/ConfigManager.h"
#include "core/InputInjector.h"
#include "core/KeyboardMouseMapper.h"
#include "core/SteamInput.h"
#include "gamepad/XInputGamepadSource.h"
#include "ui/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    // 应用元信息（用于配置文件/设置路径等；本程序配置走 exe 同目录）
    QApplication::setApplicationName(QStringLiteral("GamepadControllerQt"));
    QApplication::setOrganizationName(QStringLiteral("SteamLike"));

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
