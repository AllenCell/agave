#pragma once

#include <cstdint>

// Runtime-tunable cache settings. The disk cache *location* is intentionally
// not part of this struct: it is resolved once at app startup and registered
// via CacheManager::initialize(). Keeping it out of CacheConfig means
// the per-apply settings (enable/limits) can change freely without ever
// re-pointing the cache root.
struct CacheConfig
{
  bool enabled = false;
  bool enableDisk = false;
  std::uint64_t maxRamBytes = 0;
  std::uint64_t maxDiskBytes = 0;
};
