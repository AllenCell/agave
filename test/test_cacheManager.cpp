#include <catch2/catch_test_macros.hpp>

#include "renderlib/CacheConfig.h"
#include "renderlib/CacheManager.h"
#include "renderlib/IFileReader.h"
#include "renderlib/ImageXYZC.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

// These tests construct their own CacheManager instances (each with its own
// cache directory) rather than touching the process-wide singleton. That keeps
// every case fully isolated and means we never need to reach into CacheManager
// internals or reset shared state.

namespace {

// Bytes per pixel that CacheManager uses when estimating an image's RAM cost.
constexpr size_t kBytesPerPixel = ImageXYZC::IN_MEMORY_BPP / 8;

// Build a minimal in-memory ImageXYZC for cache testing. Memory is owned by the
// returned ImageXYZC (it deletes m_data in its destructor).
std::shared_ptr<ImageXYZC>
makeImage(uint32_t x, uint32_t y, uint32_t z, uint32_t c)
{
  const std::uint64_t bytes = static_cast<std::uint64_t>(x) * y * z * c * kBytesPerPixel;
  auto* data = new uint8_t[bytes];
  std::memset(data, 0, bytes);
  return std::make_shared<ImageXYZC>(
    x, y, z, c, static_cast<uint32_t>(ImageXYZC::IN_MEMORY_BPP), data, 1.0f, 1.0f, 1.0f, "units");
}

LoadSpec
makeSpec(const std::string& filepath, uint32_t time = 0)
{
  LoadSpec s;
  s.filepath = filepath;
  s.time = time;
  return s;
}

std::uint64_t
imageBytes(uint32_t x, uint32_t y, uint32_t z, uint32_t c)
{
  return static_cast<std::uint64_t>(x) * y * z * c * kBytesPerPixel;
}

CacheConfig
ramOnlyConfig(std::uint64_t maxRamBytes)
{
  CacheConfig cfg;
  cfg.enabled = true;
  cfg.enableDisk = false;
  cfg.maxRamBytes = maxRamBytes;
  cfg.maxDiskBytes = 0;
  return cfg;
}

CacheConfig
diskConfig(std::uint64_t maxRamBytes, std::uint64_t maxDiskBytes)
{
  CacheConfig cfg;
  cfg.enabled = true;
  cfg.enableDisk = true;
  cfg.maxRamBytes = maxRamBytes;
  cfg.maxDiskBytes = maxDiskBytes;
  return cfg;
}

// An image whose pixel bytes match a known pattern (data[i] = i & 0xFF), used
// to verify the disk round-trip didn't corrupt the data.
std::shared_ptr<ImageXYZC>
makeImageWithPattern(uint32_t x, uint32_t y, uint32_t z, uint32_t c)
{
  const std::uint64_t bytes = static_cast<std::uint64_t>(x) * y * z * c * kBytesPerPixel;
  auto* data = new uint8_t[bytes];
  for (std::uint64_t i = 0; i < bytes; ++i) {
    data[i] = static_cast<uint8_t>(i & 0xFF);
  }
  return std::make_shared<ImageXYZC>(
    x, y, z, c, static_cast<uint32_t>(ImageXYZC::IN_MEMORY_BPP), data, 1.0f, 1.0f, 1.0f, "units");
}

// RAII temporary directory for disk-cache tests. Each instance gets a unique
// path under the system temp dir and is removed on destruction. The counter
// guarantees uniqueness even within a single test process.
class TempCacheDir
{
public:
  TempCacheDir()
  {
    static std::atomic<int> sCounter{ 0 };
    int n = sCounter.fetch_add(1);
    m_path = std::filesystem::temp_directory_path() / ("agave_cache_test_" + std::to_string(n));
    std::error_code ec;
    std::filesystem::remove_all(m_path, ec);
    std::filesystem::create_directories(m_path, ec);
  }
  ~TempCacheDir()
  {
    std::error_code ec;
    std::filesystem::remove_all(m_path, ec);
  }
  TempCacheDir(const TempCacheDir&) = delete;
  TempCacheDir& operator=(const TempCacheDir&) = delete;

  std::string str() const { return m_path.string(); }
  std::filesystem::path path() const { return m_path; }

private:
  std::filesystem::path m_path;
};

// Count subdirectories under `dir` (used to verify entry-dir counts after
// store/evict/clear operations).
int
countSubdirs(const std::filesystem::path& dir)
{
  int n = 0;
  std::error_code ec;
  for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator();
       it.increment(ec)) {
    if (it->is_directory()) {
      ++n;
    }
  }
  return n;
}

} // namespace

