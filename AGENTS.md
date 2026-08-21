# AGENTS.md

## 项目概况

Windows 本机手柄 → 键鼠映射器（Qt Widgets GUI）。单包 C++17 项目，无子包、无 monorepo 结构。

## 构建

CMake 3.16+ / Ninja / Qt Widgets（Qt 5.12+ 或 Qt 6）。项目同时支持两个 Qt 版本，构建目录分开：

```powershell
# Qt 6（msys2 UCRT64）
cmake -S . -B build-qt6 -G "Ninja" `
  -DCMAKE_PREFIX_PATH=M:/msys64/ucrt64 `
  -DCMAKE_CXX_COMPILER=M:/msys64/ucrt64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=H:/Qt/Tools/Ninja/ninja.exe `
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-qt6

# Qt 5（H:\Qt\5.15.2\mingw81_64）
cmake -S . -B build -G "Ninja" `
  -DCMAKE_PREFIX_PATH=H:/Qt/5.15.2/mingw81_64 `
  -DCMAKE_CXX_COMPILER=H:/Qt/Tools/mingw810_64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=H:/Qt/Tools/Ninja/ninja.exe `
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Qt 5 运行时需将 `H:\Qt\5.15.2\mingw81_64\bin` 加入 PATH。

## 架构

三层数据流，单向：

```
XInputGamepadSource → SteamInput → KeyboardMouseMapper → Windows SendInput
```

- **XInputGamepadSource** (`src/gamepad/`): QTimer 125Hz 轮询 XInput，发 buttonChanged/stickChanged 信号
- **SteamInput** (`src/core/SteamInput.h`): 映射引擎，层管理 + 按键查询 + 输入分发
- **KeyboardMouseMapper** (`src/core/KeyboardMouseMapper.h`): 键鼠执行器，含独立 look 线程
- **InputInjector** (`src/core/InputInjector.h`): 注入器接口 + Windows SendInput 实现

## 关键设计约定

- **KeyCode 用 Android 常量**：配置文件和核心层存储 Android KeyEvent 值（如空格=62、W=51），运行时通过 `androidKeyCodeToWindowsVK()` 转为 Windows VK。不要在配置或核心逻辑中直接用 Windows VK 常量。
- **层查询顺序**：从最后激活的操作层逐层回退到公共层，返回第一个命中的映射。
- **松开精确释放**：按「已注入状态」释放，不依赖当前层映射，避免切层导致按键卡死。
- **线程模型**：主线程处理按钮映射 + 写入右摇杆原子量；look 线程固定 8ms 节拍读取并注入鼠标移动。注入器内部 `QMutex` 保护按键状态。
- **连接防抖**：连续 3 次轮询失败才判定断开，断开时 `releaseAllInputs()` 释放全部注入。

## 源码编码

CMakeLists.txt 强制 UTF-8 编译（`-finput-charset=UTF-8 -fexec-charset=UTF-8` / `/utf-8`）。源码中可含中文注释和字符串。

## 配置文件

- 路径：`<exe目录>\steamlike_config.json`
- 格式：JSON version=2，与安卓版 `steamlike_config.json` 兼容
- `triggerButton` 字段仅 UI 展示，不参与运行时层切换（实际切换由公共层 SwitchLayer 映射驱动）
- `.gitignore` 已排除 `steamlike_config.json`

## 无测试 / 无 CI / 无 Linter

项目无测试套件、无 CI 配置、无代码检查工具。验证方式为编译通过 + 手柄实机测试。
