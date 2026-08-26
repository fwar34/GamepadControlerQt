#pragma once

#include "InputTypes.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

// =====================================================================
// 映射数据模型
//
// 等效安卓版 MappedAction / KeyMapping / OperationLayer /
// GlobalSettings / ControllerProfile。
//
// 三层结构总览（从高到低）：
//   操作集（OperationSet，如"战斗"/"骑乘"）——
//     最顶层容器，由 1 个公共层 + 最多 10 个操作层组成；
//     切换操作集时，其下所有操作层整体切换（配置互不干扰）。
//   公共层（commonLayer）—— 始终激活，优先度最低，作为兜底映射；
//   操作层（layers）—— 通过公共层的 SwitchLayer 映射按住激活、松开回退，
//     优先度高于公共层。
//  按键查询顺序（getEffectiveMapping）：最后激活的操作层 -> ... -> 公共层。
//  运行时只针对「当前激活的操作集」查询，操作集之间互不影响。
// =====================================================================

// ---------------------------------------------------------------------
// MappedAction —— 映射动作
//
// 对应按键/摇杆在按下时执行的动作类型，等效安卓版 sealed class
// MappedAction。通过静态工厂方法构造，type 决定实际生效的字段。
// ---------------------------------------------------------------------
struct MappedAction {
    enum class Type {
        KeyboardKey,            // 键盘按键（keyCode）
        MouseClick,             // 鼠标单击（mouseButton，按下/松开跟随手柄）
        SwitchLayer,            // 切换操作层（layerName，按住激活/松开回退）
        MouseMove,              // 鼠标移动（摇杆动作，无需额外参数）
        LookAround,             // 视角控制（摇杆动作，右摇杆，独立线程节拍处理）
        MouseToggle,            // 鼠标长按锁存（按住期间保持按下，松开不改变状态）
        WheelUp,                // 鼠标滚轮上滚（按下时向上滚动一格）
        WheelDown,              // 鼠标滚轮下滚（按下时向下滚动一格）
        ToggleMapping,          // 切换映射启停（按下时触发）
        ToggleOnScreenKeyboard, // 切换 Windows 屏幕键盘（按下时触发）
        ToggleOverlay           // 切换悬浮窗展开/收起（按下时触发）
    };

    Type type = Type::MouseMove;
    int keyCode = 0;                                  // KeyboardKey 专用
    MouseButton mouseButton = MouseButton::LEFT;      // MouseClick / MouseToggle 专用
    QString layerName;                                // SwitchLayer 专用（目标层 id）

    // 构造「键盘按键」动作
    static MappedAction keyboardKey(int code) {
        MappedAction a;
        a.type = Type::KeyboardKey;
        a.keyCode = code;
        return a;
    }
    // 构造「鼠标单击」动作
    static MappedAction mouseClick(MouseButton b) {
        MappedAction a;
        a.type = Type::MouseClick;
        a.mouseButton = b;
        return a;
    }
    // 构造「鼠标长按锁存」动作
    static MappedAction mouseToggle(MouseButton b) {
        MappedAction a;
        a.type = Type::MouseToggle;
        a.mouseButton = b;
        return a;
    }
    // 构造「鼠标滚轮上滚」动作
    static MappedAction wheelUp() {
        MappedAction a;
        a.type = Type::WheelUp;
        return a;
    }
    // 构造「鼠标滚轮下滚」动作
    static MappedAction wheelDown() {
        MappedAction a;
        a.type = Type::WheelDown;
        return a;
    }
    // 构造「切换操作层」动作（target 为目标层 id，如 "Layer1"）
    static MappedAction switchLayer(const QString& name) {
        MappedAction a;
        a.type = Type::SwitchLayer;
        a.layerName = name;
        return a;
    }
    // 构造「鼠标移动」动作
    static MappedAction mouseMove() {
        MappedAction a;
        a.type = Type::MouseMove;
        return a;
    }
    // 构造「视角控制」动作
    static MappedAction lookAround() {
        MappedAction a;
        a.type = Type::LookAround;
        return a;
    }
    // 构造「切换映射启停」动作
    static MappedAction toggleMapping() {
        MappedAction a;
        a.type = Type::ToggleMapping;
        return a;
    }
    // 构造「切换屏幕键盘」动作
    static MappedAction toggleOnScreenKeyboard() {
        MappedAction a;
        a.type = Type::ToggleOnScreenKeyboard;
        return a;
    }
    // 构造「切换悬浮窗」动作
    static MappedAction toggleOverlay() {
        MappedAction a;
        a.type = Type::ToggleOverlay;
        return a;
    }

