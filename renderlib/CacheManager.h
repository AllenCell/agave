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

  // Notified when an entry leaves the in-memory tier, so a caller tracking
  // per-timepoint cache state can mark it uncached again instead of polling.
  // Invoked with no CacheManager lock held, so an observer may call back in.
  class IEvictionObserver
  {
  public:
    virtual ~IEvictionObserver() = default;
    virtual void onEvictedFromMemory(const CacheKey& key) = 0;
  };
  void addEvictionObserver(IEvictionObserver* observer);
  void removeEvictionObserver(IEvictionObserver* observer);

  // Protect an entry from eviction. Refcounted, so nested pins are safe; every
  // pin needs a matching unpin. Pinning is keyed, not entry-based: pinning a key
  // that is not resident yet still protects it once it is stored, which avoids a
  // race between storing a timepoint and pinning it.
  //
  // Used to keep the timepoint currently on screen resident while prefetch fills
  // the cache around it.
  void pin(const LoadSpec& loadSpec);
  void unpin(const LoadSpec& loadSpec);
  bool isPinned(const LoadSpec& loadSpec) const;

  std::shared_ptr<ImageXYZC> findImage(const LoadSpec& loadSpec);
  void storeImage(const LoadSpec& loadSpec, const std::shared_ptr<ImageXYZC>& image);
  // Drop all entries from the in-memory cache. Disk cache is untouched.
  void clearMemoryCache();
  // Drop all entries from the disk cache (refuses if the cache directory is
  // missing the AGAVE marker file). Memory cache is untouched.
  void clearDiskCache();

  // A point-in-time view of how full each tier is. Reported in the GUI
  // statistics panel and used by prefetch to decide whether another timepoint
  // will fit. diskBytesUsed is only meaningful once the disk index has been
  // built (which happens lazily on first disk access).
  struct CacheUsage
  {
    std::uint64_t ramBytesUsed = 0;
    std::uint64_t ramBytesLimit = 0;
    std::uint64_t diskBytesUsed = 0;
    std::uint64_t diskBytesLimit = 0;
    std::size_t ramEntryCount = 0;
    std::size_t diskEntryCount = 0;
  };
  CacheUsage getUsage() const;

  std::uint64_t getRamBytesUsed() const;
  // How many more bytes fit before eviction would begin. Zero once the tier is
  // at or over its limit. Prefetch throttles on this rather than letting LRU
  // evict timepoints the playhead has not reached yet.
  std::uint64_t getRamBytesAvailable() const;

  CacheStats getStats() const;
  void resetStats();

private:
  // Verify that `path` is (or can be made) a writable directory. Creates the
  // directory if it does not exist, then probes it by writing and deleting a
  // small marker file. Returns false on any failure. Probed once, at
  // initialize() time, since the cache root is fixed for the process lifetime.
  static bool canWriteCacheDir(const std::string& path);

  CacheKey makeKey(const LoadSpec& loadSpec) const;
  std::string keyToString(const CacheKey& key) const;
  std::string diskCacheId(const CacheKey& key) const;
  std::uint64_t estimateImageBytes(const ImageXYZC& image) const;
  void touchEntry(std::list<CacheKey>::iterator it);
  // Precondition: caller must hold m_mutex. Appends every key it drops to
  // `evicted`; the caller is responsible for notifying observers after
  // releasing the lock.
  void evictIfNeededLocked(std::uint64_t incomingBytes, std::vector<CacheKey>& evicted);
  // Precondition: caller must NOT hold m_mutex.
  void notifyEvicted(const std::vector<CacheKey>& keys);
  void storeImageInMemory(const CacheKey& key, const std::shared_ptr<ImageXYZC>& image);

  std::shared_ptr<ImageXYZC> loadFromDisk(const CacheKey& key, const CacheConfig& config, const std::string& cacheDir);
  void storeToDisk(const CacheKey& key,
                   const std::shared_ptr<ImageXYZC>& image,
                   const CacheConfig& config,
                   const std::string& cacheDir);
  void loadDiskIndex(const CacheConfig& config, const std::string& cacheDir);
  void evictDiskIfNeeded(const CacheConfig& config, std::uint64_t incomingBytes);
  std::uint64_t directorySizeBytes(const std::string& path) const;
  // Writes a marker file to a directory we manage as our own disk cache root.
  // clearDiskCache refuses to delete anything unless this marker is present,
  // protecting against accidental wipes of user-typed paths (e.g. "C:\").
  void writeCacheMarker(const std::string& path) const;
  bool isAgaveCacheDir(const std::string& path) const;

  mutable std::mutex m_mutex;
  CacheConfig m_config;
  // The disk cache root, fixed at construction.
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

  // Pin refcounts by key. Deliberately independent of m_entries so a pin can be
  // taken before the entry exists and still applies once it is stored.
  std::unordered_map<CacheKey, std::uint32_t, CacheKeyHash> m_pinned;

  std::vector<IEvictionObserver*> m_evictionObservers;

  struct DiskEntry
  {
    std::string path;
    std::uint64_t bytes = 0;
    std::uint64_t lastAccess = 0;
  };

  std::unordered_map<std::string, DiskEntry> m_diskEntries;
  std::uint64_t m_currentDiskBytes = 0;

  // Tracks the root the in-memory index was last built against (used to
  // decide when a rebuild is needed).
  // Set to m_cacheDir when the in-memory disk index is built, such as on startup.
  // Clear this value to signal that the in-memory disk index is no longer valid.
  std::string m_diskIndexRoot;

  CacheStats m_stats;
};
