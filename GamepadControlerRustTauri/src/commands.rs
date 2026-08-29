// =====================================================================
// commands.rs —— Tauri IPC 命令（前端 WebView <-> 后端）
//
// 前端通过 window.__TAURI__.core.invoke("命令名", 参数) 调用；
// 所有命令运行在 Tauri 主线程，内部短时 lock 共享 core。
// =====================================================================

// 【Rust 语法】use 导入：crate:: 表示从本 crate 根开始定位；::* 通配导入模块全部公开项；花括号内为多个具名项
use crate::core::config_manager; // 导入配置管理模块（保存 / 加载 / 重置）
use crate::core::input_types::*; // 通配导入手柄输入类型：按钮枚举与显示名转换函数
use crate::core::mapping_types::{ActionType, KeyMapping, MappedAction}; // 导入映射动作类型、按键映射结构体
use crate::AppState; // 导入主程序定义的全局共享状态结构体
use serde::{Deserialize, Serialize}; // serde 序列化框架：派生 trait 的基础（JSON 与结构体互转）
use std::sync::atomic::Ordering; // 原子操作的内存序（用于原子标志的读写）
use tauri::{AppHandle, Manager, State}; // tauri 类型：应用句柄、窗口管理 trait、命令状态包装

// ---------------------------------------------------------------------
// 主窗口快照
// ---------------------------------------------------------------------

// 【Rust 语法】derive 派生宏：自动为结构体实现 Serialize trait，使其可序列化为 JSON 返回前端
#[derive(Serialize)]
// 【Rust 语法】struct 结构体：pub 公开声明；字段默认私有；描述一个操作集的基本信息
pub struct SetInfo { // 操作集信息
    id: String, // 操作集唯一标识
    name: String, // 操作集显示名称
} // 结构体结束

// 【Rust 语法】derive 派生宏：自动实现 Serialize（序列化）trait
#[derive(Serialize)]
pub struct LayerInfo { // 层信息
    id: String, // 层唯一标识
    name: String, // 层显示名称
    active: bool, // 该层当前是否激活
} // 结构体结束

// 【Rust 语法】derive 派生宏：自动实现 Serialize（序列化）trait
#[derive(Serialize)]
// 【Rust 语法】struct 结构体：含多种类型字段，一次命令调用返回主窗口全部状态
pub struct Snapshot { // 主窗口整体状态快照
    connected: bool, // 手柄是否已连接
    running: bool, // 映射是否运行中
    active_set_id: String, // 当前激活操作集的 id
    active_set_name: String, // 当前激活操作集的名称
    layer_name: String, // 当前激活层的名称
    sets: Vec<SetInfo>, // 【Rust 语法】Vec<T> 泛型动态数组：全部操作集列表
    layers: Vec<LayerInfo>, // 当前操作集内全部层列表
    mouse_toggle: Option<String>, // 【Rust 语法】Option<T> 枚举：Some 表示有鼠标长按锁存，None 表示没有
    deadzone: f32, // 摇杆死区（f32：单精度浮点）
    look_sensitivity: f32, // 视角灵敏度
    look_smoothing: f32, // 视角平滑
    look_acceleration: f32, // 视角加速度
} // 结构体结束

// ---------------------------------------------------------------------
// 悬浮窗快照
// ---------------------------------------------------------------------

// 【Rust 语法】derive 派生宏：自动实现 Serialize（序列化）trait
#[derive(Serialize)]
pub struct MappingRow { // 悬浮窗中一行按键映射
    button: String, // 手柄按钮的显示名
    desc: String, // 映射动作的描述文本
    held: bool, // 该按钮当前是否被按住
} // 结构体结束

// 【Rust 语法】derive 派生宏：自动实现 Serialize（序列化）trait
#[derive(Serialize)]
pub struct OverlaySnapshot { // 悬浮窗整体快照
    set_name: String, // 当前操作集名称
    layer_name: String, // 当前层名称
    connected: bool, // 手柄连接状态
    pressed: Vec<String>, // 当前被按下的按钮显示名集合
    mouse_toggle: bool, // 是否有鼠标长按锁存
    mappings: Vec<MappingRow>, // 当前层按键映射行列表
    /// 悬浮窗卡片背景透明度（0.2 ~ 1.0），前端据此设置背景 alpha
    opacity: f32, // 透明度数值
} // 结构体结束

// ---------------------------------------------------------------------
// 层编辑快照
// ---------------------------------------------------------------------

