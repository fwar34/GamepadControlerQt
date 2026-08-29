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

// 【C++ 语法】#include "..."：包含项目自定义头文件 ControllerConfig.h（本文件是其实现）
#include "ControllerConfig.h"

// 【C++ 语法】#include <...>：包含 Qt 提供的 JSON 处理相关头文件
#include <QJsonArray>     // 【Qt】QJsonArray：JSON 数组类型，对应配置文件中的 "[...]"
#include <QJsonDocument>  // 【Qt】QJsonDocument：JSON 文档类，负责整份 JSON 的解析 / 序列化（含缩进格式化）
#include <QJsonObject>    // 【Qt】QJsonObject：JSON 对象类型，对应配置文件中的 "{...}"

// 【C++ 语法】匿名 namespace：使其中所有名字具有内部链接（internal linkage），仅本 .cpp 编译单元可见，不污染全局命名空间
namespace {

// ---- 动作 type 的字符串常量（与配置文件/安卓版一一对应）----
// 【Qt】QStringLiteral(...)：编译期构造 QString 的宏，避免运行时分配开销；以下常量是各类动作在 JSON "type" 字段中的取值
const QString kTypeKeyboard = QStringLiteral("keyboard");  // 键盘按键动作类型名
const QString kTypeMouse = QStringLiteral("mouse");  // 鼠标单击动作类型名
const QString kTypeMouseToggle = QStringLiteral("mouseToggle");  // 鼠标长按锁存动作类型名
const QString kTypeWheelUp = QStringLiteral("wheelUp");  // 滚轮上滚动作类型名
const QString kTypeWheelDown = QStringLiteral("wheelDown");  // 滚轮下滚动作类型名
const QString kTypeSwitchLayer = QStringLiteral("switchLayer");  // 切换操作层动作类型名
const QString kTypeMouseMove = QStringLiteral("mouseMove");  // 鼠标移动动作类型名
const QString kTypeLookAround = QStringLiteral("lookAround");  // 视角控制动作类型名
const QString kTypeToggleMapping = QStringLiteral("toggleMapping");  // 切换映射启停动作类型名
const QString kTypeToggleOnScreenKeyboard = QStringLiteral("toggleOnScreenKeyboard");  // 切换屏幕键盘动作类型名
const QString kTypeToggleOverlay = QStringLiteral("toggleOverlay");  // 切换悬浮窗动作类型名

// ============================================================
// actionToJson：MappedAction -> QJsonObject
// ============================================================
// 按动作类型写入对应的结构化字段（keyCode / button / layerName 等）。
// 【C++ 语法】函数返回 QJsonObject（值语义）；参数 const MappedAction& 为常量引用（只读、不拷贝、不改原对象）
QJsonObject actionToJson(const MappedAction& action) {
    QJsonObject json;  // 【Qt】创建空的 JSON 对象，后续按动作类型往里面填充字段
    switch (action.type) {  // 【C++ 语法】switch 分支语句：按动作类型枚举值分发到不同 case
        case MappedAction::Type::KeyboardKey:  // 【C++ 语法】case 标签；enum class（强类型枚举）必须用 "类型名::枚举项" 限定
            json.insert(QStringLiteral("type"), kTypeKeyboard);  // 【Qt】QJsonObject::insert(键, 值)：向 JSON 对象写入字段
            json.insert(QStringLiteral("keyCode"), action.keyCode);  // 写入 "keyCode" 字段：Android 键盘键码
            break;  // 【C++ 语法】break：结束当前 case，跳出整个 switch，防止落入下一个分支
        case MappedAction::Type::MouseClick:  // 鼠标单击动作
            json.insert(QStringLiteral("type"), kTypeMouse);  // 写入动作类型：mouse
            json.insert(QStringLiteral("button"), mouseButtonName(action.mouseButton));  // 写入 "button"：鼠标按钮名（大写，如 LEFT）
            break;  // 结束本分支
        case MappedAction::Type::MouseToggle:  // 鼠标长按锁存动作
            json.insert(QStringLiteral("type"), kTypeMouseToggle);  // 写入动作类型：mouseToggle
            json.insert(QStringLiteral("button"), mouseButtonName(action.mouseButton));  // 写入 "button"：被锁存的鼠标按钮名
            break;  // 结束本分支
        case MappedAction::Type::WheelUp:  // 滚轮上滚动作
            json.insert(QStringLiteral("type"), kTypeWheelUp);  // 写入动作类型：wheelUp
            break;  // 该动作无额外参数，结束本分支
        case MappedAction::Type::WheelDown:  // 滚轮下滚动作
            json.insert(QStringLiteral("type"), kTypeWheelDown);  // 写入动作类型：wheelDown
            break;  // 该动作无额外参数，结束本分支
        case MappedAction::Type::SwitchLayer:  // 切换操作层动作
            json.insert(QStringLiteral("type"), kTypeSwitchLayer);  // 写入动作类型：switchLayer
            json.insert(QStringLiteral("layerName"), action.layerName);  // 写入 "layerName"：目标操作层的 id
            break;  // 结束本分支
        case MappedAction::Type::MouseMove:  // 鼠标移动动作（摇杆动作）
            json.insert(QStringLiteral("type"), kTypeMouseMove);  // 写入动作类型：mouseMove
            break;  // 该动作无额外参数，结束本分支
        case MappedAction::Type::LookAround:  // 视角控制动作（右摇杆）
            json.insert(QStringLiteral("type"), kTypeLookAround);  // 写入动作类型：lookAround
            break;  // 该动作无额外参数，结束本分支
        case MappedAction::Type::ToggleMapping:  // 切换映射启停动作
            json.insert(QStringLiteral("type"), kTypeToggleMapping);  // 写入动作类型：toggleMapping
            break;  // 该动作无额外参数，结束本分支
        case MappedAction::Type::ToggleOnScreenKeyboard:  // 切换屏幕键盘动作
            json.insert(QStringLiteral("type"), kTypeToggleOnScreenKeyboard);  // 写入动作类型：toggleOnScreenKeyboard
            break;  // 该动作无额外参数，结束本分支
        case MappedAction::Type::ToggleOverlay:  // 切换悬浮窗动作
            json.insert(QStringLiteral("type"), kTypeToggleOverlay);  // 写入动作类型：toggleOverlay
            break;  // 该动作无额外参数，结束本分支
    }
    return json;  // 返回已填充动作字段的 JSON 对象
}

// ============================================================
// parseAction：QJsonObject -> MappedAction
// ============================================================
// 解析单个动作；成功返回 true 并写 *out，失败返回 false。
// 失败原因可能是：未知 type、缺少必要字段、鼠标按钮名无法识别等。
// 【C++ 语法】参数 const QJsonObject& 为常量引用；MappedAction* out 为输出指针参数，通过解引用把解析结果写回调用方
bool parseAction(const QJsonObject& json, MappedAction* out) {
    // 【Qt】value("type") 读取字段返回 QJsonValue（找不到时为空值）；toString() 将其转为 QString
    const QString type = json.value(QStringLiteral("type")).toString();  // 取出动作类型字符串
    if (type == kTypeKeyboard) {  // 【C++ 语法】if 语句：判断是否为键盘按键动作
        // 【Qt】toInt(0)：把字段值转为 int，字段缺失或无法转换时返回默认值 0
        const int keyCode = json.value(QStringLiteral("keyCode")).toInt(0);  // 读取键盘键码
        *out = MappedAction::keyboardKey(keyCode);  // 【C++ 语法】*out 解引用指针写入调用方对象；调用静态工厂方法构造动作
        return true;  // 解析成功返回 true
    }
    if (type == kTypeMouse) {  // 鼠标单击动作
        MouseButton b = MouseButton::LEFT;  // 【C++ 语法】枚举变量声明并初始化：默认取左键
        if (!mouseButtonFromName(json.value(QStringLiteral("button")).toString(), &b))  // 【C++ 语法】&b 取地址传指针，由函数把按钮名解析结果写入 b
            return false;  // 【C++ 语法】if 无花括号：条件成立时只执行紧跟的一条语句；按钮名无法识别则解析失败
        *out = MappedAction::mouseClick(b);  // 构造鼠标单击动作并写入输出对象
        return true;  // 解析成功
    }
    if (type == kTypeMouseToggle) {  // 鼠标长按锁存动作
        MouseButton b = MouseButton::LEFT;  // 默认取左键
        if (!mouseButtonFromName(json.value(QStringLiteral("button")).toString(), &b))  // 解析鼠标按钮名
            return false;  // 解析失败返回 false
        *out = MappedAction::mouseToggle(b);  // 构造鼠标长按锁存动作
        return true;  // 解析成功
    }
    if (type == kTypeWheelUp) {  // 滚轮上滚动作
        *out = MappedAction::wheelUp();  // 构造滚轮上滚动作
        return true;  // 解析成功
    }
    if (type == kTypeWheelDown) {  // 滚轮下滚动作
        *out = MappedAction::wheelDown();  // 构造滚轮下滚动作
        return true;  // 解析成功
    }
    if (type == kTypeSwitchLayer) {  // 切换操作层动作
        const QString name = json.value(QStringLiteral("layerName")).toString();  // 读取目标操作层 id
        if (name.isEmpty()) return false;   // 缺少目标层名视为无效
        *out = MappedAction::switchLayer(name);  // 构造切换操作层动作
        return true;  // 解析成功
    }
    if (type == kTypeMouseMove) {  // 鼠标移动动作
        *out = MappedAction::mouseMove();  // 构造鼠标移动动作
        return true;  // 解析成功
    }
    if (type == kTypeLookAround) {  // 视角控制动作
        *out = MappedAction::lookAround();  // 构造视角控制动作
        return true;  // 解析成功
    }
    if (type == kTypeToggleMapping) {  // 切换映射启停动作
        *out = MappedAction::toggleMapping();  // 构造切换映射启停动作
        return true;  // 解析成功
    }
    if (type == kTypeToggleOnScreenKeyboard) {  // 切换屏幕键盘动作
        *out = MappedAction::toggleOnScreenKeyboard();  // 构造切换屏幕键盘动作
        return true;  // 解析成功
    }
    if (type == kTypeToggleOverlay) {  // 切换悬浮窗动作
        *out = MappedAction::toggleOverlay();  // 构造切换悬浮窗动作
        return true;  // 解析成功
    }
    return false;  // 未知类型
}

// ============================================================
// mappingToJson：KeyMapping -> QJsonObject
// ============================================================
// 包含主动作 action 与子命令数组 subCommands（组合键）。
// 【C++ 语法】const KeyMapping& 常量引用参数；返回 QJsonObject（值语义）
QJsonObject mappingToJson(const KeyMapping& mapping) {
    QJsonObject json;  // 空的 JSON 对象，承载一条按键映射的数据
    json.insert(QStringLiteral("action"), actionToJson(mapping.action));  // 【Qt】insert 写入 "action" 子对象（主动作）
    QJsonArray subArray;  // 【Qt】QJsonArray：创建空的 JSON 数组，用于存放子命令键码
    for (const int sub : mapping.subCommands)  // 【C++ 语法】基于范围的 for 循环（range-based for）：遍历 QVector<int> 每个元素
        subArray.append(sub);  // 【Qt】append：把子命令键码追加进 JSON 数组
    json.insert(QStringLiteral("subCommands"), subArray);  // 写入 "subCommands" 数组字段
    return json;  // 返回该条映射的 JSON 对象
}

// ============================================================
// parseMapping：QJsonObject -> KeyMapping
// ============================================================
// 解析单条映射；失败返回 false（由调用方跳过该条）。
// 子命令数量超过 MAX_SUB_COMMANDS 时按无效处理（防御外部配置）。
// 【C++ 语法】输出参数 KeyMapping* out：通过指针把解析结果写回调用方
bool parseMapping(const QJsonObject& json, KeyMapping* out) {
    const QJsonValue actionVal = json.value(QStringLiteral("action"));  // 【Qt】QJsonValue：读取 "action" 字段（可能是对象）
    if (!actionVal.isObject()) return false;  // 【Qt】isObject() 判断值是否为 JSON 对象；不是则解析失败
    MappedAction action;  // 声明用于承载解析结果的主动作对象
    if (!parseAction(actionVal.toObject(), &action)) return false;  // 【Qt】toObject() 把 QJsonValue 转为 QJsonObject；主动作解析失败则整体失败
    // 【C++ 语法】QVector<int>：Qt 的动态数组容器（连续内存），这里存放子命令键码列表
    QVector<int> subs;  // 子命令列表（组合键键码）
    const QJsonValue subVal = json.value(QStringLiteral("subCommands"));  // 读取 "subCommands" 字段
    if (subVal.isArray()) {  // 【Qt】isArray()：判断该值是否为 JSON 数组
        const QJsonArray arr = subVal.toArray();  // 转为 QJsonArray 以便遍历
        for (const QJsonValue& v : arr) {  // 【C++ 语法】范围 for + const 引用：遍历数组且不拷贝元素
            if (v.isDouble()) subs.append(v.toInt());  // 【Qt】isDouble() 判断是否为数值；toInt() 转 int 后追加到列表
        }
    }
    if (subs.size() > KeyMapping::MAX_SUB_COMMANDS) return false;  // 子命令超限，跳过
    // 【C++ 语法】花括号聚合初始化：KeyMapping{action, subs} 按结构体成员声明顺序初始化（action 在前，subCommands 在后）
    *out = KeyMapping{action, subs};  // 组合主动作与子命令，写回输出对象
    return true;  // 解析成功
}

// ============================================================
// layerToJson：OperationLayer -> QJsonObject
// ============================================================
// id 与 name 分开持久化：id 用于运行时正确定位层（支持重命名），
// name 仅用于显示。triggerButton 与 buttonMappings 一并保存。
// 【C++ 语法】const OperationLayer& 常量引用参数，只读序列化不修改层对象
QJsonObject layerToJson(const OperationLayer& layer) {
    QJsonObject json;  // 空的 JSON 对象，承载一个操作层的全部数据
    // id 为空时回退写 name，保证老数据/手写数据也能有可读 id
    // 【C++ 语法】三目运算符 条件 ? 真值 : 假值：layer.id 为空字符串时用 layer.name 兜底
    json.insert(QStringLiteral("id"), layer.id.isEmpty() ? layer.name : layer.id);  // 写入 "id" 字段（为空则回退 name）
    json.insert(QStringLiteral("name"), layer.name);  // 写入 "name" 字段：显示名称
    if (layer.hasTriggerButton)  // 【C++ 语法】if：仅当该层配置了触发按键时才写入
        json.insert(QStringLiteral("triggerButton"), controllerButtonName(layer.triggerButton));  // 写入 "triggerButton"：手柄按钮名（大写枚举名）
    QJsonObject mappings;  // 空的按钮映射 JSON 对象（键为按钮名，值为映射对象）
    // 【C++ 语法】QHash 常量迭代器遍历：constBegin() 起、constEnd() 止、++it 前进；it->key() / it->value() 访问键值
    for (auto it = layer.buttonMappings.constBegin(); it != layer.buttonMappings.constEnd(); ++it)
        mappings.insert(controllerButtonName(it.key()), mappingToJson(it.value()));  // 以按钮名作键、映射对象作值写入
    json.insert(QStringLiteral("buttonMappings"), mappings);  // 写入 "buttonMappings" 映射表
    return json;  // 返回该操作层的 JSON 对象
}

// ============================================================
// parseLayer：QJsonObject -> OperationLayer
// ============================================================
// 解析一个层。isCommon 指示是否公共层（公共层不解析 triggerButton）。
// id 缺失时回退到 name（兼容旧配置/安卓配置），公共层固定为 "Common"。
// 【C++ 语法】bool isCommon 按值传参（标量拷贝开销小）；返回 OperationLayer 对象（值语义）
OperationLayer parseLayer(const QJsonObject& json, bool isCommon) {
    OperationLayer layer;  // 创建空操作层对象，随后逐步填充各成员
    const QString name = json.value(QStringLiteral("name")).toString();  // 读取 "name" 显示名称
    layer.name = name;  // 把显示名称写入层对象
    // id：优先读取持久化的 id；缺失时回退到 name（兼容旧配置/安卓配置）
    layer.id = json.value(QStringLiteral("id")).toString();  // 读取 "id" 字段（可能为空字符串）
    if (layer.id.isEmpty())  // 若 id 为空
        layer.id = isCommon ? QStringLiteral("Common") : name;  // 【C++ 语法】三目运算符：公共层固定 "Common"，操作层回退到 name

    // 触发按键：仅操作层有（公共层的触发信息由各操作层自持，供 UI 显示）
    if (!isCommon) {  // 仅对操作层解析触发按键
        ControllerButton tb = ControllerButton::A;  // 默认按钮：A 键
        if (controllerButtonFromName(json.value(QStringLiteral("triggerButton")).toString(), &tb)) {  // 【C++ 语法】&tb 取地址传指针，解析按钮名到 tb
            layer.hasTriggerButton = true;  // 解析成功则标记已配置触发按键
            layer.triggerButton = tb;  // 保存解析出的触发按钮
        }
    }

    // 按钮映射表：逐条解析，非法条目直接跳过
    const QJsonValue mappingsVal = json.value(QStringLiteral("buttonMappings"));  // 读取 "buttonMappings" 字段
    if (mappingsVal.isObject()) {  // 【Qt】isObject()：仅当其为 JSON 对象时才处理
        const QJsonObject mappings = mappingsVal.toObject();  // 转为 QJsonObject
        for (auto it = mappings.constBegin(); it != mappings.constEnd(); ++it) {  // 【C++ 语法】QHash 只读迭代器遍历
            ControllerButton button;  // 声明按钮变量，用于接收解析结果
            if (!controllerButtonFromName(it.key(), &button)) continue;  // 未知按钮名
            if (!it.value().isObject()) continue;                        // 结构非法
            KeyMapping mapping;  // 声明键映射对象，用于承载解析结果
            if (parseMapping(it.value().toObject(), &mapping))  // 解析该按钮对应的映射
                layer.buttonMappings.insert(button, mapping);  // 解析成功则把按钮 -> 映射插入层内映射表
        }
    }
    return layer;  // 返回解析完成的操作层
}

}  // namespace

