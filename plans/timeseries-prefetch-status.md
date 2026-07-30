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

## Next up

- **Phase 0** — observability: surface `CacheManager::getStats()` (still has no consumer) plus a
  "Memory" group in the Statistics dock. Needs new `CacheManager` accessors for ram/disk bytes used.
- **Phase 2** — reader efficiency: Zarr `loadMultiscaleDims` memoization + mutex on lazy
  `m_store`/`m_zattrs` init; CZI `ICZIReader` hoisted to a member + `libCZI::CreateSubBlockCache()`.
- Then phases 3–12 per the plan.

Nothing is committed yet beyond the plan itself — the Phase 1 changes are uncommitted working-tree edits.