// 【Rust 语法】derive 派生宏：自动实现 Serialize（序列化）trait
#[derive(Serialize)]
pub struct SwitchTarget { // "切换层"动作的可选目标层
    id: String, // 目标层 id
    name: String, // 目标层名称
    display: String, // 显示文本（含"公共层"等逻辑名）
} // 结构体结束

// 【Rust 语法】derive 派生宏：自动实现 Serialize（序列化）trait
#[derive(Serialize)]
pub struct ButtonGridItem { // 层编辑界面按钮网格的一个格子
    name: String, // 按钮的枚举名（供前端回传识别）
    display: String, // 按钮显示名
    pressed: bool, // 是否被按住（用于实时高亮）
} // 结构体结束

// 【Rust 语法】derive 派生宏：自动实现 Serialize（序列化）trait
#[derive(Serialize)]
pub struct LayerEditSnapshot { // 层编辑页整体快照
    layer_name: String, // 被编辑层的名称
    switch_targets: Vec<SwitchTarget>, // 可切换目标列表
    buttons: Vec<ButtonGridItem>, // 按钮网格数据
} // 结构体结束

// 【Rust 语法】derive 派生宏：自动实现 Serialize（序列化）trait
#[derive(Serialize)]
pub struct MappingView { // 单个按键映射的展示视图
    kind: String, // 动作类型字符串（keyboard / mouse / ...）
    desc: String, // 动作描述
    subs: Vec<String>, // 子命令键码（组合键）列表
    has_mapping: bool, // 是否已有映射
} // 结构体结束

// 【Rust 语法】impl 实现块：为结构体添加关联方法，通过 MappingView::none() 调用（无需实例）
impl MappingView {
    // 【Rust 语法】关联函数 + Self 返回类型：不接收 self 参数，Self 即 MappingView 自身类型
    fn none() -> Self { // 构造一个"无映射"占位视图
        Self { // 结构体字面量初始化
            kind: "keyboard".into(), // 【Rust 语法】.into() 类型转换：把 &str 字面量转换为 String
            desc: "（无映射）".into(), // 空映射的描述文本
            subs: Vec::new(), // 空子命令列表
            has_mapping: false, // 标记为无映射
        } // Self 字面量结束
    } // none 函数结束
} // impl 块结束

// ---------------------------------------------------------------------
// 层查找辅助（与 gpui 版 layer_edit.rs 一致）
// ---------------------------------------------------------------------
// 【Rust 语法】生命周期参数 + 引用返回：<'a> 声明生命周期 a；输入 profile 与返回值 Option<&'a ...> 共享同一生命周期，保证返回的引用仍然有效
fn find_layer_ref<'a>( // 按 id 查找层，返回不可变引用
    profile: &'a crate::core::mapping_types::ControllerProfile, // 【Rust 语法】&'a T 带生命周期注解的不可变借用：只读访问控制器配置
    id: &str, // 【Rust 语法】&str 字符串切片：只借用不拥有所有权，作为层 id 入参
) -> Option<&'a crate::core::mapping_types::OperationLayer> { // 返回可选的操作层引用（找不到则为 None）
    if id == "Common" { // 特殊 id "Common" 表示公共层
        profile.common_layer() // 返回公共层的引用
    } else { // 否则按 id 在层列表中查找
        // 【Rust 语法】迭代器链：into_iter() 生成迭代器，find 接收闭包 |l| 判断条件，返回首个匹配项
        profile.layers().into_iter().find(|l| l.id == id) // 遍历层列表，找到 id 相同的层
    } // else 结束
} // 函数结束

// 【Rust 语法】可变借用 &'a mut：允许调用方修改被借用的数据；同一时刻只能存在一个可变借用
fn find_layer_mut<'a>( // 按 id 查找层，返回可变引用
    profile: &'a mut crate::core::mapping_types::ControllerProfile, // 对控制器配置的可变借用（可修改）
    id: &str, // 层 id 入参（&str 借用）
) -> Option<&'a mut crate::core::mapping_types::OperationLayer> { // 返回可选的层可变引用
    if id == "Common" { // 公共层特殊处理
        profile.common_layer_mut() // 返回公共层的可变引用
    } else { // 其他层按 id 查找
        profile.layers_mut().into_iter().find(|l| l.id == id) // 遍历层列表找 id 相同者（可变迭代）
    } // else 结束
} // 函数结束