TEST_CASE("CacheManager respects RAM limit and evicts LRU entries", "[cache]")
{
  // Each 4x4x4x1 image is 4*4*4*1*2 = 128 bytes.
  const std::uint64_t oneImage = imageBytes(4, 4, 4, 1);

  // Fresh, RAM-only cache for each section (Catch2 re-runs the body per leaf).
  CacheManager cache;

  SECTION("Store and retrieve a single image (RAM hit)")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 4));

    auto img = makeImage(4, 4, 4, 1);
    auto spec = makeSpec("a");

    cache.storeImage(spec, img);
    auto retrieved = cache.findImage(spec);

    REQUIRE(retrieved != nullptr);
    REQUIRE(retrieved.get() == img.get());

    auto stats = cache.getStats();
    REQUIRE(stats.ramHits == 1);
    REQUIRE(stats.misses == 0);
  }

  SECTION("Miss returns null and increments miss counter")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 4));

    auto retrieved = cache.findImage(makeSpec("missing"));
    REQUIRE(retrieved == nullptr);
    REQUIRE(cache.getStats().misses == 1);
  }

  SECTION("Cache stays under maxRamBytes when limit is reached")
  {
    // Limit is exactly 2 images. Storing a 3rd must evict.
    cache.setConfig(ramOnlyConfig(oneImage * 2));

    cache.storeImage(makeSpec("a"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("b"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("c"), makeImage(4, 4, 4, 1));

    // "a" was the least recently used and must be evicted.
    REQUIRE(cache.findImage(makeSpec("a")) == nullptr);
    REQUIRE(cache.findImage(makeSpec("b")) != nullptr);
    REQUIRE(cache.findImage(makeSpec("c")) != nullptr);
  }

  SECTION("LRU ordering is updated on access (touched entry survives eviction)")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 2));

    cache.storeImage(makeSpec("a"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("b"), makeImage(4, 4, 4, 1));

    // Touch "a" so that "b" becomes least recently used.
    REQUIRE(cache.findImage(makeSpec("a")) != nullptr);

    cache.storeImage(makeSpec("c"), makeImage(4, 4, 4, 1));

    REQUIRE(cache.findImage(makeSpec("a")) != nullptr);
    REQUIRE(cache.findImage(makeSpec("b")) == nullptr);
    REQUIRE(cache.findImage(makeSpec("c")) != nullptr);
  }

  SECTION("Storing many images keeps total RAM usage bounded")
  {
    const std::uint64_t cap = oneImage * 3;
    cache.setConfig(ramOnlyConfig(cap));

    for (int i = 0; i < 20; ++i) {
      cache.storeImage(makeSpec("file_" + std::to_string(i)), makeImage(4, 4, 4, 1));
    }

    // Count how many of the 20 inserts are still resident. With a cap of 3
    // images, no more than 3 can be present at once.
    cache.resetStats();
    int present = 0;
    for (int i = 0; i < 20; ++i) {
      if (cache.findImage(makeSpec("file_" + std::to_string(i)))) {
        present++;
      }
    }
    REQUIRE(present <= 3);
    // The most recently inserted entries should still be in the cache.
    REQUIRE(present >= 1);
    REQUIRE(cache.findImage(makeSpec("file_19")) != nullptr);
  }

  SECTION("Image larger than maxRamBytes is not stored")
  {
    // Cap at less than the size of one image.
    cache.setConfig(ramOnlyConfig(oneImage / 2));

    cache.storeImage(makeSpec("too_big"), makeImage(4, 4, 4, 1));
    REQUIRE(cache.findImage(makeSpec("too_big")) == nullptr);
  }

  SECTION("Re-storing the same key replaces the entry without exceeding the limit")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 2));

    auto first = makeImage(4, 4, 4, 1);
    auto second = makeImage(4, 4, 4, 1);
    auto spec = makeSpec("a");

    cache.storeImage(spec, first);
    cache.storeImage(spec, second);

    // Fill the cache up to the limit with another entry. If the duplicate
    // key had double-counted bytes, this would evict "a".
    cache.storeImage(makeSpec("b"), makeImage(4, 4, 4, 1));

    auto retrieved = cache.findImage(spec);
    REQUIRE(retrieved != nullptr);
    REQUIRE(retrieved.get() == second.get());
    REQUIRE(cache.findImage(makeSpec("b")) != nullptr);
  }

  SECTION("Disabling the cache clears all entries")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 4));
    cache.storeImage(makeSpec("a"), makeImage(4, 4, 4, 1));
    REQUIRE(cache.findImage(makeSpec("a")) != nullptr);

    CacheConfig disabled;
    disabled.enabled = false;
    cache.setConfig(disabled);

    REQUIRE(cache.findImage(makeSpec("a")) == nullptr);
  }

  SECTION("Shrinking the limit evicts existing entries")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 4));
    cache.storeImage(makeSpec("a"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("b"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("c"), makeImage(4, 4, 4, 1));

    // Reconfigure to a smaller limit; oldest entries must be evicted.
    cache.setConfig(ramOnlyConfig(oneImage));

    int present = 0;
    for (const char* name : { "a", "b", "c" }) {
      if (cache.findImage(makeSpec(name))) {
        present++;
      }
    }
    REQUIRE(present <= 1);
  }

  SECTION("Reducing cache size immediately evicts LRU entries to fit")
  {
    // Fill the cache exactly to its 4-image cap.
    cache.setConfig(ramOnlyConfig(oneImage * 4));
    cache.storeImage(makeSpec("a"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("b"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("c"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("d"), makeImage(4, 4, 4, 1));

    // All four are resident. LRU order (most recent first) is: d, c, b, a.
    // Reset stats so the find() calls below count cleanly.
    cache.resetStats();

    // User shrinks the cache to hold only 2 images. Eviction must occur
    // synchronously inside setConfig() — not lazily on the next store.
    cache.setConfig(ramOnlyConfig(oneImage * 2));

    // The two least recently used entries ("a" and "b") must be gone, and
    // the two most recently used ("c" and "d") must still be present.
    REQUIRE(cache.findImage(makeSpec("a")) == nullptr);
    REQUIRE(cache.findImage(makeSpec("b")) == nullptr);
    REQUIRE(cache.findImage(makeSpec("c")) != nullptr);
    REQUIRE(cache.findImage(makeSpec("d")) != nullptr);

    auto stats = cache.getStats();
    REQUIRE(stats.misses == 2);
    REQUIRE(stats.ramHits == 2);
  }

  SECTION("Reducing cache size below a single image evicts everything")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 3));
    cache.storeImage(makeSpec("a"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("b"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("c"), makeImage(4, 4, 4, 1));

    // Shrink below the size of a single entry — everything must go.
    cache.setConfig(ramOnlyConfig(oneImage / 2));

    REQUIRE(cache.findImage(makeSpec("a")) == nullptr);
    REQUIRE(cache.findImage(makeSpec("b")) == nullptr);
    REQUIRE(cache.findImage(makeSpec("c")) == nullptr);
  }

  SECTION("clearMemoryCache() empties the cache")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 4));
    cache.storeImage(makeSpec("a"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("b"), makeImage(4, 4, 4, 1));

    cache.clearMemoryCache();

    REQUIRE(cache.findImage(makeSpec("a")) == nullptr);
    REQUIRE(cache.findImage(makeSpec("b")) == nullptr);
  }
}

