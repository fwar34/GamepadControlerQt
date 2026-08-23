#include "SteamInput.h"

// =====================================================================
// SteamInput —— 映射引擎实现
//
// 负责三件事：
//   1. 层管理：维护 activeLayers_ 栈 + buttonTriggeredLayers_ 映射
//   2. 按键查询：getEffectiveMapping 按「激活层 -> 公共层」顺序查找
//   3. 输入分发：handleButtonEvent 处理 SwitchLayer，其余广播 buttonMapped
// =====================================================================

SteamInput::SteamInput(QObject* parent) : QObject(parent) {}

// 整体替换配置（启动加载、重置默认时调用）
void SteamInput::loadProfile(const ControllerProfile& newProfile) {
    profile = newProfile;
    deactivateAllLayers();   // 配置变更后清空所有激活层，回到公共层
    emit profileChanged();   // 通知映射器/界面同步
}

// 仅更新全局设置（界面滑块实时调整时调用，避免打断进行中的层切换）
void SteamInput::setGlobalSettings(const GlobalSettings& settings) {
    profile.globalSettings = settings;
    emit profileChanged();
}

// ---------------------------------------------------------------
// 层管理
// ---------------------------------------------------------------

// 激活一个操作层（追加到栈顶，优先级最高）。
// 忽略空名/Common（公共层不可"激活"）；重复激活同一层被忽略。
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

// 停用一个操作层（从栈中移除所有匹配项）
void SteamInput::deactivateLayer(OperationLayer* layer) {
    if (!layer) return;
    const int removed = activeLayers_.removeAll(layer);
    if (removed > 0)
        updateActiveLayerName();
}

void SteamInput::deactivateLayer(const QString& name) {
    deactivateLayer(profile.findLayer(name));
}

// 停用所有操作层，回到公共层；同时清空触发层记录
void SteamInput::deactivateAllLayers() {
    if (activeLayers_.isEmpty()) {
        updateActiveLayerName();
        return;
    }
    activeLayers_.clear();
    buttonTriggeredLayers_.clear();
    updateActiveLayerName();
}

// 指定层当前是否激活
bool SteamInput::isLayerActive(const QString& name) const {
    for (const OperationLayer* layer : activeLayers_)
        if (layer->name == name) return true;
    return false;
}

// 重新计算当前激活层名（栈顶层的显示名；无激活层时为 "Common"），
// 变化时发出 layerChanged 信号（界面与悬浮窗据此更新）
void SteamInput::updateActiveLayerName() {
    QString name = QStringLiteral("Common");
    if (!activeLayers_.isEmpty())
        name = activeLayers_.last()->name;
    if (name != activeLayerName_) {
        activeLayerName_ = name;
        emit layerChanged(name);
    }
}

// 返回当前激活层列表（按激活顺序，公共层不在其中）
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

// 查询按钮在当前层栈下的有效映射：
//   从最后激活的操作层（栈顶，优先级最高）开始，逐层回退到公共层，
//   返回第一个命中的映射；全部未命中返回 nullptr（不注入任何事件）。
const KeyMapping* SteamInput::getEffectiveMapping(ControllerButton button) const {
    for (int i = activeLayers_.size() - 1; i >= 0; --i) {
        if (const KeyMapping* m = activeLayers_[i]->getMapping(button))
            return m;
    }
    return profile.commonLayer.getMapping(button);
}

// ---------------------------------------------------------------
// 输入分发
// ---------------------------------------------------------------

// 手柄按钮事件入口。
// 按下：记录到 heldButtons_，查询映射；SwitchLayer 激活目标层；
//       其余动作广播 buttonMapped（由 KeyboardMouseMapper 执行注入）。
// 松开：若该按键此前激活过某层（在 buttonTriggeredLayers_ 中），
//       则停用对应层并直接返回（不触发映射，避免"松开切层键"误触发动作）。
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

    // 查询当前层栈下的有效映射；无映射则不产生任何事件
    const KeyMapping* mapping = getEffectiveMapping(button);
    if (!mapping) {
        // 松开时：即使该按钮在「松开时刻」的层栈下已无映射，仍要广播松开事件，
        // 让映射器按「已注入状态」精确释放（防止此前注入的按键卡死）。
        // 例：按住切层键激活 Down 层期间按下按钮，先松开切层键导致层回退，
        //     再松开该按钮时当前层无其映射 —— 若丢弃松开事件，此前注入的按键会永久卡住。
        if (!isPressed)
            emit buttonMapped(button, false, KeyMapping());
        return;
    }

    // 切换层动作由引擎处理：按住激活目标层并记录触发按钮，
    // 松开时（上面分支）停用该层。注意：这里不再广播 buttonMapped。
    if (mapping->action.type == MappedAction::Type::SwitchLayer) {
        if (isPressed) {
            // 先按 id 查找；找不到则按 name 查找（兼容旧配置）
            OperationLayer* target = profile.findLayer(mapping->action.layerName);
            if (!target) {
                for (OperationLayer& l : profile.layers)
                    if (l.name == mapping->action.layerName) { target = &l; break; }
            }
            if (target && !isLayerActive(target->name)) {
                activateLayer(target);
                buttonTriggeredLayers_.insert(button, target);
            }
        }
        return;
    }

    // 切换类动作：仅在按下时触发，松开忽略
    if (isPressed) {
        switch (mapping->action.type) {
            case MappedAction::Type::ToggleMapping:
                emit toggleMappingRequested();
                return;
            case MappedAction::Type::ToggleOnScreenKeyboard:
                emit toggleOnScreenKeyboardRequested();
                return;
            case MappedAction::Type::ToggleOverlay:
                emit toggleOverlayRequested();
                return;
            default:
                break;
        }
    }

    // 其余动作（键盘/鼠标/视角等）交给映射器执行
    emit buttonMapped(button, isPressed, *mapping);
}

// 摇杆输入入口：先应用全局死区（withDeadzone），再广播 stickMapped。
// 死区采用缩放式（mag-dz)/(1-dz)，保证推到底输出满幅。
void SteamInput::handleStickInput(ControllerStick stick, float x, float y) {
    const float dz = profile.globalSettings.deadzone;
    const Vector2 d = Vector2{x, y}.withDeadzone(dz);
    float outX = d.x;
    float outY = d.y;
    if (stick == ControllerStick::RIGHT_STICK) {
        if (profile.globalSettings.invertLookX)
            outX = -outX;
        if (profile.globalSettings.invertLookY)
            outY = -outY;
    }
    emit stickMapped(stick, outX, outY);
}