// ---------------------------------------------------------------------
// 动作描述 / 类型串（与 gpui 版 layer_edit.rs 一致）
// ---------------------------------------------------------------------
// 【Rust 语法】函数：接收 &MappedAction 不可变借用，返回拥有所有权的 String
fn describe_action(a: &MappedAction) -> String { // 把映射动作转成人类可读的描述文本
    // 【Rust 语法】match 模式匹配：按枚举值逐一分支执行；r#type 是原始标识符（type 是关键字，加 r# 转义）
    match a.r#type { // 按动作类型分发
        // 【Rust 语法】format! 格式化宏：把表达式值嵌入字符串模板生成 String
        ActionType::KeyboardKey => format!("键盘: {}", key_code_to_name(a.key_code)), // 键盘键：拼出"键盘: 键名"
        ActionType::MouseClick => format!("鼠标: {}", mouse_button_display_name(a.mouse_button)), // 鼠标点击
        ActionType::MouseToggle => { // 鼠标长按锁存：花括号分支块可含多行
            format!("鼠标长按: {}", mouse_button_display_name(a.mouse_button)) // 拼出"鼠标长按: 键名"
        } // 分支块结束
        ActionType::WheelUp => "滚轮上".to_string(), // 【Rust 语法】.to_string()：把 &str 转为 String；滚轮上
        ActionType::WheelDown => "滚轮下".to_string(), // 滚轮下
        ActionType::SwitchLayer => { // 切换层动作：分支块
            // 【Rust 语法】clone() 复制 String；unwrap_or_default 取 Some 值，None 时返回默认空串
            format!("切换到层: {}", a.layer_name.clone().unwrap_or_default()) // 拼出"切换到层: 名称"
        } // 分支块结束
        ActionType::LookAround => "视角控制（右摇杆）".to_string(), // 视角控制（右摇杆）
        ActionType::MouseMove => "鼠标移动（左摇杆）".to_string(), // 鼠标移动（左摇杆）
        _ => "（未知）".to_string(), // 【Rust 语法】通配符 _ 分支：兜底匹配所有未列出的枚举值
    } // match 结束
} // 函数结束

fn kind_str(a: &MappedAction) -> String { // 返回动作类型字符串，供前端区分渲染方式
    match a.r#type { // 按动作类型匹配
        ActionType::KeyboardKey => "keyboard", // 键盘键
        ActionType::MouseClick => "mouse", // 鼠标点击
        ActionType::MouseToggle => "mousetoggle", // 鼠标长按
        ActionType::WheelUp => "wheelup", // 滚轮上
        ActionType::WheelDown => "wheeldown", // 滚轮下
        ActionType::SwitchLayer => "switchlayer", // 切换层
        ActionType::LookAround => "lookaround", // 视角控制
        ActionType::MouseMove => "mousemove", // 鼠标移动
        _ => "keyboard", // 未知类型兜底为 keyboard
    } // match 结束
    .to_string() // 把匹配出的 &str 转为 String（match 是表达式，可直接接方法）
} // 函数结束

// ---------------------------------------------------------------------
// 主窗口：整体快照
// ---------------------------------------------------------------------
// 【Rust 语法】属性宏 #[tauri::command]：把函数注册为前端可 invoke 的 IPC 命令，参数自动从 JSON 反序列化
#[tauri::command]
// 【Rust 语法】State<'_, AppState>：Tauri 状态注入，'_ 表示省略具体生命周期；返回值 Snapshot 自动序列化为 JSON
pub fn get_snapshot(state: State<'_, AppState>) -> Snapshot { // 主窗口整体快照命令
    let running = state.shared.running.load(Ordering::SeqCst); // 读取映射运行标志（SeqCst 为最强内存序）
    let core = state.shared.core.lock().unwrap(); // 对共享核心加锁，取得不可变访问（语句结束自动释放）
    let connected = core.connected; // 手柄连接状态
    let layer_name = core.steam.active_layer_name().to_string(); // 当前激活层名称
    let active_set_id = core.steam.profile.active_operation_set_id.clone(); // 复制当前操作集 id
    let active_set_name = core.steam.profile.active_operation_set_name(); // 取当前操作集名称
    // 【Rust 语法】类型标注 + 迭代器：: Vec<SetInfo> 显式标注收集目标类型
    let sets: Vec<SetInfo> = core // 收集所有操作集信息
        .steam // 访问映射引擎 steam
        .profile // 访问控制器配置
        .operation_sets // 操作集容器
        .iter() // 【Rust 语法】iter()：产生元素的不可变迭代器
        .map(|s| SetInfo { // 【Rust 语法】闭包 + map：对每个元素执行转换
            id: s.id.clone(), // 复制操作集 id
            name: s.name.clone(), // 复制操作集名称
        }) // 闭包返回 SetInfo
        .collect(); // 【Rust 语法】collect()：把迭代器收集为 Vec（类型由变量标注推断）
    let layers: Vec<LayerInfo> = core // 收集层信息
        .steam // 访问映射引擎
        .profile // 访问配置
        .layers() // 取当前操作集的层列表
        .iter() // 生成迭代器
        .map(|l| LayerInfo { // 逐层转换
            id: l.id.clone(), // 复制层 id
            name: l.name.clone(), // 复制层名称
            active: core.steam.is_layer_active(&l.id), // 查询该层是否激活
        }) // 闭包返回 LayerInfo
        .collect(); // 收集为 Vec<LayerInfo>
    let mouse_toggle = core // 当前鼠标长按锁存（取第一个）
        .mapper // 访问键鼠执行器
        .toggled_mouse_buttons // 长按锁存中的鼠标按钮集合（哈希表）
        .values() // 【Rust 语法】values()：迭代哈希表所有值
        .next() // 【Rust 语法】next()：取迭代器第一个元素，返回 Option
        .map(|mb| format!("长按锁存: {}", mouse_button_display_name(*mb))); // 【Rust 语法】Option::map：Some 时用闭包转换，*mb 解引用
    let gs = &core.steam.profile.global_settings; // 借用全局设置引用，避免重复写长路径
    Snapshot { // 构造快照结构体
        connected, // 连接状态
        running, // 运行标志
        active_set_id, // 当前操作集 id
        active_set_name, // 当前操作集名称
        layer_name, // 当前层名称
        sets, // 操作集列表
        layers, // 层列表
        mouse_toggle, // 鼠标长按锁存（Option）
        deadzone: gs.deadzone, // 死区
        look_sensitivity: gs.look_sensitivity, // 视角灵敏度
        look_smoothing: gs.look_smoothing, // 视角平滑
        look_acceleration: gs.look_acceleration, // 视角加速度
    } // Snapshot 字面量结束
} // 函数结束

