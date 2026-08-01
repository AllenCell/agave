#include "TimeSeriesLoader.h"

#include "ImageXYZC.h"
#include "LoadRequest.h"
#include "Logging.h"

#include <algorithm>
#include <chrono>

namespace {

// How long the loader thread sleeps between readiness checks while waiting on an
// interactive load. Short enough that a scrub preempts promptly, long enough not
// to spin.
constexpr auto kPollInterval = std::chrono::milliseconds(2);

std::uint64_t
imageBytes(const ImageXYZC& image)
{
  const std::uint64_t bytesPerPixel = static_cast<std::uint64_t>(ImageXYZC::IN_MEMORY_BPP / 8);
  return static_cast<std::uint64_t>(image.sizeX()) * static_cast<std::uint64_t>(image.sizeY()) *
         static_cast<std::uint64_t>(image.sizeZ()) * static_cast<std::uint64_t>(image.sizeC()) * bytesPerPixel;
}

} // namespace

TimeSeriesLoader::TimeSeriesLoader(CacheManager& cache)
  : m_cache(cache)
{
  m_cache.addEvictionObserver(this);
  m_thread = std::thread([this] { threadMain(); });
}

TimeSeriesLoader::~TimeSeriesLoader()
{
  m_cache.removeEvictionObserver(this);
  {
    std::scoped_lock lock(m_mutex);
    m_stop = true;
    // Cancel in place: the loader thread may be parked waiting on a load, and
    // FutureLoadRequest's destructor would otherwise wait for it to finish.
    for (auto& entry : m_inFlight) {
      entry.second->cancel();
    }
  }
  m_wake.notify_all();
  if (m_thread.joinable()) {
    m_thread.join();
  }

  if (m_havePinned) {
    m_cache.unpin(m_pinnedSpec);
    m_havePinned = false;
  }
}

void
TimeSeriesLoader::setSeries(const LoadSpec& base,
                            std::shared_ptr<IFileReader> reader,
                            uint32_t minTime,
                            uint32_t maxTime,
                            uint32_t currentTime)
{
  LoadSpec previousPinned;
  bool hadPinned = false;

  {
    std::scoped_lock lock(m_mutex);
    cancelPrefetchLocked();

    hadPinned = m_havePinned;
    previousPinned = m_pinnedSpec;
    m_havePinned = false;

    m_baseSpec = base;
    m_reader = std::move(reader);
    m_minTime = minTime;
    m_maxTime = std::max(minTime, maxTime);
    m_currentTime = std::clamp(currentTime, m_minTime, m_maxTime);
    m_haveSeries = true;
    ++m_seriesGeneration;

    // A different series means a different volume shape, so the previous
    // per-frame size estimate no longer applies.
    m_bytesPerFrame = 0;
    m_interactivePending = false;
    m_prefetchIdleReported = false;

    m_warmOnly.clear();
    m_warmRefused.clear();
    m_diskIdToTime.clear();
    m_status.assign(static_cast<size_t>(m_maxTime - m_minTime) + 1, TimepointStatus::NotCached);
  }

  if (hadPinned) {
    m_cache.unpin(previousPinned);
  }

  // Reconcile with whatever is already cached. Two cases: reopening a file whose
  // timepoints are still resident in this process, and -- via the disk probe -- a
  // later session opening a series this machine has already warmed.
  //
  // The disk probe is what makes a warm start visible and cheap. Without it every
  // step begins NotCached, so the strip paints blank even though the data is
  // local, and the warm pass re-targets steps that are already on disk.
  //
  // One makeKey per time step here, which stats the source file. That is once per
  // series load, the same order as the memory reconciliation this replaces -- not
  // the per-repaint polling the TimepointStatus comment warns about. The same pass
  // builds the disk-id map so the eviction path never needs a key.
  std::vector<std::pair<uint32_t, TimepointStatus>> changes;
  {
    for (uint32_t t = minTime; t <= m_maxTime; ++t) {
      LoadSpec spec;
      {
        std::scoped_lock lock(m_mutex);
        spec = specForLocked(t);
      }
      const bool inMemory = m_cache.containsInMemory(spec);
      const bool onDisk = inMemory ? false : m_cache.containsOnDisk(spec);
      const std::string diskId = m_cache.diskCacheIdFor(spec);
      {
        std::scoped_lock lock(m_mutex);
        m_diskIdToTime[diskId] = t;
        if (inMemory) {
          setStatusLocked(t, TimepointStatus::RamCached, changes);
        } else if (onDisk) {
          setStatusLocked(t, TimepointStatus::DiskCached, changes);
        }
      }
    }
  }
  notifyStatusChanges(changes);

  m_wake.notify_all();
}

