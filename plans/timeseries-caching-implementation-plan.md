# Time-Series Caching Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the memory cache hold as many forward time steps as the RAM budget allows, the disk cache hold a clamped run beyond that, and both re-aim when the time slider moves — with disk writes that cannot silently vanish and a fresh session that recognises a warm disk cache without dragging it through RAM.

**Architecture:** Two forward-only windows derived from the RAM and disk frame budgets, both produced by `TimeSeriesLoader` and shared by every call site that asks "which frames do we want". `CacheManager` gains a disk-space reservation so a queued write can be trusted, and a disk-eviction notification so a deleted entry stops being reported as cached. Policy stays in `renderlib`; `agave_app` only loses two settings widgets.

**Tech Stack:** C++17, CMake, Catch2 (`agave_test`), Qt 5/6 widgets (`agave_app`), tensorstore (disk tier).

**Spec:** `plans/timeseries-caching-design.md`. Read it before starting — every task references section numbers from it (§1-§5).

## Status: implemented 2026-08-01

All tasks landed on `feature/timeseries-loading`. Final state: **1449 assertions in 91 test cases**,
stable across five consecutive runs; `agave_test` and `install` both build.

| Commit | Task |
| --- | --- |
| `13ac1535` | 1 — disk write reservation, never drop, pending-aware probe |
| `6fc839e9` | 2 — disk eviction observer |
| `aaaef02e` | 3 — retire `depth`/`fillCache`, plus Task 4's Step 0 and all of Task 6 |
| `e3532886` | 4 — capacity-sized memory window, clamped disk warm set |
| `0dd21b99` | 5 — cross-session warm start |

### Deviations from this plan, and why

- **Task 6 was folded into Task 3.** The plan removed the settings *data* fields in Task 3 and the
  *widgets* in Task 6, which would have shipped a Cache Settings dock displaying a depth spinner and a
  "Fill available cache" checkbox that silently did nothing. Worse than either endpoint.
- **Task 4's Step 0 (the prefetch-gate split) had to land in Task 3.** It was written as a Task 4 step
  after being discovered during Task 2, but removing `depth` triggered it immediately: the termination
  test carried `cfg.depth = 2` against a 4-frame budget, so `wantedResident` peaked at 3 and the RAM
  throttle never engaged. Without `depth` the window grows to 3, `wantedResident` reaches 4, the
  throttle latches, and 18 of 21 steps were left in neither tier.
- **Task 1's tests went in `test/test_cacheManager.cpp`**, not the loader file — they are
  `CacheManager` tests and that file already has the helpers and the `[cache]` tag.
- **`PendingDiskWrite` carries its byte count** rather than `enqueueDiskWrite` taking a separate
  parameter, so the writer never recomputes it.
- **`onEvictedFromDisk` takes a `diskCacheId` string, not a `CacheKey`** — see §3; the spec was
  corrected during planning because the disk index retains no key.
- **`Disk Writes Pending Bytes`** added to the cache status report, since queued writes now reserve
  disk space and the committed total is used + pending.

### Test changes worth a reviewer's attention

Seven existing tests changed. Five were mechanical (a budget that no longer bounds the window), but
two altered assertions rather than setup, and one new test needed strengthening:

- `"A burst of stores drops the oldest writes…"` asserted that dropping was *expected*. Rewritten to
  assert back-pressure and that all 40 writes landed.
- `"prefetch reads back from the disk cache"` waited on `warmCount`, which §5a seeding satisfies the
  instant the series is set — before prefetch reads anything back. Now waits on `RamCached`.
- The new warm-only test needed an assertion on **disk hits**, not end state. Dragging every step
  through RAM leaves identical statuses behind, because a promoted step is evicted moments later and
  eviction re-marks it `DiskCached` — so the obvious assertions passed both before and after the fix.

### Not done

- **§5d** (a RAM-resident entry never refreshes its disk `lastAccess`, so the most-watched frames look
  coldest next session) — deliberately out of scope; fixing it puts disk I/O back on the RAM-hit path.
- **Manual verification of the acceptance scenario** (Task 6, Step 7) — automated coverage cannot
  exercise the Qt layer, `FileReaderZarr`, or the Vulkan upload path. Still to be walked in the app.

## Global Constraints

- **Do NOT `git commit`.** Working agreement established 2026-08-01. Every task ends with build + full test suite, then the diff is handed over for review. Steps that would normally commit instead verify.
- Branch: `feature/timeseries-loading`. Baseline before this work: 1109 assertions in 77 test cases, green.
- **The build directory is `D:\agave_build`.** There is a stale, misconfigured `./build` in the repo that points at an uninstalled Vulkan SDK (1.3.275.0) and fails to configure — ignore it entirely; do not try to fix it.
- Build the tests with `cd /d/agave_build && cmake --build . --target agave_test --config Debug`. This target **also runs the suite** as a post-build step, so a successful build implies passing tests. `--target install` does not build or run tests.
- Run the suite standalone with `/d/agave_build/Debug/agave_test.exe`. Filter a single case with `/d/agave_build/Debug/agave_test.exe "<test name>"`, or a tag with `... "[cache]"`.
- Verified baseline: **1109 assertions in 77 test cases**, green.
- `LNK1201` on build means a stale `agave_test.exe` is holding its PDB. Kill the process and rebuild.
- Keep policy in `renderlib`. `agave_app` changes are widgets and wiring only — no state machines, no cache policy.
- **All capacity arithmetic uses saturating subtraction.** `std::uint64_t` underflow here produces a huge window that clamps to the whole series, which is the exact churn every clamp in this design exists to prevent.
- `prefetchWindowLocked()` and the new `diskWarmWindowLocked()` are the single source of truth for "which frames do we want". `canStartPrefetchLocked`, `nextPrefetchTimeLocked` and `requestTime`'s cancel check must all read them rather than recomputing. Three separate historical bugs came from these disagreeing.
- Never edit a Vulkan `.frag` in this plan. No shader work here.
- When applying a scripted/multi-file edit, **assert the anchor matched**. A silent no-op replace previously shipped a crash.

## File Structure

| File | Responsibility | Tasks |
| --- | --- | --- |
| `renderlib/CacheManager.h` / `.cpp` | Disk-space reservation, never-drop write queue, pending-aware residency probe, disk-eviction notification, public disk-id mapping | 1, 2 |
| `renderlib/io/TimeSeriesLoader.h` / `.cpp` | Both windows, `PrefetchConfig`, disk-eviction handling, status seeding, warm-only probe | 2, 3, 4, 5 |
| `agave_app/CacheSettings.h` / `.cpp` | Drop two persisted settings fields | 6 |
| `agave_app/CacheSettingsWidget.h` / `.cpp` | Drop two widgets and the enable-state lambda | 6 |
| `agave_app/agaveGui.cpp` | Drop two config assignments; LoadDialog sets one flag | 6 |
| `test/test_timeSeriesLoader.cpp` | All new coverage; existing `fillCache`/`depth` call sites migrated | 1-6 |

**Task ordering rationale.** Task 1 lands first because widening the memory window (Task 4) increases eviction pressure, and the resume doc records that widening alone fails the termination test: a frame evicted before its queued disk write lands fails the `containsOnDisk` probe and is recorded as being in neither tier. Task 1's pending-aware probe removes that race, so Task 4 is safe. Task 3 removes `depth`/`fillCache` from `PrefetchConfig`, which breaks compilation everywhere they are referenced, so it updates `renderlib`, tests, **and** `agave_app` together — Task 6 then does the widget removal that Task 3 left as dead UI.

---

## Task 1: Disk writes reserve space and are never dropped (§4)

**Files:**
- Modify: `renderlib/CacheManager.h:154-163` (public write-queue accessors), `renderlib/CacheManager.h:248-269` (`PendingDiskWrite`, queue members)
- Modify: `renderlib/CacheManager.cpp:384-437` (`storeImage`, `storeImageOnDiskOnly`, `storeImageInternal`), `renderlib/CacheManager.cpp:586-616` (`kMaxPendingDiskWrites`, `enqueueDiskWrite`), `renderlib/CacheManager.cpp:618-652` (`diskWriterMain`), `renderlib/CacheManager.cpp:456-490` (`clearDiskCache`), `renderlib/CacheManager.cpp:877-895` (`containsOnDisk`)
- Test: `test/test_cacheManager.cpp` (append). These are `CacheManager` tests and belong here, not in the loader file: this file already has `TempCacheDir` (line 94), `diskConfig` (line 67), `makeSpec(path, time)` (line 41), `makeImage(x,y,z,c)` (line 31) and `imageBytes(x,y,z,c)` (line 50), and it uses the `[cache]` tag.

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `bool CacheManager::storeImage(const LoadSpec&, const std::shared_ptr<ImageXYZC>&)` — was `void`. Returns false when the disk write was refused for lack of space. The memory store is unaffected by the return value.
  - `bool CacheManager::storeImageOnDiskOnly(const LoadSpec&, const std::shared_ptr<ImageXYZC>&)` — was `void`. Returns false when refused; Task 5 uses this.
  - `bool CacheManager::containsOnDisk(const LoadSpec&) const` — semantics widen to "is on disk **or** is queued to be written".
  - `std::uint64_t CacheManager::pendingDiskBytes() const` — new, for tests and the Statistics dock.