TEST_CASE("CacheManager disk tier round-trips images and respects the disk cap", "[cache][disk]")
{
  // 4x4x4x1 uint16 image -> 128 raw bytes. On-disk zarr representation is a
  // few KB once chunk + metadata files are accounted for, so a 1 MB cap is
  // comfortably oversized for the round-trip and clear-cache tests, and we
  // size the cap explicitly down to a couple of images for the eviction
  // test. Total disk usage per test is well under a megabyte.
  const std::uint64_t oneImage = imageBytes(4, 4, 4, 1);

  // Fresh temp dir + cache rooted at it for each section.
  TempCacheDir tmp;
  CacheManager cache(tmp.str());

  SECTION("Enabling the disk tier writes the AGAVE marker file")
  {
    cache.setConfig(diskConfig(oneImage * 4, 1ULL * 1024 * 1024));

    REQUIRE(std::filesystem::exists(tmp.path() / ".agave-cache-dir"));
  }

  SECTION("Round-trip: store, drop RAM, find reloads from disk with bit-identical data")
  {
    cache.setConfig(diskConfig(oneImage * 4, 1ULL * 1024 * 1024));

    auto img = makeImageWithPattern(4, 4, 4, 1);
    auto spec = makeSpec("disk_roundtrip");
    cache.storeImage(spec, img);
    // Disk writes are asynchronous now, so wait for the write to land before
    // asserting anything about the disk tier.
    cache.flushDiskWrites();

    // Drop RAM cache; disk cache survives.
    cache.clearMemoryCache();
    cache.resetStats();

    auto found = cache.findImage(spec);
    REQUIRE(found != nullptr);
    // The reloaded image is a fresh instance, not the same shared_ptr.
    REQUIRE(found.get() != img.get());

    auto stats = cache.getStats();
    REQUIRE(stats.diskHits == 1);
    REQUIRE(stats.ramHits == 0);

    REQUIRE(found->sizeX() == 4);
    REQUIRE(found->sizeY() == 4);
    REQUIRE(found->sizeZ() == 4);
    REQUIRE(found->sizeC() == 1);

    const std::uint64_t bytes = imageBytes(4, 4, 4, 1);
    REQUIRE(std::memcmp(img->ptr(), found->ptr(), bytes) == 0);
  }

  SECTION("An image larger than maxDiskBytes is not written to disk")
  {
    // Cap is below a single image, so storeToDisk must refuse outright.
    cache.setConfig(diskConfig(oneImage * 4, oneImage / 2));

    auto spec = makeSpec("too_big_for_disk");
    cache.storeImage(spec, makeImage(4, 4, 4, 1));
    cache.flushDiskWrites();

    // No entry subdirectory should have been created.
    REQUIRE(countSubdirs(tmp.path()) == 0);

    // Drop RAM to force a disk-or-miss lookup; with no entry on disk we
    // should get a miss.
    cache.clearMemoryCache();
    REQUIRE(cache.findImage(spec) == nullptr);
  }

  SECTION("Disk eviction removes the oldest entry to stay under the cap")
  {
    // Cap large enough for two entries' raw byte estimate but not three.
    cache.setConfig(diskConfig(oneImage * 4, oneImage * 2));

    // Flush after each store: writes are asynchronous, and this section depends
    // on them completing in order so the lastAccess timestamps rank correctly.
    cache.storeImage(makeSpec("disk_a"), makeImage(4, 4, 4, 1));
    cache.flushDiskWrites();
    // Sleep briefly so lastAccess timestamps are distinct on fast disks.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    cache.storeImage(makeSpec("disk_b"), makeImage(4, 4, 4, 1));
    cache.flushDiskWrites();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    cache.storeImage(makeSpec("disk_c"), makeImage(4, 4, 4, 1));
    cache.flushDiskWrites();

    // Drop RAM so finds have to go through the disk tier.
    cache.clearMemoryCache();

    REQUIRE(cache.findImage(makeSpec("disk_a")) == nullptr);
    REQUIRE(cache.findImage(makeSpec("disk_b")) != nullptr);
    REQUIRE(cache.findImage(makeSpec("disk_c")) != nullptr);

    // After eviction there should be at most two entry subdirectories on
    // disk (the marker file is not a directory).
    REQUIRE(countSubdirs(tmp.path()) <= 2);
  }

  SECTION("clearDiskCache removes entry subdirectories but keeps the marker")
  {
    cache.setConfig(diskConfig(oneImage * 4, 1ULL * 1024 * 1024));

    cache.storeImage(makeSpec("clear_a"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("clear_b"), makeImage(4, 4, 4, 1));
    cache.flushDiskWrites();
    REQUIRE(countSubdirs(tmp.path()) >= 2);

    cache.clearDiskCache();

    REQUIRE(countSubdirs(tmp.path()) == 0);
    REQUIRE(std::filesystem::exists(tmp.path() / ".agave-cache-dir"));
  }

  SECTION("clearDiskCache refuses to touch a directory without the AGAVE marker")
  {
    cache.setConfig(diskConfig(oneImage * 4, 1ULL * 1024 * 1024));

    cache.storeImage(makeSpec("guarded_a"), makeImage(4, 4, 4, 1));
    cache.flushDiskWrites();
    int subdirsBefore = countSubdirs(tmp.path());
    REQUIRE(subdirsBefore >= 1);

    // Simulate a directory that doesn't belong to AGAVE by removing the
    // marker file. clearDiskCache must refuse.
    std::error_code ec;
    std::filesystem::remove(tmp.path() / ".agave-cache-dir", ec);
    REQUIRE_FALSE(ec);

    cache.clearDiskCache();

    REQUIRE(countSubdirs(tmp.path()) == subdirsBefore);
  }

  SECTION("A fresh cache instance rebuilds the disk index from an existing cache dir")
  {
    cache.setConfig(diskConfig(oneImage * 4, 1ULL * 1024 * 1024));
    cache.storeImage(makeSpec("persistent"), makeImage(4, 4, 4, 1));
    cache.flushDiskWrites();

    // Simulate a session restart: a brand-new CacheManager rooted at the same
    // directory must rebuild its disk index on first use and serve the
    // persisted entry from disk.
    CacheManager restarted(tmp.str());
    restarted.setConfig(diskConfig(oneImage * 4, 1ULL * 1024 * 1024));

    auto found = restarted.findImage(makeSpec("persistent"));
    REQUIRE(found != nullptr);
    REQUIRE(restarted.getStats().diskHits == 1);
  }
}

TEST_CASE("CacheManager cache directory is fixed at construction", "[cache][disk]")
{
  const std::uint64_t oneImage = imageBytes(4, 4, 4, 1);

  SECTION("getCacheDirectory reflects the constructed path")
  {
    TempCacheDir tmp;
    CacheManager cache(tmp.str());
    REQUIRE(cache.getCacheDirectory() == tmp.str());
  }

  SECTION("A cache constructed with no directory is RAM-only")
  {
    CacheManager cache; // empty cache directory
    REQUIRE(cache.getCacheDirectory().empty());

    // Even with a disk-enabled config, an empty root keeps the disk tier inert.
    cache.setConfig(diskConfig(oneImage * 4, 1ULL * 1024 * 1024));

    auto spec = makeSpec("no_dir");
    cache.storeImage(spec, makeImage(4, 4, 4, 1));

    // RAM still serves it within the session...
    REQUIRE(cache.findImage(spec) != nullptr);

    // ...but nothing was persisted to disk, so once RAM is dropped it misses.
    cache.clearMemoryCache();
    REQUIRE(cache.findImage(spec) == nullptr);
  }
}

TEST_CASE("CacheManager invalidates entries when the source file mtime changes", "[cache][mtime]")
{
  CacheManager cache;

  // Use a real file on disk so the cache key picks up its mtime via
  // std::filesystem::last_write_time. We bump the file's mtime explicitly
  // (rather than sleeping and rewriting) so the test is deterministic on
  // filesystems with coarse mtime resolution.
  static std::atomic<int> sCounter{ 0 };
  std::filesystem::path srcFile = std::filesystem::temp_directory_path() /
                                  ("agave_cache_mtime_test_" + std::to_string(sCounter.fetch_add(1)) + ".bin");
  {
    std::ofstream out(srcFile.string());
    out << "initial content";
  }

  cache.setConfig(ramOnlyConfig(imageBytes(4, 4, 4, 1) * 4));

  LoadSpec spec;
  spec.filepath = srcFile.string();
  cache.storeImage(spec, makeImage(4, 4, 4, 1));
  REQUIRE(cache.findImage(spec) != nullptr);

  // Bump the file's last_write_time well into the future; subsequent
  // makeKey calls now produce a different key and must miss.
  std::error_code ec;
  auto futureTime = std::filesystem::last_write_time(srcFile, ec) + std::chrono::seconds(60);
  REQUIRE_FALSE(ec);
  std::filesystem::last_write_time(srcFile, futureTime, ec);
  REQUIRE_FALSE(ec);

  REQUIRE(cache.findImage(spec) == nullptr);

  std::filesystem::remove(srcFile, ec);
}

TEST_CASE("CacheManager normalizes equivalent filepaths to the same key", "[cache][normalize]")
{
  // These tests use synthetic paths that don't exist on disk; statForKey
  // returns (0, 0) for them, so the cache key depends only on the
  // (normalized) filepath string and the other LoadSpec fields.
  const std::uint64_t oneImage = imageBytes(4, 4, 4, 1);

  CacheManager cache;
  cache.setConfig(ramOnlyConfig(oneImage * 4));

  SECTION("'./' segment is normalized away")
  {
    LoadSpec stored;
    stored.filepath = "/some/dir/foo.tif";
    cache.storeImage(stored, makeImage(4, 4, 4, 1));

    LoadSpec lookup;
    lookup.filepath = "/some/dir/./foo.tif";
    REQUIRE(cache.findImage(lookup) != nullptr);
  }

  SECTION("'..' segment is normalized away")
  {
    LoadSpec stored;
    stored.filepath = "/some/dir/foo.tif";
    cache.storeImage(stored, makeImage(4, 4, 4, 1));

    LoadSpec lookup;
    lookup.filepath = "/some/dir/subdir/../foo.tif";
    REQUIRE(cache.findImage(lookup) != nullptr);
  }

  SECTION("Duplicate path separators are collapsed")
  {
    LoadSpec stored;
    stored.filepath = "/some/dir/foo.tif";
    cache.storeImage(stored, makeImage(4, 4, 4, 1));

    LoadSpec lookup;
    lookup.filepath = "/some//dir///foo.tif";
    REQUIRE(cache.findImage(lookup) != nullptr);
  }

  SECTION("Bare names (no slashes) pass through unchanged")
  {
    LoadSpec stored;
    stored.filepath = "in_memory_array_42";
    cache.storeImage(stored, makeImage(4, 4, 4, 1));

    LoadSpec lookup;
    lookup.filepath = "in_memory_array_42";
    REQUIRE(cache.findImage(lookup) != nullptr);
  }

  SECTION("Remote URLs are passed through without normalization")
  {
    LoadSpec stored;
    stored.filepath = "http://example.com/path/to/data.zarr";
    cache.storeImage(stored, makeImage(4, 4, 4, 1));

    LoadSpec lookup;
    lookup.filepath = "http://example.com/path/to/data.zarr";
    REQUIRE(cache.findImage(lookup) != nullptr);
  }

  SECTION("Distinct paths still produce distinct keys (no false hits)")
  {
    LoadSpec stored;
    stored.filepath = "/some/dir/foo.tif";
    cache.storeImage(stored, makeImage(4, 4, 4, 1));

    LoadSpec lookup;
    lookup.filepath = "/some/dir/bar.tif";
    REQUIRE(cache.findImage(lookup) == nullptr);
  }

#ifdef _WIN32
  SECTION("UNC paths normalize across slash style and case on Windows")
  {
    LoadSpec stored;
    stored.filepath = "\\\\Server\\Share\\Path\\Foo.tif";
    cache.storeImage(stored, makeImage(4, 4, 4, 1));

    // Same UNC path, lowercase, mixed slashes.
    LoadSpec lookupSlash;
    lookupSlash.filepath = "//server/share/path/foo.tif";
    REQUIRE(cache.findImage(lookupSlash) != nullptr);

    // Same UNC path, with a '..' segment in the relative tail.
    LoadSpec lookupDotDot;
    lookupDotDot.filepath = "\\\\server\\share\\path\\subdir\\..\\foo.tif";
    REQUIRE(cache.findImage(lookupDotDot) != nullptr);

    // Different UNC share — must NOT collide.
    LoadSpec otherShare;
    otherShare.filepath = "\\\\server\\othershare\\path\\foo.tif";
    REQUIRE(cache.findImage(otherShare) == nullptr);
  }

  SECTION("Forward and back slashes are equivalent on Windows")
  {
    LoadSpec stored;
    stored.filepath = "C:/some/dir/foo.tif";
    cache.storeImage(stored, makeImage(4, 4, 4, 1));

    LoadSpec lookup;
    lookup.filepath = "C:\\some\\dir\\foo.tif";
    REQUIRE(cache.findImage(lookup) != nullptr);
  }

  SECTION("Case differences are equivalent on Windows")
  {
    LoadSpec stored;
    stored.filepath = "C:/Some/Dir/Foo.tif";
    cache.storeImage(stored, makeImage(4, 4, 4, 1));

    LoadSpec lookup;
    lookup.filepath = "c:/some/dir/foo.tif";
    REQUIRE(cache.findImage(lookup) != nullptr);
  }
#endif
}

TEST_CASE("CacheManager reports tier usage", "[cacheManager]")
{
  const std::uint64_t oneImage = imageBytes(4, 4, 4, 1);
  CacheManager cache;

  SECTION("Empty cache reports zero used and the configured limit")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 4));

    auto usage = cache.getUsage();
    REQUIRE(usage.ramBytesUsed == 0);
    REQUIRE(usage.ramBytesLimit == oneImage * 4);
    REQUIRE(usage.ramEntryCount == 0);
    REQUIRE(cache.getRamBytesUsed() == 0);
    REQUIRE(cache.getRamBytesAvailable() == oneImage * 4);
  }

  SECTION("Usage tracks stored images")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 4));

    cache.storeImage(makeSpec("a"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("b"), makeImage(4, 4, 4, 1));

    auto usage = cache.getUsage();
    REQUIRE(usage.ramEntryCount == 2);
    REQUIRE(usage.ramBytesUsed == oneImage * 2);
    REQUIRE(cache.getRamBytesUsed() == oneImage * 2);
    REQUIRE(cache.getRamBytesAvailable() == oneImage * 2);
  }

  SECTION("Available bytes reach zero at the limit rather than going negative")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 2));

    cache.storeImage(makeSpec("a"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("b"), makeImage(4, 4, 4, 1));
    REQUIRE(cache.getRamBytesAvailable() == 0);

    // Eviction keeps us at the limit, so available stays clamped at zero rather
    // than underflowing -- prefetch throttling depends on this.
    cache.storeImage(makeSpec("c"), makeImage(4, 4, 4, 1));
    REQUIRE(cache.getRamBytesUsed() <= oneImage * 2);
    REQUIRE(cache.getRamBytesAvailable() == 0);
  }

  SECTION("Clearing memory returns usage to zero")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 4));
    cache.storeImage(makeSpec("a"), makeImage(4, 4, 4, 1));
    REQUIRE(cache.getRamBytesUsed() > 0);

    cache.clearMemoryCache();
    auto usage = cache.getUsage();
    REQUIRE(usage.ramBytesUsed == 0);
    REQUIRE(usage.ramEntryCount == 0);
  }

  SECTION("Disk limit reads as zero when the disk tier is disabled")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 4));
    REQUIRE(cache.getUsage().diskBytesLimit == 0);
  }
}

