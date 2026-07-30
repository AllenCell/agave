# Time-Series Prefetch — Implementation Status

Companion to `timeseries-prefetch-plan.md`. Branch: `feature/timeseries-loading`.

## Build recipe on this machine (verified)

```
# VS 18 2026 x64 environment, build in D:\agave_build (Ninja Multi-Config)
cd D:\agave_build
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cmake --build . --target install'
```

**Gotcha found:** `--target install` does **not** build or run `agave_test` — the test target is not in
the install dependency graph. Also, `D:\agave_build` was configured with `AGAVE_BUILD_TESTS:BOOL=OFF`
(the project default is ON), so the target did not exist at all.

Tests were enabled in that build dir with:
```
cmake -DAGAVE_BUILD_TESTS=ON .
cmake --build . --target agave_test --config Debug     # runs the suite as a POST_BUILD step
```
To revert: `cmake -DAGAVE_BUILD_TESTS=OFF .`

Run a subset directly:
```
cd <repo>\test && D:\agave_build\Debug\agave_test.exe "[loadRequest]"
```

Formatting — run clang-format on every added/modified C++ file before committing:
```
& "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe" -i -style=file <files>
```

**Watch out for line endings.** `core.autocrlf=true` and no `.gitattributes`, and some tracked files
(e.g. `renderlib/CMakeLists.txt`) have mixed CRLF/LF. An editing tool that normalizes a whole file
produces a diff full of invisible whitespace changes. After editing, check
`git diff --numstat` against `git diff -w --numstat` per file; if they disagree and it is not explained
by intentional re-indentation, restore the file and re-apply the edit preserving byte patterns.

## Phase 1 — COMPLETE and verified

Full suite green: **802 assertions in 30 test cases**. New `[loadRequest]` tag: 38 assertions, 9 cases.

### Files added
| File | Purpose |
|---|---|
| `renderlib/io/LoadRequest.h` | `LoadProgress` (atomic cancel + progress), abstract `LoadRequest`, concrete `FutureLoadRequest` |
| `renderlib/io/LoadRequest.cpp` | `FutureLoadRequest` impl |
| `renderlib/io/BlockingFileReader.{h,cpp}` | Base implementing `submitLoad` on a worker thread for blocking readers |
| `renderlib/IFileReader.cpp` | Non-virtual `loadFromFile` convenience (`submitLoad(...)->take()`) |
| `test/test_loadRequest.cpp` | Fake reader; submit/await/cancel/destroy/concurrency/progress |

### Files modified
- `renderlib/IFileReader.h` — added `submitLoad` (pure virtual) and `maxConcurrentLoads()` (default 1);
  `loadFromFile` is now a **non-virtual** convenience declared here, defined in `IFileReader.cpp`.
  Forward-declares `class LoadRequest;` to avoid an include cycle with `LoadRequest.h`.
- All 5 readers (`FileReaderZarr`, `FileReaderTIFF`, `FileReaderCzi`, `FileReaderCCP4`,
  `FileReaderImageSequence`) now derive from `BlockingFileReader` and implement
  `loadVolumeBlocking(const LoadSpec&, LoadProgress&)` instead of overriding `loadFromFile`.
- Cancellation polls added: Zarr per channel; TIFF and CZI per Z plane (finest available granularity).
  Progress reported per channel in Zarr, TIFF, CZI.
- `FileReaderImageSequence::loadVolumeBlocking` now forwards to `m_tiffReader->loadVolumeBlocking`
  rather than `loadFromFile`, so it stays on its worker thread instead of nesting an async load inside
  another, and propagates the same cancellation state. Also added a missing bounds check on
  `loadSpec.time` against `m_sequence.size()`.
- CMake: `renderlib/CMakeLists.txt` (+`IFileReader.cpp`), `renderlib/io/CMakeLists.txt`
  (+`BlockingFileReader.*`, +`LoadRequest.*`), `test/CMakeLists.txt` (+`test_loadRequest.cpp`).

### Deviation from the plan, with reason

