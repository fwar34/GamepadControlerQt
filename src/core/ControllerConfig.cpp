// ============================================================
// ControllerConfig.cpp
// 配置序列化 / 反序列化（JSON）
// ------------------------------------------------------------
// 职责：把内存中的 ControllerProfile 与磁盘上的 JSON 配置互相转换。
// 配置文件名由 ConfigManager 决定（steamlike_config.json），
// 本文件只负责格式，不负责文件读写。
//
// 兼容性约定（重要）：
//   - 动作 type 字符串与字段名与安卓版保持一致，以便直接复用
//     安卓端导出的配置（version=2）。
//   - 鼠标按钮名使用大写（LEFT/RIGHT/...），与安卓版枚举一致。
//   - 层同时持久化 id 与 name：id 唯一固定用于运行时定位，
//     name 仅用于显示可任意改名。解析时若缺 id 字段则回退到 name
//     （兼容早期/安卓配置）。
//
// 容错策略：对无法识别的动作类型/按钮名/超限子命令，采取"跳过
// 该条映射"的宽松策略，而不是整体解析失败；只有 JSON 语法错误、
// 根节点不是对象、版本号不匹配才抛异常。
// ============================================================

#include "ControllerConfig.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

// ---- 动作 type 的字符串常量（与配置文件/安卓版一一对应）----
const QString kTypeKeyboard = QStringLiteral("keyboard");
const QString kTypeMouse = QStringLiteral("mouse");
const QString kTypeMouseToggle = QStringLiteral("mouseToggle");
const QString kTypeWheelUp = QStringLiteral("wheelUp");
const QString kTypeWheelDown = QStringLiteral("wheelDown");
const QString kTypeSwitchLayer = QStringLiteral("switchLayer");
const QString kTypeMouseMove = QStringLiteral("mouseMove");
const QString kTypeLookAround = QStringLiteral("lookAround");
const QString kTypeToggleMapping = QStringLiteral("toggleMapping");
const QString kTypeToggleOnScreenKeyboard = QStringLiteral("toggleOnScreenKeyboard");
const QString kTypeToggleOverlay = QStringLiteral("toggleOverlay");

// ============================================================
// actionToJson：MappedAction -> QJsonObject
// ============================================================
// 按动作类型写入对应的结构化字段（keyCode / button / layerName 等）。
QJsonObject actionToJson(const MappedAction& action) {
    QJsonObject json;
    switch (action.type) {
        case MappedAction::Type::KeyboardKey:
            json.insert(QStringLiteral("type"), kTypeKeyboard);
            json.insert(QStringLiteral("keyCode"), action.keyCode);
            break;
        case MappedAction::Type::MouseClick:
            json.insert(QStringLiteral("type"), kTypeMouse);
            json.insert(QStringLiteral("button"), mouseButtonName(action.mouseButton));
            break;
        case MappedAction::Type::MouseToggle:
            json.insert(QStringLiteral("type"), kTypeMouseToggle);
            json.insert(QStringLiteral("button"), mouseButtonName(action.mouseButton));
            break;
        case MappedAction::Type::WheelUp:
            json.insert(QStringLiteral("type"), kTypeWheelUp);
            break;
        case MappedAction::Type::WheelDown:
            json.insert(QStringLiteral("type"), kTypeWheelDown);
            break;
        case MappedAction::Type::SwitchLayer:
            json.insert(QStringLiteral("type"), kTypeSwitchLayer);
            json.insert(QStringLiteral("layerName"), action.layerName);
            break;
        case MappedAction::Type::MouseMove:
            json.insert(QStringLiteral("type"), kTypeMouseMove);
            break;
        case MappedAction::Type::LookAround:
            json.insert(QStringLiteral("type"), kTypeLookAround);
            break;
        case MappedAction::Type::ToggleMapping:
            json.insert(QStringLiteral("type"), kTypeToggleMapping);
            break;
        case MappedAction::Type::ToggleOnScreenKeyboard:
            json.insert(QStringLiteral("type"), kTypeToggleOnScreenKeyboard);
            break;
        case MappedAction::Type::ToggleOverlay:
            json.insert(QStringLiteral("type"), kTypeToggleOverlay);
            break;
    }
    return json;
}

