#include "CacheManager.h"

#include "ImageXYZC.h"
#include "Logging.h"

#include "tensorstore/array.h"
#include "tensorstore/context.h"
#include "tensorstore/open.h"
#include "tensorstore/tensorstore.h"

// must include after tensorstore so that tensorstore picks up its own internal json impl
#include "json/json.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <functional>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace {

// Marker file written into any directory we manage as our own disk cache root.
// clearDiskCache requires this file to be present before it will delete
// anything, which protects against the user pointing the cache dir at a path
// like "C:\" or "/home/me" and then clicking "Clear disk cache".
constexpr const char* kCacheMarkerFilename = ".agave-cache-dir";

// The magic constant 0x9e3779b9 is 2^32 / golden_ratio, used to spread out the bits of the hash value.
// This is a common technique for combining hash values of multiple fields into a single hash.
// See also https://softwareengineering.stackexchange.com/a/402543
inline void
hashCombine(std::size_t& seed, std::size_t value)
{
  seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

std::uint64_t
nowMillis()
{
  return static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string
toHex(std::size_t value)
{
  std::ostringstream stream;
  stream << std::hex << value;
  return stream.str();
}

// Paths beginning with these schemes are treated as remote; we don't try to
// stat them and the cache key omits mtime/size for them.
bool
isRemotePath(const std::string& path)
{
  return path.rfind("http", 0) == 0 || path.rfind("s3:", 0) == 0 || path.rfind("gs:", 0) == 0;
}

// Normalize a filepath into a canonical form for cache key generation. Goals:
//   - "/some/dir/./foo", "/some//dir//foo", and "/some/dir/x/../foo" all
//     produce the same key (lexically_normal collapses these).
//   - On Windows, "C:/foo", "C:\foo", and "c:\foo" all produce the same key
//     (path treats both separators; lowercase normalizes case).
//
// We deliberately use lexically_normal (purely textual) rather than
// weakly_canonical, because the latter resolves relative paths against the
// process CWD — which would make bare names like "my_in_memory_array" passed
// to loadFromArray_4D produce different keys when CWD changes. We also pass
// remote URLs through unchanged, since lexically_normal would mangle the
// "://" portion into "//".
std::string
normalizeFilepath(const std::string& path)
{
  if (path.empty() || isRemotePath(path)) {
    return path;
  }
  std::filesystem::path p(path);
  std::string normalized = p.lexically_normal().generic_string();
#ifdef _WIN32
  // NTFS and FAT are conventionally case-insensitive. (Case-sensitive
  // Windows configurations — per-directory case sensitivity flag, ReFS,
  // WSL paths reached via \\wsl$ — will see incorrect cache hits between
  // case-only-different paths. Accept this for the common case.)
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
#endif
  return normalized;
}

// Returns (mtime_ns, file_size). Either or both may be 0 if the path is
// remote, missing, or otherwise unreadable. For directories (zarr) file_size
// is 0; mtime is the directory's last_write_time, which most filesystems
// update when entries are added/removed at the top level. This is best-effort
// invalidation — a zarr whose chunks were rewritten without touching the
// root directory will not be invalidated by this check.
std::pair<std::uint64_t, std::uint64_t>
statForKey(const std::string& path)
{
  if (path.empty() || isRemotePath(path)) {
    return { 0, 0 };
  }
  std::error_code ec;
  std::filesystem::file_status status = std::filesystem::status(path, ec);
  if (ec || !std::filesystem::exists(status)) {
    return { 0, 0 };
  }
  std::uint64_t mtimeNs = 0;
  auto writeTime = std::filesystem::last_write_time(path, ec);
  if (!ec) {
    mtimeNs = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(writeTime.time_since_epoch()).count());
  }
  std::uint64_t size = 0;
  if (std::filesystem::is_regular_file(status)) {
    auto s = std::filesystem::file_size(path, ec);
    if (!ec) {
      size = static_cast<std::uint64_t>(s);
    }
  }
  return { mtimeNs, size };
}

std::vector<std::string>
channelNamesFromImage(const ImageXYZC& image)
{
  std::vector<std::string> names;
  names.reserve(image.sizeC());
  for (size_t i = 0; i < image.sizeC(); ++i) {
    names.push_back(image.channel(static_cast<uint32_t>(i))->m_name);
  }
  return names;
}

} // namespace

bool
CacheKey::operator==(const CacheKey& other) const
{
  return filepath == other.filepath && subpath == other.subpath && scene == other.scene && time == other.time &&
         channels == other.channels && minx == other.minx && maxx == other.maxx && miny == other.miny &&
         maxy == other.maxy && minz == other.minz && maxz == other.maxz && isImageSequence == other.isImageSequence &&
         fileMtimeNs == other.fileMtimeNs && fileSize == other.fileSize;
}

std::size_t
CacheKeyHash::operator()(const CacheKey& key) const
{
  std::size_t seed = 0;
  hashCombine(seed, std::hash<std::string>{}(key.filepath));
  hashCombine(seed, std::hash<std::string>{}(key.subpath));
  hashCombine(seed, std::hash<std::uint32_t>{}(key.scene));
  hashCombine(seed, std::hash<std::uint32_t>{}(key.time));
  hashCombine(seed, std::hash<std::uint32_t>{}(key.minx));
  hashCombine(seed, std::hash<std::uint32_t>{}(key.maxx));
  hashCombine(seed, std::hash<std::uint32_t>{}(key.miny));
  hashCombine(seed, std::hash<std::uint32_t>{}(key.maxy));
  hashCombine(seed, std::hash<std::uint32_t>{}(key.minz));
  hashCombine(seed, std::hash<std::uint32_t>{}(key.maxz));
  hashCombine(seed, std::hash<bool>{}(key.isImageSequence));
  hashCombine(seed, std::hash<std::uint64_t>{}(key.fileMtimeNs));
  hashCombine(seed, std::hash<std::uint64_t>{}(key.fileSize));
  for (auto ch : key.channels) {
    hashCombine(seed, std::hash<std::uint32_t>{}(ch));
  }
  return seed;
}

