#include "BlockingFileReader.h"

#include "ImageXYZC.h"
#include "Logging.h"

#include <future>

std::shared_ptr<LoadRequest>
BlockingFileReader::submitLoad(const LoadSpec& loadSpec)
{
  auto progress = std::make_shared<LoadProgress>();

  // std::async(launch::async) rather than a persistent pool: the caller bounds
  // how many loads are in flight (maxConcurrentLoads), so thread count is
  // bounded too, and one thread launch is negligible next to reading a volume.
  //
  // Note: renderlib's Tasks pool in threading.h is not usable here. Its queue()
  // is a template defined in threading.cpp so it cannot be instantiated from
  // another translation unit, and it stores packaged_task<R()> into a
  // deque<packaged_task<bool()>>, which only compiles for R = bool.
  auto future = std::async(std::launch::async, [this, loadSpec, progress]() -> std::shared_ptr<ImageXYZC> {
    if (progress->isCancelled()) {
      return {};
    }
    return loadVolumeBlocking(loadSpec, *progress);
  });

  return std::make_shared<FutureLoadRequest>(loadSpec, progress, std::move(future));
}