- [ ] **Step 1: Write the failing tests**

Append to `test/test_timeSeriesLoader.cpp`:

```cpp
TEST_CASE("CacheManager never drops disk writes under sustained pressure", "[cacheManager]")
{
  // Regression gate for widening the memory window. The queue used to drop its
  // oldest entry when full, so a frame could be reported as neither in memory
  // nor on disk -- and prefetch would re-fetch it forever.
  TempDir dir;
  CacheManager cache(dir.str());
  cache.setConfig(diskCacheConfig(frameBytes() * 4, 64ULL * 1024 * 1024));

  // Far more stores than the queue is deep, with no pause to let it drain.
  const int kStores = 64;
  for (int i = 0; i < kStores; ++i) {
    LoadSpec spec = makeBaseSpec();
    spec.time = static_cast<uint32_t>(i);
    CHECK(cache.storeImage(spec, makeImage()));
  }
  cache.flushDiskWrites();

  CHECK(cache.droppedDiskWrites() == 0);
  CHECK(cache.pendingDiskBytes() == 0);
  for (int i = 0; i < kStores; ++i) {
    LoadSpec spec = makeBaseSpec();
    spec.time = static_cast<uint32_t>(i);
    CHECK(cache.containsOnDisk(spec));
  }
}

TEST_CASE("CacheManager counts queued writes against the disk budget", "[cacheManager]")
{
  // On-disk bytes PLUS bytes for writes still in flight must never exceed the
  // cap. Without the reservation, evictDiskIfNeeded only ever sees the entry at
  // the front of the queue and the tier overshoots by the rest of the backlog.
  TempDir dir;
  CacheManager cache(dir.str());
  const std::uint64_t diskCap = frameBytes() * 8;
  cache.setConfig(diskCacheConfig(frameBytes() * 2, diskCap));

  for (int i = 0; i < 40; ++i) {
    LoadSpec spec = makeBaseSpec();
    spec.time = static_cast<uint32_t>(i);
    cache.storeImage(spec, makeImage());
    const auto usage = cache.getUsage();
    CHECK(usage.diskBytesUsed + cache.pendingDiskBytes() <= diskCap);
  }
  cache.flushDiskWrites();
  CHECK(cache.getUsage().diskBytesUsed <= diskCap);
}

TEST_CASE("CacheManager reports a queued write as present on disk", "[cacheManager]")
{
  // This is what removes the eviction-timing race: a frame evicted from RAM
  // before its write lands must still probe as disk-present, or it gets marked
  // as being in neither tier.
  TempDir dir;
  CacheManager cache(dir.str());
  cache.setConfig(diskCacheConfig(frameBytes() * 64, 64ULL * 1024 * 1024));

  LoadSpec spec = makeBaseSpec();
  spec.time = 7;
  CHECK(cache.storeImage(spec, makeImage()));
  // Deliberately BEFORE flushDiskWrites: the write is queued, not yet on disk.
  CHECK(cache.containsOnDisk(spec));

  cache.flushDiskWrites();
  CHECK(cache.containsOnDisk(spec));
}

TEST_CASE("CacheManager refuses a disk write that cannot fit", "[cacheManager]")
{
  TempDir dir;
  CacheManager cache(dir.str());
  // Disk cap smaller than a single frame: nothing can ever fit.
  cache.setConfig(diskCacheConfig(frameBytes() * 64, frameBytes() / 2));

  LoadSpec spec = makeBaseSpec();
  spec.time = 1;
  CHECK_FALSE(cache.storeImage(spec, makeImage()));
  CHECK_FALSE(cache.containsOnDisk(spec));
  // The memory tier is independent of the refusal.
  CHECK(cache.containsInMemory(spec));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build . --target agave_test --config Debug && ./agave_test "[cacheManager]"`

Expected: compile error — `pendingDiskBytes` is not a member, and `storeImage` returns `void` so `CHECK(cache.storeImage(...))` will not compile.

- [ ] **Step 3: Add the reservation members and the public accessor**

In `renderlib/CacheManager.h`, change the store signatures to `bool` and add the accessor beside the existing queue accessors near line 154:

```cpp
  // Bytes belonging to volumes queued for writing but not yet written. Counted
  // against maxDiskBytes alongside diskBytesUsed, so the tier never overshoots
  // its cap by the size of the backlog.
  std::uint64_t pendingDiskBytes() const;
```

Change the declarations:

```cpp
  // Returns false when the DISK write was refused because it could not fit in
  // maxDiskBytes even after eviction. The memory store is independent: a false
  // return does not mean the image failed to cache in RAM.
  bool storeImage(const LoadSpec& loadSpec, const std::shared_ptr<ImageXYZC>& image);
  bool storeImageOnDiskOnly(const LoadSpec& loadSpec, const std::shared_ptr<ImageXYZC>& image);
```

and the private helpers plus the new members:

```cpp
  bool storeImageInternal(const LoadSpec& loadSpec, const std::shared_ptr<ImageXYZC>& image, bool intoMemory);
  bool enqueueDiskWrite(PendingDiskWrite&& write, std::uint64_t bytes);
```

Add beside `m_droppedDiskWrites`:

```cpp
  std::condition_variable m_diskQueueSpace;
  // Guarded by m_diskQueueMutex, NOT m_mutex: it moves in lockstep with the
  // queue it describes.
  std::uint64_t m_pendingDiskBytes = 0;
```

Also update the doc comment on `containsOnDisk` (line ~110) to say it reports "on disk, or queued to be written", and the comment on `droppedDiskWrites` to note it is now structurally always zero and retained as an assertion.

- [ ] **Step 4: Make `containsOnDisk` pending-aware**

In `renderlib/CacheManager.cpp`, replace the body of `containsOnDisk` after the config guard. Note the existing comment there claims the disk index is built lazily — that is wrong (see §5c) and is corrected in Task 5; do not rely on it here.

```cpp
  const std::string id = diskCacheId(key);
  {
    std::scoped_lock lock(m_diskQueueMutex);
    for (const auto& queued : m_diskQueue) {
      if (diskCacheId(queued.key) == id) {
        return true;
      }
    }
    if (m_diskWriteInProgress && m_inProgressDiskId == id) {
      return true;
    }
  }
  std::error_code ec;
  std::filesystem::path entryPath = std::filesystem::path(cacheDirCopy) / id;
  return std::filesystem::exists(entryPath / "meta.json", ec);
```

Add `std::string m_inProgressDiskId;` beside `m_diskWriteInProgress` in the header — the entry being written right now has already left the queue but is not yet on disk, and without it there is a window where the probe says no.

- [ ] **Step 5: Reserve space at enqueue and block instead of dropping**

Replace the `kMaxPendingDiskWrites` constant and `enqueueDiskWrite`:

```cpp
namespace {
// Bounded by shutdown exposure, not memory. stopDiskWriter abandons the backlog
// so quit is instant, which makes this exactly the number of frames that can be
// lost by quitting mid-warm. Volumes queued via storeImage are already
// RAM-resident, so depth costs little memory -- but it costs completeness.
constexpr std::size_t kMaxPendingDiskWrites = 8;
} // namespace

bool
CacheManager::enqueueDiskWrite(PendingDiskWrite&& write, std::uint64_t bytes)
{
  const std::string id = diskCacheId(write.key);
  {
    std::unique_lock<std::mutex> lock(m_diskQueueMutex);
    if (m_diskWriterStop) {
      return false;
    }
    // Back-pressure, never drop. A dropped write loses the frame from disk
    // permanently: RAM eviction is a pure drop and never writes on the way out,
    // so there is no second chance.
    m_diskQueueSpace.wait(lock, [this] { return m_diskWriterStop || m_diskQueue.size() < kMaxPendingDiskWrites; });
    if (m_diskWriterStop) {
      return false;
    }
    pendingBytes = m_pendingDiskBytes;
  }

  // Evict against on-disk + already-queued + this write, so eviction makes room
  // for the whole backlog rather than just the entry at the front.
  if (!reserveDiskSpace(write.config, bytes, pendingBytes)) {
    return false;
  }

  {
    std::unique_lock<std::mutex> lock(m_diskQueueMutex);
    m_pendingDiskBytes += bytes;
    m_diskQueue.push_back(std::move(write));
    if (!m_diskWriterThread.joinable()) {
      m_diskWriterThread = std::thread([this] { diskWriterMain(); });
    }
  }
  m_diskQueueWake.notify_one();
  return true;
}
```

