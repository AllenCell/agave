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
#include <filesystem>
#include <fstream>

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

// Counts timepoints in [from, to] we hold at all, in memory or on disk.
int
warmCount(const TimeSeriesLoader& loader, uint32_t from, uint32_t to)
{
  int n = 0;
  for (uint32_t t = from; t <= to; ++t) {
    const TimepointStatus s = loader.status(t);
    if (s == TimepointStatus::RamCached || s == TimepointStatus::DiskCached) {
      ++n;
    }
  }
  return n;
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
  RecordingObserver observer;
  TimeSeriesLoader loader(cache);
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

TEST_CASE("TimeSeriesLoader prefetches forward only", "[timeSeriesLoader]")
{
  // A 5-frame budget with no history reservation gives a 4-step window: one slot
  // for the pinned current step, four ahead. Sized deliberately, because the
  // window is capacity-driven -- a budget larger than the series would cover the
  // whole thing and say nothing about where the window ends.
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 5));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.historyMargin = 0;
  loader.setPrefetchConfig(cfg);

  // Start the series already positioned at 10. Prefetch begins as soon as the
  // series is set, so opening at 0 would legitimately warm the first few steps
  // before the request for 10 is processed -- which would say nothing about
  // directionality.
  loader.setSeries(makeBaseSpec(), reader, 0, 19, 10);
  loader.requestTime(10);

  // 10 (interactive) plus the forward window.
  const uint32_t lastInWindow = 14;
  REQUIRE(waitFor([&] { return cachedCount(loader, 10, lastInWindow) == 5; }));

  // Nothing beyond the window.
  CHECK(loader.status(lastInWindow + 1) == TimepointStatus::NotCached);
  // Nothing behind the playhead: prefetch is forward-only.
  for (uint32_t t = 0; t < 10; ++t) {
    CHECK(loader.status(t) == TimepointStatus::NotCached);
  }

  auto loaded = reader->loadedTimes();
  for (uint32_t t : loaded) {
    CHECK(t >= 10);
    CHECK(t <= lastInWindow);
  }
}

TEST_CASE("TimeSeriesLoader with the disk tier off prefetches only the memory window", "[timeSeriesLoader]")
{
  // This case used to assert that "fill cache" mode warmed the whole series into
  // RAM. That configuration is gone: how much gets warmed is bounded by the RAM
  // and disk budgets, and with no disk tier there is nowhere to put the rest. A
  // warm-only fetch with the disk cache off would store nowhere and still report
  // the step as DiskCached, so the warm pass is skipped entirely.
  //
  // Budget sized well below the series so the window genuinely stops short of it.
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 5));

  auto reader = std::make_shared<CountingReader>();
  RecordingObserver observer;
  TimeSeriesLoader loader(cache);
  loader.addObserver(&observer);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.historyMargin = 0;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 19, 0);
  loader.requestTime(0);

  // The window fills and prefetch then settles, rather than sweeping onwards.
  REQUIRE(waitFor([&] { return observer.idleCount() > 0; }));
  const int warm = warmCount(loader, 0, 19);
  CHECK(warm > 1);
  CHECK(warm < 20);
  // Nothing was warmed to a disk tier that does not exist.
  for (uint32_t t = 0; t <= 19; ++t) {
    CHECK(loader.status(t) != TimepointStatus::DiskCached);
  }
}

TEST_CASE("TimeSeriesLoader does not load the same timepoint twice", "[timeSeriesLoader]")
{
  // 5-frame budget, no history reservation: a 4-step window, so prefetch warms
  // exactly 0..4 and stops. With a budget larger than the series it would run on
  // to the end and the load accounting below could not be exact.
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 5));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.historyMargin = 0;
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

  RecordingObserver observer;
  TimeSeriesLoader loader(cache);
  loader.addObserver(&observer);

  // Start with prefetch off. Otherwise setSeries begins prefetching t=1
  // immediately, which then races the slower interactive load of t=0 and can
  // finish first -- leaving nothing in flight to adopt.
  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = false;
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
  RecordingObserver observer;
  TimeSeriesLoader loader(cache);
  loader.addObserver(&observer);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
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

  RecordingObserver observer;
  TimeSeriesLoader loader(cache);
  loader.addObserver(&observer);

  // Prefetch off first, so the initial interactive load is not racing a prefetch.
  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = false;
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

