# GamepadControllerQt

Windows 本机手柄 → 键鼠映射器（Qt Widgets GUI）。参考 `L:\steamlike` 安卓工程移植，去除跨机器桥接（TCP）、Android 悬浮窗/前台服务等逻辑，手柄与键鼠注入均在本机完成。

- **手柄读取**：Windows 原生接口 **XInput**（`XInputGetState`，125Hz 轮询，不使用 QtGamepad）
- **键鼠注入**：`SendInput()` 直接注入到当前前台窗口
- **UI**：Qt Widgets（Qt 5.12+ / Qt 6 均可）

---

## 功能特性

- **公共层 + 操作层架构**：公共层始终激活（兜底），操作层通过公共层的 `SwitchLayer` 映射「按住激活、松开回退」
- **最多 10 个操作层**：WoW 预设（Layer1 战斗 / Layer2 骑乘 / …），显示名可自由修改
- **子命令组合键**：每个按键映射最多 3 个子命令，实现 `Alt+3` 等组合键（按住依次按下、松开逆序释放）
- **左摇杆 WASD 移动**：8 方向，阈值 0.5
- **右摇杆视角控制**：独立线程 125Hz 节拍，加速度曲线 + EMA 平滑 + 位移积分
- **MouseToggle 长按锁存**：按住期间鼠标键保持按下（主动锁存，松开不改变状态）
- **悬浮层信息窗**：独立置顶窗口，实时显示当前层与按下的手柄按键，可拖动、主窗口最小化不隐藏
- **配置持久化**：JSON（version=2），与安卓版 `steamlike_config.json` 格式兼容，可互换

---

## 目录结构

```
GamepadControlerQt/
├── CMakeLists.txt
├── src/
│   ├── main.cpp                 # 程序入口，组装各组件
│   ├── core/                    # 核心逻辑（无 UI 依赖）
│   │   ├── InputTypes.h/.cpp        # 枚举/向量/名称转换（Android KeyCode 常量）
│   │   ├── MappingTypes.h/.cpp      # 数据模型：MappedAction/KeyMapping/OperationLayer/Profile
│   │   ├── SteamInput.h/.cpp        # 映射引擎：层管理 + 按键查询 + 输入分发
│   │   ├── InputInjector.h/.cpp     # 注入器接口 + Windows SendInput 实现 + VK 映射表
│   │   ├── KeyboardMouseMapper.h/.cpp  # 键鼠执行器：按钮/子命令/WASD/右摇杆视角线程
│   │   ├── ControllerConfig.h/.cpp  # JSON 序列化（version=2）
│   │   └── ConfigManager.h/.cpp     # 配置文件读写（exe 目录 + steamlike_config.json）
│   ├── gamepad/
│   │   └── XInputGamepadSource.h/.cpp  # XInput 手柄轮询源
│   └── ui/
│       ├── MainWindow.h/.cpp        # 主窗口（层按钮/设置滑块/启停/保存）
│       ├── LayerEditDialog.h/.cpp   # 操作层编辑对话框
│       └── OverlayWidget.h/.cpp     # 悬浮层信息窗口
```

---

## 构建

依赖：CMake 3.16+、Qt Widgets（Qt 5.12 或 Qt 6）、编译工具链。

### Qt 6（msys2 UCRT64）

```powershell
cmake -S . -B build-qt6 -G "Ninja" `
  -DCMAKE_PREFIX_PATH=M:/msys64/ucrt64 `
  -DCMAKE_CXX_COMPILER=M:/msys64/ucrt64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=H:/Qt/Tools/Ninja/ninja.exe `
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-qt6
```

> 若 PATH 中已有 msys2 的 `qmake6.exe`，可省略 `-DCMAKE_PREFIX_PATH`（CMake 会通过 qmake 反推安装前缀）。

### Qt 5（H:\Qt\5.15.2\mingw81_64）

```powershell
cmake -S . -B build -G "Ninja" `
  -DCMAKE_PREFIX_PATH=H:/Qt/5.15.2/mingw81_64 `
  -DCMAKE_CXX_COMPILER=H:/Qt/Tools/mingw810_64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=H:/Qt/Tools/Ninja/ninja.exe `
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 运行

Qt 5 需要把 Qt 的 bin 加入 PATH：

```powershell
$env:PATH = "H:\Qt\5.15.2\mingw81_64\bin;" + $env:PATH
build\GamepadControllerQt.exe   # 或 build-qt6\GamepadControllerQt.exe
```

---

## 架构与数据流

```
XInputGamepadSource ──buttonChanged/stickChanged──> SteamInput
        (XInputGetState, 125Hz)                      │
                                                      │ 查询有效映射（激活层→公共层）
                                                      ▼
                                              KeyboardMouseMapper
                                                      │
                                                      ▼ SendInput
                                              Windows 前台窗口（目标游戏）
