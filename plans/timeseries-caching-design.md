# Time-series caching behaviour — settled design

Supersedes the "one remaining gap" section of `timeseries-caching-resume.md`. Companion to
`timeseries-prefetch-plan.md` (original 12-phase plan) and `timeseries-prefetch-status.md`
(what got built).

Branch: `feature/timeseries-loading`. Decided 2026-08-01.

**Working agreement: do not `git commit`.** Make the change, build it, run the suite, then hand the
diff over for review.

---

## The behaviour, in one paragraph

Memory holds a forward run of time steps starting at the playhead — as many as the RAM budget
holds, not a fixed small lookahead. Disk holds the run beyond that, again as many as the *disk*
budget holds. Anything past both is honestly uncached. A few slots behind the playhead are
reserved so small backward scrubs stay instant, but they are never prefetched — they fill with
frames you already displayed. Moving the time slider re-aims both runs asynchronously. Prefetch
terminates: once both runs are satisfied it goes idle and stays idle.

```
series 500 time steps, RAM budget 38 frames, disk budget 200 frames, historyMargin 4

  t-4 t-3 t-2 t-1  [t]  t+1 ... t+33   t+34 ... t+199   t+200 ...
   H   H   H   H    P    M   ...  M      D   ...   D        -

P = pinned current    M = memory (solid on the strip)
H = history, retained but never fetched      D = disk only (dim)
-  = not cached (blank)
```

The disk tier holds 200 frames *in total* — the pinned current frame and the 33 memory-window frames
are written to disk too (§4), so the warm set beyond them is 200 - 34 = 166 frames, reaching t+199.
That is why `diskReserved` subtracts `1 + forwardSteps`.

## Decisions

Section numbers in parentheses; the six decisions do not map one-to-one onto the five sections.

- **Window shape** — forward run plus a small history *reservation*. (§1)
- **History policy** — retain-only. Prefetch stays strictly forward-only, as originally recorded. (§1)
- **Config** — `PrefetchConfig::depth` **and** `fillCache` are both retired. `enabled` alone means
  "asynchronously fill memory and disk"; with it off, on-demand slider loads are still cached in
  both tiers. `historyMargin` (new, default 4, not user-facing) sizes the reservation. (§2)
- **Disk warm set is clamped to fit `maxDiskBytes`**, and the disk tier gains an eviction
  observer. (§1, §3)
- **Disk writes reserve space at enqueue time and are never dropped** — but the backlog is still
  abandoned on quit, so the queue stays shallow. (§4)
- **A fresh session recognises the warm disk cache without dragging it through RAM** — `setSeries`
  seeds status from the disk tier, and a warm-only prefetch probes disk instead of `findImage`. (§5)

---

## 1. Two clamped forward-only windows, both re-aimed on slider move

Everything derives from two frame budgets computed the same way. `bytesPerFrame` is zero until the
first load completes, in which case both windows fall back to their existing minimum (one step) and
widen once it is known — that is pre-existing behaviour and is retained.

```cpp
budgetFrames     = maxRamBytes  / bytesPerFrame;
diskBudgetFrames = maxDiskBytes / bytesPerFrame;

// Saturating, NOT raw subtraction. See "Boundary cases" below -- underflow here
// makes the window the whole series on a small budget, which is the exact churn
// the clamp exists to prevent.
reserved        = 1 + historyMargin;                                    // pinned current + history
forwardCapacity = budgetFrames > reserved ? budgetFrames - reserved : 1;
forwardSteps    = min(forwardCapacity, span - 1);

diskReserved  = 1 + forwardSteps;                                       // current + memory window
diskCapacity  = diskBudgetFrames > diskReserved ? diskBudgetFrames - diskReserved : 0;
diskWarmSteps = min(diskCapacity, span - 1 - forwardSteps);
```

### Memory window — `prefetchWindowLocked()`

`t+1 .. t+forwardSteps`. The function keeps its exact current shape and stays the **single source of
truth** shared by `canStartPrefetchLocked`, `nextPrefetchTimeLocked` and `requestTime`'s cancel
check. Only the `steps` computation changes.

Removed from it:

- the `fillMemory = fillCache && !enableDisk` special case, and with it the coupling between the
  memory window size and `enableDisk`;
- the `min(depth, maxSteps)` branch.

