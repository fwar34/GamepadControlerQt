// =====================================================================
// help.rs —— 使用说明窗口
// =====================================================================

use crate::ui::theme::*;
use crate::ui::theme::rgb; // 显式导入：消除与 gpui::rgb 的 glob 歧义
use gpui::*;

pub struct HelpView;

impl HelpView {
    pub fn new(_window: &mut Window, _cx: &mut Context<Self>) -> Self {
        Self
    }
}

impl Render for HelpView {
    fn render(&mut self, _window: &mut Window, _cx: &mut Context<Self>) -> impl IntoElement {
        let section = |title: &str, body: &str| {
            div()
                .flex()
                .flex_col()
                .gap_1()
                .child(
                    div()
                        .text_lg()
                        .text_color(rgb(ACCENT))
                        .child(title.to_string()),
                )
                .child(
                    div()
                        .text_size(px(16.0))
                        .text_color(rgb(TEXT))
                        .child(body.to_string()),
                )
        };

        div()
            .flex()
            .flex_col()
            .size_full()
            .bg(rgb(BG))
            .p_5()
            .gap_4()
            .child(
                div()
                    .text_xl()
                    .text_color(rgb(TEXT))
                    .child("Gamepad 键鼠映射 · 使用说明"),
            )
            .child(
                section(
                    "一、快速上手",
                    "1. 连接手柄，点击「开始映射」。\n2. 默认配置已含一个「默认操作集」，公共层绑定基础按键（A=空格、B=右键、X=左键、Y=I、菜单键=Esc、视图键=M）。\n3. 右摇杆 = 视角控制，左摇杆 = WASD 移动。",
                ),
            )
            .child(
                section(
                    "二、操作集与层映射",
                    "操作集是最高层容器，每个操作集内包含 1 个公共层 + 最多 10 个操作层。\n切换操作集时，其下所有层整体切换（适合不同游戏/场景一键切换整套配置）。\n• 添加：新建空操作集（默认名可再改）。\n• 复制：把当前操作集整体复制为新操作集，可直接改名。\n• 重命名：修改当前操作集的自定义名字。\n• 删除：至少保留一个操作集。\n悬浮窗始终显示当前操作集名称。",
                ),
            )
            .child(
                section(
                    "三、层切换机制",
                    "操作层由公共层的「切层」(SwitchLayer) 映射驱动：按住切层键激活目标层，松开自动回退。\n按键查询顺序：最后激活的操作层 → 较早的操作层 → 公共层（兜底）。",
                ),
            )
            .child(
                section(
                    "四、悬浮窗",
                    "「显示悬浮窗」打开置顶透明信息窗，实时显示：当前操作集、当前层、连接状态、按下的手柄按键。\n当鼠标长按锁存（MouseToggle）激活时，悬浮窗边框变橙色并显示警示。",
                ),
            )
            .child(
                section(
                    "五、配置文件",
                    "配置文件 steamlike_config.json 位于程序同目录，绿色便携。与安卓版格式兼容（version=2）。",
                ),
            )
    }
}
