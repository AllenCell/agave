#include "renderlib/io/BlockingFileReader.h"
#include "renderlib/io/LoadRequest.h"
#include "renderlib/io/TimeSeriesLoader.h"

#include "renderlib/CacheManager.h"
#include "renderlib/ImageXYZC.h"
#include "renderlib/VolumeDimensions.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>

using namespace std::chrono_literals;

namespace {

constexpr uint32_t kDim = 4;
constexpr uint32_t kChannels = 1;

std::uint64_t
frameBytes()
{
  return static_cast<std::uint64_t>(kDim) * kDim * kDim * kChannels * (ImageXYZC::IN_MEMORY_BPP / 8);
}

std::shared_ptr<ImageXYZC>
makeImage()
{
  const std::uint64_t bytes = frameBytes();
  auto* data = new uint8_t[bytes];
  std::memset(data, 0, bytes);
  return std::make_shared<ImageXYZC>(
    kDim, kDim, kDim, kChannels, static_cast<uint32_t>(ImageXYZC::IN_MEMORY_BPP), data, 1.0f, 1.0f, 1.0f, "units");
}

LoadSpec
makeBaseSpec()
{
  LoadSpec s;
  s.filepath = "series.tif";
  s.scene = 0;
  s.time = 0;
  return s;
}

CacheConfig
ramConfig(std::uint64_t maxRamBytes)
{
  CacheConfig cfg;
  cfg.enabled = true;
  cfg.enableDisk = false;
  cfg.maxRamBytes = maxRamBytes;
  cfg.maxDiskBytes = 0;
  return cfg;
}

// A reader that fabricates volumes without touching disk, and records which
// timepoints were actually loaded so tests can assert on duplicate work.
class CountingReader : public BlockingFileReader
{
public:
  bool supportChunkedLoading() const override { return false; }
  uint32_t loadNumScenes(const std::string&) override { return 1; }
  VolumeDimensions loadDimensions(const std::string&, uint32_t) override { return VolumeDimensions(); }
  std::vector<MultiscaleDims> loadMultiscaleDims(const std::string&, uint32_t) override { return {}; }

  std::shared_ptr<ImageXYZC> loadVolumeBlocking(const LoadSpec& loadSpec, LoadProgress& progress) override
  {
    {
      std::scoped_lock lock(m_mutex);
      ++m_totalLoads;
      ++m_perTime[loadSpec.time];
      m_loadedTimes.insert(loadSpec.time);
      m_inProgress.insert(loadSpec.time);
    }

    const auto deadline = std::chrono::steady_clock::now() + m_delay;
    while (std::chrono::steady_clock::now() < deadline) {
      if (progress.isCancelled()) {
        std::scoped_lock lock(m_mutex);
        m_inProgress.erase(loadSpec.time);
        ++m_cancelledLoads;
        return {};
      }
      std::this_thread::sleep_for(1ms);
    }

    if (progress.isCancelled()) {
      std::scoped_lock lock(m_mutex);
      m_inProgress.erase(loadSpec.time);
      ++m_cancelledLoads;
      return {};
    }

    {
      std::scoped_lock lock(m_mutex);
      m_inProgress.erase(loadSpec.time);
    }
    return makeImage();
  }

  void setDelay(std::chrono::milliseconds delay) { m_delay = delay; }
  void setMaxConcurrent(uint32_t n) { setMaxConcurrentLoads(n); }

