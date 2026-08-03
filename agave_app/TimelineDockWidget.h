#pragma once

#include "renderlib/io/FileReader.h"
#include "renderlib/io/TimeSeriesLoader.h"
#include "renderlib/io/TimeSeriesPlayer.h"

#include <QDockWidget>
#include <QGridLayout>

#include <chrono>
#include <memory>

class ImageXYZC;
class IFileReader;
class QCheckBox;
class QSpinBox;
class QTimelineDockWidget;
class QRenderSettings;
class QTimer;
class QToolButton;
class Scene;
class TimeSeriesLoaderBridge;
class TimeSlider;
struct TimeSeriesSettingsData;

class QTimelineWidget : public QWidget
{
  Q_OBJECT

public:
  QTimelineWidget(QWidget* pParent = nullptr,
                  QRenderSettings* qrs = nullptr,
                  TimeSeriesSettingsData* settings = nullptr);
  ~QTimelineWidget() override;

  void onNewImage(Scene* s, const LoadSpec& loadSpec, std::shared_ptr<IFileReader> reader);
  // Set the slider position. Pass blockSignals to move it without triggering a
  // load.
  void setTime(int t, bool blockSignals = false);

  void OnTimeChanged(int newTime);

  void updateUiFromSettings();

signals:
  void timeChanged(int newTime);

private:
  void onLoadComplete(uint32_t time, std::shared_ptr<ImageXYZC> image, uint64_t seq);
  void onLoadFailed(uint32_t time, uint64_t seq);
  void onTimepointStatusChanged(uint32_t time, int status);
  void refreshCacheStatus();
  void writePlaybackSettings(const TimeSeriesPlayer::Config& config);

  void buildPlaybackControls(QVBoxLayout* layout);
  void togglePlayPause();
  void stopPlayback();
  void onPlaybackTick();
  // Reflect player state in the buttons and start/stop the tick timer.
  void syncPlaybackUi();
  uint64_t nowMs() const;

  void setPlaybackConfig(const TimeSeriesPlayer::Config& config);
  void setPrefetchConfig(const TimeSeriesLoader::PrefetchConfig& config);

protected:
  QGridLayout m_MainLayout;
  TimeSlider* m_TimeSlider;

  QRenderSettings* m_qrendersettings;
  Scene* m_scene;
  LoadSpec m_loadSpec;
  std::shared_ptr<IFileReader> m_reader;

  std::unique_ptr<TimeSeriesLoader> m_loader;
  TimeSeriesLoaderBridge* m_bridge;

  // Playback. The state machine lives in renderlib; these are just its controls
  // and the clock that drives it.
  TimeSeriesPlayer m_player;
  TimeSeriesSettingsData* m_settings = nullptr;
  bool m_applyingSettings = false;
  QToolButton* m_playPauseButton = nullptr;
  QToolButton* m_stopButton = nullptr;
  QSpinBox* m_fpsSpinner = nullptr;
  QCheckBox* m_loopCheckbox = nullptr;
  QCheckBox* m_dropFramesCheckbox = nullptr;
  QTimer* m_playbackTimer = nullptr;
  std::chrono::steady_clock::time_point m_clockOrigin;
  // Playback advances from the requested playhead, not always from the scene's
  // displayed time. Those differ after the user scrubs to an uncached/red
  // timepoint: the slider moves immediately, while the scene remains on the last
  // displayed frame until loading finishes.
  uint32_t m_playbackCursorTime = 0;
  bool m_havePlaybackCursor = false;
  bool m_playbackDisplayPending = false;
  // Newest interactive request we have issued. Completions carrying an older
  // sequence number are discarded: the user has already moved on.
  uint64_t m_latestRequestSeq = 0;
};

class QTimelineDockWidget : public QDockWidget
{
  Q_OBJECT

public:
  QTimelineDockWidget(QWidget* pParent = nullptr,
                      QRenderSettings* qrs = nullptr,
                      TimeSeriesSettingsData* settings = nullptr);

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
