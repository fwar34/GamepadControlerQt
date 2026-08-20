#include "ConfigManager.h"

#include "ControllerConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <stdexcept>

namespace {
const char kConfigFileName[] = "steamlike_config.json";
}

QString ConfigManager::configFilePath() {
    return QDir(QCoreApplication::applicationDirPath()).filePath(QLatin1String(kConfigFileName));
}

bool ConfigManager::hasConfigFile() {
    return QFile::exists(configFilePath());
}

ControllerProfile ConfigManager::load() {
    QFile file(configFilePath());
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        const QByteArray data = file.readAll();
        file.close();
        try {
            return ControllerConfig::fromJson(data);
        } catch (const std::exception&) {
            // 解析失败：回退到默认配置
        }
    }

    const ControllerProfile def = ControllerProfile::createDefault();
    save(def);
    return def;
}

bool ConfigManager::save(const ControllerProfile& profile) {
    QFile file(configFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const bool ok = file.write(ControllerConfig::toJson(profile)) != -1;
    file.close();
    return ok;
}

void ConfigManager::resetToDefault() {
    save(ControllerProfile::createDefault());
}