namespace {

class RecordingEvictionObserver : public CacheManager::IEvictionObserver
{
public:
  void onEvictedFromMemory(const CacheKey& key) override { evicted.push_back(key.filepath); }
  void onEvictedFromDisk(const std::string& diskCacheId) override { evictedFromDisk.push_back(diskCacheId); }
  std::vector<std::string> evicted;
  std::vector<std::string> evictedFromDisk;
};

} // namespace

TEST_CASE("CacheManager pinning protects entries from eviction", "[cacheManager]")
{
  const std::uint64_t oneImage = imageBytes(4, 4, 4, 1);
  CacheManager cache;

  SECTION("A pinned entry survives pressure that would otherwise evict it")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 2));

    auto specA = makeSpec("a");
    cache.storeImage(specA, makeImage(4, 4, 4, 1));
    cache.pin(specA);
    REQUIRE(cache.isPinned(specA));

    // "a" is now least-recently-used, so without the pin it would be the first
    // thing dropped when "c" needs room.
    cache.storeImage(makeSpec("b"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("c"), makeImage(4, 4, 4, 1));

    REQUIRE(cache.findImage(specA) != nullptr);
    REQUIRE(cache.findImage(makeSpec("b")) == nullptr);
  }

  SECTION("Unpinning makes an entry evictable again")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 2));

    auto specA = makeSpec("a");
    cache.storeImage(specA, makeImage(4, 4, 4, 1));
    cache.pin(specA);
    cache.unpin(specA);
    REQUIRE_FALSE(cache.isPinned(specA));

    cache.storeImage(makeSpec("b"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("c"), makeImage(4, 4, 4, 1));

    REQUIRE(cache.findImage(specA) == nullptr);
  }

  SECTION("Pins are refcounted")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 2));

    auto specA = makeSpec("a");
    cache.storeImage(specA, makeImage(4, 4, 4, 1));
    cache.pin(specA);
    cache.pin(specA);
    cache.unpin(specA);
    // Still pinned: one pin outstanding.
    REQUIRE(cache.isPinned(specA));

    cache.storeImage(makeSpec("b"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("c"), makeImage(4, 4, 4, 1));
    REQUIRE(cache.findImage(specA) != nullptr);

    cache.unpin(specA);
    REQUIRE_FALSE(cache.isPinned(specA));
  }

  SECTION("Pinning a key that is not resident yet still protects it once stored")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 2));

    auto specA = makeSpec("a");
    cache.pin(specA);
    cache.storeImage(specA, makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("b"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("c"), makeImage(4, 4, 4, 1));

    REQUIRE(cache.findImage(specA) != nullptr);
  }

  SECTION("The tier may exceed its limit when everything resident is pinned")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 2));

    auto specA = makeSpec("a");
    auto specB = makeSpec("b");
    cache.storeImage(specA, makeImage(4, 4, 4, 1));
    cache.storeImage(specB, makeImage(4, 4, 4, 1));
    cache.pin(specA);
    cache.pin(specB);

    // Nothing is evictable, so we overshoot rather than dropping data in use.
    cache.storeImage(makeSpec("c"), makeImage(4, 4, 4, 1));

    REQUIRE(cache.findImage(specA) != nullptr);
    REQUIRE(cache.findImage(specB) != nullptr);
    REQUIRE(cache.getRamBytesUsed() > oneImage * 2);
  }
}

