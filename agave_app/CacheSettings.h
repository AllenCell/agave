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
  bool prefetchEnabled = true;
  // How many time steps ahead of the current one to keep warm.
  std::uint32_t prefetchDepth = 4;
  // Ignore prefetchDepth and keep loading forward until the cache budget
  // throttles. Set by the "prefetch whole time series" option at load time.
  bool prefetchFillCache = false;
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
