#pragma once

#include "MappingTypes.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

// =====================================================================
// SteamInput —— 映射引擎（等效安卓版 SteamInput）
//
// 职责：
//   - 维护当前激活的操作层栈（公共层始终激活、优先级最低）
//   - 按键查询：按「最后激活的操作层 -> ... -> 公共层」顺序查找有效映射
//   - 分发按钮/摇杆输入，并负责层切换动作的运行时处理
//
// 数据流：
//   XInputGamepadSource（手柄读取）-> handleButtonEvent/handleStickInput
//   -> buttonMapped/stickMapped 信号 -> KeyboardMouseMapper（键鼠注入）
// =====================================================================
class SteamInput : public QObject {
    Q_OBJECT
public:
    explicit SteamInput(QObject* parent = nullptr);

    // 当前配置（公共层 + 操作层 + 全局设置）
    ControllerProfile profile;

    // 整体替换配置（启动时加载配置文件后调用），同时清空所有激活层
    void loadProfile(const ControllerProfile& newProfile);

    // 仅更新全局设置（不重置已激活层），并通知映射器同步
    // （用于界面滑块实时调整死区/灵敏度等，避免打断进行中的层切换）
    void setGlobalSettings(const GlobalSettings& settings);

    // ---- 层管理 ----
    // 激活指定层（name 为层 id）；重复激活同一层会被忽略（见实现）
    void activateLayer(const QString& name);
    void activateLayer(OperationLayer* layer);
    // 停用指定层
    void deactivateLayer(const QString& name);
    void deactivateLayer(OperationLayer* layer);
    // 停用所有操作层，回到公共层
    void deactivateAllLayers();
    // 指定层当前是否激活
    bool isLayerActive(const QString& name) const;
    // 当前激活层 id（未激活任何操作层时为 "Common"）
    QString activeLayerName() const { return activeLayerName_; }

    // ---- 查询 ----
    // 查询某按钮在当前层栈下的有效映射：
    //   从最后激活的操作层开始，逐层回退到公共层，返回第一个命中
    const KeyMapping* getEffectiveMapping(ControllerButton button) const;
    // 当前激活层列表（公共层不在其中）
    QVector<const OperationLayer*> getActiveLayers() const;
    // 当前物理按下的手柄按键集合
    QSet<ControllerButton> heldButtons() const { return heldButtons_; }

    // ---- 输入入口（由手柄读取源调用） ----
    // 按钮按下/松开事件；SwitchLayer 动作在此处理（按住激活/松开回退），
    // 其余动作通过 buttonMapped 信号广播给映射器
    void handleButtonEvent(ControllerButton button, bool isPressed);
    // 摇杆输入（x,y 已归一化到 [-1,1]，未应用死区）
    void handleStickInput(ControllerStick stick, float x, float y);

signals:
    // 按钮命中映射（isPressed=true 按下，false 松开；mapping 为查询到的映射）
    void buttonMapped(ControllerButton button, bool isPressed, const KeyMapping& mapping);
    // 摇杆输入（已应用死区）
    void stickMapped(ControllerStick stick, float x, float y);
    // 当前激活层变化（未激活任何操作层时为 "Common"）
    void layerChanged(const QString& activeLayerName);
    // 配置被整体替换（loadProfile 或 setGlobalSettings 后触发）
    void profileChanged();

private:
    // 根据 activeLayers_ 重新计算 activeLayerName_ 并发出 layerChanged
    void updateActiveLayerName();

    // 已激活操作层（按下顺序，后加入的优先级更高）
    QVector<OperationLayer*> activeLayers_;
    // 记录「哪个按键激活了哪个层」，松开该按键时停用对应层并 return
    QHash<ControllerButton, OperationLayer*> buttonTriggeredLayers_;
    // 当前物理按下的手柄按键集合
    QSet<ControllerButton> heldButtons_;
    QString activeLayerName_ = QStringLiteral("Common");
};