Declare `std::uint64_t pendingBytes = 0;` before the first block, and add the reservation helper. It reuses the existing eviction logic rather than duplicating it:

```cpp
bool
CacheManager::reserveDiskSpace(const CacheConfig& config, std::uint64_t bytes, std::uint64_t pendingBytes)
{
  if (!config.enableDisk || config.maxDiskBytes == 0 || bytes == 0) {
    return false;
  }
  if (bytes + pendingBytes > config.maxDiskBytes) {
    // Cannot fit even with the tier emptied.
    return false;
  }
  evictDiskIfNeeded(config, bytes + pendingBytes);
  std::scoped_lock lock(m_mutex);
  return (m_currentDiskBytes + pendingBytes + bytes) <= config.maxDiskBytes;
}
```

Declare it in the header next to `evictDiskIfNeeded`:

```cpp
  // Evicts as needed so `bytes` can be written on top of `pendingBytes` already
  // queued. Returns false if it will not fit even after eviction.
  bool reserveDiskSpace(const CacheConfig& config, std::uint64_t bytes, std::uint64_t pendingBytes);
```

- [ ] **Step 6: Release the reservation in the writer and wake blocked producers**

In `diskWriterMain`, record the in-progress id and release the reservation. Replace from `PendingDiskWrite write = std::move(m_diskQueue.front());` through the end of the loop body:

```cpp
    PendingDiskWrite write = std::move(m_diskQueue.front());
    m_diskQueue.pop_front();
    const std::uint64_t writeBytes = estimateImageBytes(*write.image);
    m_diskWriteInProgress = true;
    m_inProgressDiskId = diskCacheId(write.key);
    // A slot is free the moment the entry leaves the queue.
    m_diskQueueSpace.notify_one();

    lock.unlock();
    try {
      storeToDisk(write.key, write.image, write.config, write.cacheDir);
    } catch (std::exception& e) {
      LOG_ERROR << "Disk cache write failed: " << e.what();
    } catch (...) {
      LOG_ERROR << "Disk cache write failed";
    }
    write.image.reset();
    lock.lock();

    // storeToDisk has folded these bytes into m_currentDiskBytes on success, or
    // written nothing on failure. Either way they are no longer pending.
    m_pendingDiskBytes = m_pendingDiskBytes >= writeBytes ? m_pendingDiskBytes - writeBytes : 0;
    m_diskWriteInProgress = false;
    m_inProgressDiskId.clear();
    m_diskQueueDrained.notify_all();
```

`estimateImageBytes` is safe to call here: it is `sizeX * sizeY * sizeZ * sizeC * (IN_MEMORY_BPP / 8)` with no locking, so calling it while holding `m_diskQueueMutex` cannot deadlock against `m_mutex`.

In `stopDiskWriter`, after setting `m_diskWriterStop = true` and clearing the queue, add `m_pendingDiskBytes = 0;` and `m_diskQueueSpace.notify_all();` so a producer blocked on a full queue during shutdown is released rather than deadlocking the join. Leave the queue-clearing behaviour itself alone — instant quit is deliberate (§4, "Shutdown").

In `clearDiskCache`, after `m_diskQueue.clear()`, add `m_pendingDiskBytes = 0;` and `m_diskQueueSpace.notify_all();`.

- [ ] **Step 7: Thread the bool through the store functions**

Replace `storeImage`, `storeImageOnDiskOnly` and `storeImageInternal`. Note `storeImage` currently computes `configCopy`, `cacheDirCopy` and `key` and uses none of them — delete those (§4, "Drive-by cleanup").

```cpp
bool
CacheManager::storeImage(const LoadSpec& loadSpec, const std::shared_ptr<ImageXYZC>& image)
{
  return storeImageInternal(loadSpec, image, /*intoMemory=*/true);
}

bool
CacheManager::storeImageOnDiskOnly(const LoadSpec& loadSpec, const std::shared_ptr<ImageXYZC>& image)
{
  return storeImageInternal(loadSpec, image, /*intoMemory=*/false);
}

bool
CacheManager::storeImageInternal(const LoadSpec& loadSpec, const std::shared_ptr<ImageXYZC>& image, bool intoMemory)
{
  if (!image) {
    return false;
  }

  CacheConfig configCopy;
  std::string cacheDirCopy;
  {
    std::scoped_lock lock(m_mutex);
    configCopy = m_config;
    cacheDirCopy = m_cacheDir;
  }

  const auto key = makeKey(loadSpec);

  // Memory first, so the volume is usable immediately. The disk write is a
  // full-volume tensorstore write; performing it inline made every prefetched
  // time step wait on disk before the frame was even available.
  if (intoMemory) {
    storeImageInMemory(key, image);
  }

  if (configCopy.enabled && configCopy.enableDisk && configCopy.maxDiskBytes > 0 && !cacheDirCopy.empty()) {
    const std::uint64_t bytes = estimateImageBytes(*image);
    return enqueueDiskWrite(PendingDiskWrite{ key, image, configCopy, cacheDirCopy }, bytes);
  }
  // No disk tier configured: nothing was refused.
  return true;
}
```

Add `pendingDiskBytes()`:

```cpp
std::uint64_t
CacheManager::pendingDiskBytes() const
{
  std::scoped_lock lock(m_diskQueueMutex);
  return m_pendingDiskBytes;
}
```

- [ ] **Step 8: Run the new tests**

Run: `cmake --build . --target agave_test --config Debug && ./agave_test "[cacheManager]"`

Expected: PASS, all four new cases.

- [ ] **Step 9: Run the whole suite**

Run: `./agave_test`

Expected: PASS. Baseline was 77 cases / 1109 assertions; expect 81 cases and more assertions. If `TimeSeriesLoader prefetch reads back from the disk cache` (line 877) fails, the pending-aware probe is reporting steps as disk-present that were never queued — re-read Step 4.

- [ ] **Step 10: Verify, do not commit**

Report the build and test output. Per the working agreement, hand the diff over for review rather than committing.

---

## Task 2: Disk eviction notifies observers (§3)

**Files:**
- Modify: `renderlib/CacheManager.h:86-92` (`IEvictionObserver`), `renderlib/CacheManager.h:109-116` (probe declarations, add `diskCacheIdFor`)
- Modify: `renderlib/CacheManager.cpp:1225-1280` (`evictDiskIfNeeded`), plus a new `notifyEvictedFromDisk`
- Modify: `renderlib/io/TimeSeriesLoader.h:145-147` (observer override), `renderlib/io/TimeSeriesLoader.cpp:265-305` (`onEvictedFromMemory`, add the disk counterpart)
- Test: `test/test_timeSeriesLoader.cpp` (append)

**Interfaces:**
- Consumes: Task 1's `bool`-returning stores (not used here, but the file must compile against them).
- Produces:
  - `virtual void CacheManager::IEvictionObserver::onEvictedFromDisk(const std::string& diskCacheId) = 0;`
  - `std::string CacheManager::diskCacheIdFor(const LoadSpec&) const;`
  - `void TimeSeriesLoader::onEvictedFromDisk(const std::string& diskCacheId) override;`
  - `TimeSeriesLoader` gains private `std::unordered_map<std::string, uint32_t> m_diskIdToTime;` — Task 5 populates it in `setSeries`. Until then it is empty, so the callback is a no-op and the test in this task populates it via a real load.

**Why not a `CacheKey`:** `m_diskEntries` is keyed by `diskCacheId` and `DiskEntry` holds only `{path, bytes, lastAccess}`. `meta.json` persists the key only as the opaque `keyToString(key)` string and `loadDiskIndex` reads just `lastAccess`/`bytes`, so an entry evicted after a fresh start has no `CacheKey` to hand back. See §3.

- [ ] **Step 1: Write the failing test**

Append to `test/test_timeSeriesLoader.cpp`:

```cpp
TEST_CASE("TimeSeriesLoader reverts DiskCached when the disk tier evicts", "[timeSeriesLoader]")
{
  // Without this, a frame whose disk entry is deleted stays marked DiskCached
  // forever: prefetch believes it is finished, the slider paints a solid strip
  // that is a lie, and playback silently falls back to source loads.
  TempDir dir;
  CacheManager cache(dir.str());
  // Disk holds 4 frames; the series needs 8. Warming it must evict its own
  // earliest writes, and those evictions must be reported.
  cache.setConfig(diskCacheConfig(frameBytes() * 2, frameBytes() * 4));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);
  RecordingObserver observer;
  loader.addObserver(&observer);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 7, 0);
  loader.requestTime(0);

  // Some time step must be reported as having left the disk tier.
  REQUIRE(waitFor([&] {
    for (uint32_t t = 0; t <= 7; ++t) {
      if (observer.sawStatus(t, TimepointStatus::DiskCached) && loader.status(t) == TimepointStatus::NotCached) {
        return true;
      }
    }
    return false;
  }));

  // And the tier stayed within its cap throughout.
  cache.flushDiskWrites();
  CHECK(cache.getUsage().diskBytesUsed <= frameBytes() * 4);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build . --target agave_test --config Debug && ./agave_test "TimeSeriesLoader reverts DiskCached when the disk tier evicts"`

