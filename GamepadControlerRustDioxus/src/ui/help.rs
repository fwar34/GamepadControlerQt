// =====================================================================
// help.rs —— 使用说明（Dioxus 版）
//
// 主窗口内的「使用说明」视图，五个章节说明 + 返回按钮。
// =====================================================================

use crate::ui::theme::*;
use dioxus::prelude::*;

/// 章节组件：标题 + 正文（正文保留换行）
#[component]
fn Section(title: String, body: String) -> Element {
    rsx! {
        div {
            style: "display:flex;flex-direction:column;gap:6px;",
            div { style: format!("font-size:17px;color:{};font-weight:600;", hex(ACCENT)), "{title}" }
            div {
                style: format!("font-size:14px;color:{};line-height:1.7;white-space:pre-line;", hex(TEXT)),
                "{body}",
            }
        }
    }
}

#[component]
pub fn HelpView(on_back: EventHandler<()>) -> Element {
    rsx! {
        div {
            style: format!("{}display:flex;flex-direction:column;gap:16px;padding:20px;overflow-y:auto;", root_css()),
            div {
                style: "display:flex;flex-direction:row;align-items:center;gap:12px;",
                div { style: format!("flex:1;font-size:20px;color:{};font-weight:600;", hex(TEXT)), "Gamepad 键鼠映射 · 使用说明" }
                button { style: btn_css(false), onclick: move |_| on_back.call(()), "返回" }
            }
            Section {
                title: "一、快速上手".to_string(),
                body: "1. 连接手柄，点击「开始映射」。\n2. 默认配置已含一个「默认操作集」，公共层绑定基础按键（A=空格、B=右键、X=左键、Y=I、菜单键=Esc、视图键=M）。\n3. 右摇杆 = 视角控制，左摇杆 = WASD 移动。".to_string(),
            }
            Section {
                title: "二、操作集与层映射".to_string(),
                body: "操作集是最高层容器，每个操作集内包含 1 个公共层 + 最多 10 个操作层。\n切换操作集时，其下所有层整体切换（适合不同游戏/场景一键切换整套配置）。\n• 添加：新建空操作集（默认名可再改）。\n• 复制：把当前操作集整体复制为新操作集，可直接改名。\n• 重命名：修改当前操作集的自定义名字。\n• 删除：至少保留一个操作集。\n悬浮窗始终显示当前操作集名称。".to_string(),
            }
            Section {
                title: "三、层切换机制".to_string(),
                body: "操作层由公共层的「切层」(SwitchLayer) 映射驱动：按住切层键激活目标层，松开自动回退。\n按键查询顺序：最后激活的操作层 → 较早的操作层 → 公共层（兜底）。".to_string(),
            }
            Section {
                title: "四、悬浮窗".to_string(),
                body: "「显示悬浮窗」打开置顶透明信息窗，实时显示：当前操作集、当前层、连接状态、按下的手柄按键。\n当鼠标长按锁存（MouseToggle）激活时，悬浮窗边框变橙色并显示警示。".to_string(),
            }
            Section {
                title: "五、配置文件".to_string(),
                body: "配置文件 steamlike_config.json 位于程序同目录，绿色便携。与安卓版格式兼容（version=2）。".to_string(),
            }
        }
    }
}