The plan said to use `threading.h`'s `Tasks` pool. **`Tasks` is not usable as written:**
1. `Tasks::queue` is a template *defined in `threading.cpp`*, so it cannot be instantiated from any
   other translation unit — it would fail to link.
2. It stores `std::packaged_task<R()>` into a `std::deque<std::packaged_task<bool()>>`. The header
   comment claims a `packaged_task<void>` can hold a `packaged_task<R>`; that is false. It only
   compiles for `R = bool`.

`BlockingFileReader::submitLoad` uses `std::async(std::launch::async, ...)` instead. The caller bounds
in-flight loads via `maxConcurrentLoads()`, so thread count is bounded, and one thread launch is
negligible against a volume read. `threading.h` was left untouched; fixing it is a separate concern.
This is documented in a comment in `BlockingFileReader.cpp`.

### Design notes worth remembering
- `FutureLoadRequest::~FutureLoadRequest` **cancels then waits**. A bare `std::future` destructor from
  `std::async` blocks unboundedly; cancelling first bounds the wait by the reader's cancel-poll interval.
  There is a test for this (`"destroying an in-flight request cancels instead of hanging"`).
- `take()` is idempotent and caches its result; `isReady()` returns true once taken.
- `maxConcurrentLoads()` is 1 for every reader right now. Deliberately not tuned — phase 5 raises it
  with a real consumer and measurements. `BlockingFileReader::setMaxConcurrentLoads()` (protected)
  clamps to >= 1.
- Verified from libCZI headers: `ICZIReader` is documented thread-safe for concurrent calls
  (`libCZI.h:745`) and `IStream` implementations must support concurrent `Read` (`libCZI.h:236`), so
  raising CZI's concurrency later is safe.

## Phase 2 — COMPLETE and verified

Full suite green (802 assertions / 30 cases) and `--target install` builds the app cleanly.

### Zarr (`FileReaderZarr.{h,cpp}`)
- `loadMultiscaleDims` now memoizes per `"<filepath>|<scene>"` in `m_multiscaleDims`; the real parse
  moved to a new private `readMultiscaleDims`. This removes **N `tensorstore::Open` calls per timepoint**
  (one per multiscale level, previously repeated on every single load). Empty results are cached too,
  since a failed parse fails identically every time.
- `jsonRead` and the dims memo are guarded by `m_metadataMutex`, a **recursive** mutex — necessary
  because `loadMultiscaleDims` and `getChannelNames` both call `jsonRead`, which also locks.
- The lazy `m_store` open is guarded by a separate `m_storeMutex`, so a load never holds the metadata
  lock across a store open. Documented rule: never take `m_metadataMutex` while holding `m_storeMutex`.

### CZI (`FileReaderCzi.{h,cpp}`)
- Added `openReader()`: opens the file **once** and keeps `m_reader` for the reader's lifetime, handling
  a path change defensively. Previously every timepoint constructed a `ScopedCziReader`, which reopened
  the file and re-parsed the subblock directory.
- Added `cachedDimensions()`: memoizes `VolumeDimensions` per scene, so the full metadata XML is parsed
  through pugixml **once** instead of per timepoint. Returns `bool` with an out-param, mirroring
  `readCziDimensions`, so callers keep their exact original failure criterion (an earlier draft used
  `dims.validate()` as a proxy, which would have subtly changed behavior).
- `loadNumScenes`, `loadDimensions` and `loadVolumeBlocking` all now use the shared reader.
- Deleted `ScopedCziReader` — orphaned by the above.
- libCZI headers stay out of `FileReaderCzi.h` via a forward declaration of `libCZI::ICZIReader`; the
  destructor is defined in the .cpp so `shared_ptr` to an incomplete type is fine.

### Deviation from the plan, with reason

The plan said to also enable `libCZI::CreateSubBlockCache()`. **Deliberately not done.** A subblock
cache only pays off when the same subblock is read more than once, but each timepoint reads an entirely
distinct set of subblocks, and within one load each plane is read exactly once. It would add memory
pressure and a pruning policy for no reuse — and whole-timepoint caching is already `CacheManager`'s job.
Revisit only if a real access pattern shows repeated subblock reads.

