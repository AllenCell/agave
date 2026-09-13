#pragma once

#include "CacheConfig.h"
#include "IFileReader.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class ImageXYZC;

// This contains enough information to uniquely identify a cache entry.
// Basically, the unit of cached volumes is a multichannel volume of a single timepoint.
struct CacheKey
{
  std::string filepath;
  // aka multiresolution level:
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
  // for loading tiff image series
  bool isImageSequence = false;
  // Store the last write time of the filepath (or directory) at the time the key was
  // built. Zero for remote URLs and for paths we couldn't stat.
  // (If the file was overwritten since being cached, we want a different cache key.)
  std::uint64_t fileModifiedTimeNs = 0;
  // file_size of filepath at the time the key was built. Zero for
  // directories (zarr) and remote URLs.
  // (If the file was overwritten since being cached, we want a different cache key.)
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
  ~CacheManager();

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
    // An entry left the DISK tier. Reports the disk-cache id rather than a
    // CacheKey, unlike its memory counterpart: the disk index is keyed by
    // diskCacheId and DiskEntry retains no key, while meta.json persists only the
    // opaque keyToString form -- so an entry evicted after a fresh start has no
    // CacheKey to hand back. Pair this with diskCacheIdFor() to map your own
    // specs onto it.
    virtual void onEvictedFromDisk(const std::string& diskCacheId) = 0;
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

  // Residency probe. Unlike findImage this does NOT count as a hit or a miss and
  // does NOT touch LRU order, so prefetch can reconcile its own bookkeeping
  // against the cache without distorting either.
  bool containsInMemory(const LoadSpec& loadSpec) const;

  // Is this entry in the disk tier, or queued to be written to it? Like
  // containsInMemory this is a pure probe: no load, no hit/miss accounting, no
  // LRU touch. Prefetch uses it to tell "never fetched" from "already safely on
  // disk", which is what lets it stop rather than endlessly pulling evicted
  // frames back into memory.
  //
  // Queued-but-unwritten counts as present. Without that there is a race: RAM
  // eviction is a pure drop and happens the instant space is needed, so a frame
  // evicted before its asynchronous write lands would probe as absent from both
  // tiers and be re-fetched forever. Reporting it present is honest because
  // enqueueDiskWrite reserves its disk space up front and never drops it.
  bool containsOnDisk(const LoadSpec& loadSpec) const;

  // The disk-cache identity for a spec, as reported by onEvictedFromDisk. Public
  // so an observer can build its own id -> domain-object map up front instead of
  // needing a reverse lookup on the eviction path. Note this calls makeKey, which
  // stats the source file.
  std::string diskCacheIdFor(const LoadSpec& loadSpec) const;

  std::shared_ptr<ImageXYZC> findImage(const LoadSpec& loadSpec);
  // Returns false when the DISK write was refused because it could not fit in
  // maxDiskBytes even after eviction. The memory store is independent: a false
  // return does not mean the image failed to cache in RAM. Returns true when
  // there is no disk tier configured -- nothing was refused.
  bool storeImage(const LoadSpec& loadSpec, const std::shared_ptr<ImageXYZC>& image);
  // Write to the disk tier only, leaving the memory tier untouched. Used to warm
  // the disk cache for time steps outside the memory window: routing those
  // through storeImage would evict the near time steps we actually want
  // resident, and would make them briefly show as in-memory.
  bool storeImageOnDiskOnly(const LoadSpec& loadSpec, const std::shared_ptr<ImageXYZC>& image);
  // Drop all entries from the in-memory cache. Disk cache is untouched.
  void clearMemoryCache();
  // Drop all entries from the disk cache (refuses if the cache directory is
  // missing the AGAVE marker file). Memory cache is untouched.
  void clearDiskCache();

  // A point-in-time view of how full each tier is. Reported in the GUI
  // statistics panel and used by prefetch to decide whether another timepoint
  // will fit. diskBytesUsed is meaningful once setConfig has run, which builds the
  // disk index for the configured root -- that is what lets a fresh session evict
  // a previous series by LRU rather than growing past the cap.
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

