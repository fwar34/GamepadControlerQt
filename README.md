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
- **鼠标滚轮动作**：映射动作支持「滚轮上滚 / 滚轮下滚」，按下即注入一次滚轮事件
- **悬浮层信息窗**：独立置顶窗口，实时显示当前层与按下的手柄按键，可拖动、主窗口最小化不隐藏；在悬浮窗上滚动鼠标滚轮可缩放窗口大小
- **窗口位置记忆**：主窗口位置、悬浮窗位置与缩放比例均持久化到配置，下次启动恢复
- **配置持久化**：JSON（version=2），与安卓版 `steamlike_config.json` 格式兼容，可互换
- **托盘行为**：最小化主窗口隐藏到系统托盘（任务栏不显示图标）；「关闭时退出程序」选项决定点关闭是退出还是最小化到托盘

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
│       ├── OverlayWidget.h/.cpp     # 悬浮层信息窗口
│       ├── HelpDialog.h/.cpp        # 使用说明对话框
│       ├── DarkTitleBar.h           # 深色标题栏工具（DwmSetWindowAttribute）
│       └── res.qrc / icons/         # 资源文件与图标（下拉箭头 PNG）
```

---

## 构建

依赖：CMake 3.16+、Qt Widgets（Qt 5.12 或 Qt 6）、编译工具链。

### Qt 6（msys2 UCRT64）

```powershell
cmake -S . -B build-qt6 -G "Ninja" `
  -DCMAKE_PREFIX_PATH=I:/msys64/ucrt64 `
  -DCMAKE_CXX_COMPILER=I:/msys64/ucrt64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=I:/msys64/ucrt64/bin/ninja.exe `
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
4. **注入线程模型**：`buttonMapped`/`stickMapped` 由手柄轮询线程经 `Qt::DirectConnection` 直接执行，注入不经过主线程事件队列——否则若注入的鼠标按下落在本程序自身标题栏上，Windows 会进入非客户区模态追踪循环阻塞主线程，注入的松开事件排不进队列，导致按键/鼠标卡死；DirectConnection 让手柄线程仍能发送松开事件解除模态循环。
5. **右摇杆视角线程**：look 线程固定 8ms 节拍读取右摇杆原子量并注入鼠标移动；注入器内部互斥锁保护按键状态。手柄线程与主线程（`releaseAllInputs`）通过互斥锁串行化对注入状态容器的访问。
6. **连接防抖**：连续 3 次轮询失败才判定手柄断开；断开时 `releaseAllInputs()` 释放全部注入（含 MouseToggle 锁存），防止鼠标卡死。

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
| `globalSettings` | 死区 / 灵敏度 / 光标速度（预留）/ 平滑 / 加速度；`invertLookX/Y` 视角翻转开关；`overlayX/Y`/`overlayScale` 悬浮窗位置与缩放（-1 / 1.0 表示未保存过）；`mainWindowX/Y` 主窗口位置（-1 表示未保存过）；`releaseOnForegroundChange` 前台窗口切换时是否释放注入（默认 true）；`confirmOnClose` 「关闭时退出程序」选项：true 点关闭直接退出，false 点关闭最小化到托盘 |
| `commonLayer` / `layers` | 层对象；`id` 唯一标识（运行时定位用，重命名不影响），`name` 显示名可修改 |
| `triggerButton` | 仅 UI 展示用，不参与运行时层切换 |
| `buttonMappings` | 键名 -> 映射；键名见「手柄按键名」 |
| `action.type` | `keyboard` / `mouse` / `mouseToggle` / `switchLayer` / `mouseMove` / `lookAround` / `toggleMapping` / `toggleOnScreenKeyboard` / `toggleOverlay` |
| `action.keyCode` | Android KeyCode（如空格=62、W=51），运行时转为 Windows VK |
| `action.button` | 鼠标键大写名：`LEFT`/`RIGHT`/`MIDDLE`/`FORWARD`/`BACK` |
| `subCommands` | 子命令 Android KeyCode 数组（最多 3 个），组合键用 |

### 手柄按键名（buttonMappings 键名 / triggerButton）

`A` `B` `X` `Y` `LB` `RB` `LT` `RT` `L3` `R3` `MENU` `OPTIONS` `DPAD_UP` `DPAD_DOWN` `DPAD_LEFT` `DPAD_RIGHT`

- `MENU` = 手柄 START 键，`OPTIONS` = 手柄 BACK 键；`LT`/`RT` 为模拟扳机，压过半程（≥128）视为按下。
- `GUIDE`（Home 键）与 `TOUCHPAD_CLICK`（触控板点击）仅保留用于兼容安卓版配置，XInput 无对应物理位，不会产生输入。

---

## 使用说明

1. 启动程序，状态栏显示手柄连接状态；未连接时插入手柄即可自动识别。
2. 点击 **开始映射** 启用键鼠注入（默认已启动）。
3. 层切换由「切换层」动作驱动：在公共层（或其他层）为某手柄按键设置「切换层」动作，按住即临时激活目标操作层、松开回到公共层。默认配置不预设层切换映射，需自行配置。
4. 点击层按钮可打开该层的按键映射编辑对话框；点击「编辑公共层…」编辑公共层映射。
5. 全局设置滑块实时生效（死区、视角灵敏度、平滑、加速度）。
6. 保存配置后写入 `steamlike_config.json`；重置默认恢复 WoW 预设。
7. 最小化主窗口后，悬浮层信息窗继续显示，可拖动到任意位置。