## Phase 0 — COMPLETE and verified

Full suite green: **818 assertions in 31 test cases**. App builds via `--target install`.

- `CacheManager` gained `getUsage()` (returning a `CacheUsage` struct: ram/disk bytes used, the
  corresponding limits, and entry counts for both tiers), plus `getRamBytesUsed()` and
  `getRamBytesAvailable()`. The latter clamps at zero rather than underflowing — prefetch throttling in
  phase 5 depends on that, and there is a test for it.
- New `renderlib/CacheStatusReport.{h,cpp}` with a single free function `reportCacheStatistics(CStatus*)`
  that publishes a **"Cache"** group: Enabled, Memory used/limit, Memory Entries, Disk used/limit,
  Disk Entries, Hit Rate %, Memory Hits, Disk Hits, Disk Writes, Misses. Byte formatting reuses the
  existing `LoadSpec::bytesToStringLabel`. `CacheManager::getStats()` finally has a consumer.
- Wired into all four renderers' existing statistics blocks: `RenderVk`, `RenderVkPT`, `RenderGL`,
  `RenderGLPT`. `m_status` is a `std::shared_ptr<CStatus>`, so the call sites pass `.get()`.
- 5 new sections in `test_cacheManager.cpp` covering empty usage, usage tracking stores, clamping at the
  limit, clearing, and the disabled-disk case.

### Notes
- **Why not a single call site:** `CStatus::SetPostRenderFrame()` and `SetRenderEnd()` looked like the
  natural single hook, but **nothing in the codebase calls either of them** — they are dead. The four
  per-frame statistics blocks are the only live emit points, matching how every other statistic is
  reported.
- **Threading:** `reportCacheStatistics` must only be called from the render/GUI thread. `CStatus`
  notifies observers synchronously and in the GUI those are Qt widgets, so calling it from the phase-5
  loader thread would touch Qt off-thread. This is documented in the header.
- Reporting happens per frame, which re-locks the CacheManager mutex each frame. Uncontended and cheap,
  and consistent with the existing per-frame timing statistics. Revisit only if it shows up in a profile.
- Visual confirmation of the dock is a manual step (View > Statistics); it was not automatically
  verified, since the test suite links renderlib only and has no Qt.

## Phase 4 — PARTIALLY COMPLETE

Suite green: **835 assertions in 33 test cases**. App builds via `--target install`.

### Done: pinning + eviction observer
- `CacheManager::pin(LoadSpec)` / `unpin()` / `isPinned()`, refcounted so nested pins are safe.
  **Pins are keyed, not entry-based** — held in a separate `m_pinned` map rather than as a field on
  `CacheEntry`, so pinning a key that is not resident yet still protects it once stored. That removes a
  race between storing a timepoint and pinning it.
- `evictIfNeededLocked` rewritten to walk LRU→MRU skipping pinned entries. **If everything resident is
  pinned it stops and lets the tier sit over its limit**, rather than dropping data in use — overshooting
  is recoverable, evicting the displayed timepoint is not. There is a test asserting exactly this.
  It also now cleans up stale LRU keys that have no matching entry.
- `CacheManager::IEvictionObserver` + `addEvictionObserver`/`removeEvictionObserver`. Notifications are
  delivered **with no lock held**: `evictIfNeededLocked` appends dropped keys to a caller-supplied
  vector, and the caller calls `notifyEvicted()` after releasing the mutex. The observer list is copied
  under the lock before notifying, so an observer may call back into the cache.
- `setConfig` now trims the tier when `maxRamBytes` is lowered (it already called the evict helper; it
  now reports what it dropped). `clearMemoryCache` notifies for every dropped entry and deliberately
  drops pinned entries too — it is an explicit "drop everything", not eviction under pressure — while
  leaving pin refcounts intact so a holder still protects the entry once it is reloaded.
- 8 new test sections covering survive-under-pressure, unpin, refcounting, pin-before-store,
  the over-limit-when-all-pinned case, eviction notification, clear notification, and observer removal.