namespace {
// Storage for the process-wide singleton. A unique_ptr (rather than a Meyers
// static) lets initialize() inject the cache directory at construction time.
std::unique_ptr<CacheManager>&
singletonSlot()
{
  static std::unique_ptr<CacheManager> slot;
  return slot;
}
} // namespace

CacheManager::CacheManager(std::string cacheDir)
  : m_cacheDir(std::move(cacheDir))
{
}

CacheManager::~CacheManager()
{
  stopDiskWriter();
}

bool
CacheManager::canWriteCacheDir(const std::string& path)
{
  if (path.empty()) {
    return false;
  }

  std::error_code ec;
  std::filesystem::path dir(path);
  if (!std::filesystem::exists(dir, ec)) {
    if (!std::filesystem::create_directories(dir, ec) || ec) {
      LOG_WARNING << "canWriteCacheDir: failed to create " << path << ": " << ec.message();
      return false;
    }
  } else if (!std::filesystem::is_directory(dir, ec) || ec) {
    LOG_WARNING << "canWriteCacheDir: not a directory: " << path;
    return false;
  }

  std::filesystem::path testPath = dir / ".agave_cache_write_test";
  {
    std::ofstream testFile(testPath, std::ios::binary | std::ios::trunc);
    if (!testFile.is_open()) {
      return false;
    }
    testFile << "test";
    if (!testFile.good()) {
      testFile.close();
      std::filesystem::remove(testPath, ec);
      return false;
    }
  }
  std::filesystem::remove(testPath, ec);
  return !ec;
}

void
CacheManager::initialize(const std::string& cacheDir)
{
  if (singletonSlot()) {
    throw std::logic_error("CacheManager::initialize() called more than once; the cache directory is fixed for the "
                           "lifetime of the process.");
  }

  // The cache root is fixed for the lifetime of the process. If the directory can't be
  // created or written, leave the root unset so the disk tier stays inert
  // regardless of what any later CacheConfig requests.
  std::string root = cacheDir;
  if (!root.empty() && !canWriteCacheDir(root)) {
    LOG_WARNING << "Disk cache disabled: cache directory not writable: " << root;
    root.clear();
  }
  singletonSlot() = std::make_unique<CacheManager>(root);
}

CacheManager&
CacheManager::instance()
{
  auto& slot = singletonSlot();
  if (!slot) {
    // initialize() was never called (e.g. a context that does no caching);
    // fall back to a RAM-only, disk-inert manager.
    slot = std::make_unique<CacheManager>(std::string{});
  }
  return *slot;
}

std::string
CacheManager::getCacheDirectory() const
{
  std::scoped_lock lock(m_mutex);
  return m_cacheDir;
}

void
CacheManager::setConfig(const CacheConfig& config)
{
  CacheConfig configCopy;
  std::string cacheDirCopy;
  bool rebuildDiskIndex = false;
  {
    std::scoped_lock lock(m_mutex);
    m_config = config;
    if (!m_config.enabled) {
      m_entries.clear();
      m_lruKeys.clear();
      m_currentRamBytes = 0;
      m_diskEntries.clear();
      m_currentDiskBytes = 0;
      m_diskIndexRoot.clear();
      return;
    }

    if (!m_config.enableDisk || m_cacheDir.empty()) {
      m_diskEntries.clear();
      m_currentDiskBytes = 0;
      m_diskIndexRoot.clear();
    } else if (m_diskIndexRoot != m_cacheDir) {
      m_diskEntries.clear();
      m_currentDiskBytes = 0;
      m_diskIndexRoot = m_cacheDir;
      rebuildDiskIndex = true;
      configCopy = m_config;
      cacheDirCopy = m_cacheDir;
    }
  }

  if (rebuildDiskIndex) {
    loadDiskIndex(configCopy, cacheDirCopy);
    evictDiskIfNeeded(configCopy, 0);
  }

  // A tightened maxRamBytes can push the tier over its new limit, so trim it
  // here rather than waiting for the next store.
  std::vector<CacheKey> evicted;
  {
    std::scoped_lock lock(m_mutex);
    evictIfNeededLocked(0, evicted);
  }
  notifyEvicted(evicted);
}

CacheConfig
CacheManager::getConfig() const
{
  std::scoped_lock lock(m_mutex);
  return m_config;
}

std::shared_ptr<ImageXYZC>
CacheManager::findImage(const LoadSpec& loadSpec)
{
  CacheConfig configCopy;
  std::string cacheDirCopy;
  CacheKey key = makeKey(loadSpec);
  {
    std::scoped_lock lock(m_mutex);
    configCopy = m_config;
    cacheDirCopy = m_cacheDir;
    if (m_config.enabled && m_config.maxRamBytes > 0) {
      auto it = m_entries.find(key);
      if (it != m_entries.end()) {
        touchEntry(it->second.lruIt);
        m_stats.ramHits++;
        LOG_DEBUG << "Cache stats: ram_hits=" << m_stats.ramHits << " disk_hits=" << m_stats.diskHits
                  << " misses=" << m_stats.misses << " disk_writes=" << m_stats.diskWrites;
        // NOTE: we deliberately do not refresh the matching DiskEntry's
        // lastAccess (or its on-disk meta.json) on a RAM hit. The disk LRU
        // is only consulted when an entry has fallen out of RAM, and
        // loadFromDisk refreshes the disk lastAccess at that point — so
        // within a session the disk bookkeeping is fresh whenever it
        // actually matters.
        //
        // TODO: edge case — an entry that stays RAM-resident for an entire
        // session never has its disk lastAccess bumped, so at the next
        // session start it can look "older" than entries that were only
        // served from disk in the previous session, and may be the first
        // thing evicted from disk on cold start. If that ever shows up as
        // a real cold-start problem, fix by bumping the in-memory
        // DiskEntry.lastAccess here (no on-disk write needed) and letting
        // the existing flush-on-eviction-or-shutdown path persist it.
        return it->second.image;
      }
    }
  }

  if (configCopy.enabled && configCopy.enableDisk && configCopy.maxDiskBytes > 0 && !cacheDirCopy.empty()) {
    auto diskImage = loadFromDisk(key, configCopy, cacheDirCopy);
    if (diskImage) {
      {
        std::scoped_lock lock(m_mutex);
        m_stats.diskHits++;
        LOG_DEBUG << "Cache stats: ram_hits=" << m_stats.ramHits << " disk_hits=" << m_stats.diskHits
                  << " misses=" << m_stats.misses << " disk_writes=" << m_stats.diskWrites;
      }
      storeImageInMemory(key, diskImage);
      return diskImage;
    }
  }

  {
    std::scoped_lock lock(m_mutex);
    m_stats.misses++;
    LOG_DEBUG << "Cache stats: ram_hits=" << m_stats.ramHits << " disk_hits=" << m_stats.diskHits
              << " misses=" << m_stats.misses << " disk_writes=" << m_stats.diskWrites;
  }

  return nullptr;
}