TEST_CASE("CacheManager notifies eviction observers", "[cacheManager]")
{
  const std::uint64_t oneImage = imageBytes(4, 4, 4, 1);
  CacheManager cache;
  RecordingEvictionObserver observer;
  cache.addEvictionObserver(&observer);

  SECTION("Eviction under pressure notifies with the evicted key")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 2));

    cache.storeImage(makeSpec("a"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("b"), makeImage(4, 4, 4, 1));
    REQUIRE(observer.evicted.empty());

    cache.storeImage(makeSpec("c"), makeImage(4, 4, 4, 1));
    REQUIRE(observer.evicted.size() == 1);
    REQUIRE(observer.evicted[0].find("a") != std::string::npos);
  }

  SECTION("clearMemoryCache notifies for every dropped entry")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 4));

    cache.storeImage(makeSpec("a"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("b"), makeImage(4, 4, 4, 1));
    cache.clearMemoryCache();

    REQUIRE(observer.evicted.size() == 2);
  }

  SECTION("A removed observer stops receiving notifications")
  {
    cache.setConfig(ramOnlyConfig(oneImage * 2));
    cache.removeEvictionObserver(&observer);

    cache.storeImage(makeSpec("a"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("b"), makeImage(4, 4, 4, 1));
    cache.storeImage(makeSpec("c"), makeImage(4, 4, 4, 1));

    REQUIRE(observer.evicted.empty());
  }
}