  int totalLoads() const
  {
    std::scoped_lock lock(m_mutex);
    return m_totalLoads;
  }
  int cancelledLoads() const
  {
    std::scoped_lock lock(m_mutex);
    return m_cancelledLoads;
  }
  std::set<uint32_t> loadedTimes() const
  {
    std::scoped_lock lock(m_mutex);
    return m_loadedTimes;
  }
  int loadCountFor(uint32_t time) const
  {
    std::scoped_lock lock(m_mutex);
    return m_perTime.count(time) ? m_perTime.at(time) : 0;
  }

private:
  mutable std::mutex m_mutex;
  std::chrono::milliseconds m_delay{ 0 };
  int m_totalLoads = 0;
  int m_cancelledLoads = 0;
  std::set<uint32_t> m_loadedTimes;
  std::set<uint32_t> m_inProgress;
  std::map<uint32_t, int> m_perTime;
};

class RecordingObserver : public ITimeSeriesLoaderObserver
{
public:
  void onInteractiveLoadComplete(uint32_t time, std::shared_ptr<ImageXYZC> image, uint64_t seq) override
  {
    std::scoped_lock lock(m_mutex);
    m_completed.push_back({ time, seq });
    m_lastImage = std::move(image);
  }
  void onInteractiveLoadFailed(uint32_t time, uint64_t seq) override
  {
    std::scoped_lock lock(m_mutex);
    m_failed.push_back({ time, seq });
  }
  void onStatusChanged(uint32_t time, TimepointStatus status) override
  {
    std::scoped_lock lock(m_mutex);
    m_statusChanges.push_back({ time, status });
  }
  void onPrefetchIdle() override
  {
    std::scoped_lock lock(m_mutex);
    ++m_idleCount;
  }

  struct Completion
  {
    uint32_t time;
    uint64_t seq;
  };

  std::vector<Completion> completed() const
  {
    std::scoped_lock lock(m_mutex);
    return m_completed;
  }
  std::vector<Completion> failed() const
  {
    std::scoped_lock lock(m_mutex);
    return m_failed;
  }
  int idleCount() const
  {
    std::scoped_lock lock(m_mutex);
    return m_idleCount;
  }
  std::vector<std::pair<uint32_t, TimepointStatus>> statusHistory() const
  {
    std::scoped_lock lock(m_mutex);
    return m_statusChanges;
  }
  bool sawStatus(uint32_t time, TimepointStatus status) const
  {
    std::scoped_lock lock(m_mutex);
    for (const auto& change : m_statusChanges) {
      if (change.first == time && change.second == status) {
        return true;
      }
    }
    return false;
  }

private:
  mutable std::mutex m_mutex;
  std::vector<Completion> m_completed;
  std::vector<Completion> m_failed;
  std::vector<std::pair<uint32_t, TimepointStatus>> m_statusChanges;
  std::shared_ptr<ImageXYZC> m_lastImage;
  int m_idleCount = 0;
};

template<class Pred>
bool
waitFor(Pred pred, std::chrono::milliseconds timeout = 5000ms)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return pred();
}

// Counts how many timepoints in [from, to] have reached RamCached.
int
cachedCount(const TimeSeriesLoader& loader, uint32_t from, uint32_t to)
{
  int n = 0;
  for (uint32_t t = from; t <= to; ++t) {
    if (loader.status(t) == TimepointStatus::RamCached) {
      ++n;
    }
  }
  return n;
}

} // namespace

TEST_CASE("TimeSeriesLoader serves an interactive request", "[timeSeriesLoader]")
{
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 64));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);
  RecordingObserver observer;
  loader.addObserver(&observer);

  // Prefetch off, so this test observes only the interactive path.
  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = false;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 9, 0);

  const uint64_t seq = loader.requestTime(3);
  REQUIRE(seq != 0);
  REQUIRE(waitFor([&] { return !observer.completed().empty(); }));

  auto completed = observer.completed();
  REQUIRE(completed.size() == 1);
  CHECK(completed[0].time == 3);
  CHECK(completed[0].seq == seq);
  CHECK(loader.status(3) == TimepointStatus::RamCached);
  CHECK(observer.sawStatus(3, TimepointStatus::RamCached));
}

TEST_CASE("TimeSeriesLoader sequence numbers increase so stale completions can be discarded", "[timeSeriesLoader]")
{
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 64));
  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);
  loader.setSeries(makeBaseSpec(), reader, 0, 9, 0);

  const uint64_t first = loader.requestTime(1);
  const uint64_t second = loader.requestTime(2);
  CHECK(second > first);
}

