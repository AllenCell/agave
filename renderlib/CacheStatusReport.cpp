#include "CacheStatusReport.h"

#include "CacheManager.h"
#include "IFileReader.h"
#include "Status.h"

#include <iomanip>
#include <sstream>
#include <string>

namespace {

// "3.5 GB / 4.0 GB". A zero limit means the tier is disabled or unbounded, in
// which case only the used figure is meaningful.
std::string
formatUsage(std::uint64_t used, std::uint64_t limit)
{
  std::string text = LoadSpec::bytesToStringLabel(static_cast<size_t>(used));
  if (limit > 0) {
    text += " / " + LoadSpec::bytesToStringLabel(static_cast<size_t>(limit));
  }
  return text;
}

std::string
formatMegabytes(std::uint64_t bytes)
{
  std::ostringstream ss;
  ss << std::fixed << std::setprecision(1) << (static_cast<double>(bytes) / (1024.0 * 1024.0));
  return ss.str();
}

} // namespace

void
reportCacheStatistics(CStatus* status)
{
  if (!status) {
    return;
  }

  const CacheManager& cache = CacheManager::instance();
  const CacheConfig config = cache.getConfig();

  if (!config.enabled) {
    status->SetStatisticChanged("Cache", "Enabled", "no");
    return;
  }

  const CacheManager::CacheUsage usage = cache.getUsage();
  const CacheManager::CacheStats stats = cache.getStats();

  status->SetStatisticChanged("Cache", "Enabled", "yes");
  status->SetStatisticChanged("Cache", "Memory", formatUsage(usage.ramBytesUsed, usage.ramBytesLimit));
  status->SetStatisticChanged("Cache", "Memory Entries", std::to_string(usage.ramEntryCount));

  if (config.enableDisk) {
    status->SetStatisticChanged("Cache", "Disk", formatUsage(usage.diskBytesUsed, usage.diskBytesLimit));
    status->SetStatisticChanged("Cache", "Disk Entries", std::to_string(usage.diskEntryCount));
  }

  // Hit rate over memory and disk together, since both avoid re-reading the
  // source. Reported alongside the raw counters so a low rate can be attributed.
  const std::uint64_t hits = stats.ramHits + stats.diskHits;
  const std::uint64_t lookups = hits + stats.misses;
  if (lookups > 0) {
    const int percent = static_cast<int>((hits * 100) / lookups);
    status->SetStatisticChanged("Cache", "Hit Rate", std::to_string(percent), "%");
  }
  status->SetStatisticChanged("Cache", "Memory Hits", std::to_string(stats.ramHits));
  if (config.enableDisk) {
    status->SetStatisticChanged("Cache", "Disk Hits", std::to_string(stats.diskHits));
    status->SetStatisticChanged("Cache", "Disk Writes", std::to_string(stats.diskWrites));
    // Disk writes are asynchronous. A steady small number here is normal. The
    // queue is bounded and applies back-pressure rather than dropping, so a value
    // pinned at the cap means the disk cannot keep up and loading is being
    // throttled to disk speed.
    status->SetStatisticChanged("Cache", "Disk Writes Pending", std::to_string(cache.pendingDiskWrites()));
    // Those queued writes have already reserved disk space, so the committed total
    // is this plus the Disk figure above -- that sum is what stays under the limit.
    const std::uint64_t pendingBytes = cache.pendingDiskBytes();
    status->SetStatisticChanged("Cache", "Disk Writes Pending Size", formatMegabytes(pendingBytes), "MB");
    // Structurally always zero now that the queue back-pressures instead of
    // dropping. Reported only if it ever isn't, as a standing assertion. Note the
    // backlog abandoned at shutdown is deliberate and not counted here.
    const std::uint64_t dropped = cache.droppedDiskWrites();
    if (dropped > 0) {
      status->SetStatisticChanged("Cache", "Disk Writes Dropped", std::to_string(dropped));
    }
  }
  status->SetStatisticChanged("Cache", "Misses", std::to_string(stats.misses));
}