void
TimeSeriesLoader::setPrefetchConfig(const PrefetchConfig& config)
{
  {
    std::scoped_lock lock(m_mutex);
    m_prefetchConfig = config;
    m_prefetchIdleReported = false;
    if (!config.enabled) {
      cancelPrefetchLocked();
    }
  }
  m_wake.notify_all();
}

TimeSeriesLoader::PrefetchConfig
TimeSeriesLoader::prefetchConfig() const
{
  std::scoped_lock lock(m_mutex);
  return m_prefetchConfig;
}

void
TimeSeriesLoader::addObserver(ITimeSeriesLoaderObserver* observer)
{
  if (!observer) {
    return;
  }
  std::scoped_lock lock(m_mutex);
  if (std::find(m_observers.begin(), m_observers.end(), observer) == m_observers.end()) {
    m_observers.push_back(observer);
  }
}

void
TimeSeriesLoader::removeObserver(ITimeSeriesLoaderObserver* observer)
{
  std::scoped_lock lock(m_mutex);
  auto it = std::find(m_observers.begin(), m_observers.end(), observer);
  if (it != m_observers.end()) {
    m_observers.erase(it);
  }
}

uint64_t
TimeSeriesLoader::requestTime(uint32_t time)
{
  uint64_t seq = 0;
  {
    std::scoped_lock lock(m_mutex);
    if (!m_haveSeries) {
      return 0;
    }
    time = std::clamp(time, m_minTime, m_maxTime);
    seq = m_nextSeq++;
    m_interactivePending = true;
    m_interactiveTime = time;
    m_interactiveSeq = seq;
    m_currentTime = time;
    m_prefetchIdleReported = false;

    // Cancel only prefetches that fall outside the prefetch window for the new
    // position. Anything still inside it remains useful, so cancelling it would
    // throw away work we are about to ask for again -- and a small scrub forward
    // usually leaves most of the window intact. The newly requested timepoint is
    // never cancelled: the loader thread adopts that request instead of starting
    // a duplicate load.
    const std::vector<uint32_t> window = prefetchWindowLocked();
    for (auto& entry : m_inFlight) {
      if (entry.first == time || std::find(window.begin(), window.end(), entry.first) != window.end()) {
        continue;
      }
      entry.second->cancel();
    }
  }
  m_wake.notify_all();
  return seq;
}

void
TimeSeriesLoader::cancelPrefetch()
{
  std::vector<std::pair<uint32_t, TimepointStatus>> changes;
  {
    std::scoped_lock lock(m_mutex);
    cancelPrefetchLocked();
    // Anything that was queued or loading is no longer either.
    for (uint32_t t = m_minTime; m_haveSeries && t <= m_maxTime; ++t) {
      TimepointStatus s = m_status[static_cast<size_t>(t - m_minTime)];
      if (s == TimepointStatus::Queued || s == TimepointStatus::Loading) {
        setStatusLocked(t, TimepointStatus::NotCached, changes);
      }
    }
  }
  notifyStatusChanges(changes);
  m_wake.notify_all();
}

void
TimeSeriesLoader::cancelPrefetchLocked()
{
  for (auto& entry : m_inFlight) {
    entry.second->cancel();
  }
  // The loader thread reaps them and corrects m_inFlightBytes; dropping the
  // shared_ptrs here would block this thread in FutureLoadRequest's destructor.
}

TimepointStatus
TimeSeriesLoader::status(uint32_t time) const
{
  std::scoped_lock lock(m_mutex);
  if (!m_haveSeries || time < m_minTime || time > m_maxTime) {
    return TimepointStatus::NotCached;
  }
  return m_status[static_cast<size_t>(time - m_minTime)];
}

