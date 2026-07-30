#pragma once

#include "renderlib/io/FileReader.h"
#include "renderlib/io/TimeSeriesLoader.h"

#include <QDockWidget>
#include <QGridLayout>

#include <memory>

class ImageXYZC;
class IFileReader;
class QIntSlider;
class QTimelineDockWidget;
class QRenderSettings;
class Scene;
class TimeSeriesLoaderBridge;

class QTimelineWidget : public QWidget
{
  Q_OBJECT

public:
  QTimelineWidget(QWidget* pParent = nullptr, QRenderSettings* qrs = nullptr);
  ~QTimelineWidget() override;

  void onNewImage(Scene* s, const LoadSpec& loadSpec, std::shared_ptr<IFileReader> reader);
  // Set the slider position. Pass blockSignals to move it without triggering a
  // load -- the old single-argument version always triggered one, which was a
  // trap for programmatic callers.
  void setTime(int t, bool blockSignals = false);

  void OnTimeChanged(int newTime);

  // Push current prefetch settings down to the loader.
  void setPrefetchConfig(const TimeSeriesLoader::PrefetchConfig& config);
  void cancelPrefetch();

  TimeSeriesLoader* loader() { return m_loader.get(); }

signals:
  void timeChanged(int newTime);

private:
  void onLoadComplete(uint32_t time, std::shared_ptr<ImageXYZC> image, uint64_t seq);
  void onLoadFailed(uint32_t time, uint64_t seq);

protected:
  QGridLayout m_MainLayout;
  QIntSlider* m_TimeSlider;

  QRenderSettings* m_qrendersettings;
  Scene* m_scene;
  LoadSpec m_loadSpec;
  std::shared_ptr<IFileReader> m_reader;

  std::unique_ptr<TimeSeriesLoader> m_loader;
  TimeSeriesLoaderBridge* m_bridge;
  // Newest interactive request we have issued. Completions carrying an older
  // sequence number are discarded: the user has already moved on.
  uint64_t m_latestRequestSeq = 0;
};

class QTimelineDockWidget : public QDockWidget
{
  Q_OBJECT

public:
  QTimelineDockWidget(QWidget* pParent = nullptr, QRenderSettings* qrs = nullptr);

  void onNewImage(Scene* s, const LoadSpec& loadSpec, std::shared_ptr<IFileReader> reader)
  {
    m_TimelineWidget.onNewImage(s, loadSpec, reader);
  }
  void setTime(int t, bool blockSignals = false) { m_TimelineWidget.setTime(t, blockSignals); }

  // other Gui needs to connect to timeline signals
  QTimelineWidget& timelineWidget() { return m_TimelineWidget; }

protected:
  QTimelineWidget m_TimelineWidget;
};