namespace {

// Minimal RAII temp directory, so this file can exercise the disk tier without
// depending on helpers in test_cacheManager.cpp.
class TempDir
{
public:
  TempDir()
  {
    static std::atomic<int> counter{ 0 };
    m_path = std::filesystem::temp_directory_path() / ("agave_tsl_test_" + std::to_string(counter.fetch_add(1)) + "_" +
                                                       std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    std::filesystem::create_directories(m_path);
  }
  ~TempDir()
  {
    std::error_code ec;
    std::filesystem::remove_all(m_path, ec);
  }
  std::string str() const { return m_path.string(); }

private:
  std::filesystem::path m_path;
};

CacheConfig
diskCacheConfig(std::uint64_t maxRamBytes, std::uint64_t maxDiskBytes)
{
  CacheConfig cfg;
  cfg.enabled = true;
  cfg.enableDisk = true;
  cfg.maxRamBytes = maxRamBytes;
  cfg.maxDiskBytes = maxDiskBytes;
  return cfg;
}

} // namespace

TEST_CASE("TimeSeriesLoader prefetch reads back from the disk cache", "[timeSeriesLoader]")
{
  // Regression test: prefetch used to probe only the memory tier and go straight
  // to the reader for anything not in RAM, so a time step it had already written
  // to the disk cache was re-fetched from the original source every session and
  // on every pass once it aged out of memory.
  TempDir dir;
  CacheManager cache(dir.str());
  cache.setConfig(diskCacheConfig(frameBytes() * 64, 64ULL * 1024 * 1024));

  auto reader = std::make_shared<CountingReader>();

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;

  const uint32_t lastTime = 5;

  {
    TimeSeriesLoader loader(cache);
    loader.setPrefetchConfig(cfg);
    loader.setSeries(makeBaseSpec(), reader, 0, lastTime, 0);
    loader.requestTime(0);
    // With a disk tier, only the memory window stays resident; the rest is warmed
    // onto disk. Either way we hold every time step.
    REQUIRE(waitFor([&] { return warmCount(loader, 0, lastTime) == static_cast<int>(lastTime) + 1; }));
  }

  // Everything is now on disk.
  cache.flushDiskWrites();
  const int loadsAfterFirstPass = reader->totalLoads();
  REQUIRE(loadsAfterFirstPass >= static_cast<int>(lastTime) + 1);

  // Simulate a later session: memory gone, disk intact.
  cache.clearMemoryCache();
  cache.resetStats();

  {
    TimeSeriesLoader loader(cache);
    loader.setPrefetchConfig(cfg);
    loader.setSeries(makeBaseSpec(), reader, 0, lastTime, 0);
    loader.requestTime(0);
    // Wait for RamCached, not merely warm. setSeries now seeds every step that is
    // on disk as DiskCached, so a warmCount check would be satisfied the instant
    // the series is set -- before prefetch has read anything back -- and this test
    // would pass without exercising the disk read at all. The whole series fits in
    // the memory window here, so every step should end up resident.
    REQUIRE(waitFor([&] { return cachedCount(loader, 0, lastTime) == static_cast<int>(lastTime) + 1; }));
  }

  // The second pass must come from disk, not from the reader.
  CHECK(reader->totalLoads() == loadsAfterFirstPass);
  CHECK(cache.getStats().diskHits >= static_cast<std::uint64_t>(lastTime) + 1);
  CHECK(cache.getStats().misses == 0);
}

TEST_CASE("TimeSeriesLoader prefetch wraps when playback loops", "[timeSeriesLoader]")
{
  // Regression test for looping playback stalling on the final frame. The
  // prefetch window was strictly forward, so sitting on the last time step it
  // was empty, and the first time step -- already evicted during the forward
  // pass -- was never fetched back. Show-every-frame playback then waited
  // forever for a frame nobody would load.
  //
  // 5-frame budget, no history reservation: a 4-step window. Sized so the wrapped
  // window is bounded and this can assert where it ends.
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 5));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.wrapAround = true;
  cfg.historyMargin = 0;
  loader.setPrefetchConfig(cfg);

  const uint32_t lastTime = 8;
  loader.setSeries(makeBaseSpec(), reader, 0, lastTime, lastTime);

  // Sit on the final frame, the way playback does just before wrapping.
  loader.requestTime(lastTime);
  REQUIRE(waitFor([&] { return loader.status(lastTime) == TimepointStatus::RamCached; }));

  // The frames after the last one are the first ones. Prefetch must supply them.
  REQUIRE(waitFor([&] { return loader.status(0) == TimepointStatus::RamCached; }));
  REQUIRE(waitFor([&] { return loader.status(1) == TimepointStatus::RamCached; }));

  // The window wraps but is still bounded: it reaches the first few frames, not
  // the whole series. Sitting on step 8 of 0..8, a 4-step window is 0,1,2,3.
  REQUIRE(waitFor([&] { return loader.status(3) == TimepointStatus::RamCached; }));
  CHECK(loader.status(4) == TimepointStatus::NotCached);
}

