# AGAVE Time-Series Prefetch & Playback — Implementation Plan

Branch: `feature/timeseries-loading`

## 1. Agreed design decisions

| Decision | Choice |
|---|---|
| Load concurrency | Single loader thread. Reader reuse across all timepoints of a series. Concurrency comes from the reader (tensorstore futures for Zarr; an internal worker pool for blocking formats). |
| Reader API | Option C — new future/handle-based `IFileReader` surface so several *timepoints* can be in flight. |
| Formats with real concurrency | Zarr (native, via tensorstore futures) + CZI (via worker pool on a shared thread-safe `ICZIReader`). TIFF/CCP4/ImageSequence get it free via the same adapter. |
| Prefetch direction | Forward only from the current time. |
| Playback stall behavior | Both modes, user-selectable: "show every frame" (stall) vs real-time (drop frames). |
| Playback controls | Play/pause toggle + stop, FPS control, loop toggle. **No** reverse/ping-pong (consistent with forward-only prefetch). |
| Slider cache indicator | 2 states (cached / not cached) by default; 5-state mode (not cached / queued / loading / on-disk / in-RAM) behind a debug setting. |
| Disk cache on prefetch | Prefetch **reads** from the disk tier (already true via `loadAndCache`). Disk **writes** move off the load hot path into a low-priority async queue. |
| Memory pressure | Throttle: stop queueing when the RAM budget is nearly full. Pin the currently displayed frame so it can never be evicted. |
| Scrub cancel policy | Auto-drop queued + cooperatively cancel in-flight; keep already-cached frames. Plus an explicit "Cancel prefetch" control. |
| In-flight dedup | Required. Registry keyed on full `CacheKey`; a scrub onto an already-queued timepoint **promotes** its priority rather than enqueueing a duplicate. |
| New commands | **None.** `SetTimeCommand` is rebuilt on the shared loader (which also fixes its missing reader reuse). No `commandbuffer.py` / `.ts` changes. |
| RenderDialog | No prefetch logic. It already calls `loadAndCache`, so it consumes GUI-warmed cache entries for free. |
| Settings placement | LoadDialog: a single "prefetch time series" checkbox (semantics: warm the whole series). Cache dock: depth / fill-cache / debug view, persisted. Timeline dock: live playback + cancel controls. |
| **Code placement** | All logic in `renderlib` (`renderlib/io` for load-related). `agave_app` gets Qt only: widgets, painting, the clock timer, and a thin marshalling shim. No policy or state machines in Qt land. |

## 2. Current state (verified)

- `FileReader::loadAndCache` (`renderlib/io/FileReader.cpp:66-98`) is the single choke point every load path funnels through, and it already checks RAM then disk before a true fetch (`CacheManager.cpp:327/355/366`).
- `CacheManager` is mutex-protected and `CacheKey` already includes `time`, so per-timepoint caching works today.
- `renderlib/threading.h:41` contains a complete, **entirely unused** `Tasks` thread pool (`queue`/`start`/`abort`/`cancel_pending`/`finish`). Qt-free.
- `QTimelineWidget::OnTimeChanged` (`TimelineDockWidget.cpp:72-115`) blocks the main thread with only a wait cursor, and has an open TODO for load-failure slider desync (`:91`).
- `SetTimeCommand::execute` (`command.cpp:558-621`) duplicates that logic and does **not** reuse the reader (`:574`).
- `libCZI::ICZIReader` is documented thread-safe for concurrent calls (`libCZI.h:745`); `IStream` implementations must support concurrent `Read` (`libCZI.h:236`).
- `QIntSlider` holds `QSlider m_slider` **by value** (`Controls.h:246`) — relevant to the slider painting work.
- `CacheManager::m_currentRamBytes` exists but is private with no accessor (`CacheManager.h:126`).
- `CacheManager::makeKey` stats the file for mtime/size — so `findImage` is **not** cheap enough to call per repaint.

### Known per-timepoint waste to fix along the way
- **Zarr:** `loadFromFile` calls `loadMultiscaleDims` on *every* load (`FileReaderZarr.cpp:476`), which re-`tensorstore::Open`s every multiscale level (`:400-404`) — N opens per timestep.
- **CZI:** `loadFromFile` constructs a `ScopedCziReader` per call, reopening the file and re-parsing the entire metadata XML through pugixml (`FileReaderCzi.cpp:309-315`).
- Both readers load channels strictly serially and throw away all available concurrency.

## 3. Architecture

**Placement rule (user requirement):** all logic lives in `renderlib` — `renderlib/io` for anything
load-related. `agave_app` gets *only* Qt: widgets, painting, the clock timer, and a thin signal-marshalling
shim. Nothing in `agave_app` should contain policy, state machines, or scene mutation.

