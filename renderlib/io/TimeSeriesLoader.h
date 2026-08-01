#pragma once

#include "CacheManager.h"
#include "IFileReader.h"

#include <condition_variable>
#include <cstdint>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class ImageXYZC;
class LoadRequest;

// What we know about one timepoint of the current series. Drives the cache
// indicator on the time slider, which reads this vector rather than querying
// CacheManager per repaint (building a CacheKey stats the file, so polling the
// cache while painting would be a stat storm).
enum class TimepointStatus
{
  NotCached,
  Queued,
  Loading,
  RamCached,
  // Not in memory, but present in the disk cache, so reloading it is cheap and
  // local. Prefetch treats this as "done" for time steps outside the memory
  // window -- without that distinction an evicted frame looks identical to one
  // never fetched, and prefetch pulls it straight back in, evicting another, for
  // ever.
  DiskCached,
  Failed,
};

// Callbacks from TimeSeriesLoader.
//
// IMPORTANT: every method is invoked on the loader thread, not the caller's
// thread. Implementations must marshal to their own thread before touching
// anything thread-affine. The Qt side does this with a small QObject shim that
// re-emits each callback as a queued signal.
class ITimeSeriesLoaderObserver
{
public:
  virtual ~ITimeSeriesLoaderObserver() = default;

  // An interactive (user-driven) load finished. `seq` is the sequence number
  // returned by requestTime; the observer should discard a completion whose seq
  // is older than the newest request it has issued, since the user has moved on.
  virtual void onInteractiveLoadComplete(uint32_t time, std::shared_ptr<ImageXYZC> image, uint64_t seq) = 0;
  virtual void onInteractiveLoadFailed(uint32_t time, uint64_t seq) = 0;

  // A timepoint's cache status changed. Used to repaint the slider indicator.
  virtual void onStatusChanged(uint32_t time, TimepointStatus status) = 0;

  // Prefetch has nothing left to do: either the window is full, the cache is
  // full, or prefetch is disabled.
  virtual void onPrefetchIdle() = 0;
};

// Loads timepoints of a series off the main thread and prefetches ahead of the
// playhead.
//
// One loader thread, reusing one IFileReader for the whole series (which is what
// lets the Zarr and CZI readers keep their parsed metadata and open handles).
// Concurrency within a load comes from the reader, via
// IFileReader::maxConcurrentLoads, not from more loader threads.
//
// Interactive requests always take priority over prefetch. A request for a
// timepoint that is already queued or in flight is never duplicated: it is
// promoted to interactive instead.
class TimeSeriesLoader : public CacheManager::IEvictionObserver
{
public:
  struct PrefetchConfig
  {
    // Asynchronously fill memory AND disk with frames from the series. With this
    // off there is no background prefetching at all, but on-demand loads driven by
    // the time slider are still cached in both tiers: `enabled` is read only by
    // the prefetch gate, never by the interactive path.
    bool enabled = true;
    // Whether the prefetch window wraps past the end of the series back to the
    // start. Must track the playback loop setting: with looping on, the frame
    // after the last one is the first one, and if prefetch does not know that it
    // never fetches it back after the forward pass evicted it -- so looping
    // playback stalls on the final frame waiting for a frame nobody will load.
    bool wrapAround = false;
    // Slots reserved BEHIND the playhead so a small backward scrub stays instant.
    // These are never prefetched -- prefetch is strictly forward-only. The
    // reservation only shrinks the forward window, leaving room that LRU fills
    // with the frames just displayed.
    //
    // Not surfaced in the UI. A config field rather than a constant so tests can
    // verify that a value of 0 works, which is the all-forward shape.
    uint32_t historyMargin = 4;
  };

  // In-flight loads own full destination buffers that CacheManager knows nothing
  // about, so they are tracked here and charged against the cache budget as
  // reserved headroom. Reported in the GUI statistics panel.
  struct MemoryStats
  {
    std::uint64_t inFlightBytes = 0;
    uint32_t inFlightCount = 0;
    std::uint64_t peakInFlightBytes = 0;
  };

  // The cache is injected rather than reached for via CacheManager::instance()
  // so tests can supply an isolated, throwaway cache instead of sharing the
  // process-wide singleton.
  explicit TimeSeriesLoader(CacheManager& cache = CacheManager::instance());
  ~TimeSeriesLoader() override;

  TimeSeriesLoader(const TimeSeriesLoader&) = delete;
  TimeSeriesLoader& operator=(const TimeSeriesLoader&) = delete;

  // Point the loader at a series. Cancels everything outstanding for any
  // previous series and resets the status vector. `base` supplies everything
  // except the time index (path, scene, subpath, channels, ROI).
  void setSeries(const LoadSpec& base,
                 std::shared_ptr<IFileReader> reader,
                 uint32_t minTime,
                 uint32_t maxTime,
                 uint32_t currentTime);

  void setPrefetchConfig(const PrefetchConfig& config);
  PrefetchConfig prefetchConfig() const;