`canStartPrefetchLocked` is **not** changed. It throttles on `wantedResident < budgetFrames`. With
`historyMargin > 0` the wanted set tops out at `budgetFrames - historyMargin`, so the throttle can
never bind and is purely a backstop. At `historyMargin = 0` the wanted set can reach exactly
`budgetFrames` and the throttle does bind — but only at the moment every wanted frame is already
resident, which is when `nextPrefetchTimeLocked` returns false anyway. Either way the condition still
clears as the playhead advances and frames fall out behind it, so prefetch stays live at any margin.

### Disk warm set — `diskWarmWindowLocked()` (new)

`t+forwardSteps+1 .. t+forwardSteps+diskWarmSteps`. Replaces priority 2's unbounded sweep over the
whole span in `nextPrefetchTimeLocked`. Its gate becomes `enableDisk && diskWarmSteps > 0` — the
`m_prefetchConfig.fillCache` term goes away, and `canStartPrefetchLocked` already covers `enabled`.

Subtracting `forwardSteps` in `diskReserved` accounts for the memory-window frames also landing on
disk, since `storeImage` queues a disk write for every frame it caches (see §4).

### History — a reservation, not a window

`t-historyMargin .. t-1` is never enumerated and never fetched. It exists solely as the
`+ historyMargin` term in `reserved`. Because the wanted set tops out at `budgetFrames -
historyMargin`, LRU has that many slots left over, and it fills them with the most recently touched
entries — which are the frames just displayed. After a large slider jump the history is empty and
refills as playback advances forward. This is what keeps decision 2 true: no backward fetch, no new
priority tier, nothing added to the shared window.

### Wrapping and re-aiming

Both windows honour `PrefetchConfig::wrapAround`, so looping playback warms across the seam.
`requestTime` already moves `m_currentTime`, re-derives the window and cancels only in-flight loads
that fall outside it; it picks up the disk warm set through the same call.

## 2. `PrefetchConfig` collapses to one meaningful switch

| Field | Meaning after this change |
| --- | --- |
| `enabled` | Asynchronously fill memory **and** disk with frames from the series. |
| `wrapAround` | unchanged; tracks the playback loop setting |
| `historyMargin` | **new**, default 4. Slots reserved behind the playhead. |
| ~~`depth`~~ | **removed** |
| ~~`fillCache`~~ | **removed** |

```cpp
struct PrefetchConfig
{
  bool enabled = true;
  bool wrapAround = false;
  uint32_t historyMargin = 4;
};
```

### What `enabled` means, precisely

- **On** — both windows are filled asynchronously: memory gets the forward run, disk gets the warm
  set beyond it.
- **Off** — no asynchronous prefetching at all, but on-demand loads driven by the time slider are
  **still cached** in memory and on disk exactly as before.

The "off" half already holds and requires no code change. `enabled` is read in exactly one place,
`canStartPrefetchLocked` (`TimeSeriesLoader.cpp:334`). The interactive path is independent of it:
`findImage` (line 591) checks RAM then disk and promotes a disk hit into RAM, and `storeImage`
(line 614) writes through to both tiers.

### Why `fillCache` is obsolete

It survived only because the memory window used to be `depth`-sized, so a second flag was needed to
mean "actually use the budget". Now the memory window is always capacity-sized and the disk warm set
is always clamped to `maxDiskBytes`, so both tiers are bounded by their own budgets. The knob that
limits how much gets warmed is the **disk cache size limit**, which is the honest place for it.

Note this is not the old `fillCache = true` behaviour under a new name: that meant "sweep the entire
series", which is what churned. The warm set is clamped, so `enabled` warms a bounded neighbourhood.

### `depth` vs `historyMargin`

`historyMargin` is a `PrefetchConfig` field rather than a `constexpr` specifically so tests can vary
it — "works at 0" is otherwise unverifiable. It deliberately gets **no UI control and no JSON
persistence**; changing the default is a one-line edit, and a settings control can be added later if
wanted. It is not a rename of `depth`: `depth` capped lookahead *ahead* of `t`, `historyMargin`
reserves slots *behind* it.

### Qt-side consequences

Both retired flags are surfaced in the **Cache Settings dock**, not LoadDialog — the earlier draft of
this spec had that wrong.

- `CacheSettingsWidget`: the Prefetch group drops from three controls to one. Remove
  `m_prefetchDepth` (spin box, `Prefetch depth` row) and `m_prefetchFillCache` (`Fill available
  cache` checkbox), keeping only `m_prefetchEnabled` ("Prefetch time steps"). The
  `updateEnabledStates` lambda and both of its `connect` calls (lines 71-78) exist solely to grey out
  those two controls and go with them; `m_showDetailedCacheStatus` is independent.
