#include <catch2/catch_test_macros.hpp>

#include "renderlib/CacheConfig.h"
#include "renderlib/CacheManager.h"
#include "renderlib/IFileReader.h"
#include "renderlib/ImageXYZC.h"

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

    cache.storeImage(makeSpec("disk_a"), makeImage(4, 4, 4, 1));
    // Sleep briefly so lastAccess timestamps are distinct on fast disks.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    cache.storeImage(makeSpec("disk_b"), makeImage(4, 4, 4, 1));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    cache.storeImage(makeSpec("disk_c"), makeImage(4, 4, 4, 1));

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
    REQUIRE(countSubdirs(tmp.path()) >= 2);

    cache.clearDiskCache();

    REQUIRE(countSubdirs(tmp.path()) == 0);
    REQUIRE(std::filesystem::exists(tmp.path() / ".agave-cache-dir"));
  }

  SECTION("clearDiskCache refuses to touch a directory without the AGAVE marker")
  {
    cache.setConfig(diskConfig(oneImage * 4, 1ULL * 1024 * 1024));

    cache.storeImage(makeSpec("guarded_a"), makeImage(4, 4, 4, 1));
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
