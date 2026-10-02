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

  // Probe writability once, here, rather than on every config-apply: the cache
  // root is fixed for the lifetime of the process. If the directory can't be
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

  std::scoped_lock lock(m_mutex);
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

void
CacheManager::storeImage(const LoadSpec& loadSpec, const std::shared_ptr<ImageXYZC>& image)
{
  if (!image) {
    return;
  }

  CacheConfig configCopy;
  std::string cacheDirCopy;
  {
    std::scoped_lock lock(m_mutex);
    configCopy = m_config;
    cacheDirCopy = m_cacheDir;
  }

  const auto key = makeKey(loadSpec);

  if (configCopy.enabled && configCopy.enableDisk && configCopy.maxDiskBytes > 0 && !cacheDirCopy.empty()) {
    storeToDisk(key, image, configCopy, cacheDirCopy);
  }

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