- `CacheSettingsData`: drop `prefetchDepth` and `prefetchFillCache`.
- `CacheSettings` JSON: stop writing `prefetchDepth` / `prefetchFillCache`. Both reads are already
  guarded by `doc.contains`, so existing settings files load fine and the stale keys drop on the next
  save. No migration needed.
- `agaveGui.cpp:911-918`: LoadDialog's "Prefetch whole time series" checkbox sets both flags; it now
  sets `prefetchEnabled = true` only. Relabel the checkbox to drop "whole", since the warm set is
  clamped to the disk budget rather than covering the entire series.
- `agaveGui.cpp:226-228`: drop the `depth` and `fillCache` assignments.

Per the recorded split, this stays widgets-and-wiring only — no policy moves into Qt.

## 3. Disk tier eviction observer

`evictDiskIfNeeded` (`CacheManager.cpp:1225`) deletes disk entries with nobody watching; only the RAM
tier notifies, via `notifyEvicted`. Consequence today: a frame marked `DiskCached` whose file is
later evicted stays marked `DiskCached` forever. Prefetch believes it is finished, the slider paints a
solid strip that is a lie, and playback silently falls back to source loads.

Add `onEvictedFromDisk` alongside the existing `onEvictedFromMemory` on
`CacheManager::IEvictionObserver`, notified after the lock is released (same contract as the memory
path). `TimeSeriesLoader` reverts `DiskCached → NotCached`, which repaints the strip and lets prefetch
re-warm if the frame is inside a window.

**It cannot take a `CacheKey`,** unlike the memory callback. The disk index is
`unordered_map<std::string, DiskEntry>` keyed by `diskCacheId`, and `DiskEntry` holds only
`{path, bytes, lastAccess}`. `meta.json` persists the key solely as the opaque `keyToString(key)`
string, and `loadDiskIndex` reads only `lastAccess` and `bytes` — so an entry evicted after a fresh
start has no `CacheKey` to hand back, and `keyToString` is a concatenation, not a reversible encoding.

So the signature is:

```cpp
virtual void onEvictedFromDisk(const std::string& diskCacheId) = 0;
```

and `CacheManager` exposes the mapping the observer needs:

```cpp
std::string diskCacheIdFor(const LoadSpec& loadSpec) const;   // public wrapper over diskCacheId(makeKey(...))
```

`TimeSeriesLoader` keeps an `std::unordered_map<std::string, uint32_t>` from disk id to time step,
built in the `setSeries` reconciliation loop that §5a already adds — so it costs no extra `makeKey`
calls, and eviction lookup is O(1) with no file stat on the eviction path. Cleared and rebuilt on
`setSeries`.

Rejected alternatives: storing a `CacheKey` in `DiskEntry` needs structured key fields added to
`meta.json` plus a fallback for entries written by older builds; and notifying without an identity
would force an O(span) re-probe per eviction, in bursts, during warming.

**Residual risk, accepted:** if other long-lived cached data shares the disk budget, a warm-set
member can still be evicted under us and re-fetched. Bounded rather than endless, because the warm
set is sized to fit — but it is a churn source worth watching on the Statistics dock.

## 4. Disk writes: reserve space at enqueue, never drop

### How writing works today

`storeImageInternal` (`CacheManager.cpp:411`) is write-through **at load time**:

```cpp
if (intoMemory) { storeImageInMemory(key, image); }
if (enabled && enableDisk && maxDiskBytes > 0 && !cacheDir.empty()) {
  enqueueDiskWrite(PendingDiskWrite{ key, image, config, cacheDir });
}
```

`storeImage` and `storeImageOnDiskOnly` are this function with `intoMemory` flipped. So every
memory-cached time step is queued for disk the moment it loads. RAM eviction is a **pure drop** —
`storeImageInMemory` → `evictIfNeededLocked` never writes on the way out, because the write already
happened. There is no second chance, which is exactly why the eviction-timing race exists and why a
dropped write loses that frame from disk permanently.

### The invariant to establish

*On-disk bytes plus bytes for writes still in flight never exceed `maxDiskBytes`.*

Today's accounting cannot give this: `evictDiskIfNeeded` runs inside `storeToDisk` and knows nothing
about the other entries queued behind it.

### Changes