TEST_CASE("CacheManager notifies disk eviction observers", "[cacheManager][disk]")
{
  const std::uint64_t oneImage = imageBytes(4, 4, 4, 1);
  TempCacheDir tmp;
  CacheManager cache(tmp.str());
  RecordingEvictionObserver observer;
  cache.addEvictionObserver(&observer);

  SECTION("clearDiskCache notifies for every dropped disk entry")
  {
    cache.setConfig(diskConfig(oneImage * 4, 1ULL * 1024 * 1024));

    const LoadSpec specA = makeSpec("clear_notify_a");
    const LoadSpec specB = makeSpec("clear_notify_b");
    const std::string idA = cache.diskCacheIdFor(specA);
    const std::string idB = cache.diskCacheIdFor(specB);

    cache.storeImage(specA, makeImage(4, 4, 4, 1));
    cache.storeImage(specB, makeImage(4, 4, 4, 1));
    cache.flushDiskWrites();

    cache.clearDiskCache();

    REQUIRE(observer.evictedFromDisk.size() == 2);
    CHECK(std::find(observer.evictedFromDisk.begin(), observer.evictedFromDisk.end(), idA) !=
          observer.evictedFromDisk.end());
    CHECK(std::find(observer.evictedFromDisk.begin(), observer.evictedFromDisk.end(), idB) !=
          observer.evictedFromDisk.end());
  }

  SECTION("Disabling the disk tier notifies for every active disk entry")
  {
    cache.setConfig(diskConfig(oneImage * 4, 1ULL * 1024 * 1024));

    const LoadSpec spec = makeSpec("disable_disk_notify");
    const std::string id = cache.diskCacheIdFor(spec);
    cache.storeImage(spec, makeImage(4, 4, 4, 1));
    cache.flushDiskWrites();

    cache.setConfig(ramOnlyConfig(oneImage * 4));

    REQUIRE(observer.evictedFromDisk.size() == 1);
    CHECK(observer.evictedFromDisk[0] == id);
    CHECK_FALSE(cache.containsOnDisk(spec));
  }

  SECTION("Disabling the whole cache notifies both memory and disk observers")
  {
    cache.setConfig(diskConfig(oneImage * 4, 1ULL * 1024 * 1024));

    const LoadSpec spec = makeSpec("disable_all_notify");
    const std::string id = cache.diskCacheIdFor(spec);
    cache.storeImage(spec, makeImage(4, 4, 4, 1));
    cache.flushDiskWrites();

    CacheConfig disabled;
    cache.setConfig(disabled);

    REQUIRE(observer.evicted.size() == 1);
    REQUIRE(observer.evictedFromDisk.size() == 1);
    CHECK(observer.evicted[0].find("disable_all_notify") != std::string::npos);
    CHECK(observer.evictedFromDisk[0] == id);
  }
}