  // Volumes waiting to be written to the disk tier, plus one currently in
  // progress. Writes are asynchronous, so a non-zero value here is normal; the
  // queue applies count and byte back-pressure before accepting more.
  std::size_t pendingDiskWrites() const;
  // Bytes belonging to volumes queued for writing or currently being written.
  // Counted against maxDiskBytes alongside diskBytesUsed, so the tier never
  // overshoots its cap by the size of the backlog.
  std::uint64_t pendingDiskBytes() const;
  // Disk writes abandoned because the queue was full. Now structurally always
  // zero -- the queue applies back-pressure instead of dropping -- and retained
  // as a standing assertion that it stays that way. Note this does NOT count the
  // backlog abandoned at shutdown, which is deliberate (quit stays instant).
  std::uint64_t droppedDiskWrites() const;
  // Block until queued disk writes have completed. For tests and shutdown; not
  // needed in normal operation.
  void flushDiskWrites();

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
  void notifyEvictedFromMemory(const std::vector<CacheKey>& keys);
  // Precondition: caller must NOT hold m_mutex.
  void notifyEvictedFromDisk(const std::vector<std::string>& ids);
  void storeImageInMemory(const CacheKey& key, const std::shared_ptr<ImageXYZC>& image);
  bool storeImageInternal(const LoadSpec& loadSpec, const std::shared_ptr<ImageXYZC>& image, bool intoMemory);
  // Evicts as needed so `bytes` can be written on top of `pendingBytes` already
  // queued. Returns false if it will not fit even with the tier emptied.
  // Precondition: caller must NOT hold m_mutex or m_diskQueueMutex.
  bool reserveDiskSpace(const CacheConfig& config, std::uint64_t bytes, std::uint64_t pendingBytes);

  std::shared_ptr<ImageXYZC> loadFromDisk(const CacheKey& key, const CacheConfig& config, const std::string& cacheDir);
  void storeToDisk(const CacheKey& key,
                   const std::shared_ptr<ImageXYZC>& image,
                   const CacheConfig& config,
                   const std::string& cacheDir);
  void loadDiskIndex(const CacheConfig& config, const std::string& cacheDir);
  // Evicts, then notifies observers of what it dropped. The notification happens
  // after the lock is released, so this must not be called with m_mutex held.
  void evictDiskIfNeeded(const CacheConfig& config, std::uint64_t incomingBytes);
  // The locked body of the above. Appends every disk-cache id it drops to
  // `evictedIds`; the caller notifies. Takes m_mutex for the whole eviction so a
  // concurrent storeToDisk cannot repopulate an entry mid-delete.
  void evictDiskIfNeededCollecting(const CacheConfig& config,
                                   std::uint64_t incomingBytes,
                                   std::vector<std::string>& evictedIds);
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

  // --- Asynchronous disk writing ---
  //
  // Writing a volume to the disk tier is a full-volume tensorstore write. Doing
  // it inline made every prefetched time step pay that cost on the loader
  // thread before the frame was even available in memory. Instead the memory
  // tier is populated immediately and the disk write is handed to a single
  // low-priority writer thread.
  //
  // The queue is bounded and applies BACK-PRESSURE when full: the producer waits
  // for count and byte capacity rather than abandoning a write. Dropping is not
  // survivable here -- RAM eviction is a pure drop that never writes on the way
  // out, so a dropped write loses that frame from the disk tier permanently.
  //
  // Each queued entry also reserves its disk bytes (m_pendingDiskBytes) so
  // eviction makes room for the whole backlog and the tier never overshoots
  // maxDiskBytes by the size of the queue.
  struct PendingDiskWrite
  {
    CacheKey key;
    std::shared_ptr<ImageXYZC> image;
    CacheConfig config;
    std::string cacheDir;
    std::uint64_t bytes = 0;
  };

  // Returns false if the write was refused for lack of disk space, or if the
  // writer is shutting down.
  bool enqueueDiskWrite(PendingDiskWrite&& write);
  void diskWriterMain();
  void stopDiskWriter();

  // Separate from m_mutex: the writer holds this only to take work, never while
  // writing, and must not block cache lookups for the duration of a write.
  mutable std::mutex m_diskQueueMutex;
  std::condition_variable m_diskQueueWake;
  std::condition_variable m_diskQueueDrained;
  // Signalled whenever count or byte capacity frees, to release a producer
  // blocked on a full queue.
  std::condition_variable m_diskQueueSpace;
  std::deque<PendingDiskWrite> m_diskQueue;
  std::thread m_diskWriterThread;
  bool m_diskWriterStop = false;
  bool m_diskWriteInProgress = false;
  // The entry being written right now. It has already left m_diskQueue but is
  // not yet on the filesystem, so containsOnDisk must consult it too or there is
  // a window where a queued write probes as absent.
  std::string m_inProgressDiskId;
  // Bytes reserved by queued writes, including the one in progress. Guarded by
  // m_diskQueueMutex, NOT m_mutex: it moves in lockstep with the queue.
  std::uint64_t m_pendingDiskBytes = 0;
  std::uint64_t m_droppedDiskWrites = 0;
};