// ============================================================
// parseAction：QJsonObject -> MappedAction
// ============================================================
// 解析单个动作；成功返回 true 并写 *out，失败返回 false。
// 失败原因可能是：未知 type、缺少必要字段、鼠标按钮名无法识别等。
bool parseAction(const QJsonObject& json, MappedAction* out) {
    const QString type = json.value(QStringLiteral("type")).toString();
    if (type == kTypeKeyboard) {
        const int keyCode = json.value(QStringLiteral("keyCode")).toInt(0);
        *out = MappedAction::keyboardKey(keyCode);
        return true;
    }
    if (type == kTypeMouse) {
        MouseButton b = MouseButton::LEFT;
        if (!mouseButtonFromName(json.value(QStringLiteral("button")).toString(), &b))
            return false;
        *out = MappedAction::mouseClick(b);
        return true;
    }
    if (type == kTypeMouseToggle) {
        MouseButton b = MouseButton::LEFT;
        if (!mouseButtonFromName(json.value(QStringLiteral("button")).toString(), &b))
            return false;
        *out = MappedAction::mouseToggle(b);
        return true;
    }
    if (type == kTypeWheelUp) {
        *out = MappedAction::wheelUp();
        return true;
    }
    if (type == kTypeWheelDown) {
        *out = MappedAction::wheelDown();
        return true;
    }
    if (type == kTypeSwitchLayer) {
        const QString name = json.value(QStringLiteral("layerName")).toString();
        if (name.isEmpty()) return false;   // 缺少目标层名视为无效
        *out = MappedAction::switchLayer(name);
        return true;
    }
    if (type == kTypeMouseMove) {
        *out = MappedAction::mouseMove();
        return true;
    }
    if (type == kTypeLookAround) {
        *out = MappedAction::lookAround();
        return true;
    }
    if (type == kTypeToggleMapping) {
        *out = MappedAction::toggleMapping();
        return true;
    }
    if (type == kTypeToggleOnScreenKeyboard) {
        *out = MappedAction::toggleOnScreenKeyboard();
        return true;
    }
    if (type == kTypeToggleOverlay) {
        *out = MappedAction::toggleOverlay();
        return true;
    }
    return false;  // 未知类型
}

// ============================================================
// mappingToJson：KeyMapping -> QJsonObject
// ============================================================
// 包含主动作 action 与子命令数组 subCommands（组合键）。
QJsonObject mappingToJson(const KeyMapping& mapping) {
    QJsonObject json;
    json.insert(QStringLiteral("action"), actionToJson(mapping.action));
    QJsonArray subArray;
    for (const int sub : mapping.subCommands)
        subArray.append(sub);
    json.insert(QStringLiteral("subCommands"), subArray);
    return json;
}

// ============================================================
// parseMapping：QJsonObject -> KeyMapping
// ============================================================
// 解析单条映射；失败返回 false（由调用方跳过该条）。
// 子命令数量超过 MAX_SUB_COMMANDS 时按无效处理（防御外部配置）。
bool parseMapping(const QJsonObject& json, KeyMapping* out) {
    const QJsonValue actionVal = json.value(QStringLiteral("action"));
    if (!actionVal.isObject()) return false;
    MappedAction action;
    if (!parseAction(actionVal.toObject(), &action)) return false;

    QVector<int> subs;
    const QJsonValue subVal = json.value(QStringLiteral("subCommands"));
    if (subVal.isArray()) {
        const QJsonArray arr = subVal.toArray();
        for (const QJsonValue& v : arr) {
            if (v.isDouble()) subs.append(v.toInt());
        }
    }
    if (subs.size() > KeyMapping::MAX_SUB_COMMANDS) return false;  // 子命令超限，跳过

    *out = KeyMapping{action, subs};
    return true;
}

