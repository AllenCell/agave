#pragma once

#include <cstdint>
#include <string>

struct CacheSettingsData
{
  bool enabled = true;
  bool enableDisk = true;
  std::uint64_t maxRamBytes = 4ULL * 1024ULL * 1024ULL * 1024ULL;
  std::uint64_t maxDiskBytes = 100ULL * 1024ULL * 1024ULL * 1024ULL;

  // Time-series prefetch. The renderlib-side structs these feed
  // (TimeSeriesLoader::PrefetchConfig and TimeSeriesPlayer::Config) hold the
  // authoritative defaults; these mirror them so the persisted file is
  // self-describing.
  // Fill memory and disk with time steps in the background. With this off,
  // slider-driven loads are still cached in both tiers. How much gets warmed is
  // bounded by the RAM and disk cache limits, so there is no separate depth or
  // fill-cache setting.
  bool prefetchEnabled = true;
  // Show queued/loading/failed on the time slider strip, not just cached.
  bool showDetailedCacheStatus = false;

  // Playback.
  float playbackFps = 10.0f;
  bool playbackLoop = true;
  // True keeps a steady frame rate by skipping time steps that are not loaded;
  // false waits for every one.
  bool playbackDropFrames = false;
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
