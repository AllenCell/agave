#pragma once

#include "renderlib/io/TimeSeriesLoader.h"

#include <QObject>

#include <memory>

class ImageXYZC;

// Marshals TimeSeriesLoader callbacks onto the Qt main thread.
//
// TimeSeriesLoader lives in renderlib and therefore cannot emit Qt signals, and
// it invokes its observers on the loader thread. This shim implements the
// observer interface, hops each callback to the main thread with a queued
// invocation, and re-emits it as a normal Qt signal.
//
// It deliberately contains no logic beyond that hop: everything that decides
// anything lives in renderlib.
class TimeSeriesLoaderBridge
  : public QObject
  , public ITimeSeriesLoaderObserver
{
  Q_OBJECT

public:
  explicit TimeSeriesLoaderBridge(QObject* parent = nullptr);
  ~TimeSeriesLoaderBridge() override;

  // ITimeSeriesLoaderObserver -- all called on the loader thread.
  void onInteractiveLoadComplete(uint32_t time, std::shared_ptr<ImageXYZC> image, uint64_t seq) override;
  void onInteractiveLoadFailed(uint32_t time, uint64_t seq) override;
  void onStatusChanged(uint32_t time, TimepointStatus status) override;
  void onPrefetchIdle() override;

signals:
  // All emitted on the main thread.
  void interactiveLoadComplete(uint32_t time, std::shared_ptr<ImageXYZC> image, uint64_t seq);
  void interactiveLoadFailed(uint32_t time, uint64_t seq);
  void statusChanged(uint32_t time, int status);
  void prefetchIdle();
};