// ---------------------------------------------------------------------
// 悬浮窗：整体快照（含当前层映射列表）
// ---------------------------------------------------------------------
// 【Rust 语法】属性宏 #[tauri::command]：注册为 Tauri IPC 命令
#[tauri::command]
pub fn get_overlay_snapshot(state: State<'_, AppState>) -> OverlaySnapshot { // 悬浮窗整体快照命令
    let core = state.shared.core.lock().unwrap(); // 对共享核心加锁
    let set_name = core.steam.profile.active_operation_set_name(); // 当前操作集名称
    let layer_name = core.steam.active_layer_name().to_string(); // 当前层名称
    let connected = core.connected; // 手柄连接状态
    let mut pressed: Vec<String> = core // 【Rust 语法】mut 可变绑定：变量后续需修改
        .steam // 访问映射引擎
        .held_buttons() // 获取当前被按住的按钮集合
        .iter() // 生成迭代器
        .map(|b| controller_button_display_name(*b).to_string()) // 把按钮枚举转成显示名
        .collect(); // 收集为 Vec<String>
    pressed.sort(); // 排序，保证前端显示顺序稳定
    let mouse_toggle = !core.mapper.toggled_mouse_buttons.is_empty(); // 【Rust 语法】! 取反：集合非空则 true
    // 当前层映射列表：最后激活的操作层，否则公共层
    let active_layers = core.steam.get_active_layers(); // 获取当前激活的层引用数组
    let layer_ref = if active_layers.is_empty() { // 【Rust 语法】if 表达式：分支都返回值，赋给 layer_ref
        core.steam.profile.common_layer() // 无激活层时用公共层
    } else { // 有激活层时
        active_layers.last().copied() // 【Rust 语法】last() 返回 Option<&T>，copied() 解引用并复制为 Option<T>
    }; // if 表达式结束
    let held = core.steam.held_buttons(); // 当前按住的按钮集合（用于判断 held 状态）
    let mut mappings: Vec<MappingRow> = Vec::new(); // 新建可变空向量，收集映射行
    if let Some(lr) = layer_ref { // 层引用存在才处理
        for b in all_controller_buttons() { // 【Rust 语法】for 循环：遍历手柄全部按钮
            if let Some(m) = lr.get_mapping(b) { // 该按钮存在映射才处理
                mappings.push(MappingRow { // 【Rust 语法】Vec::push：向向量尾部追加元素
                    button: controller_button_display_name(b).to_string(), // 按钮显示名
                    desc: m.describe(), // 映射描述
                    held: held.contains(&b), // 按钮是否被按住
                }); // MappingRow 构造结束并压入向量
            } // 内层 if-let 结束
        } // for 循环结束
    } // 外层 if-let 结束
    let opacity = *state.overlay_opacity.lock().unwrap(); // 加锁读取透明度（* 解引用 MutexGuard）
    OverlaySnapshot { // 构造悬浮窗快照
        set_name, // 操作集名称
        layer_name, // 层名称
        connected, // 连接状态
        pressed, // 按住的按钮列表
        mouse_toggle, // 鼠标长按标志
        mappings, // 映射行列表
        opacity, // 透明度
    } // OverlaySnapshot 字面量结束
} // 函数结束