TEST_CASE("TimeSeriesLoader prefetches forward only, up to the configured depth", "[timeSeriesLoader]")
{
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 64));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.depth = 3;
  cfg.fillCache = false;
  loader.setPrefetchConfig(cfg);

  // Start the series already positioned at 10. Prefetch begins as soon as the
  // series is set, so opening at 0 would legitimately warm 1..3 before the
  // request for 10 is processed -- which would say nothing about directionality.
  loader.setSeries(makeBaseSpec(), reader, 0, 19, 10);
  loader.requestTime(10);

  // Expect 10 (interactive) plus 11, 12, 13 (depth 3).
  REQUIRE(waitFor([&] { return cachedCount(loader, 10, 13) == 4; }));

  // Nothing beyond the window.
  CHECK(loader.status(14) == TimepointStatus::NotCached);
  // Nothing behind the playhead: prefetch is forward-only.
  for (uint32_t t = 0; t < 10; ++t) {
    CHECK(loader.status(t) == TimepointStatus::NotCached);
  }

  auto loaded = reader->loadedTimes();
  for (uint32_t t : loaded) {
    CHECK(t >= 10);
    CHECK(t <= 13);
  }
}

TEST_CASE("TimeSeriesLoader fillCache mode prefetches to the end of the series", "[timeSeriesLoader]")
{
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 64));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.depth = 2; // ignored when fillCache is set
  cfg.fillCache = true;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 7, 0);
  loader.requestTime(0);

  REQUIRE(waitFor([&] { return cachedCount(loader, 0, 7) == 8; }));
}

TEST_CASE("TimeSeriesLoader does not load the same timepoint twice", "[timeSeriesLoader]")
{
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 64));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.depth = 4;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 9, 0);
  loader.requestTime(0);

  // Let prefetch warm 0..4.
  REQUIRE(waitFor([&] { return cachedCount(loader, 0, 4) == 5; }));
  const int loadsAfterWarm = reader->totalLoads();

  // Scrubbing onto an already-cached timepoint must not re-read it.
  loader.requestTime(2);
  REQUIRE(waitFor([&] { return cachedCount(loader, 2, 6) == 5; }));

  // Timepoints 5 and 6 are new, so at most two additional loads. Crucially,
  // 2..4 were already resident and are not fetched again.
  CHECK(reader->totalLoads() <= loadsAfterWarm + 2);
  CHECK(loader.status(2) == TimepointStatus::RamCached);
}

TEST_CASE("TimeSeriesLoader adopts an in-flight prefetch instead of duplicating it", "[timeSeriesLoader]")
{
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 64));

  auto reader = std::make_shared<CountingReader>();
  // Slow enough that the prefetch for t=1 is reliably still in flight when we
  // request it interactively.
  reader->setDelay(150ms);

  TimeSeriesLoader loader(cache);
  RecordingObserver observer;
  loader.addObserver(&observer);

  // Start with prefetch off. Otherwise setSeries begins prefetching t=1
  // immediately, which then races the slower interactive load of t=0 and can
  // finish first -- leaving nothing in flight to adopt.
  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = false;
  cfg.depth = 2;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 9, 0);
  loader.requestTime(0);
  REQUIRE(waitFor([&] { return loader.status(0) == TimepointStatus::RamCached; }));

  // Now let prefetch start, and wait until t=1 is genuinely in flight.
  cfg.enabled = true;
  loader.setPrefetchConfig(cfg);
  REQUIRE(waitFor([&] { return loader.status(1) == TimepointStatus::Loading; }));
  const int loadsBefore = reader->totalLoads();

  // Now ask for it interactively. The loader should adopt the running request.
  loader.requestTime(1);
  REQUIRE(waitFor([&] {
    for (const auto& c : observer.completed()) {
      if (c.time == 1) {
        return true;
      }
    }
    return false;
  }));

  // The running request was adopted rather than duplicated, so t=1 was submitted
  // exactly once even though it was both prefetched and requested interactively.
  CHECK(loader.status(1) == TimepointStatus::RamCached);
  CHECK(observer.failed().empty());
  CHECK(reader->loadCountFor(1) <= 1);
  CHECK(reader->totalLoads() >= loadsBefore);
}