Expected: FAIL — the wait times out, because nothing reverts a `DiskCached` step.

- [ ] **Step 3: Add the observer method and the id mapping to `CacheManager`**

In `renderlib/CacheManager.h`, extend the interface:

```cpp
  class IEvictionObserver
  {
  public:
    virtual ~IEvictionObserver() = default;
    virtual void onEvictedFromMemory(const CacheKey& key) = 0;
    // The disk index is keyed by diskCacheId and retains no CacheKey (meta.json
    // stores only the opaque keyToString form), so this reports the id. Pair it
    // with diskCacheIdFor() to map your own specs onto it.
    virtual void onEvictedFromDisk(const std::string& diskCacheId) = 0;
  };
```

and add the public mapping beside `containsOnDisk`:

```cpp
  // The disk-cache identity for a spec, as used by onEvictedFromDisk. Public so
  // an observer can build its own id -> domain-object map without a reverse
  // lookup. Note this calls makeKey, which stats the source file.
  std::string diskCacheIdFor(const LoadSpec& loadSpec) const;
```

Declare the notify helper next to `notifyEvicted`:

```cpp
  // Precondition: caller must NOT hold m_mutex.
  void notifyEvictedFromDisk(const std::vector<std::string>& ids);
```

- [ ] **Step 4: Implement them, and make `evictDiskIfNeeded` report**

In `renderlib/CacheManager.cpp`:

```cpp
std::string
CacheManager::diskCacheIdFor(const LoadSpec& loadSpec) const
{
  return diskCacheId(makeKey(loadSpec));
}

void
CacheManager::notifyEvictedFromDisk(const std::vector<std::string>& ids)
{
  if (ids.empty()) {
    return;
  }
  std::vector<IEvictionObserver*> observers;
  {
    std::scoped_lock lock(m_mutex);
    observers = m_evictionObservers;
  }
  for (const auto& id : ids) {
    for (auto* observer : observers) {
      observer->onEvictedFromDisk(id);
    }
  }
}
```

`evictDiskIfNeeded` deliberately holds `m_mutex` for the whole eviction, so it cannot notify inline. Collect ids and notify after releasing. Change its signature to take an out-parameter and rename the locked body:

```cpp
void
CacheManager::evictDiskIfNeeded(const CacheConfig& config, std::uint64_t incomingBytes)
{
  std::vector<std::string> evictedIds;
  evictDiskIfNeededCollecting(config, incomingBytes, evictedIds);
  notifyEvictedFromDisk(evictedIds);
}
```

Rename the existing function body to `evictDiskIfNeededCollecting(const CacheConfig&, std::uint64_t, std::vector<std::string>&)` and, inside its eviction loop immediately after `m_diskEntries.erase(it);`, record the id:

```cpp
    evictedIds.push_back(aged.second);
```

`aged.second` is the map key, which is the `diskCacheId`. Declare both in the header.

Callers of `evictDiskIfNeeded` are `setConfig` (line ~303), `storeToDisk` (line ~1065) and Task 1's `reserveDiskSpace` — all outside `m_mutex`, so all three get notification for free.

- [ ] **Step 5: Handle it in `TimeSeriesLoader`**

In `renderlib/io/TimeSeriesLoader.h`, add the override beside `onEvictedFromMemory` and the map beside `m_warmOnly`:

```cpp
  void onEvictedFromDisk(const std::string& diskCacheId) override;
```

```cpp
  // Disk-cache id -> time step, for the whole current series. Built in
  // setSeries so the eviction path is O(1) and never stats a file.
  std::unordered_map<std::string, uint32_t> m_diskIdToTime;
```

Add `#include <unordered_map>`. In `renderlib/io/TimeSeriesLoader.cpp`, after `onEvictedFromMemory`:

```cpp
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
      return;
    }
    // Only a step we believed was disk-resident changes. One that is currently
    // in RAM stays RamCached: it is still displayable, and its disk copy going
    // away does not change that.
    if (m_status[static_cast<size_t>(it->second - m_minTime)] == TimepointStatus::DiskCached) {
      setStatusLocked(it->second, TimepointStatus::NotCached, changes);
      m_prefetchIdleReported = false;
    }
  }
  notifyStatusChanges(changes);
  m_wake.notify_all();
}
```

In `setSeries`, clear the map next to `m_warmOnly.clear();`:

```cpp
    m_diskIdToTime.clear();
```

and populate it in the reconciliation loop that already walks every time step (lines 99-112), inside the existing per-step block:

```cpp
      const std::string diskId = m_cache.diskCacheIdFor(spec);
      {
        std::scoped_lock lock(m_mutex);
        m_diskIdToTime[diskId] = t;
      }
```

- [ ] **Step 6: Update the second implementor in the tests**

There are **two** implementors, not one. Adding a pure virtual is a deliberate compile break so neither is missed, and the second is `RecordingEvictionObserver` in `test/test_cacheManager.cpp:759`:

```cpp
class RecordingEvictionObserver : public CacheManager::IEvictionObserver
{
public:
  void onEvictedFromMemory(const CacheKey& key) override { evicted.push_back(key.filepath); }
  void onEvictedFromDisk(const std::string& diskCacheId) override { evictedFromDisk.push_back(diskCacheId); }
  std::vector<std::string> evicted;
  std::vector<std::string> evictedFromDisk;
};
```

Then confirm there is no third:

Run: `grep -rn "IEvictionObserver" --include=*.h --include=*.cpp . | grep -v pytest_cache`

Expected: only `renderlib/CacheManager.h`, `renderlib/io/TimeSeriesLoader.h`, and `test/test_cacheManager.cpp`.

- [ ] **Step 7: Run the new test, then the suite**

Run: `cmake --build . --target agave_test --config Debug && ./agave_test "TimeSeriesLoader reverts DiskCached when the disk tier evicts"`

Expected: PASS.

Run: `./agave_test`

Expected: PASS, no regressions.

- [ ] **Step 8: Verify, do not commit**

Report build and test output; hand the diff over.

---

## Task 3: Retire `depth` and `fillCache` (§2)

**Files:**
- Modify: `renderlib/io/TimeSeriesLoader.h:76-89` (`PrefetchConfig`)
- Modify: `renderlib/io/TimeSeriesLoader.cpp:396-407` (window `steps` computation), `renderlib/io/TimeSeriesLoader.cpp:469-478` (priority 2 gate)
- Modify: `agave_app/agaveGui.cpp:226-228`, `agave_app/agaveGui.cpp:911-918`
- Modify: `agave_app/CacheSettings.h:17-22`, `agave_app/CacheSettings.cpp:106-113`, `agave_app/CacheSettings.cpp:142-144`
- Modify: `agave_app/CacheSettingsWidget.cpp:104-106` (getSettings), `agave_app/CacheSettingsWidget.cpp:89-91` (setSettings)
- Test: `test/test_timeSeriesLoader.cpp` — ~16 call sites

**Interfaces:**
- Consumes: nothing new.
- Produces: `TimeSeriesLoader::PrefetchConfig { bool enabled = true; bool wrapAround = false; uint32_t historyMargin = 4; }`. Task 4 consumes `historyMargin`.

This task deliberately keeps the memory window at its current width. It removes the flags and introduces `historyMargin` as an unused-but-honoured field; Task 4 changes the arithmetic. Splitting it this way means the ~16 test migrations and the Qt churn land separately from the behaviour change, so a reviewer can reject one without the other.

- [ ] **Step 1: Replace `PrefetchConfig`**

In `renderlib/io/TimeSeriesLoader.h`, replace the struct:

```cpp
  struct PrefetchConfig
  {
    // Asynchronously fill memory AND disk with frames from the series. With this
    // off there is no background prefetching at all, but on-demand loads driven
    // by the time slider are still cached in both tiers -- `enabled` is read only
    // by canStartPrefetchLocked, never by the interactive path.
    bool enabled = true;
    // Whether the prefetch window wraps past the end of the series back to the
    // start. Must track the playback loop setting: with looping on, the frame
    // after the last one is the first one, and if prefetch does not know that it
    // never fetches it back after the forward pass evicted it -- so looping
    // playback stalls on the final frame waiting for a frame nobody will load.
    bool wrapAround = false;
    // Slots reserved BEHIND the playhead, so a small backward scrub stays
    // instant. These are never prefetched -- prefetch is strictly forward-only.
    // The reservation just shrinks the forward window, leaving room LRU fills
    // with the frames just displayed. Not surfaced in the UI; a field rather
    // than a constant so tests can verify historyMargin == 0 works.
    uint32_t historyMargin = 4;
  };
```