Add `m_pendingDiskBytes`, guarded by `m_mutex` (the accounting lock), distinct from
`m_diskQueueMutex` (the queue lock).

`enqueueDiskWrite` returns `bool`:

1. `bytes = estimateImageBytes(*image)`.
2. Evict against `m_currentDiskBytes + m_pendingDiskBytes + bytes` vs `maxDiskBytes`, so eviction
   makes room for the **whole queue**, not just the front entry.
3. If it still will not fit, **refuse and return false**. Nothing queued, nothing marked
   disk-present.
4. Otherwise `m_pendingDiskBytes += bytes`, push, and block on a new `m_diskQueueSpace` condvar
   while the count is at `kMaxPendingDiskWrites`. Never `pop_front()`-drop.

`kMaxPendingDiskWrites` goes 4 → **8**. Depth is bounded by shutdown exposure, not memory: the
comment at `CacheManager.cpp:587` overstates the memory cost (volumes queued via `storeImage` are
already RAM-resident, so their `shared_ptr` costs nothing extra until eviction, and only warm-only
writes add memory), but `stopDiskWriter` discards the backlog on quit, so queue depth is exactly the
number of frames that can be lost by quitting mid-warm. 8 keeps that small.

The trade is that back-pressure engages more often while warming, throttling prefetch to disk write
speed sooner. Accepted: prefetch is background work, and throttling it is the correct response to a
disk that cannot keep up.

Note depth does **not** affect interactive worst-case latency. A blocked producer waits for one slot,
i.e. one write to complete, regardless of how deep the queue is. Depth only changes how *often* the
loader blocks.

### Shutdown: keep abandoning the backlog

`stopDiskWriter` keeps clearing the queue (`CacheManager.cpp:662`) and agave_app still does not call
`flushDiskWrites`. Quit stays instant; at most 8 frames are re-fetched from source in a later session.

This is a deliberate choice against draining on exit, which for full-volume tensorstore writes could
hang quit for seconds with no way out. It costs nothing across sessions: the next session's status
seeding (§5a) probes the **filesystem**, so an abandoned write is correctly reported absent rather
than trusted. The only inaccuracy is within the dying process, where the pending-aware
`containsOnDisk` had reported those steps as disk-present — and that process is exiting.

It also only bites when quitting mid-warm. Once prefetch goes idle the queue drains on its own, so
letting a series finish warming before exiting loses nothing.

`diskWriterMain`, on completion: `m_pendingDiskBytes -= bytes` as `m_currentDiskBytes += bytes`; on
failure, subtract the pending bytes without the add. Either way notify `m_diskQueueSpace`. Pending
entries are not in `m_diskEntries`, so eviction cannot delete a reservation out from under a queued
write.

`containsOnDisk` becomes pending-aware — true for a key still in the write queue, i.e. "is or will
be on disk". This is what removes the eviction-timing race, and the reservation invariant is what
makes it honest. `TimeSeriesLoader` needs no change for it.

`droppedDiskWrites` is now structurally always zero. Keep the counter and its Statistics dock row as
a standing assertion that it stays that way.

`clearDiskCache` already drains the queue before wiping; it must also zero `m_pendingDiskBytes`.

### Refusal handling

`storeImageInternal` propagates the bool. `TimeSeriesLoader`'s warm-only completion path treats a
refusal as "the disk warm set is full": mark the time step `NotCached` and add it to a small
`m_warmRefused` skip-set that priority 2 ignores, so prefetch goes idle instead of retrying forever.
Cleared on `setSeries` and when config changes.

With `diskWarmSteps` derived from `diskBudgetFrames` this should be unreachable. It is a defensive
terminator, not a normal path.

### Accepted cost

While a large series is warming, the loader thread throttles to disk write speed, and an interactive
scrub issued at that moment waits for at most one queue slot to free. Steady state is an empty queue,
because prefetch goes idle once both windows are satisfied — so this only bites during warm-up.
Chosen deliberately over dropping writes: the point of the disk tier is that the data is actually on
disk.

### Drive-by cleanup

`storeImage` (`CacheManager.cpp:384-402`) computes `configCopy`, `cacheDirCopy` and `key` and uses
none of them; `storeImageInternal` recomputes all three. Delete them.

## 5. Cross-session warm start

The payoff of the disk tier is that a later session opening the same series reads from local disk
instead of the original URL or data source, and that stale entries from other datasets are LRU-evicted
to make room. Both hold, but two existing defects stop the second session from actually getting the
benefit cheaply. Both must be fixed here.

