#include "CacheStatusReport.h"

#include "CacheManager.h"
#include "IFileReader.h"
#include "Status.h"

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
  }
  status->SetStatisticChanged("Cache", "Misses", std::to_string(stats.misses));
}
