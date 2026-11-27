#include "TimeSeriesLoaderBridge.h"

#include "renderlib/ImageXYZC.h"

#include <QMetaType>

TimeSeriesLoaderBridge::TimeSeriesLoaderBridge(QObject* parent)
  : QObject(parent)
{
  // shared_ptr<ImageXYZC> travels through a queued connection, so Qt needs to
  // know how to copy it into the event queue.
  qRegisterMetaType<std::shared_ptr<ImageXYZC>>("std::shared_ptr<ImageXYZC>");
}

TimeSeriesLoaderBridge::~TimeSeriesLoaderBridge() = default;

void
TimeSeriesLoaderBridge::onInteractiveLoadComplete(uint32_t time, std::shared_ptr<ImageXYZC> image, uint64_t seq)
{
  // QueuedConnection: this runs on the loader thread, and everything downstream
  // touches Qt widgets and the scene.
  QMetaObject::invokeMethod(
    this, [this, time, image, seq]() { emit interactiveLoadComplete(time, image, seq); }, Qt::QueuedConnection);
}

void
TimeSeriesLoaderBridge::onInteractiveLoadFailed(uint32_t time, uint64_t seq)
{
  QMetaObject::invokeMethod(this, [this, time, seq]() { emit interactiveLoadFailed(time, seq); }, Qt::QueuedConnection);
}

void
TimeSeriesLoaderBridge::onStatusChanged(uint32_t time, TimepointStatus status)
{
  const int statusValue = static_cast<int>(status);
  QMetaObject::invokeMethod(
    this, [this, time, statusValue]() { emit statusChanged(time, statusValue); }, Qt::QueuedConnection);
}

void
TimeSeriesLoaderBridge::onPrefetchIdle()
{
  QMetaObject::invokeMethod(this, [this]() { emit prefetchIdle(); }, Qt::QueuedConnection);
}