`containsOnDisk` is already sound for this: it consults the filesystem rather than the lazily-built
`m_diskEntries` index (`CacheManager.cpp:890-894`), so it answers correctly on the first call in a
fresh process. And `CacheKey` folds in `fileMtimeNs` / `fileSize`, which are zero for remote URLs, so
a URL keys on path alone and matches across sessions. Local files correctly miss if the source was
overwritten.

### 5a. `setSeries` must seed status from the disk tier

`setSeries` reconciles only against memory (`containsInMemory`, `TimeSeriesLoader.cpp:107`). In a new
session every time step therefore starts `NotCached` even though the disk cache is full. Two
consequences: the slider strip paints blank and the warm cache is invisible, and — worse — priority 2
considers only `NotCached` steps, so it selects *every* already-warm step as a warm target.

Extend the reconciliation loop to probe `containsOnDisk` when `containsInMemory` is false, and set
`DiskCached`. The strip then paints the warm series immediately on load, and priority 2 skips those
steps entirely because they are no longer `NotCached`.

Cost: one `containsOnDisk` per time step at series load, each a `makeKey` plus a
`std::filesystem::exists`. The `TimepointStatus` comment (`TimeSeriesLoader.h:18-21`) warns that
building a `CacheKey` stats the file and that polling the cache per repaint would be a stat storm —
that warning is about *painting*, and this is once per series load, on the same order as the memory
reconciliation loop that already builds a key per step. Acceptable, but for a long series it is
hundreds of stats on whichever thread calls `setSeries`; if it measures badly, move the whole
reconciliation onto the loader thread rather than trimming the probe.

### 5b. A warm-only prefetch must never promote into RAM

`nextPrefetchTimeLocked` reports `warmOnly`, but the fetch site ignores it: line 740 calls
`findImage(spec)` unconditionally, and `findImage` promotes a disk hit into memory
(`storeImageInMemory`, `CacheManager.cpp:369`). The step is then marked `RamCached` at line 753. So a
warm-only step that is already on disk gets dragged through RAM, evicting the near frames — exactly
what `storeImageOnDiskOnly` was added to prevent. That fix covered the fetch-from-source path; this is
the disk-hit path, which the fix did not reach.

Split the probe on `warmOnly`:

- **`warmOnly == false`** — unchanged. `findImage` is correct here: the step is inside the memory
  window, so promoting a disk hit into RAM is the desired outcome, and it is counted as a disk hit.
- **`warmOnly == true`** — probe `containsOnDisk` instead. Already on disk → mark `DiskCached` and do
  nothing else, no RAM traffic and no LRU touch. Not on disk → fetch and `storeImageOnDiskOnly`, as
  today.

This is needed independently of 5a: §3's disk eviction observer reverts a step to `NotCached`
mid-session, which re-enters this path.

### 5c. The disk index is built eagerly, and two comments say otherwise

Run 3 of the acceptance scenario below depends on the disk index being populated before any loading,
so that `evictDiskIfNeeded` knows what is already on disk and how big it is. It is: `loadDiskIndex`
has exactly one call site, `setConfig` (`CacheManager.cpp:302`), reached whenever
`m_diskIndexRoot != m_cacheDir` — which is true on the first `setConfig` of a process — and followed
immediately by `evictDiskIfNeeded(config, 0)` at line 303. The app does this at startup:
`main.cpp:328` calls `CacheManager::initialize`, then `agaveGui.cpp:109` calls
`CacheSettings::applyToRenderlib`, which calls `setConfig`.

**No behavioural change needed.** But two comments claim the opposite and must be corrected, because
one of them is the stated justification for a design choice:

- `CacheManager.h:136-137` — "diskBytesUsed is only meaningful once the disk index has been built
  (which happens lazily on first disk access)."
- `CacheManager.cpp:890-891` in `containsOnDisk` — "Consult the filesystem rather than m_diskEntries:
  the index is built lazily and may not have been populated yet in this session."

The filesystem probe in `containsOnDisk` stays — it is correct even when `setConfig` was never called,
which is the renderlib-without-GUI and test case, and it is what makes §5a and cross-session hits
work. Only the reason is wrong.

### 5d. Caveat on the LRU ordering, not fixed here

