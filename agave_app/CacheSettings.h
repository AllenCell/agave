#pragma once

#include <cstdint>
#include <string>

struct CacheSettingsData
{
  bool enabled = true;
  bool enableDisk = true;
  std::uint64_t maxRamBytes = 4ULL * 1024ULL * 1024ULL * 1024ULL;
  std::uint64_t maxDiskBytes = 100ULL * 1024ULL * 1024ULL * 1024ULL;
};

class CacheSettings
{
public:
  CacheSettings();

  CacheSettingsData load();
  bool save(const CacheSettingsData& data) const;

  void applyToRenderlib(const CacheSettingsData& data) const;

  CacheSettingsData defaultSettings() const;
  std::string configPath() const;
};