    bool operator==(const MappedAction& o) const {
        if (type != o.type) return false;
        switch (type) {
            case Type::KeyboardKey: return keyCode == o.keyCode;
            case Type::MouseClick:
            case Type::MouseToggle: return mouseButton == o.mouseButton;
            case Type::SwitchLayer: return layerName == o.layerName;
            default: return true;  // MouseMove/LookAround/ToggleMapping/ToggleOnScreenKeyboard/ToggleOverlay
        }
    }
    bool operator!=(const MappedAction& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------
// KeyMapping —— 单个按键映射
//
// 由 1 个主动作（action）+ 最多 3 个子命令（subCommands）组成：
//  按下主键时：先注入主动作，再依次按下各子命令；
//  松开时：逆序释放子命令，再释放主动作。
// 典型用途：将手柄按键映射为 "Alt+3" 之类的组合键，
// 其中 action=KeyboardKey(3)，subCommands=[ALT_LEFT]。
// ---------------------------------------------------------------------
struct KeyMapping {
    MappedAction action;
    QVector<int> subCommands;   // Android KeyCode 列表，最多 MAX_SUB_COMMANDS 个

    static constexpr int MAX_SUB_COMMANDS = 3;

    // 生成可读描述字符串，如 "W+ALT"
    QString describe() const;
    bool operator==(const KeyMapping& o) const {
        return action == o.action && subCommands == o.subCommands;
    }
    bool operator!=(const KeyMapping& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------
// OperationLayer —— 操作层（一组按键映射）
//
//  - id：唯一标识符，固定不变（如 "Layer1"、"Common"）。
//    层切换、按钮查找等运行时逻辑都基于 id，与显示名称解耦。
//  - name：显示名称，用户可在编辑对话框中自由修改，
//    不影响任何运行时逻辑（运行时只认 id）。
//  - hasTriggerButton / triggerButton：仅用于 UI 展示说明（如编辑对话框
//    中标注"按住 LB 激活本层"），不参与运行时层切换判定。
//    实际层切换完全由公共层的 SwitchLayer 映射驱动。
// ---------------------------------------------------------------------
class OperationLayer {
public:
    QString id; // 唯一标识符，固定不变（如"Layer1"）
    QString name; // 显示名称，用户可修改
    bool hasTriggerButton = false;
    ControllerButton triggerButton = ControllerButton::A;
    QHash<ControllerButton, KeyMapping> buttonMappings;   // 按钮 -> 映射

    OperationLayer() = default;
    explicit OperationLayer(const QString& layerName) : id(layerName), name(layerName) {}

    // 查询某按钮的映射；不存在返回 nullptr（const 版本）
    const KeyMapping* getMapping(ControllerButton b) const {
        const auto it = buttonMappings.constFind(b);
        return it == buttonMappings.constEnd() ? nullptr : &it.value();
    }
    KeyMapping* getMapping(ControllerButton b) {
        auto it = buttonMappings.find(b);
        return it == buttonMappings.end() ? nullptr : &it.value();
    }
};

// ---------------------------------------------------------------------
// GlobalSettings —— 全局设置
// 数值含义见 MainWindow 滑块的换算关系：
//  - deadzone        摇杆死区 0~1（滑块 0~50 → /100）
//  - lookSensitivity 视角灵敏度（滑块 10~200 → /100）
//  - cursorSpeed     光标速度，当前固定 1.0（预留）
//  - lookSmoothing   视角平滑系数（滑块 0~100 → /100，
//                    决定时间常数 tau = smoothing * 0.048s）
//  - lookAcceleration 视角加速度曲线指数（滑块 100~300 → /100）
// ---------------------------------------------------------------------
struct GlobalSettings {
    float deadzone = 0.15f;          // 摇杆死区
    float lookSensitivity = 0.5f;    // 视角灵敏度
    float cursorSpeed = 1.0f;        // 光标速度（预留）
    float lookSmoothing = 0.5f;      // 视角平滑
    float lookAcceleration = 1.5f;   // 视角加速度曲线
    bool invertLookX = false;        // 右摇杆 X 轴反转
    bool invertLookY = false;        // 右摇杆 Y 轴反转
    int overlayX = -1;               // 悬浮窗 X 坐标（-1 表示未设置，使用默认位置）
    int overlayY = -1;               // 悬浮窗 Y 坐标（-1 表示未设置，使用默认位置）
    double overlayScale = 1.0;       // 悬浮窗缩放系数（滚轮调整，0.5 ~ 2.0）
    int mainWindowX = -1;            // 主窗口 X 坐标（-1 表示未设置，使用默认位置）
    int mainWindowY = -1;            // 主窗口 Y 坐标（-1 表示未设置，使用默认位置）
    bool releaseOnForegroundChange = true;  // 切换前台窗口时释放所有按键
    bool confirmOnClose = true;             // 关闭时弹出确认对话框（退出/最小化）
};

// 单个操作集内最多操作层数
constexpr int kMaxLayersPerSet = 10;

// ---------------------------------------------------------------------
// OperationSet —— 操作集（最顶层容器）
//
// 一组完整的映射配置：1 个公共层 + 最多 kMaxLayersPerSet 个操作层。
//  - id：唯一标识符，固定不变（如 "Set1"），运行时定位用；
//  - name：显示名称，用户可自由修改。
// 切换操作集 = 整体切换其下所有层；运行时层查询只针对当前激活操作集。
// ---------------------------------------------------------------------
class OperationSet {
public:
    QString id;                          // 唯一标识符（"Set1"、"Set2"...）
    QString name;                        // 显示名称，可自定义
    OperationLayer commonLayer;          // 本操作集的公共层
    QVector<OperationLayer> layers;      // 本操作集的操作层

    OperationSet() = default;

    // 创建一个全新的空操作集：空公共层 + kMaxLayersPerSet 个空操作层（默认名）
    static OperationSet createEmpty(const QString& setId, const QString& setName) {
        OperationSet set;
        set.id = setId;
        set.name = setName;
        set.commonLayer = OperationLayer(QStringLiteral("Common"));
        set.commonLayer.name = QStringLiteral("Common");
        const char* layerIds[kMaxLayersPerSet] = {
            "Layer1", "Layer2", "Layer3", "Layer4", "Layer5",
            "Layer6", "Layer7", "Layer8", "Layer9", "Layer10",
        };
        for (int i = 0; i < kMaxLayersPerSet; ++i) {
            OperationLayer layer(QString::fromLatin1(layerIds[i]));
            layer.name = layerDisplayName(layer.id);
            set.layers.append(layer);
        }
        return set;
    }
};

// ---------------------------------------------------------------------
// ControllerProfile —— 完整配置（整个配置文件对应一个 ControllerProfile）
//
// 结构：operationSets（操作集列表）+ activeOperationSetId（当前激活操作集）
//      + globalSettings（全局设置）。
// 配置通过 ControllerConfig 序列化为 JSON（version=2）持久化。
// ---------------------------------------------------------------------
class ControllerProfile {
public:
    QVector<OperationSet> operationSets;   // 操作集列表（至少 1 个）
    QString activeOperationSetId;          // 当前激活的操作集 id
    GlobalSettings globalSettings;

    static constexpr int MAX_LAYERS = kMaxLayersPerSet;   // 单个操作集内最多操作层数

    // 当前激活的操作集（无有效激活集时返回 nullptr）
    OperationSet* activeSet() {
        for (OperationSet& s : operationSets)
            if (s.id == activeOperationSetId) return &s;
        return operationSets.isEmpty() ? nullptr : &operationSets.first();
    }
    const OperationSet* activeSet() const {
        for (const OperationSet& s : operationSets)
            if (s.id == activeOperationSetId) return &s;
        return operationSets.isEmpty() ? nullptr : &operationSets.first();
    }

    // 当前激活操作集的公共层 / 操作层（快捷访问）
    OperationLayer* commonLayer() { return activeSet() ? &activeSet()->commonLayer : nullptr; }
    const OperationLayer* commonLayer() const {
        return activeSet() ? &activeSet()->commonLayer : nullptr;
    }
    // 无激活集（operationSets 为空）时返回空引用，避免解引用空指针
    QVector<OperationLayer>& layers() {
        static QVector<OperationLayer> empty;
        OperationSet* set = activeSet();
        return set ? set->layers : empty;
    }
    const QVector<OperationLayer>& layers() const {
        static QVector<OperationLayer> empty;
        const OperationSet* set = activeSet();
        return set ? set->layers : empty;
    }

    // 当前激活操作集的显示名（无激活集时返回空串）
    QString activeOperationSetName() const {
        const OperationSet* s = activeSet();
        return s ? s->name : QString();
    }

    // 按 id 设置当前激活操作集；无效 id 返回 false
    bool setActiveOperationSet(const QString& id) {
        for (const OperationSet& s : operationSets)
            if (s.id == id) { activeOperationSetId = id; return true; }
        return false;
    }

    // 生成一个不与现有操作集重复的新 id（"Set1"、"Set2"...）
    QString uniqueOperationSetId() const {
        int max = 0;
        for (const OperationSet& s : operationSets) {
            bool ok = false;
            const int n = s.id.mid(3).toInt(&ok);
            if (ok && n > max) max = n;
        }
        return QStringLiteral("Set%1").arg(max + 1);
    }

    // 按 id 查找层（仅当前激活操作集内，含公共层）；不存在返回 nullptr
    OperationLayer* findLayer(const QString& id) {
        OperationSet* set = activeSet();
        if (!set) return nullptr;
        if (set->commonLayer.id == id) return &set->commonLayer;
        for (OperationLayer& l : set->layers)
            if (l.id == id) return &l;
        return nullptr;
    }
    const OperationLayer* findLayer(const QString& id) const {
        const OperationSet* set = activeSet();
        if (!set) return nullptr;
        if (set->commonLayer.id == id) return &set->commonLayer;
        for (const OperationLayer& l : set->layers)
            if (l.id == id) return &l;
        return nullptr;
    }
    // 由触发按键找操作层（当前激活操作集内，仅供 UI 展示，不参与运行时切换）
    OperationLayer* findLayerByTrigger(ControllerButton b) {
        OperationSet* set = activeSet();
        if (!set) return nullptr;
        for (OperationLayer& l : set->layers)
            if (l.hasTriggerButton && l.triggerButton == b) return &l;
        return nullptr;
    }

    // 生成默认配置（WoW 预设：1 个"默认操作集"，含公共层 + 10 个操作层）
    static ControllerProfile createDefault();
};
