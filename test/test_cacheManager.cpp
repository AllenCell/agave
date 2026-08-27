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