namespace ControllerConfig {

// ============================================================
// toJson：ControllerProfile -> JSON 字节串
// ============================================================
// 组装根对象：版本号 + 全局设置 + 当前激活操作集 + 操作集数组。
// 每个操作集含 id/name/commonLayer/layers。
// 返回格式化（带缩进）的 JSON，便于用户直接查看/手工编辑。
// 【C++ 语法】int indent 在此实现中为形参（声明处的默认值 2 由头文件提供，此处不用再写默认值）
QByteArray toJson(const ControllerProfile& profile, int indent) {
    QJsonObject root;  // 根 JSON 对象，对应配置文件顶层结构
    root.insert(QStringLiteral("version"), CONFIG_VERSION);  // 写入 "version" 版本号字段

    // 全局设置：死区 / 视角灵敏度 / 光标速度 / 平滑 / 加速
    QJsonObject gs;  // 全局设置子对象
    // 【C++ 语法】static_cast<double>(...)：把 float 显式转换为 double（JSON 数字在 Qt 中统一按 double 保存）
    gs.insert(QStringLiteral("deadzone"), static_cast<double>(profile.globalSettings.deadzone));  // 写入摇杆死区
    gs.insert(QStringLiteral("lookSensitivity"), static_cast<double>(profile.globalSettings.lookSensitivity));  // 写入视角灵敏度
    gs.insert(QStringLiteral("cursorSpeed"), static_cast<double>(profile.globalSettings.cursorSpeed));  // 写入光标速度
    gs.insert(QStringLiteral("lookSmoothing"), static_cast<double>(profile.globalSettings.lookSmoothing));  // 写入视角平滑系数
    gs.insert(QStringLiteral("lookAcceleration"), static_cast<double>(profile.globalSettings.lookAcceleration));  // 写入视角加速度曲线指数
    gs.insert(QStringLiteral("invertLookX"), profile.globalSettings.invertLookX);  // 写入 X 轴反转（bool 自动转 JSON 布尔）
    gs.insert(QStringLiteral("invertLookY"), profile.globalSettings.invertLookY);  // 写入 Y 轴反转
    gs.insert(QStringLiteral("overlayX"), profile.globalSettings.overlayX);  // 写入悬浮窗 X 坐标
    gs.insert(QStringLiteral("overlayY"), profile.globalSettings.overlayY);  // 写入悬浮窗 Y 坐标
    gs.insert(QStringLiteral("overlayScale"), profile.globalSettings.overlayScale);  // 写入悬浮窗缩放系数
    gs.insert(QStringLiteral("mainWindowX"), profile.globalSettings.mainWindowX);  // 写入主窗口 X 坐标
    gs.insert(QStringLiteral("mainWindowY"), profile.globalSettings.mainWindowY);  // 写入主窗口 Y 坐标
    gs.insert(QStringLiteral("releaseOnForegroundChange"), profile.globalSettings.releaseOnForegroundChange);  // 写入"切前台释放按键"选项
    gs.insert(QStringLiteral("confirmOnClose"), profile.globalSettings.confirmOnClose);  // 写入"关闭确认"选项
    root.insert(QStringLiteral("globalSettings"), gs);  // 把全局设置子对象挂到根对象

    // 当前激活操作集
    root.insert(QStringLiteral("activeOperationSet"), profile.activeOperationSetId);  // 写入当前激活的操作集 id

    // 操作集数组
    QJsonArray sets;  // 操作集数组
    for (const OperationSet& set : profile.operationSets) {  // 【C++ 语法】范围 for + const 引用：遍历每个操作集，避免拷贝
        QJsonObject s;  // 单个操作集的 JSON 对象
        s.insert(QStringLiteral("id"), set.id);  // 写入操作集 id
        s.insert(QStringLiteral("name"), set.name);  // 写入操作集名称
        s.insert(QStringLiteral("commonLayer"), layerToJson(set.commonLayer));  // 写入公共层（递归转为 JSON）
        QJsonArray layers;  // 操作层数组
        for (const OperationLayer& layer : set.layers)  // 遍历本操作集的所有操作层
            layers.append(layerToJson(layer));  // 逐层转为 JSON 并追加进数组
        s.insert(QStringLiteral("layers"), layers);  // 写入 "layers" 数组字段
        sets.append(s);  // 把该操作集追加进操作集数组
    }
    root.insert(QStringLiteral("operationSets"), sets);  // 把操作集数组挂到根对象

    // 【Qt】QJsonDocument(root)：用根对象构造 JSON 文档；toJson(QJsonDocument::Indented) 输出带缩进的格式化文本
    return QJsonDocument(root).toJson(QJsonDocument::Indented);  // 序列化为格式化的 JSON 字节串
}

// ============================================================
// fromJson：JSON 字节串 -> ControllerProfile
// ============================================================
// 严格校验：语法错误 / 非对象根 / 版本不匹配时抛 std::runtime_error。
// 字段级容错（缺全局设置、缺公共层、某条映射非法等）则取默认/跳过。
// 【C++ 语法】const QByteArray& 常量引用接收 JSON 字节；返回 ControllerProfile 值
ControllerProfile fromJson(const QByteArray& json) {
    // 【C++ 语法】QJsonParseError err{}：值初始化（对结构体成员零初始化），用于接收解析错误信息
    QJsonParseError err{};
    // 【Qt】QJsonDocument::fromJson：解析 JSON 字节串；出错时把错误信息写入 err（通过指针 &err）
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);  // 【C++ 语法】&err 取地址传入，函数内部写入错误信息
    if (err.error != QJsonParseError::NoError || !doc.isObject())  // 【C++ 语法】|| 逻辑或：存在语法错误 或 根节点不是 JSON 对象
        throw std::runtime_error("Invalid JSON: " + err.errorString().toStdString());  // 【C++ 语法】throw 抛出异常；errorString() 错误描述，toStdString() 转 std::string 拼接