TEST_CASE("TimeSeriesLoader cancels prefetch on request", "[timeSeriesLoader]")
{
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 64));

  auto reader = std::make_shared<CountingReader>();
  reader->setDelay(200ms);

  TimeSeriesLoader loader(cache);
  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.fillCache = true;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 30, 0);
  loader.requestTime(0);

  REQUIRE(waitFor([&] { return reader->totalLoads() >= 2; }));
  loader.cancelPrefetch();

  // Disable prefetch so it does not immediately start again, then confirm it
  // stops making progress.
  cfg.enabled = false;
  loader.setPrefetchConfig(cfg);

  REQUIRE(waitFor([&] { return loader.memoryStats().inFlightCount == 0; }));
  const int loadsAfterCancel = reader->totalLoads();
  std::this_thread::sleep_for(120ms);
  CHECK(reader->totalLoads() == loadsAfterCancel);
  CHECK(reader->cancelledLoads() > 0);
}

TEST_CASE("TimeSeriesLoader keeps already-cached timepoints when a scrub cancels prefetch", "[timeSeriesLoader]")
{
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 64));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.depth = 3;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 40, 0);
  loader.requestTime(0);
  REQUIRE(waitFor([&] { return cachedCount(loader, 0, 3) == 4; }));

  // Jump far away. Previously cached timepoints must survive.
  loader.requestTime(30);
  REQUIRE(waitFor([&] { return loader.status(30) == TimepointStatus::RamCached; }));

  CHECK(cachedCount(loader, 0, 3) == 4);
}

TEST_CASE("TimeSeriesLoader throttles prefetch instead of overfilling the cache", "[timeSeriesLoader]")
{
  CacheManager cache;
  // Room for only 4 frames. Prefetch must stop rather than thrash.
  cache.setConfig(ramConfig(frameBytes() * 4));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.fillCache = true;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 99, 0);
  loader.requestTime(0);

  // Give prefetch plenty of time to run as far as it is willing to.
  REQUIRE(waitFor([&] { return loader.memoryStats().inFlightCount == 0 && cachedCount(loader, 0, 99) >= 2; }));
  std::this_thread::sleep_for(150ms);

  // It must not have loaded the whole 100-frame series into a 4-frame budget.
  CHECK(reader->totalLoads() < 100);
  CHECK(cache.getRamBytesUsed() <= frameBytes() * 4);
}

TEST_CASE("TimeSeriesLoader pins the current timepoint so prefetch cannot evict it", "[timeSeriesLoader]")
{
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 3));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.fillCache = true;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 50, 0);
  loader.requestTime(5);

  REQUIRE(waitFor([&] { return loader.status(5) == TimepointStatus::RamCached; }));
  std::this_thread::sleep_for(150ms);

  // Even under sustained prefetch pressure against a 3-frame budget, the
  // displayed timepoint stays resident.
  LoadSpec current = makeBaseSpec();
  current.time = 5;
  CHECK(cache.isPinned(current));
  CHECK(cache.containsInMemory(current));
  CHECK(loader.status(5) == TimepointStatus::RamCached);
}

TEST_CASE("TimeSeriesLoader reports status for a range", "[timeSeriesLoader]")
{
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 64));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = false;
  loader.setPrefetchConfig(cfg);
  loader.setSeries(makeBaseSpec(), reader, 0, 9, 0);

  std::vector<TimepointStatus> statuses;
  loader.statusRange(0, 9, statuses);
  REQUIRE(statuses.size() == 10);
  for (auto s : statuses) {
    CHECK(s == TimepointStatus::NotCached);
  }

  loader.requestTime(4);
  REQUIRE(waitFor([&] { return loader.status(4) == TimepointStatus::RamCached; }));

  loader.statusRange(0, 9, statuses);
  REQUIRE(statuses.size() == 10);
  CHECK(statuses[4] == TimepointStatus::RamCached);

  // Out-of-range requests are clamped, not errors.
  loader.statusRange(5, 100, statuses);
  CHECK(statuses.size() == 5);
}