// ---------------------------------------------------------------------
// 启停
// ---------------------------------------------------------------------
// 【Rust 语法】属性宏 #[tauri::command]：注册为 Tauri IPC 命令
#[tauri::command]
pub fn start_mapping(state: State<'_, AppState>) { // 开始映射命令（无返回值）
    state.shared.start_mapping(); // 调用共享状态的启动映射方法
}

// 【Rust 语法】属性宏 #[tauri::command]：注册为 Tauri IPC 命令
#[tauri::command]
pub fn stop_mapping(state: State<'_, AppState>) { // 停止映射命令
    state.shared.stop_mapping(); // 调用共享状态的停止映射方法（释放注入）
}

// ---------------------------------------------------------------------
// 操作集管理
// ---------------------------------------------------------------------
// 【Rust 语法】属性宏 #[tauri::command]：注册为 Tauri IPC 命令
#[tauri::command]
pub fn add_operation_set(state: State<'_, AppState>) -> String { // 新增操作集命令，返回新 id
    let mut core = state.shared.core.lock().unwrap(); // 加锁并取得可变访问（mut 因为要修改核心）
    core.add_operation_set() // 新增操作集并返回其 id（块尾表达式即函数返回值）
}

// 【Rust 语法】属性宏 #[tauri::command]：注册为 Tauri IPC 命令
#[tauri::command]
pub fn rename_operation_set(state: State<'_, AppState>, set_id: String, name: String) -> bool { // 重命名操作集命令（参数由前端传入）
    let mut core = state.shared.core.lock().unwrap(); // 加锁取可变访问
    core.rename_operation_set(&set_id, &name) // 传引用调用重命名方法，返回是否成功
}

// 【Rust 语法】属性宏 #[tauri::command]：注册为 Tauri IPC 命令
#[tauri::command]
pub fn copy_operation_set(state: State<'_, AppState>, set_id: String, name: String) -> bool { // 复制操作集命令
    let mut core = state.shared.core.lock().unwrap(); // 加锁取可变访问
    core.copy_operation_set(&set_id, &name) // 调用复制方法，返回是否成功
}

// 【Rust 语法】属性宏 #[tauri::command]：注册为 Tauri IPC 命令
#[tauri::command]
pub fn delete_operation_set(state: State<'_, AppState>, set_id: String) -> bool { // 删除操作集命令
    let mut core = state.shared.core.lock().unwrap(); // 加锁取可变访问
    core.delete_operation_set(&set_id) // 调用删除方法，返回是否成功
}

// 【Rust 语法】属性宏 #[tauri::command]：注册为 Tauri IPC 命令
#[tauri::command]
pub fn switch_operation_set(state: State<'_, AppState>, set_id: String) -> bool { // 切换操作集命令
    let mut core = state.shared.core.lock().unwrap(); // 加锁取可变访问
    core.switch_operation_set(&set_id) // 调用切换方法，返回是否成功
}

// ---------------------------------------------------------------------
// 全局设置
// ---------------------------------------------------------------------
// 【Rust 语法】derive 派生宏：实现 Deserialize（反序列化），命令参数从 JSON 还原为枚举
#[derive(Deserialize)]
// 【Rust 语法】serde 属性 rename_all：把 Rust 驼峰枚举名映射为 snake_case 字符串（前端传 deadzone 等）
#[serde(rename_all = "snake_case")]
// 【Rust 语法】enum 枚举：无字段的 C 风格枚举，表示可调整的全局设置项
pub enum SettingKey { // 全局设置项枚举
    Deadzone, // 摇杆死区
    LookSensitivity, // 视角灵敏度
    LookSmoothing, // 视角平滑
    LookAcceleration, // 视角加速度
} // 枚举结束