```

关键设计：

1. **层查询顺序**：`getEffectiveMapping` 从最后激活的操作层开始，逐层回退到公共层，返回第一个命中的映射。
2. **层切换**：`SwitchLayer` 动作由引擎 `handleButtonEvent` 处理——按下时激活目标层并记录触发按钮，松开时停用对应层并直接返回（不触发映射）。`triggerButton` 字段仅用于 UI 展示，不参与运行时切换。
3. **精确释放**：松开按钮时按「已注入状态」（`releaseButtonInjection`）释放，与当前层映射无关，避免长按触发键切层导致按键卡死。
4. **右摇杆线程模型**：主线程写入 `std::atomic<float>` 摇杆值，look 线程固定 8ms 节拍读取并计算位移；注入器内部 `QMutex` 保护按键状态。
5. **连接防抖**：连续 3 次轮询失败才判定手柄断开；断开时 `releaseAllInputs()` 释放全部注入（含 MouseToggle 锁存），防止鼠标卡死。

---

## 配置格式（JSON version=2）

配置文件路径：`<exe目录>\steamlike_config.json`（与安卓版同名，可互换）。

```json
{
  "version": 2,
  "globalSettings": {
    "deadzone": 0.15,
    "lookSensitivity": 0.5,
    "cursorSpeed": 1.0,
    "lookSmoothing": 0.5,
    "lookAcceleration": 1.5
  },
  "commonLayer": {
    "id": "Common",
    "name": "Common",
    "buttonMappings": {
      "A": { "action": { "type": "keyboard", "keyCode": 62 } },
      "B": { "action": { "type": "mouse", "button": "RIGHT" } },
      "DPAD_UP": { "action": { "type": "switchLayer", "layerName": "Layer1" } }
    }
  },
  "layers": [
    {
      "id": "Layer1",
      "name": "Layer1 战斗",
      "triggerButton": "DPAD_UP",
      "buttonMappings": {
        "X": {
          "action": { "type": "keyboard", "keyCode": 51 },
          "subCommands": [ 57 ]
        }
      }
    }
  ]
}
```

### 字段说明

| 字段 | 说明 |
|---|---|
| `version` | 配置版本号，必须为 2，否则拒绝加载 |
| `globalSettings` | 死区 / 灵敏度 / 光标速度（预留）/ 平滑 / 加速度 |
| `commonLayer` / `layers` | 层对象；`id` 唯一标识（运行时定位用，重命名不影响），`name` 显示名可修改 |
| `triggerButton` | 仅 UI 展示用，不参与运行时层切换 |
| `buttonMappings` | 键名 -> 映射；键名见「手柄按键名」 |
| `action.type` | `keyboard` / `mouse` / `mouseToggle` / `switchLayer` / `mouseMove` / `lookAround` |
| `action.keyCode` | Android KeyCode（如空格=62、W=51），运行时转为 Windows VK |
| `action.button` | 鼠标键大写名：`LEFT`/`RIGHT`/`MIDDLE`/`FORWARD`/`BACK` |
| `subCommands` | 子命令 Android KeyCode 数组（最多 3 个），组合键用 |

### 手柄按键名（buttonMappings 键名 / triggerButton）

`A` `B` `X` `Y` `LB` `RB` `LT` `RT` `L3` `R3` `START` `BACK` `GUIDE` `DPAD_UP` `DPAD_DOWN` `DPAD_LEFT` `DPAD_RIGHT` `TOUCHPAD_CLICK`

---

## 使用说明

1. 启动程序，状态栏显示手柄连接状态；未连接时插入手柄即可自动识别。
2. 点击 **开始映射** 启用键鼠注入（默认已启动）。
3. 按住层切换按键（默认：DPAD 上/下/左/右 → Layer1-4，LB/RB → Layer5/6，L3 → Layer7，触摸板 → Layer8，LT/RT → Layer9/10）激活对应操作层，松开回到公共层。
4. 右键层按钮可编辑该层的按键映射；点击按钮可直接激活/停用该层（调试用）。
5. 全局设置滑块实时生效（死区、视角灵敏度、平滑、加速度）。
6. 保存配置后写入 `steamlike_config.json`；重置默认恢复 WoW 预设。
7. 最小化主窗口后，悬浮层信息窗继续显示，可拖动到任意位置。