TEST_CASE("TimeSeriesLoader prefetch does not wrap when looping is off", "[timeSeriesLoader]")
{
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 64));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.wrapAround = false;
  loader.setPrefetchConfig(cfg);

  const uint32_t lastTime = 8;
  loader.setSeries(makeBaseSpec(), reader, 0, lastTime, lastTime);
  loader.requestTime(lastTime);
  REQUIRE(waitFor([&] { return loader.status(lastTime) == TimepointStatus::RamCached; }));

  // Nothing further to do at the end of a non-looping series.
  REQUIRE(waitFor([&] { return loader.memoryStats().inFlightCount == 0; }));
  std::this_thread::sleep_for(150ms);
  CHECK(loader.status(0) == TimepointStatus::NotCached);
  CHECK(loader.status(1) == TimepointStatus::NotCached);
}

TEST_CASE("TimeSeriesLoader keeps prefetching with looping on and a series larger than the cache", "[timeSeriesLoader]")
{
  // Regression test for a deadlock introduced by making the prefetch window
  // wrap. A wrapped window spans the whole series, so frames behind the playhead
  // never left the wanted set, the resident count never fell, the throttle never
  // released, and playback stalled the moment it reached the prefetch wavefront.
  // The same effect showed up while merely prefetching as a gap cycling around
  // the slider: each load evicted another wanted frame.
  //
  // The window is now clamped to what the cache can hold, so it slides.
  CacheManager cache;
  const std::uint64_t budgetFrames = 4;
  cache.setConfig(ramConfig(frameBytes() * budgetFrames));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.wrapAround = true; // looping playback
  loader.setPrefetchConfig(cfg);

  const uint32_t lastTime = 30;
  loader.setSeries(makeBaseSpec(), reader, 0, lastTime, 0);
  loader.requestTime(0);

  REQUIRE(waitFor([&] { return loader.status(0) == TimepointStatus::RamCached; }));
  REQUIRE(waitFor([&] { return loader.memoryStats().inFlightCount == 0 && cachedCount(loader, 0, lastTime) >= 2; }));
  std::this_thread::sleep_for(150ms);

  // Walk the playhead forward over cached frames only, the way playback does,
  // and require prefetch alone to keep supplying what comes next.
  uint32_t playhead = 0;
  for (int step = 0; step < 12; ++step) {
    const uint32_t next = playhead + 1;
    REQUIRE(waitFor([&] { return loader.status(next) == TimepointStatus::RamCached; }, 5000ms));
    playhead = next;
    loader.requestTime(playhead);
  }

  CHECK(playhead == 12);
  CHECK(cache.getRamBytesUsed() <= frameBytes() * budgetFrames);
}

TEST_CASE("TimeSeriesLoader does not want more frames than the cache can hold", "[timeSeriesLoader]")
{
  // A guard that prefetch settles rather than cycling frames in and out. Note
  // this one passes with or without the window clamp -- with a synthetic reader
  // and no playback driving interactive loads, the reported "gap cycling around
  // the slider" does not reproduce. It is kept as a regression guard against
  // future churn, not as evidence for the fix above; the deadlock test is what
  // actually reproduces the reported bug.
  CacheManager cache;
  const std::uint64_t budgetFrames = 3;
  cache.setConfig(ramConfig(frameBytes() * budgetFrames));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.wrapAround = true;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 50, 0);
  loader.requestTime(0);

  REQUIRE(waitFor([&] { return loader.memoryStats().inFlightCount == 0 && cachedCount(loader, 0, 50) >= 2; }));
  std::this_thread::sleep_for(200ms);

  // Standing still it must settle rather than cycling frames in and out forever.
  const int loadsAtRest = reader->totalLoads();
  std::this_thread::sleep_for(250ms);
  CHECK(reader->totalLoads() == loadsAtRest);
}

