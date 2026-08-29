// 【C++ 语法】#include "SteamInput.h"：以双引号包含本类对应的头文件（先搜索当前目录），
// 由此获得类声明与全部成员函数定义所需的类型信息。
#include "SteamInput.h" // 包含类声明（实现文件必须包含对应头文件）

// =====================================================================
// SteamInput —— 映射引擎实现
//
// 负责三件事：
//   1. 层管理：维护 activeLayers_ 栈 + buttonTriggeredLayers_ 映射
//   2. 按键查询：getEffectiveMapping 按「激活层 -> 公共层」顺序查找
//   3. 输入分发：handleButtonEvent 处理 SwitchLayer，其余广播 buttonMapped
// =====================================================================

// 【C++ 语法】定义成员函数用"类名::函数名"限定（作用域解析运算符 ::）。
// 构造函数冒号后为"成员初始化列表"，用 parent 初始化基类 QObject（建立 Qt 对象树）；
// 函数体 {} 为空，表示无需额外初始化。
SteamInput::SteamInput(QObject* parent) : QObject(parent) {} // 构造函数：仅初始化基类

// 整体替换配置（启动加载、重置默认时调用）
// 【C++ 语法】void 返回类型 + const ControllerProfile& 参数：不返回值；参数为只读引用避免拷贝。
void SteamInput::loadProfile(const ControllerProfile& newProfile) { // 定义 loadProfile（函数体开始）
    profile = newProfile; // 整体替换当前配置
    // 【C++ 语法】成员函数调用：对自身调用 deactivateAllLayers()，清空所有激活层。
    deactivateAllLayers();   // 配置变更后清空所有激活层，回到公共层
    // 【Qt】emit：发射（触发）信号的 Qt 关键字；调用信号如同调用普通函数。
    emit profileChanged();   // 通知映射器/界面同步
} // loadProfile 函数体结束

// 仅更新全局设置（界面滑块实时调整时调用，避免打断进行中的层切换）
// 【C++ 语法】const GlobalSettings&：常量引用参数（只读、不拷贝）。
void SteamInput::setGlobalSettings(const GlobalSettings& settings) { // 定义 setGlobalSettings（函数体开始）
    profile.globalSettings = settings; // 仅更新全局设置字段（不动层与操作集）
    // 【Qt】emit profileChanged()：发射配置变化信号，通知监听方（界面等）同步。
    emit profileChanged(); // 广播配置变化
} // setGlobalSettings 函数体结束

// ---------------------------------------------------------------
// 操作集管理
// ---------------------------------------------------------------

// 切换当前操作集：先清空已激活层栈（旧操作集内的层指针不再有效，
// 必须清理避免悬垂指针），再更新激活集并广播信号。
// 【C++ 语法】bool 返回类型 + const QString& 参数：返回是否成功；参数为只读引用。
bool SteamInput::switchOperationSet(const QString& setId) { // 定义 switchOperationSet（函数体开始）
    // 【C++ 语法】! 逻辑非运算符：把返回值取反；setActiveOperationSet 返回 false 时条件为真。
    if (!profile.setActiveOperationSet(setId)) // 尝试把激活操作集设为 setId，取反判断是否失败
        // 【C++ 语法】return：提前结束函数并返回布尔值 false。
        return false;                  // 无效 id，忽略
    if (profile.activeOperationSetId != setId) // 防御性校验：确认激活 id 已真正更新
        return false;                  // 不应发生，防御
    // 【C++ 语法】成员函数调用：清空旧的激活层栈（切集后旧层指针失效，必须清理）。
    deactivateAllLayers();             // 清空层栈，回到新操作集的公共层
    // 【Qt】emit：发射配置变化信号。
    emit profileChanged(); // 广播配置变化
    // 【Qt】emit：发射"操作集变化"信号，参数为当前操作集显示名。
    emit operationSetChanged(profile.activeOperationSetName()); // 广播操作集变化（携带显示名）
    // 【C++ 语法】return true：返回成功标志并结束函数。
    return true; // 切换成功
} // switchOperationSet 函数体结束

// 操作集结构变化后的统一通知（新增/删除/复制/重命名已由调用方完成，
// activeOperationSetId 已指向目标集）。调用方须先 deactivateAllLayers()。
void SteamInput::notifyOperationSetChanged() { // 定义 notifyOperationSetChanged（函数体开始）
    emit profileChanged(); // 广播配置变化
    emit operationSetChanged(profile.activeOperationSetName()); // 广播操作集变化（携带显示名）
} // notifyOperationSetChanged 函数体结束

// ---------------------------------------------------------------
// 层管理
// ---------------------------------------------------------------