bool
CacheManager::storeImage(const LoadSpec& loadSpec, const std::shared_ptr<ImageXYZC>& image)
{
  return storeImageInternal(loadSpec, image, /*intoMemory=*/true);
}

bool
CacheManager::storeImageOnDiskOnly(const LoadSpec& loadSpec, const std::shared_ptr<ImageXYZC>& image)
{
  return storeImageInternal(loadSpec, image, /*intoMemory=*/false);
}

bool
CacheManager::storeImageInternal(const LoadSpec& loadSpec, const std::shared_ptr<ImageXYZC>& image, bool intoMemory)
{
  if (!image) {
    return false;
  }

  CacheConfig configCopy;
  std::string cacheDirCopy;
  {
    std::scoped_lock lock(m_mutex);
    configCopy = m_config;
    cacheDirCopy = m_cacheDir;
  }

  const auto key = makeKey(loadSpec);

  // Memory first, so the volume is usable immediately. The disk write is a
  // full-volume tensorstore write; performing it inline made every prefetched
  // time step wait on disk before the frame was even available.
  if (intoMemory) {
    storeImageInMemory(key, image);
  }

  if (configCopy.enabled && configCopy.enableDisk && configCopy.maxDiskBytes > 0 && !cacheDirCopy.empty()) {
    const std::uint64_t bytes = estimateImageBytes(*image);
    return enqueueDiskWrite(PendingDiskWrite{ key, image, configCopy, cacheDirCopy, bytes });
  }
  // No disk tier configured, so nothing was refused.
  return true;
}

void
CacheManager::clearMemoryCache()
{
  // Clears pinned entries too: this is an explicit "drop everything" request,
  // not eviction under pressure. Pin refcounts are left alone, so a caller
  // holding a pin still has it and will protect the entry once it is reloaded.
  std::vector<CacheKey> evicted;
  {
    std::scoped_lock lock(m_mutex);
    evicted.assign(m_lruKeys.begin(), m_lruKeys.end());
    m_entries.clear();
    m_lruKeys.clear();
    m_currentRamBytes = 0;
  }
  notifyEvicted(evicted);
}

void
CacheManager::clearDiskCache()
{
  // Drop anything queued for writing first, and wait for a write already in
  // progress. Otherwise a pending write would land after the wipe and leave
  // entries behind that the user just asked to delete.
  {
    std::unique_lock<std::mutex> lock(m_diskQueueMutex);
    m_diskQueue.clear();
    m_pendingDiskBytes = 0;
    m_diskQueueSpace.notify_all();
    m_diskQueueDrained.wait(lock, [this] { return !m_diskWriteInProgress; });
  }

  std::string cacheDir;
  std::vector<std::string> knownEntryPaths;
  {
    std::scoped_lock lock(m_mutex);
    cacheDir = m_cacheDir;

    if (!isAgaveCacheDir(cacheDir)) {
      LOG_WARNING << "Refusing to clear disk cache: directory missing AGAVE cache marker file (" << kCacheMarkerFilename
                  << "): " << cacheDir;
      return;
    }

    knownEntryPaths.reserve(m_diskEntries.size());
    for (const auto& kv : m_diskEntries) {
      knownEntryPaths.push_back(kv.second.path);
    }
    m_diskEntries.clear();
    m_currentDiskBytes = 0;
  }

  if (cacheDir.empty()) {
    return;
  }

  // Remove the per-entry subdirectories we know about.
  for (const auto& path : knownEntryPaths) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }

  // Also remove any orphan per-entry subdirectories left behind by prior
  // sessions or partial writes. We only touch subdirectories that contain a
  // meta.json (i.e. look like cache entries we wrote) — anything else the user
  // may have placed in the cache dir is preserved.
  std::error_code dirEc;
  for (auto it = std::filesystem::directory_iterator(cacheDir, dirEc);
       it != std::filesystem::directory_iterator() && !dirEc;
       it.increment(dirEc)) {
    if (!it->is_directory()) {
      continue;
    }
    std::filesystem::path metaPath = it->path() / "meta.json";
    std::error_code existEc;
    if (std::filesystem::exists(metaPath, existEc)) {
      std::error_code rmEc;
      std::filesystem::remove_all(it->path(), rmEc);
    }
  }
}