```
        ┌──────────────────── agave_app (Qt ONLY — widgets + marshalling) ────────────────────┐
        │  QTimelineWidget          buttons, wires clock ticks -> player, reads status vector  │
        │  TimeSliderWithCacheStatus / CacheStatusSlider     paintEvent only                  │
        │  QTimer (Qt::PreciseTimer)                         emits ticks, holds no state       │
        │  TimeSeriesLoaderBridge : QObject, ITimeSeriesLoaderObserver                        │
        │      one-line-per-callback QMetaObject::invokeMethod -> queued signal. No logic.     │
        │  CacheSettingsWidget (+ prefetch group), LoadDialog (+ checkbox)                    │
        │  CacheSettings           JSON persistence only (needs QStandardPaths)                │
        └───────────────────────────────────┬────────────────────────────────────────────────┘
                                            │ (Qt-free boundary)
        ┌───────────────────────────────────▼────────────────────────────────────────────────┐
        │  renderlib/io/TimeSeriesPlayer                                                     │
        │    - playback state machine: playing/paused/stopped, origin frame, loop wrap        │
        │    - advance(nowMs) -> next timepoint or "hold"; stall vs drop-frames decision      │
        │    - fully Qt-free and clock-injected, so it is directly unit-testable              │
        ├────────────────────────────────────────────────────────────────────────────────────┤
        │  renderlib/io/TimeSeriesLoader                                                     │
        │    - one loader thread                                                              │
        │    - priority queue: Interactive > Prefetch                                         │
        │    - in-flight registry (CacheKey -> LoadRequest)  [dedup + promote]                │
        │    - forward-only prefetch policy, depth or fill-cache                              │
        │    - throttle on RAM budget; pin current frame                                      │
        │    - per-timepoint status vector (drives slider, no stat storm)                     │
        │    - ITimeSeriesLoaderObserver callbacks                                            │
        ├────────────────────────────────────────────────────────────────────────────────────┤
        │  renderlib/io/applyVolumeToScene(Scene*, image, RenderSettings*)                    │
        │    - LUT/histogram remap + volume swap + dirty flags, in ONE place                  │
        │    - replaces the duplicated blocks in TimelineDockWidget.cpp:97-113 and            │
        │      command.cpp:599-609                                                            │
        └───────┬───────────────────────────────────────┬────────────────────────────────────┘
                │                                       │
 ┌──────────────▼─────────────┐          ┌──────────────▼──────────────┐
 │ FileReader::loadAndCache   │          │ CacheManager                │
 │  (unchanged choke point)   │─────────▶│  + getRamBytesUsed()        │
 └──────────────┬─────────────┘          │  + pin()/unpin()            │
                │                        │  + eviction observer        │
 ┌──────────────▼─────────────┐          │  + async disk-write queue   │
 │ IFileReader::submitLoad()  │          └─────────────────────────────┘
 │   -> LoadRequest           │
 ├────────────────────────────┤
 │ FileReaderZarr  (native)   │  batched tensorstore futures, 0 extra threads
 │ BlockingFileReader (base)  │  Tasks pool; used by CZI, TIFF, CCP4, ImgSeq
 └────────────────────────────┘
```

### 3.1 New async reader API — `renderlib/io/LoadRequest.h`

```cpp
// A volume load in progress. Cancellable, awaitable, reports coarse progress.
// Created by IFileReader::submitLoad(). Safe to query from any thread.
class LoadRequest
{
public:
  virtual ~LoadRequest() = default;

  // True once the load has finished, failed, or been cancelled.
  virtual bool isReady() const = 0;

  // Best-effort cooperative cancel. Returns immediately; the request may
  // still complete if it was already past the point of no return.
  virtual void cancel() = 0;
  virtual bool isCancelled() const = 0;

  // 0..1. Coarse — typically channels-completed / channels-total.
  virtual float progress() const = 0;

  // Blocks until isReady(). Returns null on failure or cancellation.
  virtual std::shared_ptr<ImageXYZC> take() = 0;

  const LoadSpec& spec() const { return m_spec; }

protected:
  LoadSpec m_spec;
};
```

`IFileReader` gains exactly two virtuals; `loadFromFile` becomes a non-virtual convenience so **no existing call site changes**:

```cpp
class IFileReader
{
public:
  // ...existing loadNumScenes / loadDimensions / loadMultiscaleDims unchanged...

  // Begin a load and return immediately.
  virtual std::shared_ptr<LoadRequest> submitLoad(const LoadSpec& loadSpec) = 0;

  // How many loads this reader can usefully have in flight at once.
  // 1 means no concurrency; the caller must not exceed this.
  virtual uint32_t maxConcurrentLoads() const { return 1; }

  // Convenience: submit and wait. Preserves the old blocking signature.
  std::shared_ptr<ImageXYZC> loadFromFile(const LoadSpec& loadSpec)
  {
    auto req = submitLoad(loadSpec);
    return req ? req->take() : nullptr;
  }
};
```

`renderlib/io/BlockingFileReader.{h,cpp}` — new base implementing `submitLoad` on top of a blocking
`loadVolumeBlocking(const LoadSpec&, const CancelToken&)` that subclasses override. Uses
`threading.h`'s `Tasks` pool. `CancelToken` is a shared `std::atomic<bool>` the subclass polls
between planes/channels. CZI, TIFF, CCP4 and ImageSequence derive from it; each renames its current
`loadFromFile` body to `loadVolumeBlocking` and adds cancel checks in its inner loops.

### 3.2 `renderlib/io/TimeSeriesLoader.{h,cpp}` (new)