Disk eviction sorts by `lastAccess`, which `loadFromDisk` refreshes on a disk hit. But the TODO at
`CacheManager.cpp:340-348` records that a RAM-resident entry never has its disk `lastAccess` bumped,
because a RAM hit deliberately does not touch the disk bookkeeping. So the frames watched most in a
session — the ones that stayed resident — look *coldest* on disk next session and are evicted first.

Left alone deliberately: it is pre-existing, orthogonal to this design, and fixing it means writing
`meta.json` on RAM hits, which puts disk I/O back on the hot path this work removed it from. Noted so
it is not mistaken for a regression introduced here.

## Acceptance scenario

The behaviour this design exists to deliver, as three consecutive runs of the app. Verified by
inspection against the code; §5a and §5b are the only changes it requires.

### Run 1 — cold start, series A

Load series A (a URL) with prefetch enabled. Memory fills with the forward run `t..t+forwardSteps`;
disk fills with those plus the warm set, up to `maxDiskBytes`. The strip shows solid near the
playhead, dim beyond, blank past the disk budget. Prefetch goes idle and stays idle. Quit.

Caveat: quitting *before* prefetch goes idle abandons up to 8 queued writes (§4). Letting it finish
warming loses nothing.

### Run 2 — warm start, same series A

Load the same URL. `CacheKey` folds in `fileMtimeNs`/`fileSize`, both **zero for remote URLs**, so the
key is stable across processes and A's entries match. At `setSeries`, status seeding (§5a) marks every
step still on disk as `DiskCached`, so the warm series is visible on the strip immediately, before any
loading.

Prefetch then satisfies the memory window from disk — `findImage` promotes disk hits into RAM and
counts them as disk hits — and issues **zero** source loads for anything already warm. The warm set
beyond the memory window is skipped entirely, because those steps are no longer `NotCached`. Without
§5b this run would drag every disk-cached step through RAM; with it, warm-only steps stay on disk.

For a **local file** rather than a URL, a warm start requires the file's mtime and size to be
unchanged. Overwriting the source correctly invalidates its entries — that is `fileMtimeNs` doing its
job, not a cache failure.

### Run 3 — different series B, disk cache full of A

Load an unrelated series B. At startup `setConfig` builds the disk index and computes
`m_currentDiskBytes` (§5c), so the disk tier knows it is full of A. B's steps miss in both tiers and
are fetched from source; each write calls `evictDiskIfNeeded`, which sorts `m_diskEntries` by
`lastAccess` ascending and deletes A's entries — older than B's fresh writes — until B fits. The disk
cache converges to holding B.

With §3's disk eviction observer, any of A's steps still shown as `DiskCached` in a live loader would
revert to `NotCached` as they are deleted; in this scenario A is not loaded, so nothing is displaying
them.

## Boundary cases that must hold

| Case | Required behaviour |
| --- | --- |
| `historyMargin = 0` | Reduces to the all-forward shape: `forwardCapacity = budgetFrames - 1`, identical to the pre-existing clamp at line 427. No special case. |
| `historyMargin >= budgetFrames - 1` | Saturates to `forwardCapacity = 1`. Playback still inches forward. **Must not underflow** — raw `budgetFrames - 1 - historyMargin` would wrap to a huge `uint64`, clamp to `span - 1`, and make the window the whole series. |
| `budgetFrames <= 1` | `forwardCapacity = 1`, as today. |
| `bytesPerFrame == 0` | Both windows at their minimum; one load in flight; widen after the first completion. Pre-existing. |
| `diskBudgetFrames <= 1 + forwardSteps` | `diskWarmSteps = 0`; the warm pass does nothing. |
| `enableDisk == false` | `diskWarmSteps = 0`; the warm pass does nothing and the memory window is unaffected. Prefetch is memory-only. |
| `enabled == false` | No prefetch of either tier. Slider-driven loads still hit `findImage` and `storeImage`, so they are still cached in RAM and on disk. |
| series smaller than the budgets | `span - 1` clamps both; the whole series is resident and prefetch goes idle. |
| series larger than RAM + disk | The far tail stays `NotCached` and paints blank. Prefetch still terminates. |

## Tests

Existing, as regression gates:

- *"prefetch terminates on a series larger than memory"* — the gate for widening the window.

New:

- Series larger than **memory + disk**: every time step ends `RamCached`, `DiskCached` or
  `NotCached`; the `NotCached` set is the far tail; fetches stop.