void
CacheManager::writeCacheMarker(const std::string& path) const
{
  if (path.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  std::filesystem::path marker = std::filesystem::path(path) / kCacheMarkerFilename;
  std::error_code existEc;
  if (std::filesystem::exists(marker, existEc)) {
    return;
  }
  std::ofstream out(marker.string(), std::ios::trunc);
  if (out) {
    out << "AGAVE disk cache root. Safe to delete this directory and its contents.\n";
  }
}

bool
CacheManager::isAgaveCacheDir(const std::string& path) const
{
  if (path.empty()) {
    return false;
  }
  std::error_code ec;
  std::filesystem::path marker = std::filesystem::path(path) / kCacheMarkerFilename;
  return std::filesystem::exists(marker, ec);
}

CacheManager::CacheUsage
CacheManager::getUsage() const
{
  std::scoped_lock lock(m_mutex);
  CacheUsage usage;
  usage.ramBytesUsed = m_currentRamBytes;
  usage.ramBytesLimit = m_config.maxRamBytes;
  usage.diskBytesUsed = m_currentDiskBytes;
  usage.diskBytesLimit = m_config.enableDisk ? m_config.maxDiskBytes : 0;
  usage.ramEntryCount = m_entries.size();
  usage.diskEntryCount = m_diskEntries.size();
  return usage;
}

std::uint64_t
CacheManager::getRamBytesUsed() const
{
  std::scoped_lock lock(m_mutex);
  return m_currentRamBytes;
}

std::uint64_t
CacheManager::getRamBytesAvailable() const
{
  std::scoped_lock lock(m_mutex);
  if (m_currentRamBytes >= m_config.maxRamBytes) {
    return 0;
  }
  return m_config.maxRamBytes - m_currentRamBytes;
}

CacheManager::CacheStats
CacheManager::getStats() const
{
  std::scoped_lock lock(m_mutex);
  return m_stats;
}

namespace {
// Bounded by SHUTDOWN exposure, not by memory. stopDiskWriter abandons the
// backlog so quitting stays instant, which makes this exactly the number of
// frames that can be lost by quitting mid-warm.
//
// Memory is not the constraint it once appeared to be: volumes queued via
// storeImage are already resident in the RAM tier, so their shared_ptr costs
// nothing extra until eviction. Only storeImageOnDiskOnly writes add memory the
// RAM budget does not know about, and those are issued one at a time.
constexpr std::size_t kMaxPendingDiskWrites = 8;
} // namespace

bool
CacheManager::enqueueDiskWrite(PendingDiskWrite&& write)
{
  std::uint64_t pendingBytes = 0;
  {
    std::unique_lock<std::mutex> lock(m_diskQueueMutex);
    if (m_diskWriterStop) {
      return false;
    }
    // Back-pressure, never drop. A dropped write loses the frame from the disk
    // tier permanently: RAM eviction is a pure drop and never writes on the way
    // out, so there is no second chance. Throttling the producer to disk speed is
    // the correct response to a disk that cannot keep up.
    m_diskQueueSpace.wait(lock,
                          [this] { return m_diskWriterStop || m_diskQueue.size() < kMaxPendingDiskWrites; });
    if (m_diskWriterStop) {
      return false;
    }
    pendingBytes = m_pendingDiskBytes;
  }

  // Reserve the space before queueing, counting everything already queued, so
  // eviction makes room for the whole backlog rather than just the entry about
  // to be written. Done outside the queue lock because it takes m_mutex.
  if (!reserveDiskSpace(write.config, write.bytes, pendingBytes)) {
    return false;
  }

  {
    std::unique_lock<std::mutex> lock(m_diskQueueMutex);
    if (m_diskWriterStop) {
      return false;
    }
    m_pendingDiskBytes += write.bytes;
    m_diskQueue.push_back(std::move(write));

    // Started lazily so a RAM-only cache -- which is what the tests and any
    // disk-disabled configuration use -- never spawns a thread at all.
    if (!m_diskWriterThread.joinable()) {
      m_diskWriterThread = std::thread([this] { diskWriterMain(); });
    }
  }
  m_diskQueueWake.notify_one();
  return true;
}

bool
CacheManager::reserveDiskSpace(const CacheConfig& config, std::uint64_t bytes, std::uint64_t pendingBytes)
{
  if (!config.enableDisk || config.maxDiskBytes == 0 || bytes == 0) {
    return false;
  }
  if (bytes + pendingBytes > config.maxDiskBytes) {
    // Will not fit even with the tier emptied, so there is nothing eviction can
    // do. Refuse rather than evict everything and still overshoot.
    LOG_WARNING << "Disk cache: refusing " << bytes << " byte write; " << pendingBytes
                << " bytes already queued against a cap of " << config.maxDiskBytes;
    return false;
  }
  evictDiskIfNeeded(config, bytes + pendingBytes);
  std::scoped_lock lock(m_mutex);
  return (m_currentDiskBytes + pendingBytes + bytes) <= config.maxDiskBytes;
}

void
CacheManager::diskWriterMain()
{
  std::unique_lock<std::mutex> lock(m_diskQueueMutex);
  while (true) {
    m_diskQueueWake.wait(lock, [this] { return m_diskWriterStop || !m_diskQueue.empty(); });
    if (m_diskWriterStop && m_diskQueue.empty()) {
      return;
    }
    if (m_diskQueue.empty()) {
      continue;
    }

    PendingDiskWrite write = std::move(m_diskQueue.front());
    m_diskQueue.pop_front();
    const std::uint64_t writeBytes = write.bytes;
    m_diskWriteInProgress = true;
    m_inProgressDiskId = diskCacheId(write.key);
    // A slot is free the moment the entry leaves the queue, so release a producer
    // waiting on one -- it does not have to wait for the write itself.
    m_diskQueueSpace.notify_one();

    // Write without the queue lock so producers never block on disk I/O.
    lock.unlock();
    try {
      storeToDisk(write.key, write.image, write.config, write.cacheDir);
    } catch (std::exception& e) {
      LOG_ERROR << "Disk cache write failed: " << e.what();
    } catch (...) {
      LOG_ERROR << "Disk cache write failed";
    }
    // Release the volume before re-taking the lock, so a queued image is not
    // held alive any longer than necessary.
    write.image.reset();
    lock.lock();

    // storeToDisk has folded these bytes into m_currentDiskBytes on success, or
    // written nothing at all on failure. Either way they are no longer pending.
    m_pendingDiskBytes = m_pendingDiskBytes >= writeBytes ? m_pendingDiskBytes - writeBytes : 0;
    m_diskWriteInProgress = false;
    m_inProgressDiskId.clear();
    m_diskQueueDrained.notify_all();
  }
}

void
CacheManager::stopDiskWriter()
{
  {
    std::scoped_lock lock(m_diskQueueMutex);
    m_diskWriterStop = true;
    // Abandon anything still queued rather than making shutdown wait on a
    // backlog of full-volume writes. Deliberate: quitting stays instant, and at
    // most kMaxPendingDiskWrites frames are re-fetched in a later session. A
    // later session probes the filesystem, so it correctly sees them as absent
    // rather than trusting a write that never landed.
    m_diskQueue.clear();
    m_pendingDiskBytes = 0;
  }
  m_diskQueueWake.notify_all();
  // Release any producer blocked waiting for a slot, or the join below deadlocks.
  m_diskQueueSpace.notify_all();
  if (m_diskWriterThread.joinable()) {
    m_diskWriterThread.join();
  }
}

void
CacheManager::flushDiskWrites()
{
  std::unique_lock<std::mutex> lock(m_diskQueueMutex);
  m_diskQueueDrained.wait(lock, [this] { return m_diskQueue.empty() && !m_diskWriteInProgress; });
}

std::size_t
CacheManager::pendingDiskWrites() const
{
  std::scoped_lock lock(m_diskQueueMutex);
  return m_diskQueue.size() + (m_diskWriteInProgress ? 1 : 0);
}

std::uint64_t
CacheManager::pendingDiskBytes() const
{
  std::scoped_lock lock(m_diskQueueMutex);
  return m_pendingDiskBytes;
}

std::uint64_t
CacheManager::droppedDiskWrites() const
{
  std::scoped_lock lock(m_diskQueueMutex);
  return m_droppedDiskWrites;
}

void
CacheManager::resetStats()
{
  std::scoped_lock lock(m_mutex);
  m_stats = CacheStats{};
}

CacheKey
CacheManager::makeKey(const LoadSpec& loadSpec) const
{
  CacheKey key;
  key.filepath = normalizeFilepath(loadSpec.filepath);
  key.subpath = loadSpec.subpath;
  key.scene = loadSpec.scene;
  key.time = loadSpec.time;
  key.channels = loadSpec.channels;
  key.minx = loadSpec.minx;
  key.maxx = loadSpec.maxx;
  key.miny = loadSpec.miny;
  key.maxy = loadSpec.maxy;
  key.minz = loadSpec.minz;
  key.maxz = loadSpec.maxz;
  key.isImageSequence = loadSpec.isImageSequence;
  // Use the normalized filepath for stat() too so equivalent paths produce
  // identical fileMtimeNs / fileSize.
  auto stat = statForKey(key.filepath);
  key.fileMtimeNs = stat.first;
  key.fileSize = stat.second;
  return key;
}

std::string
CacheManager::keyToString(const CacheKey& key) const
{
  std::ostringstream stream;
  stream << key.filepath << "|" << key.subpath << "|" << key.scene << "|" << key.time << "|";
  stream << key.minx << "," << key.maxx << "," << key.miny << "," << key.maxy << "," << key.minz << "," << key.maxz
         << "|" << (key.isImageSequence ? 1 : 0) << "|";
  stream << "m=" << key.fileMtimeNs << ",s=" << key.fileSize << "|";
  for (size_t i = 0; i < key.channels.size(); ++i) {
    if (i > 0) {
      stream << ",";
    }
    stream << key.channels[i];
  }
  return stream.str();
}

std::string
CacheManager::diskCacheId(const CacheKey& key) const
{
  std::size_t hashValue = std::hash<std::string>{}(keyToString(key));
  return toHex(hashValue);
}

std::uint64_t
CacheManager::estimateImageBytes(const ImageXYZC& image) const
{
  std::uint64_t bytesPerPixel = static_cast<std::uint64_t>(ImageXYZC::IN_MEMORY_BPP / 8);
  std::uint64_t numPixels = static_cast<std::uint64_t>(image.sizeX()) * static_cast<std::uint64_t>(image.sizeY()) *
                            static_cast<std::uint64_t>(image.sizeZ()) * static_cast<std::uint64_t>(image.sizeC());
  return numPixels * bytesPerPixel;
}

void
CacheManager::touchEntry(std::list<CacheKey>::iterator it)
{
  if (it == m_lruKeys.begin()) {
    return;
  }
  m_lruKeys.splice(m_lruKeys.begin(), m_lruKeys, it);
}

void
CacheManager::evictIfNeededLocked(std::uint64_t incomingBytes, std::vector<CacheKey>& evicted)
{
  if (m_config.maxRamBytes == 0) {
    return;
  }

  // Walk from least- to most-recently used, skipping pinned entries. If
  // everything still resident is pinned we stop and let the tier sit over its
  // limit rather than dropping data that is in use -- overshooting is
  // recoverable, evicting the displayed timepoint is not.
  auto it = m_lruKeys.end();
  while (it != m_lruKeys.begin()) {
    if ((m_currentRamBytes + incomingBytes) <= m_config.maxRamBytes) {
      return;
    }
    --it;

    auto entryIt = m_entries.find(*it);
    if (entryIt == m_entries.end()) {
      // LRU key with no matching entry: stale bookkeeping, just drop the key.
      // erase returns the following element, so the next --it lands on the
      // predecessor of what we removed.
      it = m_lruKeys.erase(it);
      continue;
    }

    if (m_pinned.find(*it) != m_pinned.end()) {
      // Leave it in place and keep walking toward the most-recently-used end.
      continue;
    }

    evicted.push_back(*it);
    m_currentRamBytes -= entryIt->second.bytes;
    m_entries.erase(entryIt);
    it = m_lruKeys.erase(it);
  }
}

void
CacheManager::notifyEvicted(const std::vector<CacheKey>& keys)
{
  if (keys.empty()) {
    return;
  }
  // Copy the observer list under the lock, then notify without it, so an
  // observer is free to call back into the cache.
  std::vector<IEvictionObserver*> observers;
  {
    std::scoped_lock lock(m_mutex);
    observers = m_evictionObservers;
  }
  for (IEvictionObserver* observer : observers) {
    for (const CacheKey& key : keys) {
      observer->onEvictedFromMemory(key);
    }
  }
}

void
CacheManager::addEvictionObserver(IEvictionObserver* observer)
{
  if (!observer) {
    return;
  }
  std::scoped_lock lock(m_mutex);
  if (std::find(m_evictionObservers.begin(), m_evictionObservers.end(), observer) == m_evictionObservers.end()) {
    m_evictionObservers.push_back(observer);
  }
}

void
CacheManager::removeEvictionObserver(IEvictionObserver* observer)
{
  std::scoped_lock lock(m_mutex);
  auto it = std::find(m_evictionObservers.begin(), m_evictionObservers.end(), observer);
  if (it != m_evictionObservers.end()) {
    m_evictionObservers.erase(it);
  }
}

void
CacheManager::pin(const LoadSpec& loadSpec)
{
  CacheKey key = makeKey(loadSpec);
  std::scoped_lock lock(m_mutex);
  ++m_pinned[key];
}

void
CacheManager::unpin(const LoadSpec& loadSpec)
{
  CacheKey key = makeKey(loadSpec);
  std::scoped_lock lock(m_mutex);
  auto it = m_pinned.find(key);
  if (it == m_pinned.end()) {
    LOG_WARNING << "CacheManager::unpin called for a key that was not pinned";
    return;
  }
  if (--it->second == 0) {
    m_pinned.erase(it);
  }
}

bool
CacheManager::containsInMemory(const LoadSpec& loadSpec) const
{
  CacheKey key = makeKey(loadSpec);
  std::scoped_lock lock(m_mutex);
  return m_entries.find(key) != m_entries.end();
}

bool
CacheManager::containsOnDisk(const LoadSpec& loadSpec) const
{
  CacheKey key = makeKey(loadSpec);
  CacheConfig configCopy;
  std::string cacheDirCopy;
  {
    std::scoped_lock lock(m_mutex);
    configCopy = m_config;
    cacheDirCopy = m_cacheDir;
  }
  if (!configCopy.enabled || !configCopy.enableDisk || cacheDirCopy.empty()) {
    return false;
  }

  const std::string id = diskCacheId(key);

  // A queued write counts as present. Its disk space is already reserved and it
  // will not be dropped, so reporting it absent would just make callers re-fetch
  // something that is on its way to disk. This is what closes the race between
  // RAM eviction (immediate) and the asynchronous write (not yet).
  {
    std::scoped_lock lock(m_diskQueueMutex);
    if (m_diskWriteInProgress && m_inProgressDiskId == id) {
      return true;
    }
    for (const auto& queued : m_diskQueue) {
      if (diskCacheId(queued.key) == id) {
        return true;
      }
    }
  }

  // Consult the filesystem rather than m_diskEntries. The index is built in
  // setConfig, but this must also be correct when setConfig was never called --
  // renderlib used without the GUI, and most tests. It is also what lets a fresh
  // session recognise a cache an earlier session warmed.
  std::error_code ec;
  std::filesystem::path entryPath = std::filesystem::path(cacheDirCopy) / id;
  return std::filesystem::exists(entryPath / "meta.json", ec);
}

bool
CacheManager::isPinned(const LoadSpec& loadSpec) const
{
  CacheKey key = makeKey(loadSpec);
  std::scoped_lock lock(m_mutex);
  return m_pinned.find(key) != m_pinned.end();
}

void
CacheManager::storeImageInMemory(const CacheKey& key, const std::shared_ptr<ImageXYZC>& image)
{
  std::vector<CacheKey> evicted;
  {
    std::scoped_lock lock(m_mutex);
    if (!m_config.enabled || m_config.maxRamBytes == 0) {
      return;
    }

    std::uint64_t bytes = estimateImageBytes(*image);
    if (bytes == 0 || bytes > m_config.maxRamBytes) {
      return;
    }

    auto existing = m_entries.find(key);
    if (existing != m_entries.end()) {
      m_currentRamBytes -= existing->second.bytes;
      m_lruKeys.erase(existing->second.lruIt);
      m_entries.erase(existing);
    }

    evictIfNeededLocked(bytes, evicted);

    m_lruKeys.push_front(key);
    CacheEntry entry;
    entry.image = image;
    entry.bytes = bytes;
    entry.lruIt = m_lruKeys.begin();
    m_entries.emplace(key, entry);
    m_currentRamBytes += bytes;
  }
  notifyEvicted(evicted);
}

std::shared_ptr<ImageXYZC>
CacheManager::loadFromDisk(const CacheKey& key, const CacheConfig& config, const std::string& cacheDir)
{
  if (!config.enableDisk || cacheDir.empty()) {
    return nullptr;
  }

  std::filesystem::path entryPath = std::filesystem::path(cacheDir) / diskCacheId(key);
  std::filesystem::path metaPath = entryPath / "meta.json";
  std::filesystem::path dataPath = entryPath / "data.zarr";
  if (!std::filesystem::exists(metaPath) || !std::filesystem::exists(dataPath)) {
    return nullptr;
  }

  nlohmann::json meta;
  try {
    std::ifstream metaFile(metaPath.string());
    metaFile >> meta;
  } catch (...) {
    return nullptr;
  }

  if (!meta.contains("key") || meta["key"].get<std::string>() != keyToString(key)) {
    return nullptr;
  }

  if (!meta.contains("sizeX") || !meta.contains("sizeY") || !meta.contains("sizeZ") || !meta.contains("sizeC")) {
    return nullptr;
  }

  std::uint32_t sizeX = meta["sizeX"].get<std::uint32_t>();
  std::uint32_t sizeY = meta["sizeY"].get<std::uint32_t>();
  std::uint32_t sizeZ = meta["sizeZ"].get<std::uint32_t>();
  std::uint32_t sizeC = meta["sizeC"].get<std::uint32_t>();
  std::uint32_t bpp = meta.value("bpp", static_cast<std::uint32_t>(ImageXYZC::IN_MEMORY_BPP));
  if (bpp != static_cast<std::uint32_t>(ImageXYZC::IN_MEMORY_BPP)) {
    LOG_ERROR << "Disk cache load: unsupported bpp " << bpp << " in " << metaPath.string() << " (expected "
              << ImageXYZC::IN_MEMORY_BPP << "); skipping cache entry";
    return nullptr;
  }
  float sx = meta.value("physicalSizeX", 1.0f);
  float sy = meta.value("physicalSizeY", 1.0f);
  float sz = meta.value("physicalSizeZ", 1.0f);
  std::string spatialUnits = meta.value("spatialUnits", std::string("units"));

  std::uint64_t bytes = static_cast<std::uint64_t>(sizeX) * static_cast<std::uint64_t>(sizeY) *
                        static_cast<std::uint64_t>(sizeZ) * static_cast<std::uint64_t>(sizeC) *
                        static_cast<std::uint64_t>(bpp / 8);
  if (bytes == 0) {
    return nullptr;
  }

  std::unique_ptr<uint8_t[]> data(new uint8_t[bytes]);

  auto openFuture =
    tensorstore::Open({ { "driver", "zarr3" }, { "kvstore", { { "driver", "file" }, { "path", dataPath.string() } } } },
                      tensorstore::OpenMode::open,
                      tensorstore::ReadWriteMode::read);
  const auto& result = openFuture.result();
  if (!result.ok()) {
    return nullptr;
  }

  auto store = result.value();
  std::vector<tensorstore::Index> shape = { sizeC, sizeZ, sizeY, sizeX };
  auto arr = tensorstore::Array(reinterpret_cast<uint16_t*>(data.get()), shape);
  auto readResult = tensorstore::Read(store, tensorstore::UnownedToShared(arr)).result();
  if (!readResult.ok()) {
    return nullptr;
  }

  ImageXYZC* image = new ImageXYZC(sizeX, sizeY, sizeZ, sizeC, bpp, data.release(), sx, sy, sz, spatialUnits);
  std::shared_ptr<ImageXYZC> sharedImage(image);

  if (meta.contains("channelNames")) {
    std::vector<std::string> channelNames;
    for (auto& item : meta["channelNames"]) {
      channelNames.push_back(item.get<std::string>());
    }
    image->setChannelNames(channelNames);
  }

  meta["lastAccess"] = nowMillis();
  try {
    std::ofstream metaOut(metaPath.string(), std::ios::trunc);
    metaOut << meta.dump(2);
  } catch (...) {
    // best effort
  }

  {
    std::scoped_lock lock(m_mutex);
    auto it = m_diskEntries.find(diskCacheId(key));
    if (it != m_diskEntries.end()) {
      it->second.lastAccess = meta["lastAccess"].get<std::uint64_t>();
    }
  }

  return sharedImage;
}

void
CacheManager::storeToDisk(const CacheKey& key,
                          const std::shared_ptr<ImageXYZC>& image,
                          const CacheConfig& config,
                          const std::string& cacheDir)
{
  if (!image || !config.enableDisk || cacheDir.empty()) {
    return;
  }

  std::uint64_t bytes = estimateImageBytes(*image);
  if (bytes == 0) {
    return;
  }
  if (bytes > config.maxDiskBytes) {
    // A single image larger than the entire disk cap; refuse rather than
    // evict everything and overshoot.
    LOG_WARNING << "Disk cache: skipping store of " << bytes << " byte image — larger than disk cap "
                << config.maxDiskBytes;
    return;
  }

  // Make room before writing, so we never temporarily exceed the cap on
  // disk and don't waste a large write that would have to be undone.
  evictDiskIfNeeded(config, bytes);

  writeCacheMarker(cacheDir);

  std::filesystem::path entryPath = std::filesystem::path(cacheDir) / diskCacheId(key);
  std::filesystem::path dataPath = entryPath / "data.zarr";
  std::filesystem::path metaPath = entryPath / "meta.json";
  std::error_code ec;
  std::filesystem::create_directories(entryPath, ec);

  std::uint32_t sizeX = static_cast<std::uint32_t>(image->sizeX());
  std::uint32_t sizeY = static_cast<std::uint32_t>(image->sizeY());
  std::uint32_t sizeZ = static_cast<std::uint32_t>(image->sizeZ());
  std::uint32_t sizeC = static_cast<std::uint32_t>(image->sizeC());
  std::uint32_t bpp = static_cast<std::uint32_t>(ImageXYZC::IN_MEMORY_BPP);

  std::vector<tensorstore::Index> shape = { sizeC, sizeZ, sizeY, sizeX };
  std::vector<tensorstore::Index> chunkShape = { 1,
                                                 std::min<tensorstore::Index>(16, sizeZ),
                                                 std::min<tensorstore::Index>(256, sizeY),
                                                 std::min<tensorstore::Index>(256, sizeX) };

  nlohmann::json schema = { { "dtype", "uint16" },
                            { "domain", { { "shape", shape } } },
                            { "chunk_layout",
                              { { "read_chunk", { { "shape", chunkShape } } },
                                { "write_chunk", { { "shape", chunkShape } } } } } };

  auto openFuture = tensorstore::Open<std::uint16_t, 4, tensorstore::ReadWriteMode::read_write>(
    { { "driver", "zarr3" },
      { "kvstore", { { "driver", "file" }, { "path", dataPath.string() } } },
      { "schema", schema } },
    tensorstore::OpenMode::create | tensorstore::OpenMode::open);
  const auto& result = openFuture.result();
  if (!result.ok()) {
    std::error_code rmEc;
    std::filesystem::remove_all(entryPath, rmEc);
    LOG_WARNING << "Disk cache store: tensorstore open failed for " << dataPath.string();
    return;
  }

  auto store = result.value();
  auto arr = tensorstore::Array(reinterpret_cast<uint16_t*>(image->ptr()), shape);
  auto writeResult = tensorstore::Write(tensorstore::UnownedToShared(arr), store).result();
  if (!writeResult.ok()) {
    std::error_code rmEc;
    std::filesystem::remove_all(entryPath, rmEc);
    LOG_WARNING << "Disk cache store: tensorstore write failed for " << dataPath.string();
    return;
  }

  std::uint64_t accessTime = nowMillis();
  nlohmann::json meta = { { "key", keyToString(key) },
                          { "sizeX", sizeX },
                          { "sizeY", sizeY },
                          { "sizeZ", sizeZ },
                          { "sizeC", sizeC },
                          { "bpp", bpp },
                          { "physicalSizeX", image->physicalSizeX() },
                          { "physicalSizeY", image->physicalSizeY() },
                          { "physicalSizeZ", image->physicalSizeZ() },
                          { "spatialUnits", image->spatialUnits() },
                          { "channelNames", channelNamesFromImage(*image) },
                          { "bytes", bytes },
                          { "lastAccess", accessTime } };

  try {
    std::ofstream metaOut(metaPath.string(), std::ios::trunc);
    metaOut << meta.dump(2);
  } catch (...) {
    std::error_code rmEc;
    std::filesystem::remove_all(entryPath, rmEc);
    LOG_WARNING << "Disk cache store: meta.json write failed for " << metaPath.string();
    return;
  }

  {
    std::scoped_lock lock(m_mutex);
    m_stats.diskWrites++;
    DiskEntry entry;
    entry.path = entryPath.string();
    entry.bytes = bytes;
    entry.lastAccess = accessTime;
    auto it = m_diskEntries.find(diskCacheId(key));
    if (it != m_diskEntries.end()) {
      if (m_currentDiskBytes >= it->second.bytes) {
        m_currentDiskBytes -= it->second.bytes;
      } else {
        m_currentDiskBytes = 0;
      }
    }
    m_diskEntries[diskCacheId(key)] = entry;
    m_currentDiskBytes += bytes;
  }
}

void
CacheManager::loadDiskIndex(const CacheConfig& config, const std::string& cacheDir)
{
  if (cacheDir.empty() || !config.enableDisk) {
    return;
  }

  std::filesystem::path root(cacheDir);
  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  if (!std::filesystem::exists(root)) {
    return;
  }
  writeCacheMarker(cacheDir);

  std::unordered_map<std::string, DiskEntry> entries;
  std::uint64_t totalBytes = 0;

  std::error_code iterEc;
  for (const auto& dirEntry : std::filesystem::directory_iterator(root, iterEc)) {
    if (iterEc) {
      LOG_WARNING << "loadDiskIndex: error reading cache directory " << root.string() << ": " << iterEc.message();
      break;
    }
    if (!dirEntry.is_directory()) {
      continue;
    }

    std::filesystem::path metaPath = dirEntry.path() / "meta.json";
    if (!std::filesystem::exists(metaPath)) {
      continue;
    }

    try {
      nlohmann::json meta;
      std::ifstream metaFile(metaPath.string());
      metaFile >> meta;
      if (!meta.contains("lastAccess")) {
        continue;
      }

      DiskEntry entry;
      entry.path = dirEntry.path().string();
      entry.lastAccess = meta["lastAccess"].get<std::uint64_t>();
      if (meta.contains("bytes")) {
        entry.bytes = meta["bytes"].get<std::uint64_t>();
      } else {
        entry.bytes = directorySizeBytes(entry.path);
      }

      std::string id = dirEntry.path().filename().string();
      entries[id] = entry;
      totalBytes += entry.bytes;
    } catch (...) {
      continue;
    }
  }

  std::scoped_lock lock(m_mutex);
  m_diskEntries = std::move(entries);
  m_currentDiskBytes = totalBytes;
}

void
CacheManager::evictDiskIfNeeded(const CacheConfig& config, std::uint64_t incomingBytes)
{
  if (!config.enableDisk || config.maxDiskBytes == 0) {
    return;
  }

  // Hold the lock for the entire eviction so a concurrent storeToDisk can't
  // re-populate an entry we're about to delete on disk (which would
  // otherwise let us nuke the fresh file). Each per-entry remove_all is a
  // single small zarr directory, so keeping the lock held is acceptable.
  std::scoped_lock lock(m_mutex);

  if ((m_currentDiskBytes + incomingBytes) <= config.maxDiskBytes) {
    return;
  }

  // Sort entries by lastAccess ascending so we evict the oldest first.
  std::vector<std::pair<std::uint64_t, std::string>> byAge;
  byAge.reserve(m_diskEntries.size());
  for (const auto& kv : m_diskEntries) {
    byAge.emplace_back(kv.second.lastAccess, kv.first);
  }
  std::sort(byAge.begin(), byAge.end());

  for (const auto& aged : byAge) {
    if ((m_currentDiskBytes + incomingBytes) <= config.maxDiskBytes) {
      break;
    }
    auto it = m_diskEntries.find(aged.second);
    if (it == m_diskEntries.end()) {
      continue;
    }
    std::string path = it->second.path;
    std::uint64_t bytes = it->second.bytes;
    if (m_currentDiskBytes >= bytes) {
      m_currentDiskBytes -= bytes;
    } else {
      m_currentDiskBytes = 0;
    }
    m_diskEntries.erase(it);

    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    if (ec) {
      // On Windows this can fail if another thread (e.g. loadFromDisk)
      // still holds tensorstore file handles into the same path. The
      // bookkeeping has already been updated to reflect eviction; the
      // stale files will be picked up by the next clearDiskCache.
      LOG_WARNING << "Disk cache eviction: failed to remove " << path << ": " << ec.message();
    }
  }
}

std::uint64_t
CacheManager::directorySizeBytes(const std::string& path) const
{
  std::uint64_t total = 0;
  std::error_code ec;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(path, ec)) {
    if (entry.is_regular_file()) {
      total += static_cast<std::uint64_t>(entry.file_size());
    }
  }
  return total;
}