TEST_CASE("TimeSeriesLoader prefetch terminates on a series larger than memory", "[timeSeriesLoader]")
{
  // Regression test for prefetch running forever. It used to try to hold the
  // whole series in RAM; since it does not fit, every load evicted a frame
  // prefetch still wanted, the eviction marked it uncached, and it was fetched
  // straight back -- an endless cycle visible as a gap chasing itself along the
  // slider.
  //
  // The intended behaviour, and what this asserts: the near window ends up in
  // memory, everything else ends up on disk, and prefetch then STOPS.
  TempDir dir;
  CacheManager cache(dir.str());
  const std::uint64_t budgetFrames = 4;
  cache.setConfig(diskCacheConfig(frameBytes() * budgetFrames, 64ULL * 1024 * 1024));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.wrapAround = true;
  loader.setPrefetchConfig(cfg);

  const uint32_t lastTime = 20;
  const int frameCount = static_cast<int>(lastTime) + 1;
  loader.setSeries(makeBaseSpec(), reader, 0, lastTime, 0);
  loader.requestTime(0);

  // Wait for prefetch to go quiet: no loads started for a sustained stretch.
  bool settled = false;
  int lastCount = -1;
  for (int attempt = 0; attempt < 60 && !settled; ++attempt) {
    const int before = reader->totalLoads();
    std::this_thread::sleep_for(100ms);
    if (reader->totalLoads() == before && loader.memoryStats().inFlightCount == 0) {
      settled = true;
    }
    lastCount = reader->totalLoads();
  }
  REQUIRE(settled);

  // Each time step fetched about once. Endless churn would blow way past this;
  // the slack covers a frame legitimately reloaded after eviction.
  CHECK(lastCount <= frameCount * 2);

  // And nothing is left unaccounted for: every time step is either resident or
  // safely on disk.
  cache.flushDiskWrites();
  for (uint32_t t = 0; t <= lastTime; ++t) {
    const TimepointStatus s = loader.status(t);
    CHECK((s == TimepointStatus::RamCached || s == TimepointStatus::DiskCached));
  }

  // Memory still respects the budget.
  CHECK(cache.getRamBytesUsed() <= frameBytes() * budgetFrames);
}

TEST_CASE("TimeSeriesLoader reverts DiskCached when the disk tier evicts", "[timeSeriesLoader]")
{
  // Without the disk eviction observer, a frame whose disk entry is deleted
  // stays marked DiskCached forever: prefetch believes it is finished, the
  // slider paints a solid strip that is a lie, and playback silently falls back
  // to source loads.
  //
  // Eviction is forced with UNRELATED data rather than by undersizing the disk
  // for this series. Self-eviction would work, but with the warm pass still
  // sweeping the whole span it would also churn -- fetch, evict, revert,
  // re-fetch -- and this test would be asserting against a moving target.
  TempDir dir;
  CacheManager cache(dir.str());
  const std::uint64_t diskFrames = 16;
  // RAM must exceed the memory window by more than one frame, or
  // canStartPrefetchLocked latches once the window is resident and the disk warm
  // pass never gets to run -- the RAM throttle gates disk warming too.
  cache.setConfig(diskCacheConfig(frameBytes() * 8, frameBytes() * diskFrames));

  auto reader = std::make_shared<CountingReader>();
  RecordingObserver observer;
  TimeSeriesLoader loader(cache);
  loader.addObserver(&observer);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  loader.setPrefetchConfig(cfg);

  const uint32_t lastTime = 5;
  loader.setSeries(makeBaseSpec(), reader, 0, lastTime, 0);
  loader.requestTime(0);

  // Every step is held somewhere: the near ones in RAM, the rest warmed to disk.
  REQUIRE(waitFor([&] { return warmCount(loader, 0, lastTime) == static_cast<int>(lastTime) + 1; }));
  cache.flushDiskWrites();

  // Stop prefetch so it cannot re-fetch what we are about to evict. The observer
  // is independent of prefetch, so this isolates the eviction path.
  cfg.enabled = false;
  loader.setPrefetchConfig(cfg);
  loader.cancelPrefetch();

  int diskCachedBefore = 0;
  for (uint32_t t = 0; t <= lastTime; ++t) {
    if (loader.status(t) == TimepointStatus::DiskCached) {
      ++diskCachedBefore;
    }
  }
  REQUIRE(diskCachedBefore > 0);

  // Fill the disk tier with an unrelated series. The current series' entries are
  // older, so the disk LRU drops them first.
  for (std::uint64_t i = 0; i < diskFrames * 2; ++i) {
    LoadSpec filler = makeBaseSpec();
    filler.filepath = "unrelated.tif";
    filler.time = static_cast<uint32_t>(i);
    cache.storeImage(filler, makeImage());
  }
  cache.flushDiskWrites();

  // At least one step we believed was on disk is now honestly reported as
  // uncached, rather than still claiming to be cached with its file deleted.
  REQUIRE(waitFor([&] {
    for (uint32_t t = 0; t <= lastTime; ++t) {
      if (loader.status(t) == TimepointStatus::NotCached) {
        return true;
      }
    }
    return false;
  }));

  // And the tier respected its cap throughout.
  CHECK(cache.getUsage().diskBytesUsed <= frameBytes() * diskFrames);
}