TEST_CASE("TimeSeriesLoader marks a timepoint uncached when the cache evicts it", "[timeSeriesLoader]")
{
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 64));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = false;
  loader.setPrefetchConfig(cfg);
  loader.setSeries(makeBaseSpec(), reader, 0, 9, 0);

  loader.requestTime(2);
  REQUIRE(waitFor([&] { return loader.status(2) == TimepointStatus::RamCached; }));

  cache.clearMemoryCache();

  // The eviction observer feeds back into the status vector, so the slider
  // indicator stops claiming the timepoint is resident.
  REQUIRE(waitFor([&] { return loader.status(2) != TimepointStatus::RamCached; }));
}

TEST_CASE("TimeSeriesLoader reports prefetch idle when there is nothing left to do", "[timeSeriesLoader]")
{
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 64));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);
  RecordingObserver observer;
  loader.addObserver(&observer);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.depth = 2;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 4, 0);
  loader.requestTime(0);

  REQUIRE(waitFor([&] { return observer.idleCount() > 0; }));
}

TEST_CASE("TimeSeriesLoader tracks in-flight memory", "[timeSeriesLoader]")
{
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 64));

  auto reader = std::make_shared<CountingReader>();
  reader->setDelay(200ms);

  TimeSeriesLoader loader(cache);
  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.fillCache = true;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 20, 0);
  loader.requestTime(0);

  // Once a frame has completed the loader knows the per-frame size, so in-flight
  // bytes become non-zero while prefetches are running.
  REQUIRE(waitFor([&] {
    auto stats = loader.memoryStats();
    return stats.inFlightCount > 0 && stats.inFlightBytes > 0;
  }));

  auto stats = loader.memoryStats();
  CHECK(stats.peakInFlightBytes >= stats.inFlightBytes);
}

TEST_CASE("TimeSeriesLoader can be destroyed while loads are in flight", "[timeSeriesLoader]")
{
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 64));

  auto reader = std::make_shared<CountingReader>();
  reader->setDelay(500ms);

  {
    TimeSeriesLoader loader(cache);
    TimeSeriesLoader::PrefetchConfig cfg;
    cfg.enabled = true;
    cfg.fillCache = true;
    loader.setPrefetchConfig(cfg);

    loader.setSeries(makeBaseSpec(), reader, 0, 50, 0);
    loader.requestTime(0);
    REQUIRE(waitFor([&] { return reader->totalLoads() > 0; }));
    // Destructor must cancel and join rather than block for the full delay.
  }

  SUCCEED("destroyed without hanging");
}

TEST_CASE("TimeSeriesLoader reloads a timepoint whose prefetch was cancelled", "[timeSeriesLoader]")
{
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 64));

  auto reader = std::make_shared<CountingReader>();
  reader->setDelay(200ms);

  TimeSeriesLoader loader(cache);
  RecordingObserver observer;
  loader.addObserver(&observer);

  // Prefetch off first, so the initial interactive load is not racing a prefetch.
  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = false;
  cfg.depth = 1;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 40, 0);
  loader.requestTime(0);
  REQUIRE(waitFor([&] { return loader.status(0) == TimepointStatus::RamCached; }));

  // Get a prefetch for t=1 in flight, then scrub far away so it is cancelled.
  cfg.enabled = true;
  loader.setPrefetchConfig(cfg);
  REQUIRE(waitFor([&] { return loader.status(1) == TimepointStatus::Loading; }));
  loader.requestTime(30);

  // Scrub back onto t=1 before the loader has necessarily reaped the cancelled
  // request. Adopting that doomed request would report a spurious failure; the
  // loader must notice it is cancelled and start a fresh load instead.
  loader.requestTime(1);

  REQUIRE(waitFor([&] {
    for (const auto& c : observer.completed()) {
      if (c.time == 1) {
        return true;
      }
    }
    return false;
  }));
  CHECK(loader.status(1) == TimepointStatus::RamCached);
  for (const auto& f : observer.failed()) {
    CHECK(f.time != 1);
  }
}

