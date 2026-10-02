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

struct PlaybackSettingsData
{
  float fps = 10.0f;
  bool loop = true;
  // True keeps a steady frame rate by skipping time steps that are not loaded;
  // false waits for every one.
  bool dropFrames = false;
};

struct TimeSeriesSettingsData
{
  // Fill memory and disk with time steps in the background. With this off,
  // slider-driven loads are still cached in both tiers. How much gets warmed is
  // bounded by the RAM and disk cache limits, so there is no separate depth or
  // fill-cache setting.
  bool prefetchEnabled = true;
  PlaybackSettingsData playback;
};

struct AgaveSettingsData
{
  CacheSettingsData cache;
  TimeSeriesSettingsData timeSeries;
};

class AgaveSettings
{
public:
  AgaveSettings();

  void load();
  bool save() const;

  void applyCacheToRenderlib() const;

  AgaveSettingsData& data() { return m_data; }
  const AgaveSettingsData& data() const { return m_data; }

  AgaveSettingsData defaultSettings() const;
  std::string configPath() const;

private:
  AgaveSettingsData m_data;
};
