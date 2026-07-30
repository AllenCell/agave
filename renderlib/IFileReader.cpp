#include "IFileReader.h"

#include "io/LoadRequest.h"

std::shared_ptr<ImageXYZC>
IFileReader::loadFromFile(const LoadSpec& loadSpec)
{
  std::shared_ptr<LoadRequest> request = submitLoad(loadSpec);
  if (!request) {
    return {};
  }
  return request->take();
}