- [ ] **Step 2: Drop the flags from the window computation**

In `renderlib/io/TimeSeriesLoader.cpp`, replace lines 396-407 (the comment block plus the `fillMemory`/`steps` lines) with:

```cpp
  // The MEMORY window: what we want resident in RAM. Task 4 widens this to the
  // RAM budget; for now it keeps the previous fixed width.
  std::uint64_t steps = std::min<std::uint64_t>(4, maxSteps);
```

Replace the priority 2 gate at line 478 and its comment's `fillCache` references:

```cpp
  // Priority 2: warm the rest of the series into the DISK cache. Only NotCached
  // time steps qualify -- a DiskCached one is already done, and re-fetching it is
  // exactly the endless loop this avoids. Each frame is fetched at most once, so
  // this terminates. Only worth doing when there is a disk tier to warm; with the
  // disk cache off, prefetch is the memory window above.
  if (m_cache.getConfig().enableDisk) {
```

`canStartPrefetchLocked` already gates on `m_prefetchConfig.enabled`, so no `enabled` check is needed here.

- [ ] **Step 3: Update `agave_app`**

`agave_app/agaveGui.cpp` — delete the two assignments at lines 227-228, leaving:

```cpp
  prefetch.enabled = data.prefetchEnabled;
```

At lines 911-918, delete `data.prefetchFillCache = true;`, leaving `data.prefetchEnabled = true;`.

`agave_app/CacheSettings.h` — delete the `prefetchDepth` and `prefetchFillCache` fields and their comments.

`agave_app/CacheSettings.cpp` — delete the two `doc.contains(...)` read blocks (lines 109-114) and the two `doc[...] =` writes (lines 143-144). Both reads are already `contains`-guarded, so existing settings files keep loading and the stale keys drop on the next save. No migration needed.

`agave_app/CacheSettingsWidget.cpp` — delete `data.prefetchDepth = ...` and `data.prefetchFillCache = ...` in `getSettings` and the two matching lines in `setSettings`. **Leave the widgets themselves in place for now**; Task 6 removes them. Removing the data fields without the widgets keeps this task compiling while confining the widget churn to Task 6.

- [ ] **Step 4: Migrate the test call sites**

Delete every `cfg.fillCache = ...;` line and every `cfg.depth = ...;` line in `test/test_timeSeriesLoader.cpp`. Use a scripted edit and **assert the count**:

```bash
cd /c/Users/danielt/source/repos/allen-cell-animated/agave
fc=$(grep -c 'cfg\.fillCache = ' test/test_timeSeriesLoader.cpp)
dp=$(grep -c 'cfg\.depth = ' test/test_timeSeriesLoader.cpp)
echo "fillCache=$fc depth=$dp"   # expect fillCache=13 depth=11
test "$fc" = "13" -a "$dp" = "11" || { echo "ANCHOR MISS: counts changed, inspect before deleting"; exit 1; }
sed -i '/cfg\.fillCache = /d; /cfg\.depth = /d' test/test_timeSeriesLoader.cpp
after=$(grep -c 'cfg\.fillCache\|cfg\.depth' test/test_timeSeriesLoader.cpp || true)
echo "matches after: $after"     # expect 0
test "$after" = "0" || { echo "ANCHOR MISS: references remain"; exit 1; }
```

Note `cfg.depth` appears 11 times but some of those are in cases whose *point* is the depth
behaviour. Deleting the line leaves the case asserting against the default window instead, which is
why Step 4's follow-up fixes the two cases that encode depth semantics in their names and expected
counts.

Then fix the two cases that carry semantics in their names or comments, not just a flag:

- Line ~299, `"TimeSeriesLoader prefetches forward only, up to the configured depth"` — rename to `"TimeSeriesLoader prefetches forward only"` and update its trailing comment `// Expect 10 (interactive) plus 11, 12, 13 (depth 3).` to `// Expect 10 (interactive) plus the forward window.` Adjust the assertion to the window width the code now uses rather than 3.
- Line ~336, `"TimeSeriesLoader fillCache mode prefetches to the end of the series"` — rename to `"TimeSeriesLoader prefetch fills the memory window then warms disk"`. Task 4 revisits its assertions; for now make it assert against the current window width.
- Line ~1068's comment mentioning `fillCache` — reword to attribute termination to the capacity clamp.

- [ ] **Step 5: Build and run the suite**

Run: `cmake --build . --target agave_test --config Debug && ./agave_test`

Expected: PASS. Any failure here is a test that encoded the old `depth`/`fillCache` semantics — fix the test's expectation, not the production code.

- [ ] **Step 6: Build the app to confirm the Qt side compiles**

Run: `cmake --build . --target install --config Debug`

Expected: builds. This target does not run tests; it is here only to catch `agave_app` compile breaks.

- [ ] **Step 7: Verify, do not commit**

Report both build outputs and the test result; hand the diff over.

---

## Task 4: Widen the memory window and clamp the disk warm set (§1)

**Files:**
- Modify: `renderlib/io/TimeSeriesLoader.h:155-164` (declare `diskWarmWindowLocked`)
- Modify: `renderlib/io/TimeSeriesLoader.cpp:384-444` (`prefetchWindowLocked`), `renderlib/io/TimeSeriesLoader.cpp:446-498` (`nextPrefetchTimeLocked`)
- Test: `test/test_timeSeriesLoader.cpp` (append, plus revisit the two cases renamed in Task 3)

**Interfaces:**
- Consumes: `PrefetchConfig::historyMargin` from Task 3.
- Produces:
  - `std::vector<uint32_t> TimeSeriesLoader::prefetchWindowLocked() const` — unchanged signature, now capacity-sized.
  - `std::vector<uint32_t> TimeSeriesLoader::diskWarmWindowLocked() const` — new; the clamped warm set, in priority order, excluding the memory window and the current step.

- [ ] **Step 1: Write the failing tests**

Append to `test/test_timeSeriesLoader.cpp`:

```cpp
TEST_CASE("TimeSeriesLoader memory window fills the RAM budget", "[timeSeriesLoader]")
{
  // budget 10 frames, historyMargin 4 -> 1 pinned + 4 reserved behind
  // leaves 5 forward steps.
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
  // Forward only, and no further than the budget allows.
  CHECK(loader.status(16) == TimepointStatus::NotCached);
  CHECK(loader.status(9) == TimepointStatus::NotCached);
}

TEST_CASE("TimeSeriesLoader honours historyMargin of zero", "[timeSeriesLoader]")
{
  // margin 0 -> the all-forward shape: budget - 1 forward steps.
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
  // Saturating subtraction gate. Raw `budgetFrames - 1 - historyMargin` would
  // underflow to a huge uint64, clamp to the whole series, and make prefetch
  // churn forever.
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 3));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);
  RecordingObserver observer;
  loader.addObserver(&observer);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.historyMargin = 99;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 39, 0);
  loader.requestTime(0);

  // Saturates to a single forward step, and prefetch settles rather than churning.
  REQUIRE(waitFor([&] { return observer.idleCount() > 0; }));
  CHECK(cachedCount(loader, 1, 1) == 1);
  CHECK(loader.status(5) == TimepointStatus::NotCached);
}

TEST_CASE("TimeSeriesLoader keeps history resident after playing forward", "[timeSeriesLoader]")
{
  // The margin is a reservation, not a window: nothing is ever fetched
  // backward, but frames just displayed stay resident because LRU has room.
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

  // Steps behind the playhead are still resident, and were never re-fetched.
  CHECK(cachedCount(loader, 5, 7) == 3);
  for (uint32_t t = 5; t <= 7; ++t) {
    CHECK(reader->loadCountFor(t) == 1);
  }
}

TEST_CASE("TimeSeriesLoader never fetches backward after a large jump", "[timeSeriesLoader]")
{
  CacheManager cache;
  cache.setConfig(ramConfig(frameBytes() * 10));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);
  RecordingObserver observer;
  loader.addObserver(&observer);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.historyMargin = 4;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 99, 0);
  loader.requestTime(80);
  REQUIRE(waitFor([&] { return observer.idleCount() > 0; }));

  // Nothing behind 80 was ever loaded.
  for (uint32_t t = 76; t <= 79; ++t) {
    CHECK(reader->loadCountFor(t) == 0);
  }
}

TEST_CASE("TimeSeriesLoader clamps the disk warm set to the disk budget", "[timeSeriesLoader]")
{
  // Series 40 steps. RAM holds 4 (1 pinned + 0 margin + 3 forward),
  // disk holds 12 -> 1 + 3 memory-window copies + 8 warm = 12.
  // Beyond that the tail must stay NotCached rather than churning.
  TempDir dir;
  CacheManager cache(dir.str());
  cache.setConfig(diskCacheConfig(frameBytes() * 4, frameBytes() * 12));

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);
  RecordingObserver observer;
  loader.addObserver(&observer);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.historyMargin = 0;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(makeBaseSpec(), reader, 0, 39, 0);
  loader.requestTime(0);

  REQUIRE(waitFor([&] { return observer.idleCount() > 0; }));

  // Prefetch settled, the far tail is honestly uncached, and the disk stayed
  // inside its cap.
  CHECK(warmCount(loader, 0, 39) < 40);
  CHECK(loader.status(39) == TimepointStatus::NotCached);
  cache.flushDiskWrites();
  CHECK(cache.getUsage().diskBytesUsed <= frameBytes() * 12);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build . --target agave_test --config Debug && ./agave_test "[timeSeriesLoader]"`