void
TimeSeriesLoader::statusRange(uint32_t from, uint32_t to, std::vector<TimepointStatus>& out) const
{
  out.clear();
  std::scoped_lock lock(m_mutex);
  if (!m_haveSeries) {
    return;
  }
  from = std::clamp(from, m_minTime, m_maxTime);
  to = std::clamp(to, m_minTime, m_maxTime);
  if (to < from) {
    return;
  }
  out.reserve(static_cast<size_t>(to - from) + 1);
  for (uint32_t t = from; t <= to; ++t) {
    out.push_back(m_status[static_cast<size_t>(t - m_minTime)]);
  }
}

TimeSeriesLoader::MemoryStats
TimeSeriesLoader::memoryStats() const
{
  std::scoped_lock lock(m_mutex);
  MemoryStats stats;
  stats.inFlightBytes = m_inFlightBytes;
  stats.inFlightCount = static_cast<uint32_t>(m_inFlight.size());
  stats.peakInFlightBytes = m_peakInFlightBytes;
  return stats;
}

void
TimeSeriesLoader::onEvictedFromMemory(const CacheKey& key)
{
  std::vector<std::pair<uint32_t, TimepointStatus>> changes;
  uint32_t evictedTime = 0;
  bool wasResident = false;
  LoadSpec evictedSpec;
  {
    std::scoped_lock lock(m_mutex);
    if (!m_haveSeries || key.time < m_minTime || key.time > m_maxTime) {
      return;
    }
    // Match on the fields that identify a timepoint within the current series.
    // Filepath is deliberately not compared: CacheKey stores it normalized
    // (lowercased on Windows), and a loader only ever serves one series at a
    // time. A mismatch here would only make the slider indicator briefly
    // optimistic, which self-corrects on the next load.
    if (key.scene != m_baseSpec.scene || key.subpath != m_baseSpec.subpath) {
      return;
    }
    if (m_status[static_cast<size_t>(key.time - m_minTime)] != TimepointStatus::RamCached) {
      return;
    }
    evictedTime = key.time;
    wasResident = true;
    m_prefetchIdleReported = false;
    evictedSpec = specForLocked(evictedTime);
  }

  if (wasResident) {
    // Distinguish "dropped from memory but safely on disk" from "gone". Without
    // this the frame looks never-fetched, prefetch immediately pulls it back,
    // that evicts another, and the cycle never ends.
    const bool onDisk = m_cache.containsOnDisk(evictedSpec);
    std::scoped_lock lock(m_mutex);
    setStatusLocked(evictedTime, onDisk ? TimepointStatus::DiskCached : TimepointStatus::NotCached, changes);
  }

  notifyStatusChanges(changes);
  m_wake.notify_all();
}

void
TimeSeriesLoader::onEvictedFromDisk(const std::string& diskCacheId)
{
  std::vector<std::pair<uint32_t, TimepointStatus>> changes;
  {
    std::scoped_lock lock(m_mutex);
    if (!m_haveSeries) {
      return;
    }
    auto it = m_diskIdToTime.find(diskCacheId);
    if (it == m_diskIdToTime.end()) {
      // Some other dataset's entry. Most evictions during warming are ours, but
      // the disk tier is shared.
      return;
    }
    // Only a step we believed was disk-resident changes. One currently in RAM
    // stays RamCached: it is still displayable, and losing its disk copy does not
    // change that.
    if (m_status[static_cast<size_t>(it->second - m_minTime)] != TimepointStatus::DiskCached) {
      return;
    }
    setStatusLocked(it->second, TimepointStatus::NotCached, changes);
    m_prefetchIdleReported = false;
  }
  notifyStatusChanges(changes);
  m_wake.notify_all();
}

LoadSpec
TimeSeriesLoader::specForLocked(uint32_t time) const
{
  LoadSpec spec = m_baseSpec;
  spec.time = time;
  return spec;
}

void
TimeSeriesLoader::setStatusLocked(uint32_t time,
                                  TimepointStatus status,
                                  std::vector<std::pair<uint32_t, TimepointStatus>>& changes)
{
  if (!m_haveSeries || time < m_minTime || time > m_maxTime) {
    return;
  }
  TimepointStatus& slot = m_status[static_cast<size_t>(time - m_minTime)];
  if (slot == status) {
    return;
  }
  slot = status;
  changes.emplace_back(time, status);
}