```cpp
enum class TimepointStatus { NotCached, Queued, Loading, DiskCached, RamCached, Failed };

class ITimeSeriesLoaderObserver
{
public:
  virtual ~ITimeSeriesLoaderObserver() = default;
  // All of these are invoked on the loader thread. Implementors must marshal.
  virtual void onInteractiveLoadComplete(uint32_t time, std::shared_ptr<ImageXYZC>, uint64_t seq) = 0;
  virtual void onInteractiveLoadFailed(uint32_t time, uint64_t seq) = 0;
  virtual void onStatusChanged(uint32_t time, TimepointStatus) = 0;
  virtual void onPrefetchIdle() = 0;
};

class TimeSeriesLoader
{
public:
  struct PrefetchConfig {
    bool enabled = true;
    uint32_t depth = 4;        // steps ahead
    bool fillCache = false;    // ignore depth, run until the RAM budget throttles
  };

  void setSeries(const LoadSpec& base, std::shared_ptr<IFileReader>, uint32_t minTime, uint32_t maxTime);
  void setPrefetchConfig(const PrefetchConfig&);
  void addObserver(ITimeSeriesLoaderObserver*);

  // Interactive: highest priority. Returns a sequence number so the caller can
  // discard stale completions. Promotes an already-queued/in-flight request
  // rather than duplicating it.
  uint64_t requestTime(uint32_t time);

  void cancelPrefetch();                 // drop queued + cancel in-flight prefetches
  TimepointStatus status(uint32_t time) const;
  void statusRange(uint32_t from, uint32_t to, std::vector<TimepointStatus>& out) const;
};
```

Loader-thread main loop, per iteration:
1. Drain the interactive slot first (only the newest interactive request matters).
2. Reap finished `LoadRequest`s; update status vector; fire observers.
3. If prefetch enabled and not throttled and in-flight count `< reader->maxConcurrentLoads()`:
   pick the lowest `t > current` in `[current+1, current+depth]` (or to `maxTime` if `fillCache`)
   whose status is `NotCached`, and `submitLoad` it.
4. Throttle check: `CacheManager::getRamBytesUsed() + inFlightBytes + nextFrameBytes > maxRamBytes * headroom`
   → don't queue.

**In-flight bytes matter.** Each in-flight `LoadRequest` owns a full destination buffer (`nch × channelSize`)
plus scratch, and that memory is *not* tracked by `CacheManager`. `maxConcurrentLoads` × volume size is
transient overhead that must be counted against the budget, or a 4 GB budget with 512 MB frames and
8 in flight blows past it. This is a correctness requirement, not an optimization.

`TimeSeriesLoader` therefore exposes it, and phase 0 surfaces it in the Statistics dock:

```cpp
struct LoaderMemoryStats {
  std::uint64_t inFlightBytes;    // destination + scratch buffers currently allocated
  uint32_t      inFlightCount;
  std::uint64_t peakInFlightBytes;
};
LoaderMemoryStats memoryStats() const;
```

A **"Memory"** group in the Statistics dock (the group name is already referenced by
`StatisticsWidget.cpp:135-137`) reports, via `CStatus::SetStatisticChanged`:

| Statistic | Source |
|---|---|
| Cache RAM used / limit | `CacheManager::getRamBytesUsed()` / `getConfig().maxRamBytes` |
| Cache disk used / limit | `m_currentDiskBytes` / `maxDiskBytes` (needs an accessor) |
| In-flight load buffers | `TimeSeriesLoader::memoryStats().inFlightBytes` (+ peak) |
| Pending disk writes | depth of the async disk-write queue (§3.3) |
| GPU volume texture | `VolumeTextureVk::gpuBytes()` — already exists and is already correct (`upload()` calls `release()` first, resetting the counter) |
| Cache hits / misses | `CacheManager::getStats()` — exists today with **no consumer at all** |

This makes the whole memory picture visible in one place, which is the only practical way to tell
"prefetch is throttling correctly" from "prefetch is quietly double-counting".

**One budget, not two.** Prefetched frames count against the same `maxRamBytes` as user-visited frames —
there is no separate prefetch budget. They enter the cache via the same `storeImage` path, and once
complete a prefetched frame is indistinguishable from a visited one and obeys the same LRU (so scrubbing
backward onto an earlier prefetched frame is a hit). The only special case is the pin on the current frame.

In-flight buffers are charged to that same budget as **reserved headroom**, so the throttle condition is:

```
ramUsed + inFlightBytes + nextFrameBytes  >  maxRamBytes * headroomFactor   →  don't queue
```

Without the `inFlightBytes` term, `ramUsed` can sit legitimately at the limit while N in-flight loads each
hold a full volume, overshooting the budget by N frames with no counter showing it. Charging them to the
same budget keeps a single number meaningful and makes the Statistics dock reading trustworthy.

### 3.2b `renderlib/io/TimeSeriesPlayer.{h,cpp}` (new)

The playback state machine is Qt-free and clock-injected, so `agave_app` only supplies ticks.

```cpp
class TimeSeriesPlayer
{
public:
  enum class Mode { ShowEveryFrame, RealTime };   // stall vs drop-frames
  enum class State { Stopped, Playing, Paused };

  struct Config {
    Mode mode = Mode::ShowEveryFrame;
    float fps = 10.0f;
    bool loop = true;
  };

  void setConfig(const Config&);
  void setRange(uint32_t minTime, uint32_t maxTime);

  void play(uint32_t fromTime);   // records the origin frame for stop()
  void pause();
  uint32_t stop();                // returns the origin frame to restore

  // Called from the Qt clock tick. `isReady` lets the player ask whether the
  // candidate next frame is cached, which is what distinguishes the two modes.
  // Returns the timepoint to display, or nullopt to hold the current frame.
  std::optional<uint32_t> advance(uint64_t nowMs,
                                  uint32_t currentTime,
                                  const std::function<bool(uint32_t)>& isReady);

  State state() const;
};
```