TEST_CASE("CacheManager writes to disk asynchronously", "[cache][disk]")
{
  const std::uint64_t oneImage = imageBytes(4, 4, 4, 1);
  TempCacheDir tmp;
  CacheManager cache(tmp.str());
  cache.setConfig(diskConfig(oneImage * 8, 1ULL * 1024 * 1024));

  SECTION("The image is in memory immediately, without waiting for the disk write")
  {
    auto spec = makeSpec("async_write");
    cache.storeImage(spec, makeImage(4, 4, 4, 1));

    // Available from RAM straight away. Previously storeToDisk ran inline and
    // before the memory store, so every prefetched time step paid a full-volume
    // disk write before the frame could be used.
    REQUIRE(cache.containsInMemory(spec));

    cache.flushDiskWrites();
    REQUIRE(cache.pendingDiskWrites() == 0);

    // And it did eventually reach the disk tier.
    cache.clearMemoryCache();
    REQUIRE(cache.findImage(spec) != nullptr);
    REQUIRE(cache.getStats().diskHits == 1);
  }

  SECTION("Flushing an idle cache returns immediately")
  {
    REQUIRE(cache.pendingDiskWrites() == 0);
    cache.flushDiskWrites();
    REQUIRE(cache.pendingDiskWrites() == 0);
  }

  SECTION("A burst of stores blocks the producer rather than dropping writes")
  {
    // Far more stores than the queue depth. The queue stays bounded by
    // back-pressure -- the producer waits for a slot -- so no write is ever
    // abandoned. Dropping used to be allowed here, but a dropped write loses the
    // frame from disk permanently: RAM eviction never writes on the way out, so
    // there is no second chance.
    for (int i = 0; i < 40; ++i) {
      REQUIRE(cache.storeImage(makeSpec("burst_" + std::to_string(i)), makeImage(4, 4, 4, 1)));
      REQUIRE(cache.pendingDiskWrites() <= 3); // kMaxQueuedDiskWrites + one in progress
    }
    cache.flushDiskWrites();
    REQUIRE(cache.pendingDiskWrites() == 0);
    REQUIRE(cache.droppedDiskWrites() == 0);
    // Every one of them actually landed.
    for (int i = 0; i < 40; ++i) {
      REQUIRE(cache.containsOnDisk(makeSpec("burst_" + std::to_string(i))));
    }
  }
}