    const QJsonObject root = doc.object();  // 【Qt】doc.object()：取出文档根 JSON 对象
    const int version = root.value(QStringLiteral("version")).toInt(1);  // 读取版本号字段，缺省时默认 1
    if (version != CONFIG_VERSION)  // 版本号与当前配置版本不匹配
        throw std::runtime_error("Unsupported config version: " + std::to_string(version));  // 【C++ 语法】std::to_string 把 int 转字符串拼进异常信息

    ControllerProfile profile;  // 声明结果对象，逐步填充
    // 全局设置：可缺省，逐字段带默认值读取
    profile.globalSettings = GlobalSettings();  // 【C++ 语法】先赋一份默认全局设置，保证后续缺失字段时保持默认值
    if (root.value(QStringLiteral("globalSettings")).isObject()) {  // 【Qt】isObject()：若配置中存在全局设置对象才处理
        const QJsonObject gs = root.value(QStringLiteral("globalSettings")).toObject();  // 取出全局设置子对象
        GlobalSettings s;  // 临时全局设置对象，逐字段填充
        // 【Qt】toDouble(默认值)：读取 double 字段，缺失/非法时返回默认值；【C++ 语法】static_cast<float> 把 double 转回 float 存储
        s.deadzone = static_cast<float>(gs.value(QStringLiteral("deadzone")).toDouble(0.15));  // 死区（默认 0.15）
        s.lookSensitivity = static_cast<float>(gs.value(QStringLiteral("lookSensitivity")).toDouble(0.5));  // 视角灵敏度（默认 0.5）
        s.cursorSpeed = static_cast<float>(gs.value(QStringLiteral("cursorSpeed")).toDouble(1.0));  // 光标速度（默认 1.0）
        s.lookSmoothing = static_cast<float>(gs.value(QStringLiteral("lookSmoothing")).toDouble(0.5));  // 视角平滑（默认 0.5）
        s.lookAcceleration = static_cast<float>(gs.value(QStringLiteral("lookAcceleration")).toDouble(1.5));  // 视角加速度曲线（默认 1.5）
        s.invertLookX = gs.value(QStringLiteral("invertLookX")).toBool(false);  // 【Qt】toBool(默认值)：X 轴反转（默认 false）
        s.invertLookY = gs.value(QStringLiteral("invertLookY")).toBool(false);  // Y 轴反转（默认 false）
        s.overlayX = gs.value(QStringLiteral("overlayX")).toInt(-1);  // 【Qt】toInt(默认值)：悬浮窗 X 坐标（默认 -1 未设置）
        s.overlayY = gs.value(QStringLiteral("overlayY")).toInt(-1);  // 悬浮窗 Y 坐标（默认 -1 未设置）
        s.overlayScale = gs.value(QStringLiteral("overlayScale")).toDouble(1.0);  // 悬浮窗缩放系数（默认 1.0）
        s.mainWindowX = gs.value(QStringLiteral("mainWindowX")).toInt(-1);  // 主窗口 X 坐标（默认 -1 未设置）
        s.mainWindowY = gs.value(QStringLiteral("mainWindowY")).toInt(-1);  // 主窗口 Y 坐标（默认 -1 未设置）
        s.releaseOnForegroundChange = gs.value(QStringLiteral("releaseOnForegroundChange")).toBool(true);  // 切前台释放按键（默认 true）
        s.confirmOnClose = gs.value(QStringLiteral("confirmOnClose")).toBool(true);  // 关闭确认对话框（默认 true）
        profile.globalSettings = s;  // 把解析出的全局设置赋回 profile
    }

