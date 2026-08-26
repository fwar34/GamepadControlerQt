// =====================================================================
// overlay.rs —— 悬浮窗信息（内联显示）
// =====================================================================

use crate::core::input_types::{all_controller_buttons, controller_button_display_name};
use crate::ui::shared::AppShared;
use crate::ui::theme::*;
use crate::ui::theme::rgb;
use iced::widget::{column, container, row, text};
use iced::{Background, Element};

pub fn overlay_view<'a>(
    shared: &'a AppShared,
    _expanded: bool,
) -> Element<'a, crate::ui::app::Message> {
    let core = shared.core.lock().unwrap();

    let set_name = core.steam.profile.active_operation_set_name();
    let layer_name = core.steam.active_layer_name().to_string();
    let connected = core.connected;
    let mouse_toggle = !core.mapper.toggled_mouse_buttons.is_empty();

    let mut pressed: Vec<String> = core
        .steam
        .held_buttons()
        .iter()
        .map(|b| controller_button_display_name(*b).to_string())
        .collect();
    pressed.sort();

    let active_layers = core.steam.get_active_layers();
    let layer_ref = if active_layers.is_empty() {
        core.steam.profile.common_layer()
    } else {
        active_layers.last().copied()
    };
    let held = core.steam.held_buttons();
    let mut mappings: Vec<(String, String, bool)> = Vec::new();
    if let Some(layer_ref) = layer_ref {
        for b in all_controller_buttons() {
            if let Some(m) = layer_ref.get_mapping(b) {
                let desc = m.describe();
                let is_held = held.contains(&b);
                mappings.push((
                    controller_button_display_name(b).to_string(),
                    desc,
                    is_held,
                ));
            }
        }
    }

    drop(core);

    let title = row![
        text(format!("操作集: {}", set_name)).size(14).color(rgb(ACCENT)),
        text(format!("  当前层: {}", layer_name)).size(14).color(rgb(TEXT)),
    ];

    let conn_text = if connected { "● 手柄已连接" } else { "○ 手柄未连接" };
    let conn_color = if connected { OK } else { TEXT_FAINT };

    let mut press_row = row![].spacing(4);
    if pressed.is_empty() {
        press_row = press_row.push(text("无按键按下").size(12).color(rgb(TEXT_FAINT)));
    } else {
        for name in &pressed {
            let chip = container(text(name.clone()).size(12).color(rgb(BG_DEEP)))
                .padding([2, 8])
                .style(|_| iced::widget::container::Style {
                    background: Some(Background::Color(rgb(ACCENT))),
                    border: iced::Border::default().rounded(3),
                    ..Default::default()
                });
            press_row = press_row.push(chip);
        }
    }

    let mut content = column![
        title,
        row![text(conn_text).size(12).color(rgb(conn_color))],
        press_row,
    ]
    .spacing(6);

    if mouse_toggle {
        let warn = container(text("⚠ 鼠标长按锁存中，再按一次解除").size(12).color(rgb(BG_DEEP)))
            .padding([3, 8])
            .style(|_| iced::widget::container::Style {
                background: Some(Background::Color(rgb(WARN))),
                border: iced::Border::default().rounded(3),
                ..Default::default()
            });
        content = content.push(warn);
    }

    if _expanded {
        let mut mappings_col = column![
            text(format!("当前层映射: {}", layer_name)).size(12).color(rgb(TEXT_DIM)),
        ]
        .spacing(2);

        if mappings.is_empty() {
            mappings_col = mappings_col.push(text("（无映射）").size(12).color(rgb(TEXT_FAINT)));
        } else {
            for (btn, desc, held) in &mappings {
                let color = if *held { WARN } else { ACCENT };
                mappings_col = mappings_col.push(
                    row![
                        text(btn.clone()).size(12).color(rgb(color)),
                        text("→").size(12).color(rgb(TEXT_DIM)),
                        text(desc.clone()).size(12).color(rgb(TEXT)),
                    ]
                    .spacing(4)
                );
            }
        }

        mappings_col = mappings_col
            .push(row![text("左摇杆").size(12).color(rgb(ACCENT)), text("→").size(12).color(rgb(TEXT_DIM)), text("WASD 移动").size(12).color(rgb(TEXT))].spacing(4))
            .push(row![text("右摇杆").size(12).color(rgb(ACCENT)), text("→").size(12).color(rgb(TEXT_DIM)), text("视角控制").size(12).color(rgb(TEXT))].spacing(4))
            .push(text("（点击标题收起）").size(12).color(rgb(TEXT_FAINT)));

        content = content.push(mappings_col);
    }

    container(content)
        .width(320)
        .padding(12)
        .style(move |_| iced::widget::container::Style {
            background: Some(Background::Color(argb(0xd02b2d31))),
            border: iced::Border {
                width: 1.0,
                color: if mouse_toggle { rgb(WARN) } else { rgb(ACCENT) },
                ..Default::default()
            }.rounded(8),
            ..Default::default()
        })
        .into()
}