// 【Rust 语法】属性宏 #[tauri::command]：注册为 Tauri IPC 命令
#[tauri::command]
pub fn adjust_setting(state: State<'_, AppState>, key: SettingKey, delta: f32) { // 调整全局设置命令：key 指定项，delta 为增量
    let mut core = state.shared.core.lock().unwrap(); // 加锁取可变访问
    let mut gs = core.steam.profile.global_settings.clone(); // 复制一份全局设置，避免在锁内长期借用
    match key { // 按设置项分支处理
        SettingKey::Deadzone => gs.deadzone = (gs.deadzone + delta).clamp(0.0, 0.5), // 【Rust 语法】clamp：把值限制在 [min, max] 区间；死区增减并限幅 0~0.5
        SettingKey::LookSensitivity => { // 灵敏度分支块
            gs.look_sensitivity = (gs.look_sensitivity + delta).clamp(0.05, 2.0) // 限幅 0.05~2.0
        } // 分支块结束
        SettingKey::LookSmoothing => { // 平滑分支块
            gs.look_smoothing = (gs.look_smoothing + delta).clamp(0.0, 0.95) // 限幅 0~0.95
        } // 分支块结束
        SettingKey::LookAcceleration => { // 加速度分支块
            gs.look_acceleration = (gs.look_acceleration + delta).clamp(0.5, 3.0) // 限幅 0.5~3.0
        } // 分支块结束
    } // match 结束
    core.update_global_settings(gs); // 用修改后的设置更新核心
}

// ---------------------------------------------------------------------
// 配置保存 / 重置
// ---------------------------------------------------------------------
// 【Rust 语法】属性宏 #[tauri::command]：注册为 Tauri IPC 命令
#[tauri::command]
pub fn save_config(state: State<'_, AppState>) { // 保存配置命令
    // 【Rust 语法】块表达式：锁在块内使用，块结束自动释放锁后再做文件写入
    let profile = { // 取当前配置快照
        let core = state.shared.core.lock().unwrap(); // 对核心加锁
        core.steam.profile.clone() // 复制配置快照
    }; // 块结束，锁在此释放
    config_manager::save(&profile); // 把快照写入配置文件
}

// 【Rust 语法】属性宏 #[tauri::command]：注册为 Tauri IPC 命令
#[tauri::command]
pub fn reset_config(state: State<'_, AppState>) { // 重置配置命令
    config_manager::reset_to_default(); // 把磁盘配置重置为默认值
    let def = config_manager::load(); // 重新加载默认配置
    let mut core = state.shared.core.lock().unwrap(); // 加锁取可变访问
    core.load_profile(def); // 把默认配置载入核心（替换当前配置）
}

// ---------------------------------------------------------------------
// 悬浮窗显隐 / 退出
// ---------------------------------------------------------------------
// 【Rust 语法】属性宏 #[tauri::command]：注册为 Tauri IPC 命令
#[tauri::command]
pub fn toggle_overlay(app: AppHandle, state: State<'_, AppState>) -> bool { // 切换悬浮窗显隐命令，返回新状态
    // 【Rust 语法】let-else 语句：取到 overlay 窗口则绑定 win，否则走 else 分支
    let Some(win) = app.get_webview_window("overlay") else { // 按标签查找 overlay 窗口
        return false; // 窗口不存在则返回失败
    }; // let-else 结束
    let new = !state.overlay_visible.load(Ordering::SeqCst); // 【Rust 语法】! 取反：读取当前显隐标志后取反
    state.overlay_visible.store(new, Ordering::SeqCst); // 写回新的显隐状态
    if new { // 若变为可见
        let _ = win.show(); // 显示窗口；let _ 丢弃 Result
    } else { // 否则
        let _ = win.hide(); // 隐藏窗口
    } // if-else 结束
    new // 返回新状态（函数最后一个表达式）
} // 函数结束

/// 设置悬浮窗卡片背景透明度（0.2 ~ 1.0），由主窗口滑杆调用
// 【Rust 语法】属性宏 #[tauri::command]：注册为 Tauri IPC 命令
#[tauri::command]
pub fn set_overlay_opacity(state: State<'_, AppState>, opacity: f32) { // 设置悬浮窗透明度命令
    let mut o = state.overlay_opacity.lock().unwrap(); // 加锁取得 MutexGuard（可变守卫）
    *o = opacity.clamp(0.2, 1.0); // 【Rust 语法】*o 解引用 MutexGuard 写入内部值；透明度限幅 0.2~1.0
} // 函数结束

// 【Rust 语法】属性宏 #[tauri::command]：注册为 Tauri IPC 命令
#[tauri::command]
pub fn quit_app(app: AppHandle) { // 退出应用命令
    let state = app.state::<AppState>(); // 【Rust 语法】泛型方法 state::<T>()：从应用句柄取回注入的 AppState
    state.shared.stop_mapping(); // 停止映射并释放全部注入
    app.exit(0); // 退出应用，退出码 0
}