TEST_CASE("TimeSeriesLoader keeps prefetches that stay inside the new window", "[timeSeriesLoader]")
{
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 64));

  auto reader = std::make_shared<CountingReader>();
  reader->setDelay(150ms);

  TimeSeriesLoader loader(cache);
  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = false;
  cfg.depth = 4;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 40, 0);
  loader.requestTime(0);
  REQUIRE(waitFor([&] { return loader.status(0) == TimepointStatus::RamCached; }));

  cfg.enabled = true;
  loader.setPrefetchConfig(cfg);
  REQUIRE(waitFor([&] { return loader.status(1) == TimepointStatus::Loading; }));

  // Requesting t=1 keeps a window of [1, 5], so the in-flight prefetch for t=1
  // is still wanted and must not be cancelled and re-read.
  loader.requestTime(1);
  REQUIRE(waitFor([&] { return loader.status(1) == TimepointStatus::RamCached; }));
  CHECK(reader->loadCountFor(1) == 1);
  CHECK(reader->cancelledLoads() == 0);
}

TEST_CASE("TimeSeriesLoader prefetches past the end of a full cache once the playhead moves", "[timeSeriesLoader]")
{
  // Regression test for a playback deadlock. Prefetch filled the budget and then
  // refused to queue anything more, because it gated on free space and nothing
  // frees space except eviction. Playback reached the end of the cached run and
  // stalled forever: in show-every-frame mode the player will not advance onto a
  // frame that is not ready, so it never requests it, so the interactive path
  // never rescues it either.
  //
  // The test therefore moves the playhead only onto already-cached frames -- as
  // playback does -- and then requires prefetch ALONE to push the frontier
  // forward. Requesting the next frame directly would mask the bug, since
  // interactive loads evict freely.
  CacheManager cache;
  const std::uint64_t budgetFrames = 4;
  cache.setConfig(ramConfig(frameBytes() * budgetFrames));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.fillCache = true;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 40, 0);
  loader.requestTime(0);

  // Let prefetch fill as far as it will while standing still.
  REQUIRE(waitFor([&] { return loader.status(0) == TimepointStatus::RamCached; }));
  REQUIRE(waitFor([&] { return loader.memoryStats().inFlightCount == 0 && cachedCount(loader, 0, 40) >= 2; }));
  std::this_thread::sleep_for(150ms);

  // Find the end of the contiguous cached run: that is where playback stalls.
  uint32_t frontier = 0;
  while (frontier + 1 <= 40 && loader.status(frontier + 1) == TimepointStatus::RamCached) {
    ++frontier;
  }
  REQUIRE(frontier >= 1);
  REQUIRE(frontier < 40);

  // Walk the playhead up to the frontier, touching only cached frames.
  for (uint32_t t = 1; t <= frontier; ++t) {
    REQUIRE(loader.status(t) == TimepointStatus::RamCached);
    loader.requestTime(t);
  }

  // The frame past the frontier was never requested. Prefetch must supply it by
  // evicting frames behind the playhead, which are the oldest entries and so the
  // first LRU reclaims.
  const uint32_t beyond = frontier + 1;
  REQUIRE(waitFor([&] { return loader.status(beyond) == TimepointStatus::RamCached; }, 5000ms));

  // And the budget was still honoured: this is forward progress, not an
  // unbounded cache.
  CHECK(cache.getRamBytesUsed() <= frameBytes() * budgetFrames);
}

TEST_CASE("TimeSeriesLoader stops prefetching once the window fills the budget", "[timeSeriesLoader]")
{
  // The complement of the test above: at a standstill it must NOT keep loading,
  // or it would thrash, evicting frames it is about to want.
  CacheManager cache;
  const std::uint64_t budgetFrames = 4;
  cache.setConfig(ramConfig(frameBytes() * budgetFrames));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.fillCache = true;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 99, 0);
  loader.requestTime(0);

  REQUIRE(waitFor([&] { return loader.memoryStats().inFlightCount == 0 && cachedCount(loader, 0, 99) >= 2; }));
  std::this_thread::sleep_for(200ms);

  const int loadsAtRest = reader->totalLoads();
  std::this_thread::sleep_for(200ms);
  // Standing still, it has stopped rather than cycling frames in and out.
  CHECK(reader->totalLoads() == loadsAtRest);
  CHECK(reader->totalLoads() < 100);
}
