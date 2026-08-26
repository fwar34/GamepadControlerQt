// =====================================================================
// config_manager.rs —— 配置文件读写管理
//
// 等效 Qt 版 ConfigManager.h/.cpp。
// 配置文件固定放在可执行文件同目录（绿色便携，不写注册表/用户目录），
// 文件名与安卓版保持一致（steamlike_config.json）。
//
// 加载策略：
//   - 文件存在且可正常解析 -> 返回解析结果
//   - 文件不存在 / 解析失败 / 版本不匹配 -> 回退到默认配置并保存
// =====================================================================

use crate::core::config;
use crate::core::mapping_types::ControllerProfile;
use std::path::PathBuf;

const CONFIG_FILE_NAME: &str = "steamlike_config.json";

/// 配置文件完整路径（exe 目录 + "steamlike_config.json"）
pub fn config_file_path() -> PathBuf {
    let exe_dir = std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.to_path_buf()))
        .unwrap_or_else(|| PathBuf::from("."));
    exe_dir.join(CONFIG_FILE_NAME)
}

/// 配置文件是否存在
pub fn has_config_file() -> bool {
    config_file_path().exists()
}

/// 加载配置：文件不存在或解析失败时回退到默认配置并保存
pub fn load() -> ControllerProfile {
    let path = config_file_path();
    if path.exists() {
        if let Ok(text) = std::fs::read_to_string(&path) {
            if let Ok(profile) = config::profile_from_json_str(&text) {
                return profile;
            }
        }
    }
    let def = ControllerProfile::create_default();
    save(&def);
    def
}

/// 保存配置到文件，成功返回 true
pub fn save(profile: &ControllerProfile) -> bool {
    let text = config::profile_to_json_string(profile);
    std::fs::write(config_file_path(), text).is_ok()
}

/// 重置为默认配置并保存
pub fn reset_to_default() {
    let def = ControllerProfile::create_default();
    save(&def);
}
