// ============================================================
// ConfigManager.cpp
// 配置文件读写管理
// ------------------------------------------------------------
// 职责：管理配置文件在磁盘上的位置与读写。
//   - 文件固定放在可执行文件同目录（本机使用，不写注册表/用户目录）
//   - 文件名：steamlike_config.json
//   - 首次启动（无配置文件）或解析失败时，自动生成默认配置
//
// 注意：本文件只负责"文件层面"的存取，JSON 的编解码逻辑
// 全部委托给 ControllerConfig 命名空间（见 ControllerConfig.cpp）。
// ============================================================

#include "ConfigManager.h"

#include "ControllerConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <stdexcept>

namespace {
const char kConfigFileName[] = "steamlike_config.json";
}

// ============================================================
// configFilePath：返回配置文件绝对路径
// ============================================================
// 取可执行文件所在目录 + 固定文件名。
// 选择 exe 目录而非用户文档目录，是为了"绿色便携"——
// 拷贝整个程序目录即可迁移配置。
QString ConfigManager::configFilePath() {
    return QDir(QCoreApplication::applicationDirPath()).filePath(QLatin1String(kConfigFileName));
}

// ============================================================
// hasConfigFile：判断配置文件是否已存在
// ============================================================
bool ConfigManager::hasConfigFile() {
    return QFile::exists(configFilePath());
}

// ============================================================
// load：加载配置，失败时回退默认配置
// ============================================================
// 流程：
//   1. 文件存在且可读 -> 读取全部字节并交给 ControllerConfig::fromJson；
//      - 解析成功：直接返回；
//      - 解析失败（语法错误/版本不符）：捕获异常后落入默认配置。
//   2. 文件不存在或读取失败：使用默认配置。
//   3. 无论哪种回退，都会把默认配置保存一份到磁盘，方便用户查看
//      并保证下次启动有据可依。
ControllerProfile ConfigManager::load() {
    QFile file(configFilePath());
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        const QByteArray data = file.readAll();
        file.close();
        try {
            return ControllerConfig::fromJson(data);
        } catch (const std::exception&) {
            // 解析失败：回退到默认配置（损坏的配置不致命）
        }
    }

    const ControllerProfile def = ControllerProfile::createDefault();
    save(def);
    return def;
}

// ============================================================
// save：把配置写入磁盘
// ============================================================
// 以"写覆盖（Truncate）"方式打开文件，写入序列化后的 JSON。
// 写入失败（如磁盘只读/目录无权限）返回 false，由调用方提示用户。
bool ConfigManager::save(const ControllerProfile& profile) {
    QFile file(configFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const bool ok = file.write(ControllerConfig::toJson(profile)) != -1;
    file.close();
    return ok;
}

// ============================================================
// resetToDefault：重置为默认配置并落盘
// ============================================================
void ConfigManager::resetToDefault() {
    save(ControllerProfile::createDefault());
}