// ---------------------------------------------------------------------
// 层编辑
// ---------------------------------------------------------------------
// 【Rust 语法】属性宏 #[tauri::command]：注册为 Tauri IPC 命令
#[tauri::command]
pub fn get_layer_edit_snapshot(state: State<'_, AppState>, layer_id: String) -> LayerEditSnapshot { // 层编辑快照命令
    let core = state.shared.core.lock().unwrap(); // 对核心加锁
    let layer_name = if layer_id == "Common" { // 【Rust 语法】if 表达式：公共层特殊命名
        "公共层".to_string() // 公共层的显示名
    } else { // 其他层按 id 查找名称
        find_layer_ref(&core.steam.profile, &layer_id) // 查找层引用
            .map(|l| l.name.clone()) // 【Rust 语法】Option::map：有则取层名称
            .unwrap_or_default() // 【Rust 语法】unwrap_or_default：None 时返回 String 默认空串
    }; // if 表达式结束
    let switch_targets: Vec<SwitchTarget> = core // 收集可切换目标
        .steam // 访问映射引擎
        .profile // 访问配置
        .layers() // 层列表
        .iter() // 生成迭代器
        .map(|l| SwitchTarget { // 逐层转换
            id: l.id.clone(), // 目标层 id
            name: l.name.clone(), // 目标层名称
            display: layer_display_name(&l.name), // 层显示名（含公共层逻辑名）
        }) // 闭包返回 SwitchTarget
        .collect(); // 收集为 Vec<SwitchTarget>
    let held = core.steam.held_buttons(); // 当前按住的按钮集合
    let buttons: Vec<ButtonGridItem> = all_controller_buttons() // 收集按钮网格数据
        .into_iter() // 【Rust 语法】into_iter()：消费所有权生成迭代器
        .map(|b| ButtonGridItem { // 逐个按钮转换
            name: controller_button_name(b).to_string(), // 按钮枚举名
            display: controller_button_display_name(b).to_string(), // 按钮显示名
            pressed: held.contains(&b), // 是否被按住
        }) // 闭包返回 ButtonGridItem
        .collect(); // 收集为 Vec<ButtonGridItem>
    LayerEditSnapshot { // 构造层编辑快照
        layer_name, // 层名称
        switch_targets, // 切换目标列表
        buttons, // 按钮网格
    } // LayerEditSnapshot 字面量结束
} // 函数结束

// 【Rust 语法】属性宏 #[tauri::command]：注册为 Tauri IPC 命令
#[tauri::command]
pub fn get_mapping(state: State<'_, AppState>, layer_id: String, button: String) -> MappingView { // 读取单个按键映射命令
    let Some(b) = controller_button_from_name(&button) else { // 【Rust 语法】let-else：按钮名转枚举，失败则走 else
        return MappingView::none(); // 按钮名无效则返回无映射视图
    }; // let-else 结束
    let core = state.shared.core.lock().unwrap(); // 对核心加锁
    let Some(layer) = find_layer_ref(&core.steam.profile, &layer_id) else { // 【Rust 语法】let-else：查层失败也走 else
        return MappingView::none(); // 层不存在则返回无映射视图
    }; // let-else 结束
    match layer.get_mapping(b) { // 【Rust 语法】match 匹配 Option：按该按钮的映射结果分支
        Some(m) => MappingView { // 有映射则构造视图
            kind: kind_str(&m.action), // 动作类型字符串
            desc: describe_action(&m.action), // 动作描述
            subs: m.sub_commands.iter().map(|&k| key_code_to_name(k)).collect(), // 【Rust 语法】闭包 |&k| 解构引用；子命令键码转名称
            has_mapping: true, // 标记有映射
        }, // 视图构造结束
        None => MappingView::none(), // 无映射则返回空视图
    } // match 结束
} // 函数结束