TEST_CASE("TimeSeriesLoader memory window fills the RAM budget", "[timeSeriesLoader]")
{
  // A budget of 10 frames with historyMargin 4 reserves one for the pinned
  // current step and four behind it, leaving five forward steps -- not the fixed
  // small lookahead the retired `depth` setting used to impose.
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 10));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.historyMargin = 4;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 39, 10);
  loader.requestTime(10);

  REQUIRE(waitFor([&] { return cachedCount(loader, 11, 15) == 5; }));
  // No further than the budget allows, and nothing behind the playhead.
  CHECK(loader.status(16) == TimepointStatus::NotCached);
  CHECK(loader.status(9) == TimepointStatus::NotCached);
}

TEST_CASE("TimeSeriesLoader honours a historyMargin of zero", "[timeSeriesLoader]")
{
  // Margin 0 is the all-forward shape: budget - 1 forward steps, reserving only
  // the pinned current step. This must work, not just the default of 4.
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 10));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.historyMargin = 0;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 39, 10);
  loader.requestTime(10);

  REQUIRE(waitFor([&] { return cachedCount(loader, 11, 19) == 9; }));
  CHECK(loader.status(20) == TimepointStatus::NotCached);
}

TEST_CASE("TimeSeriesLoader survives a historyMargin larger than the budget", "[timeSeriesLoader]")
{
  // The saturating-subtraction gate. Written as budgetFrames - 1 - historyMargin
  // this underflows to a huge uint64, clamps to the whole series, and makes
  // prefetch churn forever -- the exact failure every clamp here exists to
  // prevent. It must saturate to a single forward step instead.
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 3));

  auto reader = std::make_shared<CountingReader>();
  RecordingObserver observer;
  TimeSeriesLoader loader(cache);
  loader.addObserver(&observer);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.historyMargin = 99;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 39, 0);
  loader.requestTime(0);

  REQUIRE(waitFor([&] { return observer.idleCount() > 0; }));
  CHECK(cachedCount(loader, 1, 1) == 1);
  CHECK(loader.status(5) == TimepointStatus::NotCached);
}

TEST_CASE("TimeSeriesLoader keeps history resident after playing forward", "[timeSeriesLoader]")
{
  // The margin is a reservation, not a window: nothing is ever fetched backward,
  // but the slots it leaves free are filled by LRU with the frames just
  // displayed, so a small backward scrub stays instant.
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 10));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.historyMargin = 4;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 39, 0);
  for (uint32_t t = 0; t <= 8; ++t) {
    loader.requestTime(t);
    REQUIRE(waitFor([&] { return loader.status(t) == TimepointStatus::RamCached; }));
  }

  // Steps just behind the playhead are still resident, and were each loaded once
  // -- they were retained, not evicted and fetched back.
  CHECK(cachedCount(loader, 5, 7) == 3);
  for (uint32_t t = 5; t <= 7; ++t) {
    CHECK(reader->loadCountFor(t) == 1);
  }
}

TEST_CASE("TimeSeriesLoader never fetches backward after a large jump", "[timeSeriesLoader]")
{
  // After a jump the history slots are simply empty. They refill as playback
  // advances; prefetch must not go and fetch them.
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 10));

  auto reader = std::make_shared<CountingReader>();
  RecordingObserver observer;
  TimeSeriesLoader loader(cache);
  loader.addObserver(&observer);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.historyMargin = 4;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 99, 0);
  loader.requestTime(80);
  REQUIRE(waitFor([&] { return observer.idleCount() > 0; }));

  for (uint32_t t = 76; t <= 79; ++t) {
    CHECK(reader->loadCountFor(t) == 0);
  }
}

