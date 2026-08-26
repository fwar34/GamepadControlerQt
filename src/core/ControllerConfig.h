#pragma once

#include "MappingTypes.h"

#include <QByteArray>
#include <stdexcept>

// =====================================================================
// ControllerConfig —— 配置序列化（等效安卓版 ControllerConfig）
//
// JSON 格式与安卓版（version=2）同版本号，同一份配置文件可在
// Windows 版与安卓版之间互换使用。
//
// 顶层结构（新格式，含操作集）：
//   {
//     "version": 2,
//     "globalSettings": { ... },
//     "activeOperationSet": "Set1",
//     "operationSets": [
//       {
//         "id": "Set1",
//         "name": "默认操作集",
//         "commonLayer": { ... },
//         "layers": [ ... ]
//       }
//     ]
//   }
// 兼容：旧 v2 配置（顶层直接 commonLayer/layers）加载时自动包装成
// 单个「默认操作集」，实现无缝升级。
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
