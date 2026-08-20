#pragma once

#include "MappingTypes.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

// =====================================================================
// 映射引擎（等效安卓版 SteamInput）
// 职责：
//   - 维护当前激活的操作层栈（公共层始终激活）
//   - 按键查询：按"激活层 -> 公共层"顺序查找有效映射
//   - 分发按钮/摇杆输入
// =====================================================================
class SteamInput : public QObject {
    Q_OBJECT
public:
    explicit SteamInput(QObject* parent = nullptr);

    // 当前配置
    ControllerProfile profile;

    void loadProfile(const ControllerProfile& newProfile);

    // 仅更新全局设置（不重置已激活层），并通知映射器
    void setGlobalSettings(const GlobalSettings& settings);

    // 层管理
    void activateLayer(const QString& name);
    void activateLayer(OperationLayer* layer);
    void deactivateLayer(const QString& name);
    void deactivateLayer(OperationLayer* layer);
    void deactivateAllLayers();
    bool isLayerActive(const QString& name) const;
    QString activeLayerName() const { return activeLayerName_; }

    // 查询
    const KeyMapping* getEffectiveMapping(ControllerButton button) const;
    QVector<const OperationLayer*> getActiveLayers() const;
    QSet<ControllerButton> heldButtons() const { return heldButtons_; }

    // 输入入口
    void handleButtonEvent(ControllerButton button, bool isPressed);
    void handleStickInput(ControllerStick stick, float x, float y);

signals:
    // 按钮命中映射（isPressed=true 按下，false 松开；mapping 为查询到的映射）
    void buttonMapped(ControllerButton button, bool isPressed, const KeyMapping& mapping);
    // 摇杆输入（已应用死区）
    void stickMapped(ControllerStick stick, float x, float y);
    // 当前激活层变化（未激活任何操作层时为 "Common"）
    void layerChanged(const QString& activeLayerName);
    // 配置被整体替换
    void profileChanged();

private:
    void updateActiveLayerName();

    // 已激活操作层（按下顺序）
    QVector<OperationLayer*> activeLayers_;
    // 记录"哪个按键激活了哪个层"，松开该按键时停用对应层
    QHash<ControllerButton, OperationLayer*> buttonTriggeredLayers_;
    QSet<ControllerButton> heldButtons_;
    QString activeLayerName_ = QStringLiteral("Common");
};