Loop wrapping reuses `Timeline::WrapMode` (`renderlib/Timeline.h`) rather than reimplementing it.
Because `advance()` takes the time and the readiness predicate as arguments, the whole
stall-vs-drop-frames behavior is unit-testable with no Qt and no real I/O.

### 3.3 `CacheManager` additions

```cpp
std::uint64_t getRamBytesUsed() const;
std::uint64_t getRamBytesAvailable() const;      // maxRamBytes - used, clamped at 0

// Pinned entries are never evicted. Refcounted so nested pins are safe.
void pin(const LoadSpec&);
void unpin(const LoadSpec&);

// Notified (under no lock) when an entry leaves the RAM tier, so TimeSeriesLoader
// can mark that timepoint NotCached again instead of polling.
class IEvictionObserver { public: virtual void onEvicted(const CacheKey&) = 0; };
void addEvictionObserver(IEvictionObserver*);
void removeEvictionObserver(IEvictionObserver*);
```

- `CacheEntry` gains `uint32_t pinCount = 0`; `evictIfNeededLocked` skips pinned entries and gives up
  rather than looping forever if everything is pinned (log a warning).
- **Async disk write:** `storeImage` currently calls `storeToDisk` inline (`CacheManager.cpp:719-837`).
  Split into `storeImageInMemory` (synchronous, makes the frame available immediately) + an enqueue onto
  a single low-priority writer thread. Needs a bounded queue (drop oldest pending writes under pressure —
  a dropped disk write is a cache miss later, never a correctness bug) and a drain-on-shutdown.

### 3.3b `renderlib/io/applyVolumeToScene` (new, small)

```cpp
// Remap the transfer functions from the outgoing volume's histograms to the
// incoming one's, swap the scene's volume, and raise the dirty flags.
// Extracted verbatim from the two existing copies so they cannot drift again.
void applyVolumeToScene(Scene* scene,
                        const std::shared_ptr<ImageXYZC>& image,
                        RenderSettings* renderSettings);
```

Replaces `TimelineDockWidget.cpp:97-113` and `command.cpp:599-609`, which are near-identical today.
Keeps the channel-count mismatch warning from `command.cpp:594`. Must be called on the thread that
owns the scene (main thread for the GUI, render thread for commands) — it is cheap LUT work, not I/O.

### 3.4 Qt layer — deliberately thin

- **`TimeSeriesLoaderBridge : QObject, public ITimeSeriesLoaderObserver`** (new, `agave_app/`) — every
  observer callback is a single `QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection)` that re-emits
  as a Qt signal on the main thread. **No logic beyond marshalling.** This shim is the only reason any of
  this touches `agave_app` at all; renderlib cannot emit Qt signals itself.
- **`QTimelineWidget`** rebuilt: `OnTimeChanged` becomes `m_loader->requestTime(t)` and returns.
  The completion handler calls `applyVolumeToScene(...)` and emits `timeChanged`; it does not contain the
  remap logic itself. Discards completions whose `seq` is stale.
  - **Enable slider tracking.** `setTracking(false)` (`TimelineDockWidget.cpp:32`) exists only because
    loading blocked. With async loads, live scrubbing becomes possible, and the spinner-disable hack at
    `Controls.cpp:455-471` (whose comment explicitly blames slow loads) can be removed.
  - Fix the load-failure desync TODO (`TimelineDockWidget.cpp:91`) — on failure, restore the slider to
    `m_scene->m_timeLine.currentTime()` with signals blocked.
  - Fix `setTime()` (`:66-70`) to take a `blockSignals` argument like `QIntSlider::setValue` does.
- **Slider painting.** `QIntSlider` holds its `QSlider` by value, so: change the member to
  `QSlider* m_slider` and add a protected constructor that accepts an injected slider instance.
  Public API is untouched, so the many existing `QIntSlider` users are unaffected. Then add
  `CacheStatusSlider : QSlider` overriding `paintEvent` to draw the status strip inside
  `style()->subControlRect(CC_Slider, &opt, SC_SliderGroove, this)` before the base paint, and
  `TimeSliderWithCacheStatus : QIntSlider` to inject it.
  Repaints read `TimeSeriesLoader`'s status vector — **never** `CacheManager::findImage`, which stats
  the file on every call.
- **Playback clock.** A `QTimer` (`Qt::PreciseTimer`, like `GLView3D::m_etimer`) whose only job is to call
  `TimeSeriesPlayer::advance(...)` and act on the returned timepoint. The stall-vs-drop-frames decision,
  loop wrapping and stop-origin all live in the player, not here. The buttons map 1:1 onto
  `play()`/`pause()`/`stop()`.

**Total `agave_app` footprint** — everything else is renderlib:

| File | Change |
|---|---|
| `TimelineDockWidget.{h,cpp}` | buttons, QTimer, wiring; no policy |
| `TimeSeriesLoaderBridge.{h,cpp}` (new) | signal marshalling only |
| `CacheStatusSlider.{h,cpp}` (new) | `paintEvent` only |
| `Controls.{h,cpp}` | `QSlider m_slider` → `QSlider*` + protected injecting ctor |
| `CacheSettings.{h,cpp}` | JSON persistence of the new fields (needs `QStandardPaths`) |
| `CacheSettingsWidget.{h,cpp}` | prefetch form section |
| `loadDialog.{h,cpp}` | one checkbox |
| `agaveGui.cpp` | construct/own the loader + player, wire the bridge |

### 3.4b Vulkan upload fast path (`renderlib/gfxVulkan`) — in scope

Vulkan is the priority backend, and the current volume upload is the dominant per-timestep GPU cost.
`RenderVk.cpp:165` treats `VolumeDirty | VolumeDataDirty` identically and calls
`VolumeTextureVk::upload()`, whose **first statement is `release()`** (`VolumeTextureVk.cpp:39`).
So every single timestep tears down and rebuilds everything, even though the dimensions and format
never change across a time series.

What one timestep costs today, in order:

| Step | Location | Cost |
|---|---|---|
| Allocate `std::vector<uint16_t> rgba16(voxelCount * 4)` | `VolumeTextureVk.cpp:434` | Full-volume heap alloc + free, every frame (1.6 GB for 1024²×200) |
| `parallel_for` interleave 4 channels into it | `:435-442` | Already parallel — fine |
| Create a **new** staging `VkBuffer` + `vkAllocateMemory` | `:100-103` | New device allocation every frame; drivers cap allocation counts |
| `vkMapMemory` + `memcpy` whole volume + `vkUnmapMemory` | `:109-114` | **Second** full-volume CPU copy |
| Create a **new** `VkImage` + view + sampler | `:116-133` | Destination 3D texture reallocated per frame for identical dimensions |
| `transitionImageLayout` → `copyBufferToImage` → `transitionImageLayout` | `:135-145` | Each wraps `beginSingleTimeCommands`/`endSingleTimeCommands`… |
| …and `endSingleTimeCommands` ends in **`vkQueueWaitIdle`** | `Backend.cpp:486` | **Three full GPU queue drains per upload.** The single worst offender. |

**Which path applies depends on the render mode — and raymarch is the heavier one on CPU:**

| Mode | Class | `volumeTextureMode()` | Upload path |
|---|---|---|---|
| **Raymarch** (initial test target) | `RenderVk` | `FusedRgba8` (`RenderVk.cpp:104`) | `uploadFused` |
| Path trace | `RenderVkPT` | `RawRgba16` (`RenderVkPT.cpp:262`) | `uploadRaw` |

Full-volume CPU work per timestep:

| | `uploadFused` (raymarch) | `uploadRaw` (pathtrace) |
|---|---|---|
| Allocations | 2 (`rgb` voxels×3, `rgba` voxels×4) | 1 (`rgba16` voxels×4) |
| Full-volume write passes | **3** — `Fuse::fuse` → rgb, then rgb→rgba expand, then `memcpy` to staging | 2 — interleave, then `memcpy` to staging |
| Bytes crossing to GPU | 4 B/voxel | 8 B/voxel |

So raymarch does *more* CPU passes but transfers *half* the bytes and uses half the VRAM. **Both paths
matter equally** — moving a volume to the GPU is potentially the playback bottleneck in either mode, so
phase 11 optimizes both, not one first.

That argues for unifying them rather than optimizing each separately. Both paths have the identical shape
"produce `voxelCount × 4` elements into a buffer, then hand that buffer to Vulkan", so invert the
control flow: let the caller fill the persistently-mapped staging memory directly.

```cpp
// Replaces uploadVolumeBytes(const void* data, size_t byteCount, ...).
// `fill` is invoked with the mapped staging pointer; it writes byteCount bytes
// in place. Both modes then produce their voxels exactly once, with no
// intermediate std::vector and no memcpy.
bool uploadVolumeFrom(const std::function<void(void* mapped)>& fill,
                      size_t byteCount,
                      VkFormat format,
                      uint32_t width, uint32_t height, uint32_t depth,
                      bool linearFiltering);
```

- `uploadRaw`'s `parallel_for` interleave writes straight into `mapped` instead of into `rgba16`:
  2 full-volume passes → **1**, one fewer allocation.
- `uploadFused` has `Fuse::fuse` emit RGBA8 directly into `mapped`: 3 full-volume passes → **1**, two
  fewer allocations. Needs an output-stride (or RGBA-output) parameter on `Fuse::fuse`, which currently
  writes tightly-packed rgb — the one piece of per-path work in this item.

Everything else (items 1–3 below) lives in the shared machinery and benefits both modes identically.

Two notes specific to raymarch:
- `RenderVk::usesProgressiveAccumulation()` returns **false** (`RenderVk.cpp:110`), so the
  accumulation-reset noise issue does not apply — raymarch playback is clean frame to frame.
- In `FusedRgba8` mode color/opacity are baked into the volume, so `needFullUpload` is also true on
  `TransferFunctionDirty` (`RenderVk.cpp:169`) and the cheap `refreshColormap` path is never available.
  During playback that costs nothing extra (the frame is already a full upload from `VolumeDataDirty`),
  but any transfer-function tweak forces a full re-fuse + re-upload.

