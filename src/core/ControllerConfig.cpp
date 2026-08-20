#include "ControllerConfig.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

const QString kTypeKeyboard = QStringLiteral("keyboard");
const QString kTypeMouse = QStringLiteral("mouse");
const QString kTypeMouseToggle = QStringLiteral("mouseToggle");
const QString kTypeSwitchLayer = QStringLiteral("switchLayer");
const QString kTypeMouseMove = QStringLiteral("mouseMove");
const QString kTypeLookAround = QStringLiteral("lookAround");

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
    }
    return json;
}

// 解析动作；失败返回 false
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
    if (type == kTypeSwitchLayer) {
        const QString name = json.value(QStringLiteral("layerName")).toString();
        if (name.isEmpty()) return false;
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
    return false;  // 未知类型
}

QJsonObject mappingToJson(const KeyMapping& mapping) {
    QJsonObject json;
    json.insert(QStringLiteral("action"), actionToJson(mapping.action));
    QJsonArray subArray;
    for (const int sub : mapping.subCommands)
        subArray.append(sub);
    json.insert(QStringLiteral("subCommands"), subArray);
    return json;
}

// 解析单个映射；失败返回 false
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

QJsonObject layerToJson(const OperationLayer& layer) {
    QJsonObject json;
    json.insert(QStringLiteral("name"), layer.name);
    if (layer.hasTriggerButton)
        json.insert(QStringLiteral("triggerButton"), controllerButtonName(layer.triggerButton));
    QJsonObject mappings;
    for (auto it = layer.buttonMappings.constBegin(); it != layer.buttonMappings.constEnd(); ++it)
        mappings.insert(controllerButtonName(it.key()), mappingToJson(it.value()));
    json.insert(QStringLiteral("buttonMappings"), mappings);
    return json;
}

OperationLayer parseLayer(const QJsonObject& json, bool isCommon) {
    OperationLayer layer;
    layer.name = json.value(QStringLiteral("name")).toString();

    if (!isCommon) {
        ControllerButton tb = ControllerButton::A;
        if (controllerButtonFromName(json.value(QStringLiteral("triggerButton")).toString(), &tb)) {
            layer.hasTriggerButton = true;
            layer.triggerButton = tb;
        }
    }

    const QJsonValue mappingsVal = json.value(QStringLiteral("buttonMappings"));
    if (mappingsVal.isObject()) {
        const QJsonObject mappings = mappingsVal.toObject();
        for (auto it = mappings.constBegin(); it != mappings.constEnd(); ++it) {
            ControllerButton button;
            if (!controllerButtonFromName(it.key(), &button)) continue;
            if (!it.value().isObject()) continue;
            KeyMapping mapping;
            if (parseMapping(it.value().toObject(), &mapping))
                layer.buttonMappings.insert(button, mapping);
        }
    }
    return layer;
}

}  // namespace

namespace ControllerConfig {

QByteArray toJson(const ControllerProfile& profile, int indent) {
    QJsonObject root;
    root.insert(QStringLiteral("version"), CONFIG_VERSION);

    QJsonObject gs;
    gs.insert(QStringLiteral("deadzone"), static_cast<double>(profile.globalSettings.deadzone));
    gs.insert(QStringLiteral("lookSensitivity"), static_cast<double>(profile.globalSettings.lookSensitivity));
    gs.insert(QStringLiteral("cursorSpeed"), static_cast<double>(profile.globalSettings.cursorSpeed));
    gs.insert(QStringLiteral("lookSmoothing"), static_cast<double>(profile.globalSettings.lookSmoothing));
    gs.insert(QStringLiteral("lookAcceleration"), static_cast<double>(profile.globalSettings.lookAcceleration));
    root.insert(QStringLiteral("globalSettings"), gs);

    root.insert(QStringLiteral("commonLayer"), layerToJson(profile.commonLayer));

    QJsonArray layers;
    for (const OperationLayer& layer : profile.layers)
        layers.append(layerToJson(layer));
    root.insert(QStringLiteral("layers"), layers);

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

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
    profile.globalSettings = GlobalSettings();
    if (root.value(QStringLiteral("globalSettings")).isObject()) {
        const QJsonObject gs = root.value(QStringLiteral("globalSettings")).toObject();
        GlobalSettings s;
        s.deadzone = static_cast<float>(gs.value(QStringLiteral("deadzone")).toDouble(0.15));
        s.lookSensitivity = static_cast<float>(gs.value(QStringLiteral("lookSensitivity")).toDouble(0.5));
        s.cursorSpeed = static_cast<float>(gs.value(QStringLiteral("cursorSpeed")).toDouble(1.0));
        s.lookSmoothing = static_cast<float>(gs.value(QStringLiteral("lookSmoothing")).toDouble(0.5));
        s.lookAcceleration = static_cast<float>(gs.value(QStringLiteral("lookAcceleration")).toDouble(1.5));
        profile.globalSettings = s;
    }

    const QJsonValue commonVal = root.value(QStringLiteral("commonLayer"));
    profile.commonLayer = commonVal.isObject()
        ? parseLayer(commonVal.toObject(), /*isCommon=*/true)
        : OperationLayer(QStringLiteral("Common"));

    if (root.value(QStringLiteral("layers")).isArray()) {
        const QJsonArray arr = root.value(QStringLiteral("layers")).toArray();
        profile.layers.clear();
        for (const QJsonValue& v : arr) {
            if (v.isObject())
                profile.layers.append(parseLayer(v.toObject(), /*isCommon=*/false));
        }
    }

    return profile;
}

}  // namespace ControllerConfig