- `historyMargin = 0`: window is `budgetFrames - 1` forward, prefetch terminates.
- `historyMargin` larger than the budget: `forwardCapacity` saturates to 1, no underflow, prefetch
  terminates.
- History is retained but never fetched: after playing forward, frames behind the playhead are
  resident; after a large jump, they are not, and no fetch is issued for them.
- Both windows re-aim on `requestTime`, and in-flight loads still inside the new windows are not
  cancelled.
- Disk eviction reverts `DiskCached → NotCached` and notifies.
- `enqueueDiskWrite` never drops under sustained pressure, and on-disk + pending bytes never exceed
  `maxDiskBytes`.
- A refused write leaves the time step `NotCached` and does not cause a retry loop.
- `enabled = false`: no prefetch is issued for any time step, but a `requestTime` load still lands in
  RAM and is queued to disk, and a second `requestTime` for the same step is a cache hit.
- **Cross-session warm start** (§5), using a second `TimeSeriesLoader` against the same throwaway
  cache directory to stand in for a second session:
  - `setSeries` on a warm disk cache seeds `DiskCached` for steps present on disk and `RamCached` for
    steps still in memory, without fetching anything.
  - With the disk cache warm and the source reader rigged to fail (or count calls), prefetch satisfies
    the memory window entirely from disk and issues **zero** source loads.
  - A warm-only prefetch of a step already on disk leaves `ramBytesUsed` **unchanged** and the step
    `DiskCached`, never `RamCached` — the regression gate for 5b.
  - Warming a long series never pushes RAM usage above the memory window's worth of frames.
- **The three-run acceptance scenario**, end to end against one throwaway cache directory, using a
  fresh `CacheManager` + `TimeSeriesLoader` pair per "run" and `flushDiskWrites()` between runs to
  stand in for a clean exit:
  1. Run 1 warms series A; assert the disk tier holds it and `diskBytesUsed <= maxDiskBytes`.
  2. Run 2 reloads A with the reader counting calls; assert seeding marks the warm steps `DiskCached`
     before any load, that source loads are zero for warm steps, and that RAM never exceeds the
     memory window.
  3. Run 3 loads an unrelated series B sized to need the whole disk budget; assert B ends up resident
     or on disk, that A's entries are gone, and that `diskBytesUsed <= maxDiskBytes` throughout.
- A `LoadSpec` with a URL path produces a `CacheKey` with `fileMtimeNs == 0` and `fileSize == 0`, and
  two `CacheManager` instances over the same directory agree on `containsOnDisk` for it — the
  cross-session key-stability gate for run 2.

**Existing-test churn.** Roughly fifteen call sites in `test/test_timeSeriesLoader.cpp` set
`cfg.fillCache = true` and one sets `cfg.depth`; all need updating. Two need more than a mechanical
edit:

- `"TimeSeriesLoader fillCache mode prefetches to the end of the series"` (line 336) encodes the old
  sweep-everything semantics. Rewrite as "prefetch fills the memory window then warms the disk set",
  asserting the clamped boundaries rather than reaching the end of the series.
- The termination regression test near line 1068 has a comment explaining the failure in terms of
  `fillCache`; reword it to the capacity-clamp reasoning, since that is what now prevents the churn.

No Qt-layer coverage, per the existing split — removing the two settings controls is compile-verified
only.

## Landmines carried forward from the resume doc

- **The window is the single source of truth.** `canStartPrefetchLocked`, `nextPrefetchTimeLocked` and
  `requestTime`'s cancel check must keep sharing `prefetchWindowLocked()`. Three separate bugs came
  from these disagreeing. The new `diskWarmWindowLocked()` must be shared the same way.
- **Throttle on "wanted frames already resident", never on free space.** Gating on free space
  deadlocks: nothing frees space except eviction.
- **Never want more frames than fit.** The capacity clamp is load-bearing in both tiers.
- **Build**: `cmake --build . --target install` does not build or run tests. Use
  `--target agave_test --config Debug`. Occasional `LNK1201` is a stale `agave_test.exe` holding its
  PDB — kill the process and retry.
- Scripted edits: **always assert the anchor matched.** A silent no-op replace once shipped a crash.

## Out of scope

Unchanged from the resume doc: Phase 12 (double-buffered Vulkan upload, gated on measuring Phase 11
first, which has never been executed), the Phase 11 leftovers (`Fuse` output stride; replace the final
`vkQueueWaitIdle` with a fence), and the absent coverage of `FileReaderZarr` and the Vulkan upload
path.
