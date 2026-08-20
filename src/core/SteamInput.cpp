#include "SteamInput.h"

SteamInput::SteamInput(QObject* parent) : QObject(parent) {}

void SteamInput::loadProfile(const ControllerProfile& newProfile) {
    profile = newProfile;
    deactivateAllLayers();
    emit profileChanged();
}

void SteamInput::setGlobalSettings(const GlobalSettings& settings) {
    profile.globalSettings = settings;
    emit profileChanged();
}

// ---------------------------------------------------------------
// 层管理
// ---------------------------------------------------------------

void SteamInput::activateLayer(OperationLayer* layer) {
    if (!layer || layer->name.isEmpty() || layer->name == QStringLiteral("Common"))
        return;
    if (!activeLayers_.contains(layer)) {
        activeLayers_.append(layer);
        updateActiveLayerName();
    }
}

void SteamInput::activateLayer(const QString& name) {
    activateLayer(profile.findLayer(name));
}

void SteamInput::deactivateLayer(OperationLayer* layer) {
    if (!layer) return;
    const int removed = activeLayers_.removeAll(layer);
    if (removed > 0)
        updateActiveLayerName();
}

void SteamInput::deactivateLayer(const QString& name) {
    deactivateLayer(profile.findLayer(name));
}

void SteamInput::deactivateAllLayers() {
    if (activeLayers_.isEmpty()) {
        updateActiveLayerName();
        return;
    }
    activeLayers_.clear();
    buttonTriggeredLayers_.clear();
    updateActiveLayerName();
}

bool SteamInput::isLayerActive(const QString& name) const {
    for (const OperationLayer* layer : activeLayers_)
        if (layer->name == name) return true;
    return false;
}

void SteamInput::updateActiveLayerName() {
    QString name = QStringLiteral("Common");
    if (!activeLayers_.isEmpty())
        name = activeLayers_.last()->name;
    if (name != activeLayerName_) {
        activeLayerName_ = name;
        emit layerChanged(name);
    }
}

QVector<const OperationLayer*> SteamInput::getActiveLayers() const {
    QVector<const OperationLayer*> out;
    out.reserve(activeLayers_.size());
    for (const OperationLayer* layer : activeLayers_)
        out.append(layer);
    return out;
}

// ---------------------------------------------------------------
// 查询
// ---------------------------------------------------------------

const KeyMapping* SteamInput::getEffectiveMapping(ControllerButton button) const {
    // 已激活操作层（最后激活的最优先）
    for (int i = activeLayers_.size() - 1; i >= 0; --i) {
        if (const KeyMapping* m = activeLayers_[i]->getMapping(button))
            return m;
    }
    return profile.commonLayer.getMapping(button);
}

// ---------------------------------------------------------------
// 输入分发
// ---------------------------------------------------------------

void SteamInput::handleButtonEvent(ControllerButton button, bool isPressed) {
    if (isPressed)
        heldButtons_.insert(button);
    else
        heldButtons_.remove(button);

    // 松开时：若该按键激活了某个层，停用该层并返回（不触发映射）
    if (!isPressed) {
        const auto it = buttonTriggeredLayers_.find(button);
        if (it != buttonTriggeredLayers_.end()) {
            OperationLayer* triggered = it.value();
            buttonTriggeredLayers_.erase(it);
            deactivateLayer(triggered);
            return;
        }
    }

    const KeyMapping* mapping = getEffectiveMapping(button);
    if (!mapping) return;

    // 切换层动作由引擎处理
    if (mapping->action.type == MappedAction::Type::SwitchLayer) {
        if (isPressed) {
            OperationLayer* target = profile.findLayer(mapping->action.layerName);
            if (target && !isLayerActive(target->name)) {
                activateLayer(target);
                buttonTriggeredLayers_.insert(button, target);
            }
        }
        return;
    }

    emit buttonMapped(button, isPressed, *mapping);
}

void SteamInput::handleStickInput(ControllerStick stick, float x, float y) {
    const float dz = profile.globalSettings.deadzone;
    const Vector2 d = Vector2{x, y}.withDeadzone(dz);
    emit stickMapped(stick, d.x, d.y);
}
