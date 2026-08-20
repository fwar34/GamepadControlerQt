#pragma once

#include "MappingTypes.h"

#include <QByteArray>
#include <stdexcept>

// =====================================================================
// 配置序列化（等效安卓版 ControllerConfig，JSON 格式 version=2 兼容）
// =====================================================================
namespace ControllerConfig {
    constexpr int CONFIG_VERSION = 2;

    QByteArray toJson(const ControllerProfile& profile, int indent = 2);

    // 解析失败（版本不匹配 / 格式错误）时抛出 std::runtime_error
    ControllerProfile fromJson(const QByteArray& json);
}
