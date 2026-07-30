#pragma once

#include "IFileReader.h"

#include <atomic>
#include <future>
#include <memory>

class ImageXYZC;

// Cooperative cancellation and progress reporting for one in-flight volume load.
//
// Shared between the thread that issued the load and the thread performing it,
// so every accessor is atomic. A reader implementation polls isCancelled() at
// natural boundaries -- between channels, Z planes, or chunks -- and returns
// null early when it sees a cancellation, so an unwanted prefetch stops without
// reading a whole volume nobody is waiting for.
class LoadProgress
{
public:
  bool isCancelled() const { return m_cancelled.load(std::memory_order_relaxed); }
  void cancel() { m_cancelled.store(true, std::memory_order_relaxed); }

  // Fraction complete, 0..1.
  float progress() const { return m_progress.load(std::memory_order_relaxed); }
  void setProgress(float fraction) { m_progress.store(fraction, std::memory_order_relaxed); }

  // Convenience for the common "finished `done` of `total` units" case.
  // A total of 0 is treated as "no progress information" and stores 0.
  void setProgress(uint32_t done, uint32_t total)
  {
    setProgress(total == 0 ? 0.0f : static_cast<float>(done) / static_cast<float>(total));
  }

private:
  std::atomic<bool> m_cancelled{ false };
  std::atomic<float> m_progress{ 0.0f };
};

// A volume load in progress.
//
// Created by IFileReader::submitLoad. cancel(), isCancelled() and progress()
// are safe to call from any thread. isReady() and take() are intended for the
// single thread that owns the request (in AGAVE that is the loader thread).
class LoadRequest
{
public:
  virtual ~LoadRequest() = default;

  // True once the load has finished, failed, or observed its cancellation.
  virtual bool isReady() const = 0;

  // Blocks until isReady(). Returns null on failure or cancellation. Repeated
  // calls return the same result without blocking again.
  virtual std::shared_ptr<ImageXYZC> take() = 0;

  // Best-effort cooperative cancel. Returns immediately; the load may still
  // complete normally if it was already past its last cancellation check.
  void cancel() { m_progress->cancel(); }
  bool isCancelled() const { return m_progress->isCancelled(); }
  float progress() const { return m_progress->progress(); }

  const LoadSpec& spec() const { return m_spec; }

protected:
  LoadRequest(const LoadSpec& spec, std::shared_ptr<LoadProgress> progress)
    : m_spec(spec)
    , m_progress(std::move(progress))
  {
  }

  LoadSpec m_spec;
  std::shared_ptr<LoadProgress> m_progress;
};

// A LoadRequest backed by a std::future, used by readers that perform the load
// on a worker thread (see BlockingFileReader).
//
// Destroying an unfinished request cancels it first and then waits, because the
// worker writes into memory owned by the task. Without the cancel, the wait
// would be unbounded; with it, the wait is bounded by how promptly the reader
// polls for cancellation.
class FutureLoadRequest : public LoadRequest
{
public:
  FutureLoadRequest(const LoadSpec& spec,
                    std::shared_ptr<LoadProgress> progress,
                    std::future<std::shared_ptr<ImageXYZC>> future);
  ~FutureLoadRequest() override;

  bool isReady() const override;
  std::shared_ptr<ImageXYZC> take() override;

private:
  std::future<std::shared_ptr<ImageXYZC>> m_future;
  std::shared_ptr<ImageXYZC> m_result;
  bool m_taken = false;
};