TEST_CASE("TimeSeriesLoader clamps the disk warm set to the disk budget", "[timeSeriesLoader]")
{
  // Series of 40. RAM holds 4 frames, so with margin 0 the memory window is 3.
  // Disk holds 12, and the current step plus the memory window are written there
  // too, so the warm set beyond them is 12 - 4 = 8 steps. The far tail must stay
  // honestly uncached rather than being swept in and churning.
  TempDir dir;
  CacheManager cache(dir.str());
  const std::uint64_t diskFrames = 12;
  cache.setConfig(diskCacheConfig(frameBytes() * 4, frameBytes() * diskFrames));

  auto reader = std::make_shared<CountingReader>();
  RecordingObserver observer;
  TimeSeriesLoader loader(cache);
  loader.addObserver(&observer);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.historyMargin = 0;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 39, 0);
  loader.requestTime(0);

  REQUIRE(waitFor([&] { return observer.idleCount() > 0; }));

  // Prefetch settled without covering the series, and the disk stayed capped.
  CHECK(warmCount(loader, 0, 39) < 40);
  CHECK(loader.status(39) == TimepointStatus::NotCached);
  cache.flushDiskWrites();
  CHECK(cache.getUsage().diskBytesUsed <= frameBytes() * diskFrames);

  // Prefetch really stopped rather than cycling.
  const int loadsAtRest = reader->totalLoads();
  std::this_thread::sleep_for(250ms);
  CHECK(reader->totalLoads() == loadsAtRest);
}

TEST_CASE("TimeSeriesLoader seeds status from the disk cache on a fresh series", "[timeSeriesLoader]")
{
  // A later session must recognise an already-warm disk cache immediately, before
  // loading anything. Without this every step starts NotCached: the strip paints
  // blank even though the data is local, and the warm pass re-targets steps that
  // are already on disk.
  TempDir dir;
  CacheManager cache(dir.str());
  cache.setConfig(diskCacheConfig(frameBytes() * 64, 64ULL * 1024 * 1024));

  LoadSpec base = makeBaseSpec();
  for (uint32_t t = 0; t <= 5; ++t) {
    LoadSpec spec = base;
    spec.time = t;
    cache.storeImage(spec, makeImage());
  }
  cache.flushDiskWrites();
  cache.clearMemoryCache();

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);
  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = false; // isolate seeding from prefetch
  loader.setPrefetchConfig(cfg);

  loader.setSeries(base, reader, 0, 5, 0);

  for (uint32_t t = 0; t <= 5; ++t) {
    CHECK(loader.status(t) == TimepointStatus::DiskCached);
  }
  CHECK(reader->totalLoads() == 0);
}

TEST_CASE("TimeSeriesLoader warm-only prefetch does not pull volumes into RAM", "[timeSeriesLoader]")
{
  // Regression gate. The fetch site called findImage unconditionally, ignoring
  // warmOnly, and findImage promotes a disk hit into memory -- so warming a series
  // that was already on disk dragged the whole timeline through RAM, evicting the
  // near steps that storeImageOnDiskOnly exists to protect.
  TempDir dir;
  CacheManager cache(dir.str());
  const std::uint64_t ramFrames = 4;
  cache.setConfig(diskCacheConfig(frameBytes() * ramFrames, 64ULL * 1024 * 1024));

  LoadSpec base = makeBaseSpec();
  for (uint32_t t = 0; t <= 19; ++t) {
    LoadSpec spec = base;
    spec.time = t;
    cache.storeImage(spec, makeImage());
  }
  cache.flushDiskWrites();
  cache.clearMemoryCache();

  auto reader = std::make_shared<CountingReader>();
  RecordingObserver observer;
  TimeSeriesLoader loader(cache);
  loader.addObserver(&observer);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.historyMargin = 0;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(base, reader, 0, 19, 0);
  loader.requestTime(0);
  REQUIRE(waitFor([&] { return observer.idleCount() > 0; }));

  // RAM never exceeds the memory window's worth, and steps outside it were never
  // promoted into it.
  CHECK(cache.getUsage().ramBytesUsed <= frameBytes() * ramFrames);
  CHECK(loader.status(19) == TimepointStatus::DiskCached);
  // Nothing went back to the source: it was all already local.
  CHECK(reader->totalLoads() == 0);

  // The load-bearing assertion. The end state above is NOT sufficient: dragging
  // every step through RAM leaves the same statuses behind, because a promoted
  // step is evicted moments later (RAM holds four) and eviction re-marks it
  // DiskCached. What distinguishes the two is how the volumes were reached --
  // findImage promotes and counts a disk hit, while a warm-only probe does
  // neither. So only the memory-window steps may have hit the disk tier, not all
  // twenty.
  CHECK(cache.getStats().diskHits <= ramFrames + 1);
}

