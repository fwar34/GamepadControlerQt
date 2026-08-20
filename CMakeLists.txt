cmake_minimum_required(VERSION 3.16)
project(GamepadControllerQt VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 优先 Qt6，兼容 Qt5（msys2 UCRT64 提供 Qt6；H:\Qt 提供 Qt5）
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(PROJECT_SOURCES
    src/main.cpp

    # 核心数据模型
    src/core/InputTypes.h
    src/core/InputTypes.cpp
    src/core/MappingTypes.h
    src/core/MappingTypes.cpp

    # 映射引擎
    src/core/SteamInput.h
    src/core/SteamInput.cpp

    # 键鼠注入（Windows SendInput）
    src/core/InputInjector.h
    src/core/InputInjector.cpp

    # 键鼠映射器
    src/core/KeyboardMouseMapper.h
    src/core/KeyboardMouseMapper.cpp

    # 配置读写
    src/core/ControllerConfig.h
    src/core/ControllerConfig.cpp
    src/core/ConfigManager.h
    src/core/ConfigManager.cpp

    # 手柄读取（Windows XInput）
    src/gamepad/XInputGamepadSource.h
    src/gamepad/XInputGamepadSource.cpp

    # 界面
    src/ui/OverlayWidget.h
    src/ui/OverlayWidget.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/LayerEditDialog.h
    src/ui/LayerEditDialog.cpp
)

add_executable(GamepadControllerQt ${PROJECT_SOURCES})

target_link_libraries(GamepadControllerQt PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

if(WIN32)
    target_compile_definitions(GamepadControllerQt PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(GamepadControllerQt PRIVATE winmm xinput)
endif()