Expected: the six new cases FAIL — the window is still the fixed width Task 3 left, so `cachedCount(loader, 11, 15) == 5` times out.

- [ ] **Step 0: Split the prefetch gate so warm-only fetches are not RAM-throttled**

Discovered while implementing Task 2, and it must land in this task or the widening silently disables disk warming. `canStartPrefetchLocked` gates **all** prefetch on `wantedResident < budgetFrames`, counting only the pinned step and the resident memory window. Once the window is capacity-sized, `wantedResident` maxes out at `budgetFrames - historyMargin` — which is `budgetFrames` when `historyMargin == 0`, so the condition never clears and the warm pass never runs.

A warm-only fetch goes to `storeImageOnDiskOnly` and consumes no RAM, so it must not be throttled on the RAM budget. Replace the single bool with a small enum reporting what is permitted:

```cpp
  // What kind of prefetch may start right now. A warm-only fetch consumes no RAM
  // (it goes straight to the disk tier), so it must not be blocked by the RAM
  // throttle -- with a capacity-sized memory window and historyMargin 0, that
  // throttle never clears and would stop disk warming entirely.
  enum class PrefetchPermission
  {
    None,      // reader busy, prefetch disabled, or an interactive request pending
    WarmOnly,  // RAM throttle engaged; only disk-warming fetches may start
    Any,
  };
  PrefetchPermission prefetchPermissionLocked() const;
```

`prefetchPermissionLocked` keeps the existing early-outs (`!enabled`, `m_stop`, `m_interactivePending`, `!m_reader`, in-flight cap, `budgetFrames == 0`) returning `None`, and returns `WarmOnly` instead of `false` where it currently fails the `wantedResident < budgetFrames` test. `nextPrefetchTimeLocked` takes the permission and skips its priority-1 (memory window) loop unless it is `Any`. Both call sites (~line 722 and ~line 798) pass it through.

The in-flight cap still applies to both kinds, so concurrency stays bounded.

- [ ] **Step 3: Rewrite `prefetchWindowLocked`'s capacity computation**

In `renderlib/io/TimeSeriesLoader.cpp`, replace the `steps` line from Task 3 and the existing clamp block (lines ~409-429) with a single capacity computation:

```cpp
  // The MEMORY window: as many forward steps as the RAM budget holds, minus the
  // pinned current step and the history reservation.
  //
  // The clamp is not an optimization, it is what keeps prefetch live. The
  // throttle stops once the frames we want are all resident, so if we want more
  // frames than fit, that condition can never clear: prefetch either stalls
  // forever or churns, evicting one wanted frame to load another. Wrapping made
  // this acute -- a wrapped window spans the whole series, so frames behind the
  // playhead never leave the wanted set and the window stops sliding as playback
  // advances.
  std::uint64_t steps = maxSteps;
  if (m_bytesPerFrame > 0) {
    const std::uint64_t budgetFrames = m_cache.getConfig().maxRamBytes / m_bytesPerFrame;
    // Saturating, NOT `budgetFrames - 1 - historyMargin`: unsigned underflow
    // there yields a huge value that clamps to the whole series, which is
    // exactly the churn this clamp prevents. Always allow at least one so
    // playback can inch forward on a budget too small to hold even two frames.
    const std::uint64_t reserved = 1ULL + m_prefetchConfig.historyMargin;
    const std::uint64_t forwardCapacity = budgetFrames > reserved ? budgetFrames - reserved : 1;
    steps = std::min<std::uint64_t>(steps, forwardCapacity);
  }
```

Keep the existing `window.reserve` / wrap-aware emit loop below it unchanged.

- [ ] **Step 4: Add `diskWarmWindowLocked`**

Declare in `renderlib/io/TimeSeriesLoader.h` beside `prefetchWindowLocked`:

```cpp
  // The time steps we want on DISK but not in memory, in priority order,
  // starting just past the memory window. Clamped to what maxDiskBytes holds,
  // accounting for the current step and the memory window also being written to
  // disk. Empty when there is no disk tier or no room beyond the memory window.
  std::vector<uint32_t> diskWarmWindowLocked() const;
```

Implement it in `renderlib/io/TimeSeriesLoader.cpp` after `prefetchWindowLocked`:

```cpp
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
    // The memory window already covers the series.
    return window;
  }

  const std::uint64_t diskBudgetFrames = config.maxDiskBytes / m_bytesPerFrame;
  // Saturating. The current step and every memory-window step is written to disk
  // too, so the warm set is what is left of the disk budget after them.
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
```

- [ ] **Step 5: Make priority 2 iterate the warm window**

Replace the priority 2 body in `nextPrefetchTimeLocked` (the `if (m_cache.getConfig().enableDisk) { ... }` block Task 3 left) with:

```cpp
  if (m_cache.getConfig().enableDisk) {
    for (uint32_t t : diskWarmWindowLocked()) {
      if (m_status[static_cast<size_t>(t - m_minTime)] != TimepointStatus::NotCached) {
        continue;
      }
      if (m_inFlight.find(t) != m_inFlight.end()) {
        continue;
      }
      time = t;
      // Disk-only: this volume must not enter the memory tier, or warming the
      // series would evict the near time steps and paint the whole timeline as
      // in-memory as the warm pass sweeps along it.
      warmOnly = true;
      return true;
    }
  }
```

- [ ] **Step 6: Run the new tests**

Run: `cmake --build . --target agave_test --config Debug && ./agave_test "[timeSeriesLoader]"`

Expected: PASS, including the six new cases.

- [ ] **Step 7: Run the whole suite, watching the termination test**

Run: `./agave_test`

Expected: PASS. The gate is `"prefetch terminates on a series larger than memory"` (~line 1068). If it fails, a wanted frame is being recorded as in neither tier — check that Task 1's pending-aware `containsOnDisk` is in place, since that is what this widening depends on.

Also re-check the two cases renamed in Task 3 (`"prefetches forward only"`, `"prefetch fills the memory window then warms disk"`); their expected counts were written against the pre-widening width and now need the capacity-derived numbers.

- [ ] **Step 8: Verify, do not commit**

Report build and test output; hand the diff over.

---

## Task 5: Cross-session warm start (§5)

**Files:**
- Modify: `renderlib/io/TimeSeriesLoader.cpp:97-113` (`setSeries` reconciliation), `renderlib/io/TimeSeriesLoader.cpp:719-770` (the prefetch fetch site), `renderlib/io/TimeSeriesLoader.h` (add `m_warmRefused`)
- Modify: `renderlib/CacheManager.h:134-137` (stale `CacheUsage` comment), `renderlib/CacheManager.cpp:890-891` (stale `containsOnDisk` comment)
- Test: `test/test_timeSeriesLoader.cpp` (append)

**Interfaces:**
- Consumes: `CacheManager::containsOnDisk` (Task 1, pending-aware), `CacheManager::diskCacheIdFor` and `m_diskIdToTime` (Task 2), `storeImageOnDiskOnly` returning `bool` (Task 1).
- Produces: no new public API. `TimeSeriesLoader` gains private `std::set<uint32_t> m_warmRefused;`.

- [ ] **Step 1: Write the failing tests**

Append to `test/test_timeSeriesLoader.cpp`:

