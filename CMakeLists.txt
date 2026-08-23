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

# WIN32：编译为 GUI 子系统（Windows 下不弹黑色控制台窗口）
add_executable(GamepadControllerQt WIN32 ${PROJECT_SOURCES})

target_link_libraries(GamepadControllerQt PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

if(WIN32)
    target_compile_definitions(GamepadControllerQt PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(GamepadControllerQt PRIVATE winmm xinput1_4)

    # ---- UAC 管理员清单 ----
    # 目标游戏（如 WoW）常以管理员权限运行（高完整性级别），普通权限的
    # SendInput 注入会被 Windows UIPI 静默屏蔽，表现为"只有本程序在前台
    # 时手柄操作才生效"。声明 requireAdministrator 让本程序以管理员权限
    # 运行，注入即可到达高权限前台窗口。若不需要提升，删除本段即可。
    set(MANIFEST_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest)
    if(MINGW)
        # MinGW：用 windres 把 manifest 编译成 COFF 资源对象并链接进 exe
        set(MANIFEST_RC ${CMAKE_CURRENT_BINARY_DIR}/app_manifest.rc)
        set(MANIFEST_OBJ ${CMAKE_CURRENT_BINARY_DIR}/app_manifest.o)
        file(WRITE ${MANIFEST_RC} "1 24 \"${MANIFEST_FILE}\"\n")
        if(NOT CMAKE_RC_COMPILER)
            set(CMAKE_RC_COMPILER windres)
        endif()
        add_custom_command(
            OUTPUT ${MANIFEST_OBJ}
            COMMAND ${CMAKE_RC_COMPILER} -i ${MANIFEST_RC} -o ${MANIFEST_OBJ}
            DEPENDS ${MANIFEST_RC} ${MANIFEST_FILE}
        )
        target_sources(GamepadControllerQt PRIVATE ${MANIFEST_OBJ})
    else()
        # MSVC：直接作为 manifest 资源编译进 exe
        target_sources(GamepadControllerQt PRIVATE ${MANIFEST_FILE})
        set_source_files_properties(${MANIFEST_FILE} PROPERTIES VS_TOOL_OVERRIDE "MANIFEST")
    endif()
endif()

# 源码与可执行字符集统一为 UTF-8，避免中文注释/字符串在编译输出中乱码
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(GamepadControllerQt PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
elseif(MSVC)
    target_compile_options(GamepadControllerQt PRIVATE /utf-8)
endif()