Fast path, in descending value-per-effort. Items 1–4 are ordinary Vulkan hygiene with no extension or
device-support risk, and together should dominate:

1. **Batch into one command buffer, one submit, and replace `vkQueueWaitIdle` with a fence.**
   Three queue drains → zero. `VulkanUtil.cpp` already has a `transitionImageLayout(commandBuffer, ...)`
   overload (`:94`) that takes an external command buffer, so the plumbing exists — the callers just
   aren't using it.
2. **Persist the `VkImage`/view/sampler across uploads.** Add a `reupload()`/`updateVolumeBytes()` path
   that reuses the existing image when `width/height/depth/format` match (exactly the
   `updateTransferBytes` pattern that already exists for the colormap at `:218-256`, just applied to the
   volume). Stop calling `release()` unconditionally at the top of `upload()`.
3. **Persist the staging buffer**, kept mapped (`VK_MEMORY_PROPERTY_HOST_VISIBLE | HOST_COHERENT`,
   mapped once at creation). Removes a per-frame `vkAllocateMemory` + map/unmap pair.
4. **Produce voxels directly into the mapped staging memory**, via the `uploadVolumeFrom(fill, ...)`
   inversion above. Eliminates one full-volume allocation and one full-volume `memcpy` for `uploadRaw`,
   and two allocations plus two passes for `uploadFused`. Biggest CPU-side win of the four, for both modes.
5. **Double-buffer the volume image + upload on a dedicated transfer queue.** Two `VkImage`s ping-ponged
   so timestep *t+1* is uploaded while *t* renders; the render just swaps which view the descriptor
   points at. This is the natural extension of CPU prefetch into VRAM, and the only item that makes
   GPU upload cost disappear from the frame time rather than merely shrink. Requires a transfer-queue
   family + semaphore handoff, and doubles volume VRAM — so it should be gated on available VRAM and
   report through the Statistics dock "GPU volume texture" line.
6. **`VK_EXT_host_image_copy`** (optional, if supported): copy host memory straight into an
   optimally-tiled image with no staging buffer and no command buffer at all. Cleanest possible path;
   needs a runtime capability check and a fallback to 1–4.
7. **ReBAR / `DEVICE_LOCAL | HOST_VISIBLE` memory** (optional): write directly to VRAM where the
   device exposes it. Also needs a fallback.

Implement 1–4 for **both** upload paths and measure in both render modes; only pursue 5 if GPU upload is
still bounding playback FPS. 6 and 7 are opportunistic. The equivalent OpenGL work
(`ImageXyzcGpu.cpp:141-240`, which has the same allocate-interleave-copy shape) stays out of scope per the
Vulkan-first priority.

Note for item 5 (double-buffering): the VRAM cost differs by mode — `RawRgba16` is 8 B/voxel, so
double-buffering a 1024²×200 volume costs ~3.2 GB versus ~1.6 GB for `FusedRgba8`. Gate on available VRAM
per-mode rather than with one fixed threshold.

### 3.5 Settings

This mirrors the existing `CacheConfig` (renderlib, `renderlib/CacheConfig.h`) vs `CacheSettings`
(agave_app, JSON + `QStandardPaths`) split exactly: **the config structs live in renderlib, only the
persistence lives in Qt.** So `PrefetchConfig` and `TimeSeriesPlayer::Config` are renderlib types.

- `CacheSettingsData` (`agave_app/CacheSettings.h:6`) gains `prefetchEnabled`, `prefetchDepth`,
  `prefetchFillCache`, `showDetailedCacheStatus`, `playbackFps`, `playbackDropFrames`, `playbackLoop`.
  `load()`/`save()` (`CacheSettings.cpp:79/114`) extend with the same `doc.contains(key)` guard pattern.
  `applyToRenderlib()` also builds and pushes the renderlib `PrefetchConfig` / player `Config`.
- `CacheSettingsWidget` gains a "Prefetch" form section: enable checkbox, depth spinbox,
  "Fill available cache" checkbox (disables depth when checked), "Detailed cache status (debug)" checkbox.
- `LoadDialog` gains one checkbox — "Prefetch time series" — meaning *warm the whole series*, only shown
  when `sizeT > 1` (same condition as the existing time slider, `loadDialog.cpp:160`). Returned alongside
  `getLoadSpec()`.

## 4. Phasing

Each phase builds, passes `agave_test` (which runs automatically post-build via
`test/CMakeLists.txt:38-42`), and leaves the app working.

