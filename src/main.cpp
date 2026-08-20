#include <QApplication>

#include "core/ConfigManager.h"
#include "core/InputInjector.h"
#include "core/KeyboardMouseMapper.h"
#include "core/SteamInput.h"
#include "gamepad/XInputGamepadSource.h"
#include "ui/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("GamepadControllerQt"));
    QApplication::setOrganizationName(QStringLiteral("SteamLike"));

    // 映射引擎 + 配置
    SteamInput input;
    input.loadProfile(ConfigManager::load());

    // 键鼠注入 + 映射器（公共层 + 操作层、子命令、摇杆视角）
    InputInjector* injector = createWindowsInputInjector();
    KeyboardMouseMapper mapper(&input, injector);
    mapper.start();

    // 手柄读取（Windows XInput，替代 QtGamepad）
    XInputGamepadSource gamepad;
    gamepad.start();

    // 手柄输入 -> 映射引擎
    QObject::connect(&gamepad, &XInputGamepadSource::buttonChanged,
                     &input, &SteamInput::handleButtonEvent);
    QObject::connect(&gamepad, &XInputGamepadSource::stickChanged,
                     &input, &SteamInput::handleStickInput);
    // 手柄断开时释放全部注入状态，避免 MouseToggle 保持的鼠标键卡死
    QObject::connect(&gamepad, &XInputGamepadSource::connectedChanged,
                     &mapper, [&mapper](bool connected) {
                         if (!connected) mapper.releaseAllInputs();
                     });

    MainWindow window(&input, &mapper, &gamepad);
    window.show();

    const int rc = app.exec();

    mapper.stop();
    injector->releaseAll();
    delete injector;

    return rc;
}