// 激活一个操作层（追加到栈顶，优先级最高）。
// 忽略空名/Common（公共层不可"激活"）；重复激活同一层被忽略。
// 【C++ 语法】OperationLayer* 参数：层对象指针（指向 profile 持有的对象，不拷贝对象本身）。
void SteamInput::activateLayer(OperationLayer* layer) { // 定义 activateLayer（指针版，函数体开始）
    // 【C++ 语法】|| 逻辑或短路：任一条件为真即整体为真，不再计算后续条件。
    if (!layer || layer->name.isEmpty() || layer->name == QStringLiteral("Common")) // 过滤空指针/空名/公共层
        return; // 忽略非法参数
    if (!activeLayers_.contains(layer)) { // 仅当层尚未激活时才入栈（避免重复激活）
        activeLayers_.append(layer); // 追加到激活栈顶（优先级最高）
        updateActiveLayerName(); // 重算当前激活层名并广播变化
    } // if 分支结束
} // activateLayer（指针版）函数体结束

// 【C++ 语法】重载版本：参数为层 id 字符串，经 profile 查找层指针后转调指针版本。
void SteamInput::activateLayer(const QString& name) { // 定义 activateLayer（字符串版，函数体开始）
    activateLayer(profile.findLayer(name)); // 按 id 查层后转调指针版本（查不到传空指针）
} // activateLayer（字符串版）函数体结束

// 停用一个操作层（从栈中移除所有匹配项）
void SteamInput::deactivateLayer(OperationLayer* layer) { // 定义 deactivateLayer（指针版，函数体开始）
    if (!layer) return; // 空指针直接返回
    // 【C++ 语法】const int：const 修饰局部变量，初始化后不可修改（此处保存移除的数量）。
    const int removed = activeLayers_.removeAll(layer); // 移除栈中所有匹配项并记录数量
    if (removed > 0) // 确有移除才触发名称更新
        updateActiveLayerName(); // 重算当前激活层名
} // deactivateLayer（指针版）函数体结束

void SteamInput::deactivateLayer(const QString& name) { // 定义 deactivateLayer（字符串版，函数体开始）
    deactivateLayer(profile.findLayer(name)); // 按 id 查层后转调指针版本
} // deactivateLayer（字符串版）函数体结束

// 停用所有操作层，回到公共层；同时清空触发层记录
void SteamInput::deactivateAllLayers() { // 定义 deactivateAllLayers（函数体开始）
    if (activeLayers_.isEmpty()) { // 激活栈为空则直接走提前返回
        updateActiveLayerName(); // 仍重算一次名称（保证信号一致）
        return; // 无层可清，直接返回
    } // if 分支结束
    activeLayers_.clear(); // 清空全部激活层
    buttonTriggeredLayers_.clear(); // 同时清空"按键->层"触发记录
    updateActiveLayerName(); // 重算激活层名（栈空后应回到 "Common"）
} // deactivateAllLayers 函数体结束

// 指定层（按 id）当前是否激活
// 注意：用 id 匹配（而非显示名 name），因为界面层按钮的 objectName 存的是 id，
// 层改名后 name 变化但 id 不变，按 id 判断才能正确高亮。
// 【C++ 语法】末尾 const：const 成员函数，承诺不修改对象状态，可对 const 对象调用。
bool SteamInput::isLayerActive(const QString& id) const { // 定义 isLayerActive（const 只读版，函数体开始）
    // 【C++ 语法】范围 for 循环（range-based for）：依次取出 activeLayers_ 中每个元素；
    // const OperationLayer* layer 声明"指向常对象的指针"元素，循环体内只读访问。
    for (const OperationLayer* layer : activeLayers_) // 遍历每个已激活层
        if (layer->id == id) return true; // 按 id 匹配则立即返回已激活
    return false; // 全部未匹配，判定未激活
} // isLayerActive 函数体结束

// 重新计算当前激活层名（栈顶层的显示名；无激活层时为 "Common"），
// 变化时发出 layerChanged 信号（界面与悬浮窗据此更新）
void SteamInput::updateActiveLayerName() { // 定义 updateActiveLayerName（函数体开始）
    // 【C++ 语法】QStringLiteral("Common")：编译期构造 QString 字面量；"=" 为初始化局部变量。
    QString name = QStringLiteral("Common"); // 初始取公共层名
    if (!activeLayers_.isEmpty()) // 有激活层时才覆盖默认值
        name = activeLayers_.last()->name; // 取栈顶层（最后激活层）的显示名
    if (name != activeLayerName_) { // 名称确有变化才继续
        activeLayerName_ = name; // 更新当前激活层名成员
        // 【Qt】emit：发射"激活层变化"信号，参数为最新层名。
        emit layerChanged(name); // 广播激活层名变化
    } // if 分支结束
} // updateActiveLayerName 函数体结束