// 【Rust 语法】属性宏 #[tauri::command]：注册为 Tauri IPC 命令
#[tauri::command]
pub fn set_mapping( // 写入按键映射命令（多参数，含多个可选参数）
    state: State<'_, AppState>, // 共享状态
    layer_id: String, // 目标层 id
    button: String, // 按钮名
    kind: String, // 动作类型字符串
    key_code: Option<i32>, // 【Rust 语法】Option<i32>：可空的整数（键盘键码）
    mouse_button: Option<String>, // 可空的鼠标按钮名
    layer_name: Option<String>, // 可空的切换层目标名
) { // 参数表结束，函数无返回值
    let Some(b) = controller_button_from_name(&button) else { // 按钮名转枚举
        return; // 无效则直接返回
    }; // let-else 结束
    // 【Rust 语法】as_str()：String 转为 &str 供匹配；整个 match 结果是 Option<MappedAction>
    let action: Option<MappedAction> = match kind.as_str() { // 按类型字符串构建动作
        "keyboard" => key_code.map(MappedAction::keyboard_key), // 【Rust 语法】Option::map 传构造函数：键码存在则构建键盘动作
        "mouse" => mouse_button // 鼠标点击分支
            .as_deref() // 【Rust 语法】as_deref：Option<String> → Option<&str>
            .and_then(mouse_button_from_name) // 【Rust 语法】and_then：扁平化 Option，解析失败则为 None
            .map(MappedAction::mouse_click), // 构建鼠标点击动作
        "mousetoggle" => mouse_button // 鼠标长按分支
            .as_deref() // 转 &str
            .and_then(mouse_button_from_name) // 解析按钮名
            .map(MappedAction::mouse_toggle), // 构建鼠标长按动作
        "wheelup" => Some(MappedAction::wheel_up()), // 滚轮上（无参数，直接 Some 包装）
        "wheeldown" => Some(MappedAction::wheel_down()), // 滚轮下
        "switchlayer" => layer_name.map(|n| MappedAction::switch_layer(&n)), // 【Rust 语法】闭包借用 n：构建切换层动作
        "lookaround" => Some(MappedAction::look_around()), // 视角控制
        "mousemove" => Some(MappedAction::mouse_move()), // 鼠标移动
        _ => None, // 未知类型不生成动作
    }; // match 结束
    let Some(action) = action else { // 【Rust 语法】let-else：动作构建失败则返回
        return; // 无有效动作则直接返回
    }; // let-else 结束
    let mut core = state.shared.core.lock().unwrap(); // 加锁取可变访问
    if let Some(layer) = find_layer_mut(&mut core.steam.profile, &layer_id) { // 【Rust 语法】if-let：可变借用配置查找层
        // 写入动作时保留已有子命令
        let subs = layer // 取原有子命令
            .button_mappings // 按钮映射表
            .get(&b) // 【Rust 语法】HashMap::get：按按钮查映射，返回 Option
            .map(|m| m.sub_commands.clone()) // 有则复制其子命令
            .unwrap_or_default(); // 无则用空列表
        layer // 写回映射
            .button_mappings // 按钮映射表
            .insert(b, KeyMapping { action, sub_commands: subs }); // 【Rust 语法】HashMap::insert：写入/覆盖该按钮映射
        core.profile_rev += 1; // 配置版本号自增，通知前端刷新
    } // if-let 结束
} // 函数结束

// 【Rust 语法】属性宏 #[tauri::command]：注册为 Tauri IPC 命令
#[tauri::command]
pub fn clear_mapping(state: State<'_, AppState>, layer_id: String, button: String) { // 清空按键映射命令
    let Some(b) = controller_button_from_name(&button) else { // 按钮名转枚举
        return; // 无效则返回
    }; // let-else 结束
    let mut core = state.shared.core.lock().unwrap(); // 加锁取可变访问
    if let Some(layer) = find_layer_mut(&mut core.steam.profile, &layer_id) { // 可变借用查找层
        layer.button_mappings.remove(&b); // 删除该按钮的映射
        core.profile_rev += 1; // 配置版本号自增
    } // if-let 结束
} // 函数结束

// 【Rust 语法】属性宏 #[tauri::command]：注册为 Tauri IPC 命令
#[tauri::command]
pub fn toggle_sub(state: State<'_, AppState>, layer_id: String, button: String, key_code: i32) { // 切换子命令（组合键）命令
    let Some(b) = controller_button_from_name(&button) else { // 按钮名转枚举
        return; // 无效则返回
    }; // let-else 结束
    let mut core = state.shared.core.lock().unwrap(); // 加锁取可变访问
    if let Some(layer) = find_layer_mut(&mut core.steam.profile, &layer_id) { // 可变借用查找层
        if let Some(m) = layer.button_mappings.get_mut(&b) { // 【Rust 语法】get_mut：可变借用该按钮的映射
            // 【Rust 语法】position：闭包找首个匹配索引，返回 Option<usize>
            if let Some(pos) = m.sub_commands.iter().position(|&k| k == key_code) { // 子命令已存在
                m.sub_commands.remove(pos); // 移除该子命令（取消）
            } else if m.sub_commands.len() < KeyMapping::MAX_SUB_COMMANDS { // 不存在且未满
                m.sub_commands.push(key_code); // 追加该子命令
            } // else-if 结束
        } // 内层 if-let 结束
        core.profile_rev += 1; // 配置版本号自增
    } // 外层 if-let 结束
} // 函数结束