TEST_CASE("TimeSeriesLoader three-run cross-session scenario", "[timeSeriesLoader]")
{
  // Run 1 warms series A. Run 2 reloads A and must hit disk, not the source.
  // Run 3 loads an unrelated series B, which must displace A by disk LRU.
  // A fresh CacheManager per run stands in for a fresh process over the same
  // cache directory.
  TempDir dir;
  const std::uint64_t ramFrames = 4;
  const std::uint64_t diskFrames = 12;

  LoadSpec seriesA = makeBaseSpec();
  seriesA.filepath = "seriesA.tif";
  LoadSpec seriesB = makeBaseSpec();
  seriesB.filepath = "seriesB.tif";

  auto makeCfg = [] {
    TimeSeriesLoader::PrefetchConfig cfg;
    cfg.enabled = true;
    cfg.historyMargin = 0;
    return cfg;
  };

  {
    CacheManager cache(dir.str());
    cache.setConfig(diskCacheConfig(frameBytes() * ramFrames, frameBytes() * diskFrames));
    auto reader = std::make_shared<CountingReader>();
    RecordingObserver observer;
    TimeSeriesLoader loader(cache);
    loader.addObserver(&observer);
    loader.setPrefetchConfig(makeCfg());

    loader.setSeries(seriesA, reader, 0, 7, 0);
    loader.requestTime(0);
    REQUIRE(waitFor([&] { return observer.idleCount() > 0; }));
    cache.flushDiskWrites();

    REQUIRE(reader->totalLoads() > 0);
    CHECK(cache.getUsage().diskBytesUsed <= frameBytes() * diskFrames);
  }

  {
    CacheManager cache(dir.str());
    cache.setConfig(diskCacheConfig(frameBytes() * ramFrames, frameBytes() * diskFrames));
    auto reader = std::make_shared<CountingReader>();
    TimeSeriesLoader loader(cache);
    loader.setPrefetchConfig(makeCfg());

    loader.setSeries(seriesA, reader, 0, 7, 0);
    // Seeded from disk before anything is loaded.
    CHECK(warmCount(loader, 0, 7) > 0);
    CHECK(reader->totalLoads() == 0);

    loader.requestTime(0);
    REQUIRE(waitFor([&] { return loader.status(0) == TimepointStatus::RamCached; }));
    // Served from the disk tier, not re-read from the original source.
    CHECK(reader->totalLoads() == 0);
    CHECK(cache.getStats().diskHits > 0);
  }

  {
    CacheManager cache(dir.str());
    cache.setConfig(diskCacheConfig(frameBytes() * ramFrames, frameBytes() * diskFrames));
    auto reader = std::make_shared<CountingReader>();
    RecordingObserver observer;
    TimeSeriesLoader loader(cache);
    loader.addObserver(&observer);
    loader.setPrefetchConfig(makeCfg());

    loader.setSeries(seriesB, reader, 0, 7, 0);
    loader.requestTime(0);
    REQUIRE(waitFor([&] { return observer.idleCount() > 0; }));
    cache.flushDiskWrites();

    // B is held, the cap was respected, and A gave way to make room for it.
    CHECK(warmCount(loader, 0, 7) > 0);
    CHECK(cache.getUsage().diskBytesUsed <= frameBytes() * diskFrames);
    int aStillOnDisk = 0;
    for (uint32_t t = 0; t <= 7; ++t) {
      LoadSpec spec = seriesA;
      spec.time = t;
      if (cache.containsOnDisk(spec)) {
        ++aStillOnDisk;
      }
    }
    CHECK(aStillOnDisk < 8);
  }
}