// 返回当前激活层列表（按激活顺序，公共层不在其中）
// 【C++ 语法】返回类型 QVector<const OperationLayer*>：返回"指向常对象的指针"的动态数组；
// 末尾 const 表示只读成员函数。
QVector<const OperationLayer*> SteamInput::getActiveLayers() const { // 定义 getActiveLayers（const 只读版）
    QVector<const OperationLayer*> out; // 声明返回结果容器
    out.reserve(activeLayers_.size()); // 预分配容量，避免多次扩容的开销
    for (const OperationLayer* layer : activeLayers_) // 遍历每个已激活层
        out.append(layer); // 依次收集到结果列表
    return out; // 返回激活层列表
} // getActiveLayers 函数体结束

// ---------------------------------------------------------------
// 查询
// ---------------------------------------------------------------

// 查询按钮在当前层栈下的有效映射：
//   从最后激活的操作层（栈顶，优先级最高）开始，逐层回退到公共层，
//   返回第一个命中的映射；全部未命中返回 nullptr（不注入任何事件）。
// 注：层栈只属于当前激活操作集，公共层取 profile.commonLayer()。
// 【C++ 语法】返回 const KeyMapping* 且末尾 const：返回只读映射指针（可空），函数为只读查询。
const KeyMapping* SteamInput::getEffectiveMapping(ControllerButton button) const { // 定义 getEffectiveMapping
    // 【C++ 语法】传统 for 循环：从栈顶（最后一个索引 size()-1）向栈底（索引 0）反向遍历；
    // 前置自减 --i；i >= 0 为循环继续条件。
    for (int i = activeLayers_.size() - 1; i >= 0; --i) { // 从最高优先级层向下遍历
        // 【C++ 语法】在 if 条件中声明并初始化指针变量：若 m 非空（命中映射）则条件为真，
        // 把"查询+判空"合并到一行；该变量作用域仅限此 if 语句。
        if (const KeyMapping* m = activeLayers_[i]->getMapping(button)) // 查当前层映射并判空
            return m; // 命中即返回该映射
    } // for 循环体结束
    const OperationLayer* cl = profile.commonLayer(); // 获取整个配置共享的公共层
    // 【C++ 语法】三目运算符（条件 ? 真值 : 假值）：cl 非空则查公共层映射，否则返回 nullptr。
    return cl ? cl->getMapping(button) : nullptr; // 公共层兜底查询，无映射返回空指针
} // getEffectiveMapping 函数体结束

// ---------------------------------------------------------------
// 输入分发
// ---------------------------------------------------------------