  void addObserver(ITimeSeriesLoaderObserver* observer);
  void removeObserver(ITimeSeriesLoaderObserver* observer);

  // Ask for a timepoint on the user's behalf. Returns a monotonically
  // increasing sequence number so the caller can ignore stale completions.
  // Supersedes any previous interactive request, and re-aims prefetch at `time`.
  //
  // If the timepoint is already in the memory cache this still goes through the
  // loader thread, so completion always arrives via the observer and callers
  // have exactly one code path.
  uint64_t requestTime(uint32_t time);

  // Drop queued prefetches and cancel in-flight ones. Already-cached timepoints
  // are kept. Does not disable prefetch; the next requestTime will start it
  // again. To stop it for good, clear PrefetchConfig::enabled.
  void cancelPrefetch();

  TimepointStatus status(uint32_t time) const;
  // Fills `out` with the status of [from, to] clamped to the series range.
  void statusRange(uint32_t from, uint32_t to, std::vector<TimepointStatus>& out) const;

  MemoryStats memoryStats() const;

  // CacheManager::IEvictionObserver. Called on whichever thread evicted.
  void onEvictedFromMemory(const CacheKey& key) override;
  void onEvictedFromDisk(const std::string& diskCacheId) override;

private:
  void threadMain();
  // All of these require m_mutex to be held.
  void setStatusLocked(uint32_t time,
                       TimepointStatus status,
                       std::vector<std::pair<uint32_t, TimepointStatus>>& changes);
  void cancelPrefetchLocked();

  // What kind of prefetch may start right now.
  //
  // A warm-only fetch consumes no RAM -- it goes straight to the disk tier via
  // storeImageOnDiskOnly -- so it must not be blocked by the RAM throttle. Once
  // the memory window fills the RAM budget that throttle latches permanently, and
  // a single bool here would stop disk warming with the rest of the series left in
  // neither tier.
  enum class PrefetchPermission
  {
    None,     // reader busy, prefetch off, stopping, or an interactive request pending
    WarmOnly, // RAM throttle engaged: only disk-warming fetches may start
    Any,
  };
  PrefetchPermission prefetchPermissionLocked() const;
  // The timepoints the current policy wants resident, in priority order,
  // starting just after the current one. Wraps past the end when wrapAround is
  // set. Excludes the current timepoint itself.
  std::vector<uint32_t> prefetchWindowLocked() const;
  // Next timepoint worth prefetching, or false if there is nothing to do.
  // `warmOnly` reports that the chosen time step is outside the memory window
  // and is being fetched purely to populate the disk cache, so its volume must
  // not be inserted into the memory tier. `permission` decides whether the memory
  // window is eligible at all; WarmOnly considers the disk warm set only.
  bool nextPrefetchTimeLocked(PrefetchPermission permission, uint32_t& time, bool& warmOnly) const;
  LoadSpec specForLocked(uint32_t time) const;

  void notifyStatusChanges(const std::vector<std::pair<uint32_t, TimepointStatus>>& changes);
  void notifyPrefetchIdle();

  CacheManager& m_cache;

  mutable std::mutex m_mutex;
  std::condition_variable m_wake;
  std::thread m_thread;
  bool m_stop = false;

  std::vector<ITimeSeriesLoaderObserver*> m_observers;

  LoadSpec m_baseSpec;
  std::shared_ptr<IFileReader> m_reader;
  uint32_t m_minTime = 0;
  uint32_t m_maxTime = 0;
  uint32_t m_currentTime = 0;
  bool m_haveSeries = false;
  // Bumped on setSeries so in-flight work from a previous series is discarded
  // rather than misattributed.
  uint64_t m_seriesGeneration = 0;

  std::vector<TimepointStatus> m_status;

  PrefetchConfig m_prefetchConfig;

  // Pending interactive request, if any, not yet picked up by the loader thread.
  bool m_interactivePending = false;
  uint32_t m_interactiveTime = 0;
  uint64_t m_interactiveSeq = 0;
  uint64_t m_nextSeq = 1;

  // The timepoint currently pinned in the cache, so it can be unpinned when the
  // playhead moves on.
  bool m_havePinned = false;
  LoadSpec m_pinnedSpec;

  // Prefetch loads currently in flight, keyed by timepoint.
  std::map<uint32_t, std::shared_ptr<LoadRequest>> m_inFlight;
  // In-flight time steps being fetched only to warm the disk cache.
  std::set<uint32_t> m_warmOnly;
  // Disk-cache id -> time step, for every step of the current series. Built once
  // in setSeries so onEvictedFromDisk is an O(1) lookup that never stats a file:
  // eviction arrives in bursts while warming, and building ids on that path would
  // mean a makeKey (and therefore a file stat) per evicted entry.
  std::unordered_map<std::string, uint32_t> m_diskIdToTime;
  std::uint64_t m_bytesPerFrame = 0;
  std::uint64_t m_inFlightBytes = 0;
  std::uint64_t m_peakInFlightBytes = 0;
  bool m_prefetchIdleReported = false;
};