    // 公共层/操作层改为按操作集组织：
    //   - 新格式：根节点 operationSets 数组（含 activeOperationSet 指定激活集）；
    //   - 旧 v2 格式兼容：根节点仅有顶层 commonLayer/layers 时，
    //     自动包装成单个「默认操作集」（Set1），保证旧配置无缝升级。
    const QJsonValue setsVal = root.value(QStringLiteral("operationSets"));  // 读取 "operationSets" 字段
    if (setsVal.isArray() && !setsVal.toArray().isEmpty()) {  // 【C++ 语法】&& 逻辑与：是数组且非空时按新格式解析
        const QJsonArray arr = setsVal.toArray();  // 转为操作集数组
        for (const QJsonValue& v : arr) {  // 【C++ 语法】范围 for + const 引用：遍历每个操作集元素
            if (!v.isObject()) continue;  // 【Qt】isObject()：非对象元素直接跳过
            const QJsonObject so = v.toObject();  // 转为操作集的 JSON 对象
            OperationSet set;  // 创建操作集对象
            set.id = so.value(QStringLiteral("id")).toString();  // 读取操作集 id
            set.name = so.value(QStringLiteral("name")).toString();  // 读取操作集名称
            if (set.id.isEmpty())  // 操作集 id 缺失时
                set.id = set.name.isEmpty() ? QStringLiteral("Set") : set.name;  // 【C++ 语法】三目运算符：用名称或固定 "Set" 兜底
            // 本操作集的公共层
            const QJsonValue cVal = so.value(QStringLiteral("commonLayer"));  // 读取公共层字段
            // 【C++ 语法】三目表达式：公共层为对象则解析，否则用 "Common" 构造默认公共层
            set.commonLayer = cVal.isObject()
                ? parseLayer(cVal.toObject(), /*isCommon=*/true)  // 【C++ 语法】/*...*/ 块注释用作实参标签：isCommon 传 true 表示公共层
                : OperationLayer(QStringLiteral("Common"));  // 【C++ 语法】OperationLayer(QString) 调用带参构造函数创建默认公共层
            // 本操作集的操作层数组
            if (so.value(QStringLiteral("layers")).isArray()) {  // 【Qt】isArray()：有操作层数组才处理
                const QJsonArray larr = so.value(QStringLiteral("layers")).toArray();  // 转为操作层数组
                for (const QJsonValue& lv : larr) {  // 遍历每个操作层元素
                    if (lv.isObject())  // 元素须为 JSON 对象
                        set.layers.append(parseLayer(lv.toObject(), /*isCommon=*/false));  // 解析操作层（isCommon=false）并追加
                }
            }
            profile.operationSets.append(set);  // 把解析完成的操作集加入 profile
        }
        // 恢复上次激活的操作集；缺失时回退到第一个
        if (!profile.setActiveOperationSet(root.value(QStringLiteral("activeOperationSet")).toString())  // 尝试按 id 恢复激活操作集
            && !profile.operationSets.isEmpty())  // 【C++ 语法】&&：恢复失败 且 存在操作集时走回退逻辑
            profile.activeOperationSetId = profile.operationSets.first().id;  // 【Qt】first() 取首元素：回退激活到第一个操作集
    } else {
        // ---- 旧 v2 格式兼容：包装为单个「默认操作集」 ----
        OperationSet set;  // 创建默认操作集
        set.id = QStringLiteral("Set1");  // 固定 id 为 "Set1"
        set.name = QStringLiteral("默认操作集");  // 显示名称
        const QJsonValue commonVal = root.value(QStringLiteral("commonLayer"));  // 读取旧格式顶层公共层
        set.commonLayer = commonVal.isObject()  // 是对象则解析
            ? parseLayer(commonVal.toObject(), /*isCommon=*/true)  // 解析为公共层（isCommon=true）
            : OperationLayer(QStringLiteral("Common"));  // 否则创建默认公共层
        if (root.value(QStringLiteral("layers")).isArray()) {  // 旧格式有顶层操作层数组
            const QJsonArray arr = root.value(QStringLiteral("layers")).toArray();  // 转为操作层数组
            for (const QJsonValue& v : arr) {  // 遍历每个操作层
                if (v.isObject())  // 元素须为对象
                    set.layers.append(parseLayer(v.toObject(), /*isCommon=*/false));  // 解析操作层并追加
            }
        }
        profile.operationSets.append(set);  // 把默认操作集加入 profile
        profile.activeOperationSetId = set.id;  // 直接激活该默认操作集
    }

    // ---- 兜底保证：至少保留一个有效操作集 ----
    // operationSets 数组为空或元素全无效（解析时被跳过）时，
    // profile.operationSets 可能为空，此时 activeSet() 返回 nullptr，
    // 下游 commonLayer()/layers() 解引用将导致未定义行为（崩溃）。
    // 这里兜底创建一个默认操作集并指向它，保证返回的 profile 始终有效。
    if (profile.operationSets.isEmpty()) {  // 【C++ 语法】isEmpty()：判断容器是否为空（这里防御空操作集）
        // 【C++ 语法】静态成员函数 OperationSet::createEmpty(id, name)：无需实例即可调用，创建空操作集
        OperationSet fallback = OperationSet::createEmpty(
            QStringLiteral("Set1"), QStringLiteral("默认操作集"));
        profile.operationSets.append(fallback);  // 把兜底操作集加入 profile
        profile.activeOperationSetId = fallback.id;  // 激活兜底操作集
    }

    return profile;  // 返回解析完成的完整配置对象
}

}  // namespace ControllerConfig
