# RESUME HERE — finishing the time-series caching behaviour

Read this first. Companion to `timeseries-prefetch-plan.md` (the original plan) and
`timeseries-prefetch-status.md` (what got built, phase by phase).

Branch: `feature/timeseries-loading`. As of the last session the tree is **clean and green**:
1109 assertions in 77 test cases, app builds and runs.

**Working agreement: do not `git commit`.** Make the change, build it, run the suite, then hand the diff
over for review. (Established 2026-08-01.)

---

## The target behaviour, in the user's words

> Ideally for time series data, the frames in memory cache are in the neighborhood around the current
> time step. In particular, we assume forward playback so only immediate future frames really need be in
> memory (t up to t+N). When the time slider is dragged then we should asynchronously again take care of
> prefetching to fill the memory cache with the next n frames that fit in memory.

And earlier, on prefetch terminating:

> It should stop once the nearest timesteps are in memory, and the rest of the data is in disk cache.
> Then any playback or time slider manipulation should only need to negotiate between memory cache and
> disk cache.

## What already works

- Memory holds a **forward window** `t..t+N`; disk holds the rest.
- Dragging the slider re-aims the window asynchronously — `requestTime` moves `m_currentTime`, the
  window slides, prefetch re-targets, and the UI never blocks.
- Prefetch **terminates**: `TimepointStatus::DiskCached` distinguishes "evicted but safely on disk" from
  "never fetched", which is what stopped the endless re-fetch loop.
- Disk warming **bypasses the memory tier** (`CacheManager::storeImageOnDiskOnly`), so warming no longer
  drags a band of resident frames across the whole timeline or evicts the near ones.
- The slider strip paints in-memory (solid) vs on-disk (dimmer) vs not-cached (blank).

## The one remaining gap, and why it is two coupled changes

The memory window is currently `depth` frames (default **4**), even when the budget would hold ~38.
The user wants it to hold **as many forward frames as fit**.

**Do not just widen the window.** That was tried and the termination test failed immediately, for a real
reason: a wider window evicts more, and a frame evicted **before its asynchronous disk write lands**
fails the `containsOnDisk` probe and is marked `NotCached` — recorded as being in neither tier. The write
queue is bounded at 4 and **drops its oldest entry** under pressure, so widening makes this common.

So land these together, in this order:

### 1. Make disk writes reliable
In `CacheManager` (`enqueueDiskWrite`, `kMaxPendingDiskWrites = 4`, `m_droppedDiskWrites`):
- Replace drop-oldest with **back-pressure**: block the producer when the queue is full, so every write
  completes. The user explicitly wants the data on disk, and dropping silently undermines that.
- Consider a deeper queue too, but note each entry holds a `shared_ptr` to a whole volume (~104 MB for
  their data), so depth costs memory the RAM budget does not know about.
- Once writes cannot be dropped, it becomes honest to mark a time step `DiskCached` when its write is
  **queued** rather than when it completes — which removes the eviction-timing race entirely. That is
  probably the cleaner fix than back-pressure alone.
- Diagnostic already in place: Statistics dock → Cache → "Disk Writes Pending" / "Disk Writes Dropped".
  Ask the user whether Dropped climbs during prefetch; that decides how aggressive to be.

### 2. Widen the memory window
`TimeSeriesLoader::prefetchWindowLocked()`, the `steps` calculation:
```cpp
std::uint64_t steps =
  m_prefetchConfig.fillCache ? maxSteps : std::min<std::uint64_t>(m_prefetchConfig.depth, maxSteps);
```
i.e. under `fillCache`, want the whole series and let the **capacity clamp** just below reduce it to what
fits (`budgetFrames - 1`, reserving the pinned current frame). Keep that clamp — it is what makes
prefetch settle instead of churning. Without `fillCache`, honour `depth` as an explicit user lookahead.

Verify with the existing test *"prefetch terminates on a series larger than memory"*, which asserts every
time step ends up `RamCached` **or** `DiskCached` and that fetches stop.

## Landmines worth knowing before touching this code

- **The window is the single source of truth.** `canStartPrefetchLocked`, `nextPrefetchTimeLocked` and
  `requestTime`'s cancel check all use `prefetchWindowLocked()`. Three separate bugs came from these
  disagreeing about which frames were wanted. Keep them sharing it.
- **Throttle on "wanted frames already resident", never on free space.** Gating on free space deadlocks:
  nothing frees space except eviction.
- **Wrapping.** `PrefetchConfig::wrapAround` tracks the playback loop setting. Without it, looping
  playback stalls on the last frame waiting for a first frame nobody fetches.
- **Never want more frames than fit.** The clamp to `budgetFrames - 1` is load-bearing.
- **Vulkan shaders**: SPIR-V is checked in, not built. Re-run `make_spirv.py` after editing a `.frag`.
  And `RenderVk` loads **`volume.frag`**, not `basicVolume.frag` — three fixes were wasted on the wrong
  file. `basicVolume.frag` appears unused and still carries a latent Y-flip bug.
- **Build**: `cmake --build . --target install` does **not** build or run tests. Use
  `--target agave_test --config Debug`. Occasional `LNK1201` is a stale `agave_test.exe` holding its
  PDB — kill the process and retry.
- Scripted edits: **always assert the anchor matched.** A silent no-op replace once shipped a crash
  (a declared-but-never-constructed Qt widget).

## Also outstanding, unrelated to caching

- **Phase 12** (double-buffered Vulkan upload) — explicitly gated on measuring whether GPU upload still
  bounds playback FPS after Phase 11. Phase 11 has still never been executed. Measure first.
- Phase 11 leftovers: give `Fuse` an output stride (removes `uploadFused`'s last full-volume pass);
  replace the final `vkQueueWaitIdle` with a fence.
- No automated coverage of the Qt layer, `FileReaderZarr`, or the Vulkan upload path — those are
  compile-verified only.