TimeSeriesLoader::PrefetchPermission
TimeSeriesLoader::prefetchPermissionLocked() const
{
  if (!m_haveSeries || !m_prefetchConfig.enabled || m_stop || m_interactivePending) {
    return PrefetchPermission::None;
  }
  if (!m_reader) {
    return PrefetchPermission::None;
  }
  const uint32_t maxInFlight = std::max(1u, m_reader->maxConcurrentLoads());
  if (m_inFlight.size() >= maxInFlight) {
    return PrefetchPermission::None;
  }

  if (m_bytesPerFrame == 0) {
    // Frame size still unknown (nothing has completed yet), so stay at one load
    // in flight rather than guessing.
    return m_inFlight.empty() ? PrefetchPermission::Any : PrefetchPermission::None;
  }

  // Throttle on how many frames we still *want* are already resident, NOT on
  // whether the cache happens to be full.
  //
  // Gating on free space deadlocks playback: prefetch fills the budget, then
  // refuses to queue because nothing is free, and nothing ever becomes free
  // because eviction is the only thing that frees space. Playback then waits
  // forever for the next frame while frames far behind the playhead sit there
  // uselessly.
  //
  // Frames behind the playhead are exactly what LRU should reclaim, and they are
  // the oldest entries so LRU reclaims them first. The case actually worth
  // avoiding is prefetching so far ahead that we evict frames we are about to
  // display, which happens only once the window itself no longer fits the
  // budget. So: stop when the frames we want are already filling the budget, and
  // otherwise let the store proceed and let LRU do its job.
  //
  // Crucially this throttle applies ONLY to memory-window fetches. A warm-only
  // fetch goes straight to the disk tier via storeImageOnDiskOnly and never
  // enters RAM, so gating it on the RAM budget stops disk warming for a reason
  // that does not apply to it. That is not hypothetical: once the memory window
  // fills the budget this condition latches permanently, and every remaining time
  // step would be left in neither tier.
  const std::uint64_t budgetFrames = m_cache.getConfig().maxRamBytes / m_bytesPerFrame;
  if (budgetFrames == 0) {
    return PrefetchPermission::None;
  }

  std::uint64_t wantedResident = m_inFlight.size();
  // The current timepoint counts too: it is pinned and must stay resident.
  if (m_status[static_cast<size_t>(m_currentTime - m_minTime)] == TimepointStatus::RamCached) {
    ++wantedResident;
  }
  for (uint32_t t : prefetchWindowLocked()) {
    if (m_status[static_cast<size_t>(t - m_minTime)] == TimepointStatus::RamCached) {
      ++wantedResident;
    }
  }
  return wantedResident < budgetFrames ? PrefetchPermission::Any : PrefetchPermission::WarmOnly;
}

std::vector<uint32_t>
TimeSeriesLoader::prefetchWindowLocked() const
{
  std::vector<uint32_t> window;
  if (!m_haveSeries || m_maxTime <= m_minTime) {
    return window;
  }

  const std::uint64_t span = static_cast<std::uint64_t>(m_maxTime - m_minTime) + 1;
  // Never include the current timepoint, so a wrapping window stops one short of
  // a full lap rather than coming back around to where it started.
  const std::uint64_t maxSteps = span - 1;
  // The MEMORY window: as many forward steps as the RAM budget holds, after the
  // pinned current step and the history reservation.
  //
  // The capacity clamp is not an optimization, it is what keeps prefetch live. The
  // throttle stops once the frames we want are all resident, so if we want more
  // frames than fit, that condition can never clear: prefetch either stalls
  // forever or churns, evicting one wanted frame to load another. Wrapping made
  // this acute -- a wrapped window spans the whole series, so frames behind the
  // playhead never leave the wanted set and the window stops sliding as playback
  // advances, which deadlocks exactly when the playhead reaches the prefetch
  // wavefront.
  //
  // Bounding the window means frames fall out behind the playhead, the count
  // drops, prefetch resumes, and LRU reclaims the frames that just left.
  std::uint64_t steps = maxSteps;
  if (m_bytesPerFrame > 0) {
    const std::uint64_t budgetFrames = m_cache.getConfig().maxRamBytes / m_bytesPerFrame;
    // Saturating, NOT `budgetFrames - 1 - historyMargin`. Unsigned underflow there
    // yields a huge value that clamps to the whole series -- precisely the churn
    // this clamp prevents. Always allow at least one step so playback can inch
    // forward on a budget too small to hold even two frames.
    //
    // The reservation is the pinned current step plus historyMargin slots behind
    // the playhead. Those are never fetched: leaving them out of the window is the
    // whole mechanism, because it leaves room that LRU fills with the frames just
    // displayed.
    const std::uint64_t reserved = 1ULL + m_prefetchConfig.historyMargin;
    const std::uint64_t forwardCapacity = budgetFrames > reserved ? budgetFrames - reserved : 1;
    steps = std::min<std::uint64_t>(steps, forwardCapacity);
  }

  window.reserve(static_cast<size_t>(steps));
  const std::uint64_t offsetOfCurrent = static_cast<std::uint64_t>(m_currentTime - m_minTime);
  for (std::uint64_t i = 1; i <= steps; ++i) {
    if (m_prefetchConfig.wrapAround) {
      window.push_back(static_cast<uint32_t>(m_minTime + ((offsetOfCurrent + i) % span)));
    } else {
      if (offsetOfCurrent + i >= span) {
        break;
      }
      window.push_back(static_cast<uint32_t>(m_minTime + offsetOfCurrent + i));
    }
  }
  return window;
}