// ============================================================
// layerToJson：OperationLayer -> QJsonObject
// ============================================================
// id 与 name 分开持久化：id 用于运行时正确定位层（支持重命名），
// name 仅用于显示。triggerButton 与 buttonMappings 一并保存。
QJsonObject layerToJson(const OperationLayer& layer) {
    QJsonObject json;
    // id 为空时回退写 name，保证老数据/手写数据也能有可读 id
    json.insert(QStringLiteral("id"), layer.id.isEmpty() ? layer.name : layer.id);
    json.insert(QStringLiteral("name"), layer.name);
    if (layer.hasTriggerButton)
        json.insert(QStringLiteral("triggerButton"), controllerButtonName(layer.triggerButton));
    QJsonObject mappings;
    for (auto it = layer.buttonMappings.constBegin(); it != layer.buttonMappings.constEnd(); ++it)
        mappings.insert(controllerButtonName(it.key()), mappingToJson(it.value()));
    json.insert(QStringLiteral("buttonMappings"), mappings);
    return json;
}

// ============================================================
// parseLayer：QJsonObject -> OperationLayer
// ============================================================
// 解析一个层。isCommon 指示是否公共层（公共层不解析 triggerButton）。
// id 缺失时回退到 name（兼容旧配置/安卓配置），公共层固定为 "Common"。
OperationLayer parseLayer(const QJsonObject& json, bool isCommon) {
    OperationLayer layer;
    const QString name = json.value(QStringLiteral("name")).toString();
    layer.name = name;
    // id：优先读取持久化的 id；缺失时回退到 name（兼容旧配置/安卓配置）
    layer.id = json.value(QStringLiteral("id")).toString();
    if (layer.id.isEmpty())
        layer.id = isCommon ? QStringLiteral("Common") : name;

    // 触发按键：仅操作层有（公共层的触发信息由各操作层自持，供 UI 显示）
    if (!isCommon) {
        ControllerButton tb = ControllerButton::A;
        if (controllerButtonFromName(json.value(QStringLiteral("triggerButton")).toString(), &tb)) {
            layer.hasTriggerButton = true;
            layer.triggerButton = tb;
        }
    }

    // 按钮映射表：逐条解析，非法条目直接跳过
    const QJsonValue mappingsVal = json.value(QStringLiteral("buttonMappings"));
    if (mappingsVal.isObject()) {
        const QJsonObject mappings = mappingsVal.toObject();
        for (auto it = mappings.constBegin(); it != mappings.constEnd(); ++it) {
            ControllerButton button;
            if (!controllerButtonFromName(it.key(), &button)) continue;  // 未知按钮名
            if (!it.value().isObject()) continue;                        // 结构非法
            KeyMapping mapping;
            if (parseMapping(it.value().toObject(), &mapping))
                layer.buttonMappings.insert(button, mapping);
        }
    }
    return layer;
}

}  // namespace

