#include "CacheSettings.h"

#include "renderlib/CacheManager.h"
#include "renderlib/Logging.h"
#include "renderlib/SystemInfo.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <nlohmann/json.hpp>

namespace {

::CacheConfig
toRenderlibConfig(const CacheSettingsData& data)
{
  ::CacheConfig config;
  config.enabled = data.enabled;
  config.enableDisk = data.enableDisk;

  std::uint64_t availableMem = SystemInfo::availableMemoryBytes();
  if (availableMem > 0) {
    config.maxRamBytes = std::min(data.maxRamBytes, availableMem);
    if (config.maxRamBytes < data.maxRamBytes) {
      LOG_WARNING << "Cache RAM limit reduced from requested " << data.maxRamBytes << " to " << config.maxRamBytes
                  << " bytes to fit available memory.";
    }
  } else {
    config.maxRamBytes = data.maxRamBytes;
  }

  // The cache root is owned and writability-checked by CacheManager; clamp the
  // disk limit against whatever filesystem it actually lives on.
  std::uint64_t availableDisk = SystemInfo::availableDiskBytes(CacheManager::instance().getCacheDirectory());
  if (availableDisk > 0) {
    config.maxDiskBytes = std::min(data.maxDiskBytes, availableDisk);
    if (config.maxDiskBytes < data.maxDiskBytes && data.enableDisk) {
      LOG_WARNING << "Cache disk limit reduced from requested " << data.maxDiskBytes << " to " << config.maxDiskBytes
                  << " bytes to fit available disk space.";
    }
  } else {
    config.maxDiskBytes = data.maxDiskBytes;
  }

  if (!config.enableDisk) {
    config.maxDiskBytes = 0;
  }

  return config;
}

} // namespace

CacheSettings::CacheSettings() = default;

CacheSettingsData
CacheSettings::defaultSettings() const
{
  // Tunable defaults come from CacheSettingsData's in-class initializers.
  return {};
}

std::string
CacheSettings::configPath() const
{
  QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  if (baseDir.isEmpty()) {
    baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  }
  if (baseDir.isEmpty()) {
    baseDir = QDir::currentPath();
  }
  QDir().mkpath(baseDir);
  return QDir(baseDir).filePath("cache_settings.json").toStdString();
}

CacheSettingsData
CacheSettings::load()
{
  CacheSettingsData data = defaultSettings();
  QString path = QString::fromStdString(configPath());
  QFile file(path);
  if (!file.exists()) {
    return data;
  }
  if (!file.open(QIODevice::ReadOnly)) {
    return data;
  }

  QByteArray raw = file.readAll();
  try {
    nlohmann::json doc = nlohmann::json::parse(raw.toStdString());
    if (doc.contains("enabled")) {
      data.enabled = doc["enabled"].get<bool>();
    }
    if (doc.contains("enableDisk")) {
      data.enableDisk = doc["enableDisk"].get<bool>();
    }
    if (doc.contains("maxRamBytes")) {
      data.maxRamBytes = doc["maxRamBytes"].get<std::uint64_t>();
    }
    if (doc.contains("maxDiskBytes")) {
      data.maxDiskBytes = doc["maxDiskBytes"].get<std::uint64_t>();
    }
  } catch (...) {
    return defaultSettings();
  }

  return data;
}

bool
CacheSettings::save(const CacheSettingsData& data) const
{
  nlohmann::json doc;
  doc["enabled"] = data.enabled;
  doc["enableDisk"] = data.enableDisk;
  doc["maxRamBytes"] = data.maxRamBytes;
  doc["maxDiskBytes"] = data.maxDiskBytes;

  QString path = QString::fromStdString(configPath());
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  std::string out = doc.dump(2);
  file.write(out.c_str(), static_cast<int>(out.size()));
  return true;
}

void
CacheSettings::applyToRenderlib(const CacheSettingsData& data) const
{
  // The cache directory (and its writability) is settled once at startup in
  // CacheManager::initialize(); if it wasn't writable the manager left its root
  // unset, so a disk-enabled config here is simply honored as RAM-only. We only
  // push the runtime tunables.
  ::CacheConfig config = toRenderlibConfig(data);
  LOG_INFO << "Cache config: enabled=" << (config.enabled ? 1 : 0) << " ram_bytes=" << config.maxRamBytes
           << " disk_enabled=" << (config.enableDisk ? 1 : 0) << " disk_bytes=" << config.maxDiskBytes
           << " cache_dir=" << CacheManager::instance().getCacheDirectory();
  CacheManager::instance().setConfig(config);
  auto applied = CacheManager::instance().getConfig();
  LOG_INFO << "Cache config applied: enabled=" << (applied.enabled ? 1 : 0) << " ram_bytes=" << applied.maxRamBytes
           << " disk_enabled=" << (applied.enableDisk ? 1 : 0) << " disk_bytes=" << applied.maxDiskBytes
           << " cache_dir=" << CacheManager::instance().getCacheDirectory();
}
