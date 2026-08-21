#pragma once

#include "MappingTypes.h"

#include <QByteArray>
#include <stdexcept>

// =====================================================================
// ControllerConfig —— 配置序列化（等效安卓版 ControllerConfig）
//
// JSON 格式与安卓版完全兼容（version=2），使得同一份配置文件
// 可以在 Windows 版与安卓版之间互换使用。
//
// 顶层结构：
//   {
//     "version": 2,
//     "globalSettings": { ... },
//     "commonLayer":   { ... },
//     "layers":        [ ... ]
//   }
// 动作 type 取值：keyboard / mouse / mouseToggle / switchLayer /
//                 mouseMove / lookAround
// 鼠标按钮名：大写（LEFT/RIGHT/MIDDLE/FORWARD/BACK）
// =====================================================================
namespace ControllerConfig {
    // 当前配置版本号（不匹配时 fromJson 抛异常）
    constexpr int CONFIG_VERSION = 2;

    // 序列化：ControllerProfile -> JSON 文本（indent 为格式化缩进）
    QByteArray toJson(const ControllerProfile& profile, int indent = 2);

    // 反序列化：JSON 文本 -> ControllerProfile
    // 解析失败（版本不匹配 / 格式错误 / 字段无效）时抛出 std::runtime_error
    ControllerProfile fromJson(const QByteArray& json);
}