```cpp
TEST_CASE("TimeSeriesLoader seeds status from the disk cache on a fresh series", "[timeSeriesLoader]")
{
  // A later session must see the warm disk cache immediately, before loading
  // anything -- otherwise the strip paints blank and the warm pass re-targets
  // every already-warm step.
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
  // Regression gate. The fetch site called findImage unconditionally, and
  // findImage promotes a disk hit into memory -- so warming a series that was
  // already on disk dragged the whole timeline through RAM, evicting the near
  // steps that storeImageOnDiskOnly exists to protect.
  TempDir dir;
  CacheManager cache(dir.str());
  // RAM holds 4 frames; disk holds the whole 20-step series.
  cache.setConfig(diskCacheConfig(frameBytes() * 4, frameBytes() * 64));

  LoadSpec base = makeBaseSpec();
  for (uint32_t t = 0; t <= 19; ++t) {
    LoadSpec spec = base;
    spec.time = t;
    cache.storeImage(spec, makeImage());
  }
  cache.flushDiskWrites();
  cache.clearMemoryCache();

  auto reader = std::make_shared<CountingReader>();
  TimeSeriesLoader loader(cache);
  RecordingObserver observer;
  loader.addObserver(&observer);

  TimeSeriesLoader::PrefetchConfig cfg;
  cfg.enabled = true;
  cfg.historyMargin = 0;
  loader.setPrefetchConfig(cfg);

  loader.setSeries(base, reader, 0, 19, 0);
  loader.requestTime(0);
  REQUIRE(waitFor([&] { return observer.idleCount() > 0; }));

  // RAM never holds more than the memory window's worth, and no step outside it
  // was promoted.
  CHECK(cache.getUsage().ramBytesUsed <= frameBytes() * 4);
  CHECK(loader.status(19) == TimepointStatus::DiskCached);
  // And nothing went back to the source.
  CHECK(reader->totalLoads() == 0);
}

TEST_CASE("TimeSeriesLoader three-run cross-session scenario", "[timeSeriesLoader]")
{
  // Run 1: warm series A. Run 2: reload A with zero source loads.
  // Run 3: load unrelated series B and let LRU evict A.
  TempDir dir;
  const std::uint64_t diskCap = frameBytes() * 12;

  LoadSpec seriesA = makeBaseSpec();
  seriesA.filepath = "seriesA.tif";
  LoadSpec seriesB = makeBaseSpec();
  seriesB.filepath = "seriesB.tif";

  int loadsAfterRun1 = 0;
  {
    CacheManager cache(dir.str());
    cache.setConfig(diskCacheConfig(frameBytes() * 4, diskCap));
    auto reader = std::make_shared<CountingReader>();
    TimeSeriesLoader loader(cache);
    RecordingObserver observer;
    loader.addObserver(&observer);
    TimeSeriesLoader::PrefetchConfig cfg;
    cfg.enabled = true;
    cfg.historyMargin = 0;
    loader.setPrefetchConfig(cfg);
    loader.setSeries(seriesA, reader, 0, 7, 0);
    loader.requestTime(0);
    REQUIRE(waitFor([&] { return observer.idleCount() > 0; }));
    cache.flushDiskWrites();
    loadsAfterRun1 = reader->totalLoads();
    CHECK(cache.getUsage().diskBytesUsed <= diskCap);
  }

  {
    // Run 2: a brand new CacheManager over the same directory, as a new process
    // would have.
    CacheManager cache(dir.str());
    cache.setConfig(diskCacheConfig(frameBytes() * 4, diskCap));
    auto reader = std::make_shared<CountingReader>();
    TimeSeriesLoader loader(cache);
    TimeSeriesLoader::PrefetchConfig cfg;
    cfg.enabled = true;
    cfg.historyMargin = 0;
    loader.setPrefetchConfig(cfg);

    loader.setSeries(seriesA, reader, 0, 7, 0);
    // Seeded from disk before anything loads.
    CHECK(warmCount(loader, 0, 7) > 0);
    CHECK(reader->totalLoads() == 0);

    loader.requestTime(0);
    REQUIRE(waitFor([&] { return loader.status(0) == TimepointStatus::RamCached; }));
    // Everything warm came from disk, not the source.
    CHECK(reader->totalLoads() == 0);
    CHECK(cache.getStats().diskHits > 0);
  }

  {
    // Run 3: unrelated series B, disk already full of A.
    CacheManager cache(dir.str());
    cache.setConfig(diskCacheConfig(frameBytes() * 4, diskCap));
    auto reader = std::make_shared<CountingReader>();
    TimeSeriesLoader loader(cache);
    RecordingObserver observer;
    loader.addObserver(&observer);
    TimeSeriesLoader::PrefetchConfig cfg;
    cfg.enabled = true;
    cfg.historyMargin = 0;
    loader.setPrefetchConfig(cfg);

    loader.setSeries(seriesB, reader, 0, 7, 0);
    loader.requestTime(0);
    REQUIRE(waitFor([&] { return observer.idleCount() > 0; }));
    cache.flushDiskWrites();

    // B is held, the cap was respected, and A had to give way for it.
    CHECK(warmCount(loader, 0, 7) > 0);
    CHECK(cache.getUsage().diskBytesUsed <= diskCap);
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
  (void)loadsAfterRun1;
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build . --target agave_test --config Debug && ./agave_test "[timeSeriesLoader]"`

Expected: `"seeds status from the disk cache"` FAILS (every step reports `NotCached`), and `"warm-only prefetch does not pull volumes into RAM"` FAILS on the `ramBytesUsed` check.

- [ ] **Step 3: Seed status from disk in `setSeries`**

In `renderlib/io/TimeSeriesLoader.cpp`, replace the reconciliation loop (lines 99-112) with one that also probes disk and builds Task 2's id map in the same pass:

```cpp
  // Reconcile with whatever is already cached. This covers reopening a file
  // whose timepoints are still resident in this process, and -- via the disk
  // probe -- a later session opening a series this machine has already warmed.
  // Without the disk probe every step starts NotCached, the strip paints blank,
  // and the warm pass re-targets steps that are already on disk.
  //
  // One makeKey per time step here (which stats the source file). That is once
  // per series load, on the same order as the memory reconciliation this
  // replaces -- not the per-repaint polling TimepointStatus warns about.
  std::vector<std::pair<uint32_t, TimepointStatus>> changes;
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
  notifyStatusChanges(changes);
```

This replaces the `m_diskIdToTime` population Task 2 added to the same loop — fold them together rather than walking the series twice. Also clear `m_warmRefused` alongside `m_warmOnly.clear();` earlier in `setSeries`.

- [ ] **Step 4: Stop warm-only prefetch from promoting into RAM**

Add to `renderlib/io/TimeSeriesLoader.h` beside `m_warmOnly`:

```cpp
  // Time steps whose disk warm write was refused for lack of disk space. Skipped
  // by the warm pass so prefetch goes idle instead of retrying forever. With the
  // warm set clamped to the disk budget this should stay empty; it is a
  // defensive terminator.
  std::set<uint32_t> m_warmRefused;
```

In `renderlib/io/TimeSeriesLoader.cpp`, replace the probe at the fetch site (lines ~732-753) so a warm-only step never goes through `findImage`:

```cpp
      // Consult the whole cache before fetching from source.
      //
      // For a step inside the MEMORY window, findImage is what we want: it
      // checks RAM then disk, promotes a disk hit into memory, and counts it as
      // a disk hit. For a warm-only step we must NOT promote -- findImage would
      // drag it into RAM and evict the near steps that storeImageOnDiskOnly
      // exists to protect -- so probe disk residency instead.
      bool resident = false;
      bool alreadyWarm = false;
      if (prefetchWarmOnly) {
        alreadyWarm = m_cache.containsOnDisk(spec);
      } else {
        std::shared_ptr<ImageXYZC> cached = m_cache.findImage(spec);
        resident = cached != nullptr;
        // Release before re-locking so the volume is not held any longer than
        // the cache already holds it.
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
```

Leave the rest of that `else if (request)` branch and the trailing `else` unchanged.

- [ ] **Step 5: Honour a refused warm write**

At the prefetch completion site (~line 680), use the store's return value and record a refusal. Replace:

```cpp
      if (loaded) {
        loadedBytes = imageBytes(*image);
        if (warmOnly) {
          m_cache.storeImageOnDiskOnly(spec, image);
        } else {
          m_cache.storeImage(spec, image);
        }
      }
```

with:

```cpp
      bool warmRefused = false;
      if (loaded) {
        loadedBytes = imageBytes(*image);
        if (warmOnly) {
          // A refusal means the disk warm set will not fit. Record it so the
          // warm pass skips this step and prefetch goes idle rather than
          // retrying forever.
          warmRefused = !m_cache.storeImageOnDiskOnly(spec, image);
        } else {
          m_cache.storeImage(spec, image);
        }
      }
```

Then in the status block below (~line 702), where `warmOnly ? DiskCached : RamCached` is chosen:

```cpp
          if (warmOnly && warmRefused) {
            m_warmRefused.insert(time);
            setStatusLocked(time, TimepointStatus::NotCached, changes);
          } else {
            setStatusLocked(time, warmOnly ? TimepointStatus::DiskCached : TimepointStatus::RamCached, changes);
          }
```

And in `diskWarmWindowLocked`'s consumer — the priority 2 loop from Task 4 — skip refused steps by adding after the `NotCached` check:

```cpp
      if (m_warmRefused.count(t)) {
        continue;
      }
```

- [ ] **Step 6: Correct the two stale comments**

In `renderlib/CacheManager.h`, the `CacheUsage` comment claims `diskBytesUsed` is "only meaningful once the disk index has been built (which happens lazily on first disk access)". Replace that clause with:

```cpp
  // diskBytesUsed is meaningful once setConfig has run, which builds the disk
  // index for the configured root.
```

In `renderlib/CacheManager.cpp`, `containsOnDisk`'s comment claims the index "is built lazily and may not have been populated yet in this session". Replace with:

```cpp
  // Consult the filesystem rather than m_diskEntries. The index is built in
  // setConfig, but this must also be correct when setConfig was never called --
  // renderlib used without the GUI, and most tests. It is also what makes a
  // fresh session recognise an already-warm cache.
```

`loadDiskIndex` is called only from `setConfig` (`CacheManager.cpp:302`), immediately followed by `evictDiskIfNeeded`, and the app reaches it at startup via `main.cpp:328` then `agaveGui.cpp:109`. No behaviour change — comments only.

- [ ] **Step 7: Run the new tests**

Run: `cmake --build . --target agave_test --config Debug && ./agave_test "[timeSeriesLoader]"`

Expected: PASS, including all three new cases.

- [ ] **Step 8: Run the whole suite**

Run: `./agave_test`

Expected: PASS. Pay attention to `"TimeSeriesLoader prefetch reads back from the disk cache"` (~line 877), which asserts `diskHits >= lastTime + 1`. With a 64-frame RAM budget and a 6-step series the whole series is inside the memory window, so every step still goes through `findImage` and still counts a disk hit. If that case fails, the warm/non-warm split in Step 4 is routing memory-window steps down the warm path.

- [ ] **Step 9: Verify, do not commit**

Report build and test output; hand the diff over.

---

## Task 6: Remove the dead settings widgets (§2, Qt-side consequences)

**Files:**
- Modify: `agave_app/CacheSettingsWidget.h:33-36`, `agave_app/CacheSettingsWidget.cpp:34-46` (construction), `:58-66` (layout), `:68-78` (`updateEnabledStates`)

**Interfaces:**
- Consumes: `CacheSettingsData` without `prefetchDepth` / `prefetchFillCache` (Task 3).
- Produces: nothing. Compile-verified only — there is no automated Qt coverage, per the recorded split.

Task 3 already removed the data fields these widgets read and write, leaving them present but inert. This task deletes them.

- [ ] **Step 1: Delete the widget members**

In `agave_app/CacheSettingsWidget.h`, delete:

```cpp
  QSpinBox* m_prefetchDepth = nullptr;
  QCheckBox* m_prefetchFillCache = nullptr;
```

Keep `m_prefetchEnabled` and `m_showDetailedCacheStatus`. If `QSpinBox` is now unused in the header, leave the include — `m_ramLimitMB` and `m_diskLimitGB` are spin boxes.

- [ ] **Step 2: Delete their construction and layout rows**

In `agave_app/CacheSettingsWidget.cpp`, delete the `m_prefetchDepth` block (lines 38-42: construction, `setRange`, `setSuffix`, `setToolTip`, `setStatusTip`) and the `m_prefetchFillCache` block (lines 44-46). Delete the two layout rows:

```cpp
  layout->addRow(tr("Prefetch depth"), m_prefetchDepth);
  layout->addRow(m_prefetchFillCache);
```

- [ ] **Step 3: Delete `updateEnabledStates`**

Its only purpose was greying out the two departing controls. Delete the lambda, both `connect` calls, and the initial invocation (lines 68-78), along with the comment above it about keeping enabled states honest. `m_showDetailedCacheStatus` is independent and needs no gating.

**Assert nothing else referenced them** before building:

```bash
cd /c/Users/danielt/source/repos/allen-cell-animated/agave
grep -rn "m_prefetchDepth\|m_prefetchFillCache\|updateEnabledStates\|prefetchDepth\|prefetchFillCache" agave_app/ renderlib/ test/ || echo "clean"
```

Expected: `clean`. Any hit is a reference Task 3 or this task missed.

- [ ] **Step 4: Relabel the LoadDialog checkbox**

The LoadDialog checkbox reads "Prefetch whole time series", but the warm set is now clamped to the disk budget rather than covering the whole series. Find and relabel it:

```bash
grep -rn "whole time series" agave_app/
```

Change the user-visible string to `"Prefetch time series"`, and update its tooltip/status tip if they also promise the whole series. Leave `getPrefetchWholeTimeSeries()` alone — renaming the accessor is churn beyond this plan's scope.

- [ ] **Step 5: Build the app**

Run: `cmake --build . --target install --config Debug`

Expected: builds clean, no unused-member or unused-variable warnings for the deleted widgets.

- [ ] **Step 6: Run the suite once more**

Run: `cmake --build . --target agave_test --config Debug && ./agave_test`

Expected: PASS. Confirms the Qt removal did not disturb `renderlib`.

- [ ] **Step 7: Manual verification against the acceptance scenario**

Automated coverage cannot exercise the real Qt layer. Launch AGAVE and walk §5's acceptance scenario:

1. Open the Cache Settings dock. Confirm the Prefetch group shows **only** "Prefetch time steps" — no depth spin box, no "Fill available cache".
2. Load a time series with prefetch on. Watch the time slider strip: solid near the playhead, dimmer beyond, blank past the disk budget. Statistics dock → Cache → "Disk Writes Dropped" must stay **0**.
3. Drag the slider to a distant time step. The strip's solid band should re-aim to the new position without the UI blocking.
4. Quit once prefetch has gone idle. Relaunch, load the same series. The strip should show the warm series **immediately** on load, and the Statistics dock should report disk hits rather than misses.
5. Quit, relaunch, load a different large series. The disk cache should fill with the new series; disk usage must stay at or under the configured limit.

Report what you observe at each step, including anything that contradicts the expectation.

- [ ] **Step 8: Verify, do not commit**

Report all build output, the full test result, and the manual observations. Hand the diff over for review.

---

## Self-Review

**Spec coverage:**

| Spec section | Task |
| --- | --- |
| §1 memory window capacity-sized | 4 (Step 3) |
| §1 `diskWarmWindowLocked` | 4 (Steps 4-5) |
| §1 history as reservation | 4 (Step 3 `reserved`; tests Step 1) |
| §1 wrapping / re-aiming | 4 (Step 4 honours `wrapAround`); `requestTime` needs no change |
| §2 `depth` / `fillCache` removed, `historyMargin` added | 3 |
| §2 `enabled` semantics (off still caches) | 3 (Step 1 comment); already true, tested in Task 1's suite run |
| §2 Qt consequences | 3 (Step 3, data), 6 (widgets) |
| §3 disk eviction observer | 2 |
| §4 reservation, never drop, pending-aware probe, depth 8, shutdown unchanged | 1 |
| §4 refusal handling | 5 (Step 5) |
| §4 drive-by cleanup of `storeImage` | 1 (Step 7) |
| §5a status seeding | 5 (Step 3) |
| §5b warm-only must not promote | 5 (Step 4) |
| §5c stale comments | 5 (Step 6) |
| §5d LRU caveat | Out of scope by decision; no task |
| Acceptance scenario runs 1-3 | 5 (Step 1, three-run test) + 6 (Step 7, manual) |
| Boundary: `historyMargin = 0` | 4 (Step 1) |
| Boundary: margin ≥ budget | 4 (Step 1) |
| Boundary: `enableDisk == false` | 4 (`diskWarmWindowLocked` early return); covered by existing RAM-only cases |
| Boundary: series larger than RAM + disk | 4 (Step 1, clamp test) |

**Type consistency:** `storeImage` / `storeImageOnDiskOnly` / `storeImageInternal` / `enqueueDiskWrite` all return `bool` from Task 1 and are consumed as `bool` in Task 5. `onEvictedFromDisk(const std::string&)` is declared in Task 2 and matched by the `TimeSeriesLoader` override in the same task. `diskCacheIdFor` is introduced in Task 2 and reused in Task 5's seeding loop. `diskWarmWindowLocked()` is declared and defined in Task 4 and consumed by Task 5's refusal skip. `m_diskIdToTime` is added in Task 2 and its population is folded into Task 5's rewritten loop — Task 5 Step 3 says so explicitly so the two do not both walk the series.

**Known risks flagged in-plan rather than resolved:**

- Task 2's pure-virtual addition is an intentional compile break. Verified: there are **two** implementors — `TimeSeriesLoader` and `RecordingEvictionObserver` in `test/test_cacheManager.cpp:759`. Step 6 updates the second and re-greps for a third.
- Task 3's test migration counts are asserted by the script, not assumed: 13 `cfg.fillCache` lines and 11 `cfg.depth` lines. The script aborts if either differs.
- Task 5 rewrites the `setSeries` loop that Task 2 also touches. Task 5 Step 3 states this explicitly so the series is walked once, not twice. If the tasks are executed by separate agents, Task 5's agent must read Task 2's version of the loop first.
- Task 4 Step 7 warns that two tests renamed in Task 3 carry pre-widening expected counts and will need the capacity-derived numbers.