std::vector<uint32_t>
TimeSeriesLoader::diskWarmWindowLocked() const
{
  std::vector<uint32_t> window;
  const CacheConfig config = m_cache.getConfig();
  if (!m_haveSeries || !config.enableDisk || m_maxTime <= m_minTime || m_bytesPerFrame == 0) {
    return window;
  }

  const std::uint64_t span = static_cast<std::uint64_t>(m_maxTime - m_minTime) + 1;
  const std::uint64_t forwardSteps = prefetchWindowLocked().size();
  if (forwardSteps + 1 >= span) {
    // The memory window already covers the series; there is nothing left to warm.
    return window;
  }

  const std::uint64_t diskBudgetFrames = config.maxDiskBytes / m_bytesPerFrame;
  // Saturating, for the same reason as the memory window. The current step and
  // every memory-window step is written to disk too -- storeImage queues a disk
  // write for everything it caches -- so the warm set is what remains of the disk
  // budget after them.
  //
  // Clamping here is what stops the warm pass evicting its own earlier writes and
  // then reporting steps as cached whose files are gone.
  const std::uint64_t diskReserved = 1ULL + forwardSteps;
  const std::uint64_t diskCapacity = diskBudgetFrames > diskReserved ? diskBudgetFrames - diskReserved : 0;
  const std::uint64_t steps = std::min<std::uint64_t>(diskCapacity, span - 1 - forwardSteps);

  window.reserve(static_cast<size_t>(steps));
  const std::uint64_t offsetOfCurrent = static_cast<std::uint64_t>(m_currentTime - m_minTime);
  for (std::uint64_t i = forwardSteps + 1; i <= forwardSteps + steps; ++i) {
    if (m_prefetchConfig.wrapAround) {
      window.push_back(static_cast<uint32_t>(m_minTime + ((offsetOfCurrent + i) % span)));
    } else {
      if (offsetOfCurrent + i >= span) {
        break;
      }
      window.push_back(static_cast<uint32_t>(m_minTime + offsetOfCurrent + i));
    }
  }
  return window;
}

