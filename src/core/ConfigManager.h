#pragma once

#include "MappingTypes.h"

#include <QString>

// =====================================================================
// ConfigManager —— 配置管理器
//
// 配置文件位于可执行文件所在目录（本机使用，不跨机器），
// 文件名与安卓版保持一致（steamlike_config.json）。
//
// 加载策略：
//   - 文件存在且可正常解析 -> 返回解析结果
//   - 文件不存在 / 解析失败 / 版本不匹配 -> 回退到默认配置并保存
// =====================================================================
class ConfigManager {
public:
    // 配置文件完整路径（exe 目录 + "steamlike_config.json"）
    static QString configFilePath();
    // 配置文件是否存在
    static bool hasConfigFile();
    // 加载配置：文件不存在或解析失败时回退到默认配置并保存
    static ControllerProfile load();
    // 保存配置到文件，成功返回 true
    static bool save(const ControllerProfile& profile);
    // 重置为默认配置并保存
    static void resetToDefault();
};