TEST_CASE("TimeSeriesLoader does not want the whole series before the frame size is known",
          "[timeSeriesLoader]")
{
  // Regression test for a prefetch loop that thrashed RAM forever.
  //
  // The memory window is sized from the RAM budget in frames, which needs
  // bytesPerFrame. That is learned from a completed load -- but on a fully warm
  // disk cache no load ever completes, because every step is served by a
  // findImage disk hit. So bytesPerFrame stayed 0, the capacity clamp was skipped
  // entirely, and the window became the ENTIRE series.
  //
  // With a series larger than the RAM budget that is an unbounded cyclic sweep:
  // each promotion evicts the frame it is about to need next, the eviction marks
  // that step DiskCached, the window still wants it, and it is promoted again.
  // Forever. No source fetches and no disk evictions, so it looks like nothing is
  // happening except a gap walking along the slider.
  //
  // Deliberately no requestTime() here: the interactive path sets bytesPerFrame
  // from a cache hit, which masks this entirely.
  TempDir dir;
  CacheManager cache(dir.str());
  // 10 frames of RAM with the default margin of 4 gives a 5-step window: big
  // enough that "window collapsed to 1" and "window is the whole series" are both
  // distinguishable from correct behaviour.
  const std::uint64_t ramFrames = 10;
  const uint32_t lastTime = 19; // 20 steps, well over the RAM budget
  cache.setConfig(diskCacheConfig(frameBytes() * ramFrames, 64ULL * 1024 * 1024));

  LoadSpec base = makeBaseSpec();
  for (uint32_t t = 0; t <= lastTime; ++t) {
    LoadSpec spec = base;
    spec.time = t;
    cache.storeImage(spec, makeImage());
  }
  cache.flushDiskWrites();
  cache.clearMemoryCache();
  cache.resetStats();

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);
  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.wrapAround = true; // playback loop is on by default in the app
  loader.setPrefetchConfig(cfg);
  loader.setSeries(base, reader, 0, lastTime, 0);

  // Let it run, then confirm it has stopped rather than cycling.
  std::this_thread::sleep_for(700ms);
  const std::uint64_t hitsA = cache.getStats().diskHits;
  std::this_thread::sleep_for(500ms);
  const std::uint64_t hitsB = cache.getStats().diskHits;

  // Settled, rather than sweeping the series for ever.
  CHECK(hitsB == hitsA);
  // Bounded by the budget. An unclamped window shows up as a disk hit count far
  // past the number of steps that can be resident, and still climbing.
  CHECK(hitsB <= ramFrames);
  // But it did NOT collapse to the unknown-frame-size minimum of one step either:
  // the frame size has to be learned from these promotions, or the window stays at
  // its minimum for the whole session and prefetch is useless on a warm cache.
  CHECK(hitsB > 1);
}

TEST_CASE("TimeSeriesLoader with prefetch off still caches on-demand loads", "[timeSeriesLoader]")
{
  // The observable difference the prefetch switch is supposed to make. With it
  // off nothing is loaded except what is asked for -- no window, no warm pass --
  // but a requested step is still cached in memory and queued to disk, so
  // returning to it is free.
  TempDir dir;
  CacheManager cache(dir.str());
  cache.setConfig(diskCacheConfig(frameBytes() * 16, 64ULL * 1024 * 1024));

  auto reader = std::make_shared<CountingReader>();
  RecordingObserver observer;
  TimeSeriesLoader loader(cache);
  loader.addObserver(&observer);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = false;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 19, 0);
  loader.requestTime(5);
  REQUIRE(waitFor([&] { return loader.status(5) == TimepointStatus::RamCached; }));

  // Nothing was warmed around it, in either tier.
  std::this_thread::sleep_for(300ms);
  CHECK(reader->totalLoads() == 1);
  for (uint32_t t = 0; t <= 19; ++t) {
    if (t != 5) {
      CHECK(loader.status(t) == TimepointStatus::NotCached);
    }
  }

  // But the step that WAS requested is cached in both tiers.
  LoadSpec spec = makeBaseSpec();
  spec.time = 5;
  CHECK(cache.containsInMemory(spec));
  cache.flushDiskWrites();
  CHECK(cache.containsOnDisk(spec));

  // So asking again costs no source read.
  loader.requestTime(5);
  REQUIRE(waitFor([&] { return observer.completed().size() >= 2; }));
  CHECK(reader->totalLoads() == 1);
}
