#pragma once

#include "IFileReader.h"
#include "LoadRequest.h"

#include <cstdint>
#include <memory>

class ImageXYZC;

// Base class for readers whose underlying library offers only blocking reads:
// TIFF, CZI, CCP4 and image sequences. It implements the asynchronous
// IFileReader::submitLoad by running loadVolumeBlocking on a worker thread, so
// these formats get time-series prefetch without each one growing its own
// threading code.
//
// Subclasses implement loadVolumeBlocking instead of overriding submitLoad, and
// should poll progress.isCancelled() at natural boundaries (between channels or
// Z planes) so a cancelled prefetch stops promptly.
//
// Concurrency: maxConcurrentLoads() defaults to 1. Subclasses whose underlying
// library is safe to call from several threads at once can raise it via
// setMaxConcurrentLoads(). The caller is responsible for not exceeding it.
class BlockingFileReader : public IFileReader
{
public:
  std::shared_ptr<LoadRequest> submitLoad(const LoadSpec& loadSpec) override;
  uint32_t maxConcurrentLoads() const override { return m_maxConcurrentLoads; }

  // Load synchronously on the calling thread. Public so that a reader which
  // delegates to another reader (FileReaderImageSequence -> FileReaderTIFF) can
  // forward the call directly, staying on the worker thread it is already on
  // and propagating the same cancellation state, instead of nesting one
  // asynchronous load inside another.
  virtual std::shared_ptr<ImageXYZC> loadVolumeBlocking(const LoadSpec& loadSpec, LoadProgress& progress) = 0;

protected:
  void setMaxConcurrentLoads(uint32_t n) { m_maxConcurrentLoads = n < 1 ? 1 : n; }

private:
  uint32_t m_maxConcurrentLoads = 1;
};