### Not done: async disk-write queue
`storeImage` still calls `storeToDisk` inline, so a cache-cold prefetch pays a full-volume disk write on
the loader thread. Splitting that onto a low-priority writer thread (bounded queue, drop-oldest under
pressure, drain on shutdown) is the remaining Phase 4 item. Not required for Phase 5 to function — it is
a playback-smoothness optimization — but it should land before prefetch is enabled by default.

## Phase 5 — COMPLETE and verified

Suite green: **920 assertions in 50 test cases**, and the `[timeSeriesLoader]` tag was run 5 times
consecutively with identical results to check for flakiness (these are concurrency tests). App builds.

New `renderlib/io/TimeSeriesLoader.{h,cpp}` plus `test/test_timeSeriesLoader.cpp` (17 cases).

- One loader thread, one reused `IFileReader` per series. Concurrency within a load comes from the
  reader via `maxConcurrentLoads()`, not from more loader threads.
- Interactive requests preempt prefetch, and are themselves **preemptible**: a newer scrub cancels an
  in-flight interactive load rather than making the user wait for a frame they have already left.
- `requestTime` returns a monotonic sequence number so the GUI can discard stale completions.
- Forward-only prefetch, `depth` or `fillCache`. Throttles on
  `CacheManager::getRamBytesAvailable()` **counting in-flight bytes as reserved headroom**, so N
  in-flight full-volume buffers cannot silently overshoot the budget. Per-frame size is measured from
  the first completed load rather than estimated; until it is known, only one prefetch runs.
- Pins the displayed timepoint (new pin taken before the old is released) so prefetch can never evict it.
- Per-timepoint status vector (`NotCached`/`Queued`/`Loading`/`RamCached`/`Failed`) for the slider
  indicator, kept current by `CacheManager::IEvictionObserver`. Deliberately **not** backed by cache
  queries: building a `CacheKey` stats the file, so polling per repaint would be a stat storm.
- Also added `CacheManager::containsInMemory()` — a residency probe that, unlike `findImage`, does not
  count as a hit/miss or touch LRU order, so prefetch bookkeeping does not distort the statistics.
- `CacheManager` is **injected** into the constructor (defaulting to the singleton) so tests use
  isolated caches, matching how `test_cacheManager.cpp` already avoids the singleton.

### Three real bugs found by the tests, all fixed

1. **Adopting a cancelled request.** Scrub away (cancelling a prefetch), then scrub back before the
   loader reaped it: the loader adopted the doomed request and reported a spurious `Failed` with no image
   displayed. Now it checks `isCancelled()` and starts a fresh load. Regression test added.
2. **Over-eager cancel on scrub.** `requestTime` cancelled *every* other in-flight prefetch, including
   ones still inside the new prefetch window — so every scrub threw away work it was about to re-request.
   Now only prefetches outside `[newTime, newTime + depth]` are cancelled. Regression test added.
3. **Idle loader ignored config changes.** The idle `wait` predicate only covered new interactive work,
   so enabling prefetch, raising the depth, or an eviction freeing room left the loader parked until the
   next scrub. In the app, toggling the prefetch setting would have appeared to do nothing. The predicate
   now includes `canStartPrefetchLocked() && nextPrefetchTimeLocked()`.

### Testing note
Prefetch starts as soon as `setSeries` is called, so a test that sets the series at t=0 and then requests
a different timepoint is racing its own prefetch. Three tests were rewritten to sequence this explicitly
(prefetch disabled → interactive load → enable prefetch → wait for `Loading`) instead of assuming an
ordering. Worth remembering when adding tests here.

## Next up

- **Phase 0** — observability: surface `CacheManager::getStats()` (still has no consumer) plus a
  "Memory" group in the Statistics dock. Needs new `CacheManager` accessors for ram/disk bytes used.
- **Phase 2** — reader efficiency: Zarr `loadMultiscaleDims` memoization + mutex on lazy
  `m_store`/`m_zattrs` init; CZI `ICZIReader` hoisted to a member + `libCZI::CreateSubBlockCache()`.
- Then phases 3–12 per the plan.

Nothing is committed yet beyond the plan itself — the Phase 1 changes are uncommitted working-tree edits.