// 手柄按钮事件入口。
// 按下：记录到 heldButtons_，查询映射；SwitchLayer 激活目标层；
//       其余动作广播 buttonMapped（由 KeyboardMouseMapper 执行注入）。
// 松开：若该按键此前激活过某层（在 buttonTriggeredLayers_ 中），
//       则停用对应层并直接返回（不触发映射，避免"松开切层键"误触发动作）。
void SteamInput::handleButtonEvent(ControllerButton button, bool isPressed) { // 定义 handleButtonEvent（函数体开始）
    if (isPressed) // 区分按下/松开
        heldButtons_.insert(button); // 按下：把按键加入按下集合
    else // 松开分支
        heldButtons_.remove(button); // 松开：从按下集合移除该按键

    // 松开时：若该按键激活了某个层，停用该层并返回（不触发映射）
    if (!isPressed) { // 仅松开时检查"切层键回退"
        // 【C++ 语法】const auto：由编译器自动推导迭代器类型并加上 const；
        // auto 可避免写出冗长的 QHash 迭代器类型全名。
        const auto it = buttonTriggeredLayers_.find(button); // 在触发表中查找该按键的记录
        // 【C++ 语法】QHash 迭代器：等于 end() 表示"未找到"；不等于则指向有效元素。
        if (it != buttonTriggeredLayers_.end()) { // 存在触发记录才处理
            // 【C++ 语法】QHash 迭代器取值：it.key() 取键，it.value() 取值（此处为层指针）。
            OperationLayer* triggered = it.value(); // 取出被该按键激活的层指针
            buttonTriggeredLayers_.erase(it); // 先删除记录（erase 后迭代器失效），再停用层
            deactivateLayer(triggered); // 停用对应的激活层（实现层回退）
            return; // 提前返回，不广播映射（避免松开切层键误触发动作）
        } // if 分支结束
    } // 外层 if 结束

    // 查询当前层栈下的有效映射；无映射则不产生任何事件
    const KeyMapping* mapping = getEffectiveMapping(button); // 按层栈优先级查询当前有效映射
    if (!mapping) { // 无有效映射时
        // 松开时：即使该按钮在「松开时刻」的层栈下已无映射，仍要广播松开事件，
        // 让映射器按「已注入状态」精确释放（防止此前注入的按键卡死）。
        // 例：按住切层键激活 Down 层期间按下按钮，先松开切层键导致层回退，
        //     再松开该按钮时当前层无其映射 —— 若丢弃松开事件，此前注入的按键会永久卡住。
        if (!isPressed) // 仅在松开时补发
            // 【Qt】emit：发射"按钮映射"信号；KeyMapping() 构造空默认映射，让映射器
            // 按"已注入状态"精确释放按键（防止卡键）。
            emit buttonMapped(button, false, KeyMapping()); // 补发松开事件（带空映射）
        return; // 无映射则结束处理
    } // if 分支结束

    // 切换层动作由引擎处理：按住激活目标层并记录触发按钮，
    // 松开时（上面分支）停用该层。注意：这里不再广播 buttonMapped。
    if (mapping->action.type == MappedAction::Type::SwitchLayer) { // 判断是否为"切层"动作
        if (isPressed) { // 仅按下时激活目标层
            // 先按 id 查找；找不到则按 name 查找（兼容旧配置）。
            // 目标层必须属于当前激活操作集（findLayer 只查激活集）。
            OperationLayer* target = profile.findLayer(mapping->action.layerName); // 按 id 查目标层
            if (!target) { // id 查找失败则回退按名称查找
                // 【C++ 语法】范围 for + 引用元素（OperationLayer& l）：以引用遍历，可修改元素；
                // 因需让 target 指向该元素，故取其地址 &l。
                for (OperationLayer& l : profile.layers()) // 遍历当前操作集的所有层
                    if (l.name == mapping->action.layerName) { target = &l; break; } // 名称匹配则指向该层并退出
            } // if 分支结束
            if (target && !isLayerActive(target->id)) { // 目标层有效且尚未激活才激活
                activateLayer(target); // 激活目标层
                buttonTriggeredLayers_.insert(button, target); // 记录"按键->层"，供松开时回退
            } // if 分支结束
        } // isPressed 分支结束
        return; // 切层动作处理完毕，直接返回（不广播映射）
    } // 切层分支结束

    // 切换类动作：仅在按下时触发，松开忽略
    if (isPressed) { // 仅按下时处理"切换类"动作
        // 【C++ 语法】switch 语句：按整型/枚举值多分支跳转；case 后为匹配的常量值，default 兜底。
        switch (mapping->action.type) { // 按动作类型分发
            case MappedAction::Type::ToggleMapping: // 映射启停动作
                emit toggleMappingRequested(); // 请求切换映射启停
                return; // 处理完返回
            case MappedAction::Type::ToggleOnScreenKeyboard: // 屏幕键盘启停动作
                emit toggleOnScreenKeyboardRequested(); // 请求切换屏幕键盘
                return; // 处理完返回
            case MappedAction::Type::ToggleOverlay: // 悬浮窗启停动作
                emit toggleOverlayRequested(); // 请求切换悬浮窗
                return; // 处理完返回
            default: // 其他动作类型（无需特殊处理）
                break; // 跳出 switch 语句
        } // switch 语句结束
    } // if 分支结束

    // 其余动作（键盘/鼠标/视角等）交给映射器执行
    // 【Qt】emit：发射"按钮映射"信号，携带按键、按下状态及映射内容（*mapping 解引用指针）。
    emit buttonMapped(button, isPressed, *mapping); // 广播映射事件给映射器执行注入
} // handleButtonEvent 函数体结束

// 摇杆输入入口：先应用全局死区（withDeadzone），再广播 stickMapped。
// 死区采用缩放式（mag-dz)/(1-dz)，保证推到底输出满幅。
void SteamInput::handleStickInput(ControllerStick stick, float x, float y) { // 定义 handleStickInput（函数体开始）
    // 【C++ 语法】const float：const 局部变量，初始化后不可修改（此处存死区阈值）。
    const float dz = profile.globalSettings.deadzone; // 读取全局死区阈值
    // 【C++ 语法】聚合初始化 Vector2{x, y}：用花括号初始化结构体成员；
    // 随后链式调用成员函数 withDeadzone(dz)，对临时对象取应用死区后的结果。
    const Vector2 d = Vector2{x, y}.withDeadzone(dz); // 对摇杆向量应用死区
    float outX = d.x; // 取处理后的 X 分量
    float outY = d.y; // 取处理后的 Y 分量
    if (stick == ControllerStick::RIGHT_STICK) { // 仅右摇杆（视角）应用反转
        if (profile.globalSettings.invertLookX) // X 反转开关开启时
            outX = -outX; // X 分量取反
        if (profile.globalSettings.invertLookY) // Y 反转开关开启时
            outY = -outY; // Y 分量取反
    } // 右摇杆分支结束
    // 【Qt】emit：发射"摇杆映射"信号（坐标已应用死区与反转）。
    emit stickMapped(stick, outX, outY); // 广播处理后的摇杆输入
} // handleStickInput 函数体结束