TEST_CASE("CacheManager with the disk tier off never starts a writer thread", "[cache]")
{
  const std::uint64_t oneImage = imageBytes(4, 4, 4, 1);
  CacheManager cache;
  cache.setConfig(ramOnlyConfig(oneImage * 4));

  cache.storeImage(makeSpec("ram_only"), makeImage(4, 4, 4, 1));
  REQUIRE(cache.pendingDiskWrites() == 0);
  REQUIRE(cache.droppedDiskWrites() == 0);
  // Destruction must not hang waiting on a thread that was never needed.
  SUCCEED("ram-only cache stored without a disk writer");
}

TEST_CASE("CacheManager never drops disk writes under sustained pressure", "[cache][disk]")
{
  // Regression gate for widening the memory window. The queue used to drop its
  // oldest entry when full, so a frame could end up recorded as neither in
  // memory nor on disk -- and prefetch would re-fetch it forever.
  const std::uint64_t oneImage = imageBytes(4, 4, 4, 1);
  TempCacheDir tmp;
  CacheManager cache(tmp.str());
  // RAM deliberately tiny so eviction races the writes; disk roomy so nothing
  // is refused for lack of space.
  cache.setConfig(diskConfig(oneImage * 2, 1ULL * 1024 * 1024));

  const int kStores = 64;
  for (int i = 0; i < kStores; ++i) {
    REQUIRE(cache.storeImage(makeSpec("pressure_" + std::to_string(i)), makeImage(4, 4, 4, 1)));
  }
  cache.flushDiskWrites();

  CHECK(cache.droppedDiskWrites() == 0);
  CHECK(cache.pendingDiskBytes() == 0);
  for (int i = 0; i < kStores; ++i) {
    CHECK(cache.containsOnDisk(makeSpec("pressure_" + std::to_string(i))));
  }
}

TEST_CASE("CacheManager counts queued writes against the disk budget", "[cache][disk]")
{
  // On-disk bytes PLUS bytes for writes still in flight must never exceed the
  // cap. Without the reservation, evictDiskIfNeeded only ever sees the entry it
  // is about to write and the tier overshoots by the rest of the backlog.
  const std::uint64_t oneImage = imageBytes(4, 4, 4, 1);
  const std::uint64_t diskCap = oneImage * 8;
  TempCacheDir tmp;
  CacheManager cache(tmp.str());
  cache.setConfig(diskConfig(oneImage * 2, diskCap));

  for (int i = 0; i < 40; ++i) {
    cache.storeImage(makeSpec("budget_" + std::to_string(i)), makeImage(4, 4, 4, 1));
    CHECK(cache.getUsage().diskBytesUsed + cache.pendingDiskBytes() <= diskCap);
  }
  cache.flushDiskWrites();
  CHECK(cache.getUsage().diskBytesUsed <= diskCap);
}

TEST_CASE("CacheManager reports a queued write as present on disk", "[cache][disk]")
{
  // This is what removes the eviction-timing race: a frame dropped from RAM
  // before its write lands must still probe as disk-present, or it gets recorded
  // as being in neither tier and prefetch pulls it straight back.
  const std::uint64_t oneImage = imageBytes(4, 4, 4, 1);
  TempCacheDir tmp;
  CacheManager cache(tmp.str());
  cache.setConfig(diskConfig(oneImage * 64, 1ULL * 1024 * 1024));

  // These images are tiny, so the writer may drain faster than we can observe.
  // Store until a write is genuinely still pending, then probe that one -- that
  // is the case the pending-aware probe exists for.
  bool observedPending = false;
  for (int i = 0; i < 200 && !observedPending; ++i) {
    auto spec = makeSpec("queued_" + std::to_string(i));
    REQUIRE(cache.storeImage(spec, makeImage(4, 4, 4, 1)));
    if (cache.pendingDiskWrites() > 0) {
      observedPending = true;
      // Deliberately BEFORE flushDiskWrites: not yet on the filesystem.
      CHECK(cache.containsOnDisk(spec));
    }
  }
  REQUIRE(observedPending);

  // And it is still reported present once the queue drains.
  cache.flushDiskWrites();
  CHECK(cache.containsOnDisk(makeSpec("queued_0")));
}

TEST_CASE("CacheManager refuses a disk write that cannot fit", "[cache][disk]")
{
  // A disk cap smaller than a single image: nothing can ever fit, so the write
  // must be refused up front rather than reported as cached.
  const std::uint64_t oneImage = imageBytes(4, 4, 4, 1);
  TempCacheDir tmp;
  CacheManager cache(tmp.str());
  cache.setConfig(diskConfig(oneImage * 64, oneImage / 2));

  auto spec = makeSpec("too_big_for_disk");
  CHECK_FALSE(cache.storeImage(spec, makeImage(4, 4, 4, 1)));
  cache.flushDiskWrites();
  CHECK_FALSE(cache.containsOnDisk(spec));
  // The memory tier is independent of a disk refusal.
  CHECK(cache.containsInMemory(spec));
}