bool
TimeSeriesLoader::nextPrefetchTimeLocked(PrefetchPermission permission, uint32_t& time, bool& warmOnly) const
{
  warmOnly = false;
  if (!m_haveSeries || permission == PrefetchPermission::None) {
    return false;
  }
  // Priority 1: the memory window, nearest first. A DiskCached frame here is
  // worth pulling back into RAM -- it is about to be displayed and a disk read
  // is much cheaper than going to the source.
  //
  // Skipped entirely under WarmOnly: the RAM budget is already full of frames we
  // want, so pulling another into memory would evict one of them.
  if (permission == PrefetchPermission::Any) {
    for (uint32_t t : prefetchWindowLocked()) {
      const TimepointStatus s = m_status[static_cast<size_t>(t - m_minTime)];
      if (s != TimepointStatus::NotCached && s != TimepointStatus::DiskCached) {
        continue;
      }
      if (m_inFlight.find(t) != m_inFlight.end()) {
        continue;
      }
      time = t;
      warmOnly = false;
      return true;
    }
  }

  // Priority 2: warm the disk set beyond the memory window.
  //
  // Bounded by diskWarmWindowLocked rather than sweeping the whole series. An
  // unbounded sweep on a series larger than the disk budget evicts its own earlier
  // writes as it goes, and the eviction marks those steps uncached again -- so it
  // never terminates. Clamped to what the disk holds, each step is fetched at most
  // once and prefetch goes idle.
  //
  // Only NotCached steps qualify: a DiskCached one is already done, and
  // re-fetching it is exactly the endless loop this avoids.
  for (uint32_t t : diskWarmWindowLocked()) {
    if (m_status[static_cast<size_t>(t - m_minTime)] != TimepointStatus::NotCached) {
      continue;
    }
    if (m_inFlight.find(t) != m_inFlight.end()) {
      continue;
    }
    if (m_warmRefused.count(t)) {
      continue;
    }
    time = t;
    // Disk-only: this volume must not enter the memory tier, or warming the
    // series would evict the near time steps and paint the whole timeline as
    // in-memory as the warm pass sweeps along it.
    warmOnly = true;
    return true;
  }
  return false;
}

void
TimeSeriesLoader::notifyStatusChanges(const std::vector<std::pair<uint32_t, TimepointStatus>>& changes)
{
  if (changes.empty()) {
    return;
  }
  std::vector<ITimeSeriesLoaderObserver*> observers;
  {
    std::scoped_lock lock(m_mutex);
    observers = m_observers;
  }
  for (ITimeSeriesLoaderObserver* observer : observers) {
    for (const auto& change : changes) {
      observer->onStatusChanged(change.first, change.second);
    }
  }
}

void
TimeSeriesLoader::notifyPrefetchIdle()
{
  std::vector<ITimeSeriesLoaderObserver*> observers;
  {
    std::scoped_lock lock(m_mutex);
    observers = m_observers;
  }
  for (ITimeSeriesLoaderObserver* observer : observers) {
    observer->onPrefetchIdle();
  }
}

