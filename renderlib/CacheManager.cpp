#include "CacheManager.h"

#include "ImageXYZC.h"
#include "Logging.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <functional>
#include <fstream>
#include <memory>
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
  std::scoped_lock lock(m_mutex);
  m_config = config;
  if (!m_config.enabled) {
    m_entries.clear();
    m_lruKeys.clear();
    m_currentRamBytes = 0;
    m_diskEntries.clear();
    m_currentDiskBytes = 0;
    return;
  }
  if (!m_config.enableDisk || m_cacheDir.empty()) {
    m_diskEntries.clear();
    m_currentDiskBytes = 0;
  }
  evictIfNeededLocked(0);
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
  CacheKey key = makeKey(loadSpec);
  {
    std::scoped_lock lock(m_mutex);
    if (m_config.enabled && m_config.maxRamBytes > 0) {
      auto it = m_entries.find(key);
      if (it != m_entries.end()) {
        touchEntry(it->second.lruIt);
        m_stats.ramHits++;
        LOG_DEBUG << "Cache stats: ram_hits=" << m_stats.ramHits << " disk_hits=" << m_stats.diskHits
                  << " misses=" << m_stats.misses << " disk_writes=" << m_stats.diskWrites;
        return it->second.image;
      }
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

void
CacheManager::storeImage(const LoadSpec& loadSpec, const std::shared_ptr<ImageXYZC>& image)
{
  if (!image) {
    return;
  }

  const auto key = makeKey(loadSpec);
  storeImageInMemory(key, image);
}

void
CacheManager::clearMemoryCache()
{
  std::scoped_lock lock(m_mutex);
  m_entries.clear();
  m_lruKeys.clear();
  m_currentRamBytes = 0;
}

void
CacheManager::clearDiskCache()
{
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

CacheManager::CacheStats
CacheManager::getStats() const
{
  std::scoped_lock lock(m_mutex);
  return m_stats;
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
CacheManager::evictIfNeededLocked(std::uint64_t incomingBytes)
{
  if (m_config.maxRamBytes == 0) {
    return;
  }

  while (!m_lruKeys.empty() && (m_currentRamBytes + incomingBytes) > m_config.maxRamBytes) {
    const CacheKey& key = m_lruKeys.back();
    auto it = m_entries.find(key);
    if (it != m_entries.end()) {
      m_currentRamBytes -= it->second.bytes;
      m_entries.erase(it);
    }
    m_lruKeys.pop_back();
  }
}

void
CacheManager::storeImageInMemory(const CacheKey& key, const std::shared_ptr<ImageXYZC>& image)
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

  evictIfNeededLocked(bytes);

  m_lruKeys.push_front(key);
  CacheEntry entry;
  entry.image = image;
  entry.bytes = bytes;
  entry.lruIt = m_lruKeys.begin();
  m_entries.emplace(key, entry);
  m_currentRamBytes += bytes;
}
