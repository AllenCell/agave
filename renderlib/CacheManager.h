#pragma once

#include "CacheConfig.h"
#include "IFileReader.h"

#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class ImageXYZC;

struct CacheKey
{
  std::string filepath;
  std::string subpath;
  std::uint32_t scene = 0;
  std::uint32_t time = 0;
  std::vector<std::uint32_t> channels;
  std::uint32_t minx = 0;
  std::uint32_t maxx = 0;
  std::uint32_t miny = 0;
  std::uint32_t maxy = 0;
  std::uint32_t minz = 0;
  std::uint32_t maxz = 0;
  bool isImageSequence = false;
  // last_write_time of the filepath (or directory) at the time the key was
  // built, expressed as nanoseconds since epoch. Zero for remote URLs and for
  // paths we couldn't stat. Folding this into the key invalidates cache
  // entries when the source file is overwritten.
  std::uint64_t fileMtimeNs = 0;
  // file_size of filepath at the time the key was built. Zero for
  // directories (zarr) and remote URLs.
  std::uint64_t fileSize = 0;

  bool operator==(const CacheKey& other) const;
};

struct CacheKeyHash
{
  std::size_t operator()(const CacheKey& key) const;
};

class CacheManager
{
public:
  struct CacheStats
  {
    std::uint64_t ramHits = 0;
    std::uint64_t diskHits = 0;
    std::uint64_t misses = 0;
    std::uint64_t diskWrites = 0;
  };

  // Construct a cache rooted at `cacheDir`. An empty `cacheDir` disables the
  // disk tier (the cache is RAM-only). The directory is fixed for the lifetime
  // of the instance and is never derived from per-apply CacheConfig. Production
  // code uses the process-wide singleton (initialize() + instance()); this
  // constructor is public so tests can create isolated, throwaway caches with
  // their own directories.
  explicit CacheManager(std::string cacheDir = {});

  // The process-wide singleton. initialize() creates it rooted at `cacheDir`
  // and must be called exactly once, at app startup, before the cache is used;
  // a second call throws std::logic_error. The platform-appropriate path is
  // resolved by the GUI layer (renderlib has no Qt and so cannot resolve
  // QStandardPaths itself) and injected here. If initialize() is never called,
  // instance() lazily yields a RAM-only (disk-inert) manager.
  static void initialize(const std::string& cacheDir);
  static CacheManager& instance();
  std::string getCacheDirectory() const;

  void setConfig(const CacheConfig& config);
  CacheConfig getConfig() const;

  std::shared_ptr<ImageXYZC> findImage(const LoadSpec& loadSpec);
  void storeImage(const LoadSpec& loadSpec, const std::shared_ptr<ImageXYZC>& image);
  void clearMemoryCache();

  CacheStats getStats() const;
  void resetStats();

private:
  CacheKey makeKey(const LoadSpec& loadSpec) const;
  std::uint64_t estimateImageBytes(const ImageXYZC& image) const;
  void touchEntry(std::list<CacheKey>::iterator it);
  // Precondition: caller must hold m_mutex.
  void evictIfNeededLocked(std::uint64_t incomingBytes);
  void storeImageInMemory(const CacheKey& key, const std::shared_ptr<ImageXYZC>& image);

  mutable std::mutex m_mutex;
  CacheConfig m_config;
  // Optional cache root recorded at construction. Disk support is added in a
  // later stack slice; this commit only uses in-memory entries.
  const std::string m_cacheDir;
  std::uint64_t m_currentRamBytes = 0;
  std::list<CacheKey> m_lruKeys;

  struct CacheEntry
  {
    std::shared_ptr<ImageXYZC> image;
    std::uint64_t bytes = 0;
    std::list<CacheKey>::iterator lruIt;
  };

  std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> m_entries;
  CacheStats m_stats;
};
