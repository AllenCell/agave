#include "AgaveSettings.h"

#include "renderlib/CacheManager.h"
#include "renderlib/Logging.h"
#include "renderlib/SystemInfo.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <nlohmann/json.hpp>

#include <algorithm>

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

AgaveSettings::AgaveSettings() = default;

AgaveSettingsData
AgaveSettings::defaultSettings() const
{
  // Tunable defaults come from the settings structs' in-class initializers.
  return {};
}

std::string
AgaveSettings::configPath() const
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

void
AgaveSettings::load()
{
  AgaveSettingsData data = defaultSettings();
  QString path = QString::fromStdString(configPath());
  QFile file(path);
  if (!file.exists()) {
    m_data = data;
    return;
  }
  if (!file.open(QIODevice::ReadOnly)) {
    m_data = data;
    return;
  }

  QByteArray raw = file.readAll();
  try {
    nlohmann::json doc = nlohmann::json::parse(raw.toStdString());
    if (doc.contains("enabled")) {
      data.cache.enabled = doc["enabled"].get<bool>();
    }
    if (doc.contains("enableDisk")) {
      data.cache.enableDisk = doc["enableDisk"].get<bool>();
    }
    if (doc.contains("maxRamBytes")) {
      data.cache.maxRamBytes = doc["maxRamBytes"].get<std::uint64_t>();
    }
    if (doc.contains("maxDiskBytes")) {
      data.cache.maxDiskBytes = doc["maxDiskBytes"].get<std::uint64_t>();
    }
    if (doc.contains("prefetchEnabled")) {
      data.timeSeries.prefetchEnabled = doc["prefetchEnabled"].get<bool>();
    }
    // Older prefetch and status-detail keys were retired. Reads were
    // contains()-guarded, so older settings files still load; stale keys simply
    // drop on the next save. No migration needed.
    if (doc.contains("playbackFps")) {
      data.timeSeries.playback.fps = doc["playbackFps"].get<float>();
    }
    if (doc.contains("playbackLoop")) {
      data.timeSeries.playback.loop = doc["playbackLoop"].get<bool>();
    }
    if (doc.contains("playbackDropFrames")) {
      data.timeSeries.playback.dropFrames = doc["playbackDropFrames"].get<bool>();
    }
  } catch (...) {
    m_data = defaultSettings();
    return;
  }

  m_data = data;
}

bool
AgaveSettings::save() const
{
  nlohmann::json doc;
  doc["enabled"] = m_data.cache.enabled;
  doc["enableDisk"] = m_data.cache.enableDisk;
  doc["maxRamBytes"] = m_data.cache.maxRamBytes;
  doc["maxDiskBytes"] = m_data.cache.maxDiskBytes;
  doc["prefetchEnabled"] = m_data.timeSeries.prefetchEnabled;
  doc["playbackFps"] = m_data.timeSeries.playback.fps;
  doc["playbackLoop"] = m_data.timeSeries.playback.loop;
  doc["playbackDropFrames"] = m_data.timeSeries.playback.dropFrames;

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
AgaveSettings::applyCacheToRenderlib() const
{
  // The cache directory (and its writability) is settled once at startup in
  // CacheManager::initialize(); if it wasn't writable, the manager left its root
  // unset, so a disk-enabled config here is simply honored as RAM-only. We only
  // push the runtime tunables.
  ::CacheConfig config = toRenderlibConfig(m_data.cache);
  LOG_INFO << "Cache config: enabled=" << (config.enabled ? 1 : 0) << " ram_bytes=" << config.maxRamBytes
           << " disk_enabled=" << (config.enableDisk ? 1 : 0) << " disk_bytes=" << config.maxDiskBytes
           << " cache_dir=" << CacheManager::instance().getCacheDirectory();
  CacheManager::instance().setConfig(config);
  auto applied = CacheManager::instance().getConfig();
  LOG_INFO << "Cache config applied: enabled=" << (applied.enabled ? 1 : 0) << " ram_bytes=" << applied.maxRamBytes
           << " disk_enabled=" << (applied.enableDisk ? 1 : 0) << " disk_bytes=" << applied.maxDiskBytes
           << " cache_dir=" << CacheManager::instance().getCacheDirectory();
}