void
TimeSeriesLoader::threadMain()
{
  std::unique_lock<std::mutex> lock(m_mutex);

  while (!m_stop) {
    // ---- Interactive request wins over everything else. ----
    if (m_interactivePending) {
      const uint32_t time = m_interactiveTime;
      const uint64_t seq = m_interactiveSeq;
      const uint64_t generation = m_seriesGeneration;
      m_interactivePending = false;

      LoadSpec spec = specForLocked(time);
      std::shared_ptr<IFileReader> reader = m_reader;

      // Adopt an in-flight prefetch for this timepoint instead of starting a
      // duplicate load -- unless it has already been cancelled. A prefetch
      // cancelled by an earlier scrub stays in m_inFlight until it is reaped, so
      // scrubbing back onto it would otherwise adopt a doomed request and report
      // a spurious failure instead of loading the frame.
      std::shared_ptr<LoadRequest> request;
      bool adopted = false;
      auto inFlightIt = m_inFlight.find(time);
      if (inFlightIt != m_inFlight.end()) {
        if (!inFlightIt->second->isCancelled()) {
          request = inFlightIt->second;
          adopted = true;
        }
        m_inFlight.erase(inFlightIt);
        // Released here whether or not we adopted it, since the entry is gone
        // from m_inFlight either way.
        m_inFlightBytes = m_inFlightBytes > m_bytesPerFrame ? m_inFlightBytes - m_bytesPerFrame : 0;
      }

      std::vector<std::pair<uint32_t, TimepointStatus>> changes;
      const bool alreadyCached = m_status[static_cast<size_t>(time - m_minTime)] == TimepointStatus::RamCached;
      if (!alreadyCached) {
        setStatusLocked(time, TimepointStatus::Loading, changes);
      }

      LoadSpec previousPinned = m_pinnedSpec;
      const bool hadPinned = m_havePinned;
      m_pinnedSpec = spec;
      m_havePinned = true;

      lock.unlock();
      notifyStatusChanges(changes);

      // Pin the new timepoint before releasing the old one, so prefetch can
      // never evict what is about to be displayed.
      m_cache.pin(spec);
      if (hadPinned) {
        m_cache.unpin(previousPinned);
      }

      std::shared_ptr<ImageXYZC> image;
      bool preempted = false;

      if (!adopted) {
        image = m_cache.findImage(spec);
      }

      if (!image && !adopted && reader) {
        request = reader->submitLoad(spec);
      }

      if (!image && request) {
        // Wait, but stay responsive: a newer scrub cancels this load.
        lock.lock();
        while (!request->isReady()) {
          if (m_stop || (m_interactivePending && m_interactiveSeq != seq)) {
            request->cancel();
            preempted = true;
            break;
          }
          m_wake.wait_for(lock, kPollInterval);
        }
        lock.unlock();

        if (!preempted) {
          image = request->take();
          if (image) {
            m_cache.storeImage(spec, image);
          }
        }
      }

      changes.clear();
      lock.lock();

      const bool stale = generation != m_seriesGeneration;
      if (image && m_bytesPerFrame == 0) {
        m_bytesPerFrame = imageBytes(*image);
      }
      if (!stale) {
        if (image) {
          setStatusLocked(time, TimepointStatus::RamCached, changes);
        } else if (preempted) {
          setStatusLocked(time, TimepointStatus::NotCached, changes);
        } else {
          setStatusLocked(time, TimepointStatus::Failed, changes);
        }
      }
      m_prefetchIdleReported = false;

      lock.unlock();
      notifyStatusChanges(changes);

      if (!stale && !preempted) {
        std::vector<ITimeSeriesLoaderObserver*> observers;
        {
          std::scoped_lock guard(m_mutex);
          observers = m_observers;
        }
        for (ITimeSeriesLoaderObserver* observer : observers) {
          if (image) {
            observer->onInteractiveLoadComplete(time, image, seq);
          } else {
            observer->onInteractiveLoadFailed(time, seq);
          }
        }
      }

      lock.lock();
      continue;
    }

    // ---- Reap finished prefetches. ----
    bool reaped = false;
    for (auto it = m_inFlight.begin(); it != m_inFlight.end();) {
      if (!it->second->isReady()) {
        ++it;
        continue;
      }
      const uint32_t time = it->first;
      std::shared_ptr<LoadRequest> request = it->second;
      const bool warmOnly = m_warmOnly.erase(time) > 0;
      it = m_inFlight.erase(it);
      m_inFlightBytes = m_inFlightBytes > m_bytesPerFrame ? m_inFlightBytes - m_bytesPerFrame : 0;
      reaped = true;

      LoadSpec spec = specForLocked(time);
      const uint64_t generation = m_seriesGeneration;

      lock.unlock();
      std::shared_ptr<ImageXYZC> image = request->take();
      const bool loaded = image != nullptr;
      std::uint64_t loadedBytes = 0;
      bool warmRefused = false;
      if (loaded) {
        loadedBytes = imageBytes(*image);
        if (warmOnly) {
          // A refusal means this will not fit in the disk tier. Record it so the
          // warm pass skips the step and prefetch goes idle, rather than fetching
          // it again on every pass forever.
          warmRefused = !m_cache.storeImageOnDiskOnly(spec, image);
        } else {
          m_cache.storeImage(spec, image);
        }
      }
      // Released before re-locking. For a warm-only fetch this is the last
      // reference, so the volume is freed immediately rather than lingering in
      // memory the cache never took ownership of.
      image.reset();
      lock.lock();

      std::vector<std::pair<uint32_t, TimepointStatus>> changes;
      if (generation == m_seriesGeneration) {
        if (loaded) {
          if (m_bytesPerFrame == 0) {
            m_bytesPerFrame = loadedBytes;
          }
          if (warmRefused) {
            m_warmRefused.insert(time);
            setStatusLocked(time, TimepointStatus::NotCached, changes);
          } else {
            // A warm-only fetch deliberately never entered the memory tier, so it
            // is disk-resident, not resident.
            setStatusLocked(time, warmOnly ? TimepointStatus::DiskCached : TimepointStatus::RamCached, changes);
          }
        } else if (request->isCancelled()) {
          setStatusLocked(time, TimepointStatus::NotCached, changes);
        } else {
          setStatusLocked(time, TimepointStatus::Failed, changes);
        }
      }
      lock.unlock();
      notifyStatusChanges(changes);
      lock.lock();
      // The map changed while unlocked, so restart the sweep.
      it = m_inFlight.begin();
    }
    if (reaped) {
      continue;
    }

    // ---- Start a prefetch if there is room and something to fetch. ----
    uint32_t prefetchTime = 0;
    bool prefetchWarmOnly = false;
    if (nextPrefetchTimeLocked(prefetchPermissionLocked(), prefetchTime, prefetchWarmOnly)) {
      LoadSpec spec = specForLocked(prefetchTime);
      std::shared_ptr<IFileReader> reader = m_reader;

      std::vector<std::pair<uint32_t, TimepointStatus>> changes;
      setStatusLocked(prefetchTime, TimepointStatus::Queued, changes);

      lock.unlock();
      notifyStatusChanges(changes);

      // Consult the whole cache, memory AND disk, before fetching from source.
      //
      // This used to probe containsInMemory, which only sees the memory tier, so
      // prefetch went straight to the reader for anything not in RAM and never
      // read back a time step it had already written to the disk cache. Every
      // session, and every pass once frames aged out of memory, re-fetched from
      // the original source.
      //
      // Which probe depends on where the step is wanted:
      //
      // - Inside the memory window, findImage is right. It checks RAM then disk,
      //   promotes a disk hit into memory, and counts it as a disk hit -- exactly
      //   what we want, since the step is about to be displayed.
      //
      // - For a warm-only step it would be wrong. findImage promotes, so warming a
      //   series already on disk would drag the whole timeline through RAM,
      //   evicting the near steps storeImageOnDiskOnly exists to protect. Probe
      //   disk residency instead: no promotion, no LRU touch, no hit counted.
      bool resident = false;
      bool alreadyWarm = false;
      if (prefetchWarmOnly) {
        alreadyWarm = m_cache.containsOnDisk(spec);
      } else {
        std::shared_ptr<ImageXYZC> cached = m_cache.findImage(spec);
        resident = cached != nullptr;
        // Release before re-locking so the volume is not held any longer than the
        // cache already holds it.
        cached.reset();
      }
      std::shared_ptr<LoadRequest> request;
      if (!resident && !alreadyWarm && reader) {
        request = reader->submitLoad(spec);
      }
      lock.lock();

      changes.clear();
      if (resident) {
        setStatusLocked(prefetchTime, TimepointStatus::RamCached, changes);
      } else if (alreadyWarm) {
        setStatusLocked(prefetchTime, TimepointStatus::DiskCached, changes);
      } else if (request) {
        m_inFlight.emplace(prefetchTime, request);
        if (prefetchWarmOnly) {
          m_warmOnly.insert(prefetchTime);
        }
        m_inFlightBytes += m_bytesPerFrame;
        m_peakInFlightBytes = std::max(m_peakInFlightBytes, m_inFlightBytes);
        setStatusLocked(prefetchTime, TimepointStatus::Loading, changes);
      } else {
        setStatusLocked(prefetchTime, TimepointStatus::Failed, changes);
      }

      lock.unlock();
      notifyStatusChanges(changes);
      lock.lock();
      continue;
    }

    // ---- Nothing to do. ----
    if (!m_inFlight.empty()) {
      // Waiting on in-flight prefetches; poll for their completion.
      m_wake.wait_for(lock, kPollInterval);
      continue;
    }

    const bool reportIdle = m_haveSeries && !m_prefetchIdleReported;
    if (reportIdle) {
      m_prefetchIdleReported = true;
      lock.unlock();
      notifyPrefetchIdle();
      lock.lock();
      continue;
    }

    // The predicate has to include "prefetch became possible", not just new
    // interactive work. Otherwise enabling prefetch, raising the depth, or an
    // eviction freeing room would all leave the loader parked here until the
    // next scrub happened to wake it.
    m_wake.wait(lock, [this] {
      if (m_stop || m_interactivePending || !m_inFlight.empty()) {
        return true;
      }
      uint32_t time = 0;
      bool warmOnly = false;
      return nextPrefetchTimeLocked(prefetchPermissionLocked(), time, warmOnly);
    });
  }

  // Draining: cancel and reap anything still outstanding so no worker outlives
  // this object.
  for (auto& entry : m_inFlight) {
    entry.second->cancel();
  }
  auto outstanding = std::move(m_inFlight);
  m_inFlight.clear();
  lock.unlock();
  outstanding.clear();
}