namespace ControllerConfig {

// ============================================================
// toJson：ControllerProfile -> JSON 字节串
// ============================================================
// 组装根对象：版本号 + 全局设置 + 当前激活操作集 + 操作集数组。
// 每个操作集含 id/name/commonLayer/layers。
// 返回格式化（带缩进）的 JSON，便于用户直接查看/手工编辑。
QByteArray toJson(const ControllerProfile& profile, int indent) {
    QJsonObject root;
    root.insert(QStringLiteral("version"), CONFIG_VERSION);

    // 全局设置：死区 / 视角灵敏度 / 光标速度 / 平滑 / 加速
    QJsonObject gs;
    gs.insert(QStringLiteral("deadzone"), static_cast<double>(profile.globalSettings.deadzone));
    gs.insert(QStringLiteral("lookSensitivity"), static_cast<double>(profile.globalSettings.lookSensitivity));
    gs.insert(QStringLiteral("cursorSpeed"), static_cast<double>(profile.globalSettings.cursorSpeed));
    gs.insert(QStringLiteral("lookSmoothing"), static_cast<double>(profile.globalSettings.lookSmoothing));
    gs.insert(QStringLiteral("lookAcceleration"), static_cast<double>(profile.globalSettings.lookAcceleration));
    gs.insert(QStringLiteral("invertLookX"), profile.globalSettings.invertLookX);
    gs.insert(QStringLiteral("invertLookY"), profile.globalSettings.invertLookY);
    gs.insert(QStringLiteral("overlayX"), profile.globalSettings.overlayX);
    gs.insert(QStringLiteral("overlayY"), profile.globalSettings.overlayY);
    gs.insert(QStringLiteral("overlayScale"), profile.globalSettings.overlayScale);
    gs.insert(QStringLiteral("mainWindowX"), profile.globalSettings.mainWindowX);
    gs.insert(QStringLiteral("mainWindowY"), profile.globalSettings.mainWindowY);
    gs.insert(QStringLiteral("releaseOnForegroundChange"), profile.globalSettings.releaseOnForegroundChange);
    gs.insert(QStringLiteral("confirmOnClose"), profile.globalSettings.confirmOnClose);
    root.insert(QStringLiteral("globalSettings"), gs);

    // 当前激活操作集
    root.insert(QStringLiteral("activeOperationSet"), profile.activeOperationSetId);

    // 操作集数组
    QJsonArray sets;
    for (const OperationSet& set : profile.operationSets) {
        QJsonObject s;
        s.insert(QStringLiteral("id"), set.id);
        s.insert(QStringLiteral("name"), set.name);
        s.insert(QStringLiteral("commonLayer"), layerToJson(set.commonLayer));
        QJsonArray layers;
        for (const OperationLayer& layer : set.layers)
            layers.append(layerToJson(layer));
        s.insert(QStringLiteral("layers"), layers);
        sets.append(s);
    }
    root.insert(QStringLiteral("operationSets"), sets);

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

// ============================================================
// fromJson：JSON 字节串 -> ControllerProfile
// ============================================================
// 严格校验：语法错误 / 非对象根 / 版本不匹配时抛 std::runtime_error。
// 字段级容错（缺全局设置、缺公共层、某条映射非法等）则取默认/跳过。
ControllerProfile fromJson(const QByteArray& json) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        throw std::runtime_error("Invalid JSON: " + err.errorString().toStdString());

    const QJsonObject root = doc.object();
    const int version = root.value(QStringLiteral("version")).toInt(1);
    if (version != CONFIG_VERSION)
        throw std::runtime_error("Unsupported config version: " + std::to_string(version));

    ControllerProfile profile;
    // 全局设置：可缺省，逐字段带默认值读取
    profile.globalSettings = GlobalSettings();
    if (root.value(QStringLiteral("globalSettings")).isObject()) {
        const QJsonObject gs = root.value(QStringLiteral("globalSettings")).toObject();
        GlobalSettings s;
        s.deadzone = static_cast<float>(gs.value(QStringLiteral("deadzone")).toDouble(0.15));
        s.lookSensitivity = static_cast<float>(gs.value(QStringLiteral("lookSensitivity")).toDouble(0.5));
        s.cursorSpeed = static_cast<float>(gs.value(QStringLiteral("cursorSpeed")).toDouble(1.0));
        s.lookSmoothing = static_cast<float>(gs.value(QStringLiteral("lookSmoothing")).toDouble(0.5));
        s.lookAcceleration = static_cast<float>(gs.value(QStringLiteral("lookAcceleration")).toDouble(1.5));
        s.invertLookX = gs.value(QStringLiteral("invertLookX")).toBool(false);
        s.invertLookY = gs.value(QStringLiteral("invertLookY")).toBool(false);
        s.overlayX = gs.value(QStringLiteral("overlayX")).toInt(-1);
        s.overlayY = gs.value(QStringLiteral("overlayY")).toInt(-1);
        s.overlayScale = gs.value(QStringLiteral("overlayScale")).toDouble(1.0);
        s.mainWindowX = gs.value(QStringLiteral("mainWindowX")).toInt(-1);
        s.mainWindowY = gs.value(QStringLiteral("mainWindowY")).toInt(-1);
        s.releaseOnForegroundChange = gs.value(QStringLiteral("releaseOnForegroundChange")).toBool(true);
        s.confirmOnClose = gs.value(QStringLiteral("confirmOnClose")).toBool(true);
        profile.globalSettings = s;
    }

    // 公共层/操作层改为按操作集组织：
    //   - 新格式：根节点 operationSets 数组（含 activeOperationSet 指定激活集）；
    //   - 旧 v2 格式兼容：根节点仅有顶层 commonLayer/layers 时，
    //     自动包装成单个「默认操作集」（Set1），保证旧配置无缝升级。
    const QJsonValue setsVal = root.value(QStringLiteral("operationSets"));
    if (setsVal.isArray() && !setsVal.toArray().isEmpty()) {
        const QJsonArray arr = setsVal.toArray();
        for (const QJsonValue& v : arr) {
            if (!v.isObject()) continue;
            const QJsonObject so = v.toObject();
            OperationSet set;
            set.id = so.value(QStringLiteral("id")).toString();
            set.name = so.value(QStringLiteral("name")).toString();
            if (set.id.isEmpty())
                set.id = set.name.isEmpty() ? QStringLiteral("Set") : set.name;
            // 本操作集的公共层
            const QJsonValue cVal = so.value(QStringLiteral("commonLayer"));
            set.commonLayer = cVal.isObject()
                ? parseLayer(cVal.toObject(), /*isCommon=*/true)
                : OperationLayer(QStringLiteral("Common"));
            // 本操作集的操作层数组
            if (so.value(QStringLiteral("layers")).isArray()) {
                const QJsonArray larr = so.value(QStringLiteral("layers")).toArray();
                for (const QJsonValue& lv : larr) {
                    if (lv.isObject())
                        set.layers.append(parseLayer(lv.toObject(), /*isCommon=*/false));
                }
            }
            profile.operationSets.append(set);
        }
        // 恢复上次激活的操作集；缺失时回退到第一个
        if (!profile.setActiveOperationSet(root.value(QStringLiteral("activeOperationSet")).toString())
            && !profile.operationSets.isEmpty())
            profile.activeOperationSetId = profile.operationSets.first().id;
    } else {
        // ---- 旧 v2 格式兼容：包装为单个「默认操作集」 ----
        OperationSet set;
        set.id = QStringLiteral("Set1");
        set.name = QStringLiteral("默认操作集");
        const QJsonValue commonVal = root.value(QStringLiteral("commonLayer"));
        set.commonLayer = commonVal.isObject()
            ? parseLayer(commonVal.toObject(), /*isCommon=*/true)
            : OperationLayer(QStringLiteral("Common"));
        if (root.value(QStringLiteral("layers")).isArray()) {
            const QJsonArray arr = root.value(QStringLiteral("layers")).toArray();
            for (const QJsonValue& v : arr) {
                if (v.isObject())
                    set.layers.append(parseLayer(v.toObject(), /*isCommon=*/false));
            }
        }
        profile.operationSets.append(set);
        profile.activeOperationSetId = set.id;
    }

    // ---- 兜底保证：至少保留一个有效操作集 ----
    // operationSets 数组为空或元素全无效（解析时被跳过）时，
    // profile.operationSets 可能为空，此时 activeSet() 返回 nullptr，
    // 下游 commonLayer()/layers() 解引用将导致未定义行为（崩溃）。
    // 这里兜底创建一个默认操作集并指向它，保证返回的 profile 始终有效。
    if (profile.operationSets.isEmpty()) {
        OperationSet fallback = OperationSet::createEmpty(
            QStringLiteral("Set1"), QStringLiteral("默认操作集"));
        profile.operationSets.append(fallback);
        profile.activeOperationSetId = fallback.id;
    }

    return profile;
}

}  // namespace ControllerConfig