| # | Phase | Files | Verification |
|---|---|---|---|
| 0 | Observability baseline. Surface `CacheManager::getStats()` in the Statistics dock via `CStatus::SetStatisticChanged` under a "Cache" group. Log per-timepoint load timings. | `StatisticsWidget.cpp`, `CacheManager.cpp` | Cache hit/miss/disk counters visible while scrubbing; gives a before/after number for every later phase. |
| 1 | Async reader API. `LoadRequest`, `submitLoad`, `maxConcurrentLoads`, `BlockingFileReader`. All readers ported to `loadVolumeBlocking`. `loadFromFile` becomes non-virtual. | `IFileReader.h`, new `io/LoadRequest.h`, new `io/BlockingFileReader.{h,cpp}`, all 5 readers, `renderlib/CMakeLists.txt` | No behavior change anywhere. New `test_loadRequest.cpp` with a fake reader covering submit/await/cancel. |
| 2 | Reader efficiency, no concurrency yet. Zarr: memoize `loadMultiscaleDims` per scene + mutex the lazy `m_store`/`m_zattrs` init. CZI: hoist `ICZIReader` into a member (open once), enable `libCZI::CreateSubBlockCache()`. | `FileReaderZarr.{h,cpp}`, `FileReaderCzi.{h,cpp}` | Measurable per-timestep speedup on both formats, standalone. Compare against phase-0 timings. |
| 3 | Zarr native concurrency. Override `submitLoad` to issue all per-channel `tensorstore::Read` futures at once and complete when they resolve. Per-request buffers (no shared scratch). `maxConcurrentLoads()` > 1. | `FileReaderZarr.{h,cpp}` | Multi-channel and multi-timepoint loads overlap. Verify cancel-by-future-release actually cancels (see risks). |
| 4 | `CacheManager` additions: used-bytes accessors, pin/unpin refcount, eviction observer, async disk-write queue. | `CacheManager.{h,cpp}` | Extend `test_cacheManager.cpp`: pinned entries survive pressure; disk writes land eventually; eviction observer fires. |
| 5 | `TimeSeriesLoader` + `applyVolumeToScene`. Queue, dedup/promote, forward-only prefetch, throttle, pin, status vector, observers; extract the duplicated remap block. | new `renderlib/io/TimeSeriesLoader.{h,cpp}`, new `renderlib/io/applyVolumeToScene.{h,cpp}`, `renderlib/CMakeLists.txt` | New `test_timeSeriesLoader.cpp` with a fake reader — **this is where dedup, promote, throttle and cancel get proven**, since none of it is testable through Qt. |
| 6 | GUI async time change. Bridge + rebuilt `QTimelineWidget`. Enable tracking, remove the spinner hack, fix the failure-desync and `setTime` TODOs. | `TimelineDockWidget.{h,cpp}`, new `TimeSeriesLoaderBridge.{h,cpp}`, `Controls.{h,cpp}`, `agaveGui.cpp` | Scrubbing no longer blocks the UI; wait cursor gone; live scrub works. |
| 7 | Playback. `TimeSeriesPlayer` in renderlib; Qt side is buttons + a QTimer that calls `advance()`. | new `renderlib/io/TimeSeriesPlayer.{h,cpp}`, `TimelineDockWidget.{h,cpp}` | New `test_timeSeriesPlayer.cpp` covers stall vs drop-frames, loop wrap and stop-origin with an injected clock and fake readiness predicate. Then confirm in the app. |
| 8 | Slider status strip. `CacheStatusSlider`, `TimeSliderWithCacheStatus`, `QIntSlider` slider-injection refactor. 2-state + 5-state debug. | `Controls.{h,cpp}`, new `CacheStatusSlider.{h,cpp}`, `TimelineDockWidget.cpp` | Strip tracks prefetch frontier during playback; no per-repaint `findImage` calls; other `QIntSlider` users visually unchanged. |
| 9 | Settings. Prefetch group in the cache dock; checkbox in `LoadDialog`. | `CacheSettings.{h,cpp}`, `CacheSettingsWidget.{h,cpp}`, `loadDialog.{h,cpp}`, `agaveGui.cpp` | Settings persist across restart; LoadDialog checkbox warms the whole series. |
| 10 | Unify `SetTimeCommand` onto `TimeSeriesLoader` + `applyVolumeToScene`. Fixes its missing reader reuse. | `renderlib/command.cpp` | `test_commands.cpp` still passes; python `set_time` loop is faster and hits GUI-warmed cache. |
| 11 | **Vulkan upload fast path** (§3.4b) items 1–4, for **both** upload paths equally: one command buffer + fence instead of 3× `vkQueueWaitIdle`; persist image/view/sampler; persist mapped staging buffer; invert to `uploadVolumeFrom(fill, ...)` so `uploadFused` and `uploadRaw` both produce voxels once, in place. | `VolumeTextureVk.{h,cpp}`, `VulkanUtil.{h,cpp}`, `Backend.{h,cpp}`, `Fuse.{h,cpp}` (output stride) See "Phase 11 verification" below. |
| 12 | *(only if still bound)* Vulkan double-buffered volume image + dedicated transfer queue (§3.4b item 5), gated on available VRAM. | `VolumeTextureVk.{h,cpp}`, `RenderVk.cpp`, `Backend.cpp` | Upload cost leaves the critical path; playback FPS tracks the target. |

Phases 0–4 are independently valuable and low-risk; 5 is the core; 6–9 are the user-visible feature.

### Phase 11 verification

Phase 11 is a pure plumbing change: the *same voxel bytes* must reach the GPU, just faster. The two render
modes are expected to look completely different from each other — raymarch is a single pass; pathtrace
accumulates over many passes with noise and variance — so cross-mode comparison is meaningless and is not
the criterion. Nor is frame-to-frame pixel comparison a sound check for pathtrace, whose output is
stochastic and iteration-count dependent (and whose RNG is actively being changed — see commit
`e7b4711a "better shader rand?"`).

So verify the thing actually being modified, not the whole renderer:

1. **Byte-exact upload check (primary).** Capture the buffer contents that `uploadVolumeFrom`'s `fill`
   produces, before and after the refactor, for the same scene/timepoint/channel selection. These must
   match **exactly**, per mode:
   - `uploadRaw` → the RGBA16 interleave must be bit-identical to what the old `rgba16` vector held.
   - `uploadFused` → the RGBA8 output must be bit-identical to what the old rgb→rgba expansion produced.
   This is deterministic, cheap, and is the real contract of the change. Worth a unit test that calls the
   fill callback against a small synthetic `ImageXYZC` and compares against the old code path's output.
2. **Timing (the point of the phase).** Per-timestep upload wall-clock, measured separately in each mode
   and reported through the Statistics dock "Performance" group. Expect the three `vkQueueWaitIdle` drains
   and the per-frame allocations to disappear.
3. **Visual smoke check, per mode independently.** Same scene, same timepoint, same camera: raymarch
   before vs raymarch after should be indistinguishable (it is deterministic — no accumulation). Pathtrace
   before vs after should converge to the same image given the same iteration count; compare after letting
   both settle, with tolerance, or just confirm no structural difference or artifacts.
4. **`gpuBytes()` still correct** once `release()` is no longer called on every upload — the counter is
   currently only correct *because* `upload()` resets it (`VolumeTextureVk.cpp:39`, `:64`), and phase 11
   removes that reset. Cross-check against the Statistics dock "GPU volume texture" line over many
   timesteps: it must stay flat, not climb.

## 5. Risks and things to verify during implementation

1. **tensorstore cancellation semantics.** The design assumes releasing all references to a
   `tensorstore::Future` cancels the underlying read. Verify early in phase 3. If it doesn't, fall back to
   "let in-flight finish, discard the result" for Zarr — functionally fine, just wastes some bandwidth.
2. **In-flight memory is untracked.** Covered in §3.2, repeated here because it is the most likely way to
   ship a memory regression. `maxConcurrentLoads` × frame size must be counted against `maxRamBytes`.
3. **Zarr thread-safety beyond the loader thread.** `loadMultiscaleDims` is also called from the main
   thread by `LoadDialog` (`loadDialog.cpp:26` creates its own reader, so it's a *different* instance
   today — but phase 2's memoization plus phase 3's concurrency means the shared instance needs the mutex
   regardless). Don't rely on "only the loader thread touches it".
4. **Two reader instances per open.** `agaveGui::open` (`agaveGui.cpp:818`) and `LoadDialog`
   (`loadDialog.cpp:26`) each create one. Worth consolidating in phase 6 so the metadata memoization from
   phase 2 is actually shared, but not required.
5. **GPU upload is the real FPS ceiling — now addressed in phase 11.** `VolumeTextureVk::upload` runs on
   the render thread on every `VolumeDataDirty` and currently reallocates the image, view, sampler and
   staging buffer *and* drains the GPU queue three times per timestep (§3.4b). For large volumes this,
   not loading, will bound playback FPS. Measure before and after phase 11 rather than assuming the FPS
   control is broken. Note `VolumeDataDirty` also resets progressive accumulation
   (`RenderVk.cpp:188-192`), so **path-traced** playback restarts from sample 0 every frame and will look
   noisy by nature, independent of upload speed. This is deferred, not solved. It does **not** affect
   raymarch (`RenderVk::usesProgressiveAccumulation()` returns false, `RenderVk.cpp:110`), which is the
   initial test target.
6. **LUT remap ordering.** The remap uses the *currently displayed* volume's histogram as the source
   (`TimelineDockWidget.cpp:97-101`). Keep it on the main thread at display time and never in the loader,
   otherwise out-of-order prefetch completions produce wrong transfer functions. Prefetched frames sit in
   cache un-remapped; that is correct.
7. **`ImageXYZC` construction cost moves to the loader thread** — the per-channel full-volume histogram
   (`ImageXYZC.cpp:204-213`) is a real chunk of the per-frame cost and benefits automatically. Good, but it
   means the loader thread is CPU-bound as well as I/O-bound; don't size concurrency assuming pure I/O.
8. **Channel-count mismatch across timepoints.** `SetTimeCommand` already warns on this (`command.cpp:594`).
   Preserve that check in the shared path.
9. **`CacheKey` includes `fileMtimeNs`** — for an image sequence or a zarr directory being written to while
   open, prefetched keys can silently stop matching. Existing behavior, just be aware it interacts with the
   status vector (a frame can read `RamCached` and then miss).

## 6. Out of scope (explicitly)

- Reverse / ping-pong playback.
- New websocket/python commands; python-driven prefetch.
- RenderDialog prefetching ahead (it consumes the warm cache passively).
- Async/chunked *initial* file open (the separate TODO at `agaveGui.cpp:887-890`).
- **OpenGL** upload optimization (`ImageXyzcGpu.cpp:141-240`) — same allocate/interleave/copy shape as the
  Vulkan path, but Vulkan is the priority backend. Vulkan upload optimization **is** in scope (phase 11).
- `VK_EXT_host_image_copy` and ReBAR paths (§3.4b items 6–7) — opportunistic, only if 1–4 fall short.
- Unifying the tensorstore in-memory cache pool with `CacheManager` (the TODO currently sitting
  uncommitted at `FileReaderZarr.cpp:501-502`).
