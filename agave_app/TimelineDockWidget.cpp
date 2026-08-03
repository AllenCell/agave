#include "TimelineDockWidget.h"

#include "AgaveSettings.h"
#include "TimeSlider.h"
#include "Controls.h"
#include "QRenderSettings.h"
#include "TimeSeriesLoaderBridge.h"

#include "renderlib/AppScene.h"
#include "renderlib/ImageXYZC.h"
#include "renderlib/Logging.h"
#include "renderlib/RenderSettings.h"
#include "renderlib/io/ApplyTimeStepToScene.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QToolButton>

#include <algorithm>

QTimelineWidget::QTimelineWidget(QWidget* pParent, QRenderSettings* qrs, TimeSeriesSettingsData* settings)
  : QWidget(pParent)
  , m_MainLayout()
  , m_qrendersettings(qrs)
  , m_scene(nullptr)
  , m_loader(std::make_unique<TimeSeriesLoader>())
  , m_bridge(new TimeSeriesLoaderBridge(this))
  , m_settings(settings)
{
  // Create main layout
  m_MainLayout.setAlignment(Qt::AlignTop);
  setLayout(&m_MainLayout);

  QScrollArea* scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);

  auto* fullLayout = new QVBoxLayout();

  m_TimeSlider = new TimeSlider();
  m_TimeSlider->setStatusTip(tr("Set current time sample"));
  m_TimeSlider->setToolTip(tr("Set current time sample"));
  // Tracking is on so scrubbing over cached frames updates the view live.
  // OnTimeChanged gates uncached frames while the handle is being dragged; the
  // final position is committed by the sliderReleased handler below.
  m_TimeSlider->setTracking(true);
  m_TimeSlider->setRange(0, 0);
  m_TimeSlider->setSingleStep(1);
  m_TimeSlider->setTickPosition(QSlider::TickPosition::TicksBelow);
  fullLayout->addWidget(m_TimeSlider);

  QObject::connect(m_TimeSlider, &QIntSlider::valueChanged, [this](int t) { this->OnTimeChanged(t); });
  // Committing the drag when the mouse comes up guarantees the final frame is
  // requested even if we skipped it mid-drag because it was uncached remote data.
  QObject::connect(
    m_TimeSlider, &TimeSlider::sliderReleased, this, [this]() { this->OnTimeChanged(m_TimeSlider->value()); });

  buildPlaybackControls(fullLayout);

  connect(m_bridge,
          &TimeSeriesLoaderBridge::interactiveLoadComplete,
          this,
          [this](uint32_t time, std::shared_ptr<ImageXYZC> image, uint64_t seq) { onLoadComplete(time, image, seq); });
  connect(m_bridge, &TimeSeriesLoaderBridge::interactiveLoadFailed, this, [this](uint32_t time, uint64_t seq) {
    onLoadFailed(time, seq);
  });

  connect(m_bridge, &TimeSeriesLoaderBridge::statusChanged, this, [this](uint32_t time, int status) {
    onTimepointStatusChanged(time, status);
  });

  m_loader->addObserver(m_bridge);

  scrollArea->setLayout(fullLayout);

  m_MainLayout.addWidget(scrollArea, 1, 0);
}

void
QTimelineWidget::buildPlaybackControls(QVBoxLayout* layout)
{
  auto* row = new QHBoxLayout();

  // Qt's standard media pixmaps rather than new SVG assets, so the icons follow
  // the platform style and the light/dark theme switch for free.
  m_playPauseButton = new QToolButton();
  m_playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
  m_playPauseButton->setToolTip(tr("Play/pause through time"));
  m_playPauseButton->setStatusTip(tr("Play or pause playback through the time series"));
  row->addWidget(m_playPauseButton);

  m_stopButton = new QToolButton();
  m_stopButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
  m_stopButton->setToolTip(tr("Stop and return to where playback started"));
  m_stopButton->setStatusTip(tr("Stop playback and return to the time it started from"));
  row->addWidget(m_stopButton);

  m_fpsSpinner = new QSpinBox();
  m_fpsSpinner->setRange(1, 120);
  m_fpsSpinner->setValue(10);
  m_fpsSpinner->setSuffix(tr(" fps"));
  m_fpsSpinner->setToolTip(tr("Target playback frame rate"));
  m_fpsSpinner->setStatusTip(tr("Target playback frame rate"));
  row->addWidget(m_fpsSpinner);

  m_loopCheckbox = new QCheckBox(tr("Loop"));
  m_loopCheckbox->setChecked(true);
  m_loopCheckbox->setToolTip(tr("Wrap to the beginning at the end of the series"));
  m_loopCheckbox->setStatusTip(tr("Wrap to the beginning at the end of the series"));
  row->addWidget(m_loopCheckbox);

  m_dropFramesCheckbox = new QCheckBox(tr("Smooth"));
  m_dropFramesCheckbox->setChecked(false);
  m_dropFramesCheckbox->setToolTip(tr("Keep a steady frame rate by skipping time steps that are not loaded yet.\n"
                                      "Unchecked, playback waits for every time step so none are missed."));
  m_dropFramesCheckbox->setStatusTip(tr("Keep a steady frame rate by skipping time steps that are not loaded yet"));
  row->addWidget(m_dropFramesCheckbox);

  row->addStretch(1);
  layout->addLayout(row);

  // A short fixed tick; the player does the rate limiting, so this only bounds
  // how precisely a frame boundary can be hit.
  m_playbackTimer = new QTimer(this);
  m_playbackTimer->setTimerType(Qt::PreciseTimer);
  m_playbackTimer->setInterval(5);

  m_clockOrigin = std::chrono::steady_clock::now();

  // pressed rather than clicked so the toggle fires on mouse-down instead of
  // waiting for release + hit-test. Under Smooth playback the event loop is busy
  // repainting a fresh volume frame each tick, and cutting the click round-trip
  // in half makes pause visibly snappier.
  connect(m_playPauseButton, &QToolButton::pressed, this, [this]() { togglePlayPause(); });
  connect(m_stopButton, &QToolButton::pressed, this, [this]() { stopPlayback(); });
  connect(m_playbackTimer, &QTimer::timeout, this, [this]() { onPlaybackTick(); });

  // Spacebar toggles play/pause without having to route through the button's
  // mouse-event pipeline, so it stays responsive when the mouse is over the 3D
  // view. Scoped to the top-level window so focus does not have to be on the
  // timeline dock for it to work.
  auto* playShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
  playShortcut->setContext(Qt::WindowShortcut);
  connect(playShortcut, &QShortcut::activated, this, [this]() { togglePlayPause(); });

  auto pushConfig = [this]() {
    if (m_applyingSettings) {
      return;
    }
    TimeSeriesPlayer::Config config = m_player.config();
    config.fps = static_cast<float>(m_fpsSpinner->value());
    config.loop = m_loopCheckbox->isChecked();
    config.mode =
      m_dropFramesCheckbox->isChecked() ? TimeSeriesPlayer::Mode::RealTime : TimeSeriesPlayer::Mode::ShowEveryFrame;
    m_player.setConfig(config);

    // Prefetch has to wrap when playback does, otherwise looping stalls on the
    // final frame: the first frame was evicted during the forward pass and a
    // strictly forward window would never fetch it back.
    if (m_loader) {
      TimeSeriesLoader::PrefetchConfig prefetch = m_loader->prefetchConfig();
      prefetch.wrapAround = config.loop;
      m_loader->setPrefetchConfig(prefetch);
    }
    writePlaybackSettings(config);
  };
  connect(m_fpsSpinner, QOverload<int>::of(&QSpinBox::valueChanged), this, [pushConfig](int) { pushConfig(); });
  connect(m_loopCheckbox, &QCheckBox::toggled, this, [pushConfig](bool) { pushConfig(); });
  connect(m_dropFramesCheckbox, &QCheckBox::toggled, this, [pushConfig](bool) { pushConfig(); });
  pushConfig();

  syncPlaybackUi();
}

uint64_t
QTimelineWidget::nowMs() const
{
  return static_cast<uint64_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_clockOrigin).count());
}

void
QTimelineWidget::togglePlayPause()
{
  if (!m_scene) {
    return;
  }
  if (m_player.isPlaying()) {
    m_player.pause();
  } else {
    const uint32_t displayedTime = static_cast<uint32_t>(std::max(0, m_scene->m_timeLine.currentTime()));
    const uint32_t requestedTime =
      m_TimeSlider ? static_cast<uint32_t>(std::max(0, m_TimeSlider->value())) : displayedTime;
    m_playbackCursorTime = requestedTime;
    m_havePlaybackCursor = true;
    m_playbackDisplayPending = false;
    m_player.play(requestedTime, nowMs());
  }
  syncPlaybackUi();
}

void
QTimelineWidget::stopPlayback()
{
  std::optional<uint32_t> origin = m_player.stop();
  m_playbackDisplayPending = false;
  syncPlaybackUi();
  if (origin) {
    // Returning to the origin is a normal time change, so go through the slider
    // and let the usual load path run.
    m_playbackCursorTime = *origin;
    m_havePlaybackCursor = true;
    setTime(static_cast<int>(*origin));
  }
}

void
QTimelineWidget::syncPlaybackUi()
{
  const bool playing = m_player.isPlaying();
  if (m_playPauseButton) {
    m_playPauseButton->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
  }
  if (m_playbackTimer) {
    if (playing && !m_playbackTimer->isActive()) {
      m_playbackTimer->start();
    } else if (!playing && m_playbackTimer->isActive()) {
      m_playbackTimer->stop();
    }
  }
}

void
QTimelineWidget::onPlaybackTick()
{
  if (!m_scene || !m_loader) {
    return;
  }
  if (m_playbackDisplayPending) {
    return;
  }

  const uint32_t current =
    m_havePlaybackCursor ? m_playbackCursorTime : static_cast<uint32_t>(std::max(0, m_scene->m_timeLine.currentTime()));
  std::optional<uint32_t> next = m_player.advance(
    nowMs(), current, [this](uint32_t t) { return m_loader->status(t) == TimepointStatus::RamCached; });

  if (!m_player.isPlaying()) {
    // Reached the end with looping off.
    syncPlaybackUi();
  }
  if (next) {
    m_playbackCursorTime = *next;
    m_havePlaybackCursor = true;
    m_playbackDisplayPending = true;
    setTime(static_cast<int>(*next));
    return;
  }

  // Holding in ShowEveryFrame, because the next frame is not in memory yet. Make
  // sure something is actually fetching it: advance() only asks whether a frame
  // is ready, it never causes it to become ready. With prefetch enabled the
  // window supplies it, but with prefetch off nothing would, and ShowEveryFrame
  // waits for ever -- which looked like the play button doing nothing at all.
  //
  // RealTime/Smooth deliberately skips this. It should advance to a resident
  // frame when the interval elapses, not start source reads for every red frame
  // it is supposed to skip.
  //
  // Requested straight on the loader rather than through setTime(), on purpose.
  // setTime() moves the slider, which would display a frame the player has not
  // advanced to. Going direct leaves m_latestRequestSeq alone, so onLoadComplete
  // discards this completion and only warms the cache; the player then sees the
  // frame as ready on a later tick and advances to it through the normal path.
  //
  // Only for a frame nothing is working on. Queued and Loading are already in
  // hand, and re-requesting every 5ms tick would cancel and restart the load it
  // is waiting for. Failed is left alone deliberately: retrying a frame that
  // cannot load would spin here for ever.
  if (m_player.isPlaying() && m_player.config().mode == TimeSeriesPlayer::Mode::ShowEveryFrame) {
    if (const std::optional<uint32_t> candidate = m_player.peekNextTime(current)) {
      const TimepointStatus s = m_loader->status(*candidate);
      if (s == TimepointStatus::NotCached || s == TimepointStatus::DiskCached) {
        m_loader->requestTime(*candidate);
      }
    }
  }
}

void
QTimelineWidget::onTimepointStatusChanged(uint32_t time, int status)
{
  if (m_TimeSlider) {
    m_TimeSlider->setStatus(time, static_cast<TimepointStatus>(status));
  }
}

void
QTimelineWidget::refreshCacheStatus()
{
  if (!m_TimeSlider || !m_loader || !m_scene) {
    return;
  }
  const int32_t minT = m_scene->m_timeLine.minTime();
  const int32_t maxT = m_scene->m_timeLine.maxTime();
  if (maxT <= minT) {
    m_TimeSlider->clearStatuses();
    return;
  }
  // Seed the strip with a full snapshot; incremental updates arrive via
  // onTimepointStatusChanged afterwards.
  std::vector<TimepointStatus> statuses;
  m_loader->statusRange(static_cast<uint32_t>(std::max(0, minT)), static_cast<uint32_t>(std::max(0, maxT)), statuses);
  m_TimeSlider->setStatuses(static_cast<uint32_t>(std::max(0, minT)), statuses);
}

void
QTimelineWidget::writePlaybackSettings(const TimeSeriesPlayer::Config& config)
{
  if (!m_settings) {
    return;
  }
  m_settings->playback.fps = config.fps;
  m_settings->playback.loop = config.loop;
  m_settings->playback.dropFrames = config.mode == TimeSeriesPlayer::Mode::RealTime;
}

void
QTimelineWidget::updateUiFromSettings()
{
  if (!m_settings) {
    return;
  }

  TimeSeriesLoader::PrefetchConfig prefetch;
  prefetch.enabled = m_settings->prefetchEnabled;
  // Prefetch must wrap when playback loops, or looping stalls at the end of the
  // series waiting for a first frame that nothing would fetch back.
  prefetch.wrapAround = m_settings->playback.loop;
  setPrefetchConfig(prefetch);

  TimeSeriesPlayer::Config playback;
  playback.fps = m_settings->playback.fps;
  playback.loop = m_settings->playback.loop;
  playback.mode =
    m_settings->playback.dropFrames ? TimeSeriesPlayer::Mode::RealTime : TimeSeriesPlayer::Mode::ShowEveryFrame;
  setPlaybackConfig(playback);
}

void
QTimelineWidget::setPlaybackConfig(const TimeSeriesPlayer::Config& config)
{
  m_player.setConfig(config);
  writePlaybackSettings(config);
  m_applyingSettings = true;
  if (m_fpsSpinner) {
    m_fpsSpinner->setValue(static_cast<int>(config.fps));
  }
  if (m_loopCheckbox) {
    m_loopCheckbox->setChecked(config.loop);
  }
  if (m_dropFramesCheckbox) {
    m_dropFramesCheckbox->setChecked(config.mode == TimeSeriesPlayer::Mode::RealTime);
  }
  m_applyingSettings = false;
}

QTimelineWidget::~QTimelineWidget()
{
  // Detach before the loader is destroyed so no callback can arrive against a
  // half-destroyed widget.
  if (m_loader) {
    m_loader->removeObserver(m_bridge);
  }
}

void
QTimelineWidget::onNewImage(Scene* s, const LoadSpec& loadSpec, std::shared_ptr<IFileReader> reader)
{
  m_reader = reader;
  m_loadSpec = loadSpec;
  m_scene = s;

  int32_t minT = m_scene ? m_scene->m_timeLine.minTime() : 0;
  int32_t maxT = m_scene ? m_scene->m_timeLine.maxTime() : 0;
  int32_t currentT = m_scene ? m_scene->m_timeLine.currentTime() : 0;
  m_playbackCursorTime = static_cast<uint32_t>(std::max(0, currentT));
  m_havePlaybackCursor = true;
  m_playbackDisplayPending = false;

  m_TimeSlider->setRange(minT, maxT);
  m_TimeSlider->setValue(currentT, true);
  m_TimeSlider->setTickInterval((maxT - minT) / 10);
  m_TimeSlider->setSingleStep(1);

  // disable the slider if there is less than 2 time samples.
  const bool haveTimeSeries = maxT > minT;
  m_TimeSlider->setEnabled(haveTimeSeries);
  this->parentWidget()->setWindowTitle(haveTimeSeries ? tr("Time") : tr("Time (disabled)"));

  // A new image means the old playback position is meaningless, so halt rather
  // than carrying on into a different series.
  m_player.stop();
  m_player.setRange(static_cast<uint32_t>(std::max(0, minT)), static_cast<uint32_t>(std::max(0, maxT)));
  syncPlaybackUi();
  if (m_playPauseButton) {
    m_playPauseButton->setEnabled(haveTimeSeries);
    m_stopButton->setEnabled(haveTimeSeries);
    m_fpsSpinner->setEnabled(haveTimeSeries);
    m_loopCheckbox->setEnabled(haveTimeSeries);
    m_dropFramesCheckbox->setEnabled(haveTimeSeries);
  }

  m_latestRequestSeq = 0;
  // Re-point the loader unconditionally, including for single-timepoint files.
  // Skipping it there would leave the loader still prefetching the previously
  // opened series against a reader for a file that is no longer displayed.
  if (m_loader && m_scene) {
    m_loader->setSeries(loadSpec,
                        reader,
                        static_cast<uint32_t>(std::max(0, minT)),
                        static_cast<uint32_t>(std::max(0, maxT)),
                        static_cast<uint32_t>(std::max(0, currentT)));
  }
  // setSeries reconciles against whatever is already resident, so take the
  // snapshot after it rather than before.
  refreshCacheStatus();
}

void
QTimelineWidget::setTime(int t, bool blockSignals)
{
  m_TimeSlider->setValue(t, blockSignals);
}

void
QTimelineWidget::setPrefetchConfig(const TimeSeriesLoader::PrefetchConfig& config)
{
  if (m_loader) {
    m_loader->setPrefetchConfig(config);
  }
}

void
QTimelineWidget::cancelPrefetch()
{
  if (m_loader) {
    m_loader->cancelPrefetch();
  }
}

void
QTimelineWidget::OnTimeChanged(int newTime)
{
  if (!m_scene || !m_loader) {
    return;
  }
  if (m_scene->m_timeLine.currentTime() == newTime) {
    return;
  }

  // While the user is dragging the slider handle, only fetch frames that are
  // already resident (RAM or disk). Frames that would require a network fetch
  // are skipped until the drag ends -- otherwise every intermediate position
  // over remote uncached data kicks off a load, and the view drags behind the
  // handle. The sliderReleased handler commits whatever the drag lands on.
  //
  // Keyboard, wheel, spinner, and programmatic changes all leave isSliderDown
  // false, so they go through unchanged.
  if (m_TimeSlider->isSliderDown()) {
    const TimepointStatus s = m_loader->status(static_cast<uint32_t>(std::max(0, newTime)));
    if (s != TimepointStatus::RamCached && s != TimepointStatus::DiskCached) {
      return;
    }
  }

  // Fire and forget. The load happens on the loader thread; onLoadComplete
  // installs the volume when it arrives. No wait cursor, because the UI is not
  // blocked any more.
  m_playbackCursorTime = static_cast<uint32_t>(std::max(0, newTime));
  m_havePlaybackCursor = true;
  m_latestRequestSeq = m_loader->requestTime(m_playbackCursorTime);
}

void
QTimelineWidget::onLoadComplete(uint32_t time, std::shared_ptr<ImageXYZC> image, uint64_t seq)
{
  if (!m_scene || !image) {
    return;
  }
  // Discard a completion the user has already scrubbed past.
  if (seq != m_latestRequestSeq) {
    return;
  }

  // The LUT remap reads the outgoing volume's histograms, so it has to happen
  // here at display time rather than on the loader thread.
  if (!applyTimeStepToScene(m_scene, image, m_qrendersettings ? m_qrendersettings->renderSettings() : nullptr)) {
    // The volume was rejected (e.g. a channel-count mismatch, which should not
    // happen for time steps of one source). Treat it as a failed load so the
    // slider does not sit on a time the scene never reached.
    onLoadFailed(time, seq);
    return;
  }

  m_scene->m_timeLine.setCurrentTime(static_cast<int32_t>(time));
  m_playbackCursorTime = time;
  m_havePlaybackCursor = true;
  m_playbackDisplayPending = false;

  m_loadSpec.time = time;

  // update the AppearanceSettings channel gui with new Histograms
  emit timeChanged(static_cast<int>(time));
}

void
QTimelineWidget::onLoadFailed(uint32_t time, uint64_t seq)
{
  if (seq != m_latestRequestSeq) {
    return;
  }
  LOG_DEBUG << "Failed to open " << m_loadSpec.filepath << " at scene " << m_loadSpec.scene << " at time " << time;

  // Put the slider back where the scene actually is, rather than leaving it
  // pointing at a time we never managed to load.
  if (m_scene) {
    const uint32_t displayedTime = static_cast<uint32_t>(std::max(0, m_scene->m_timeLine.currentTime()));
    m_playbackCursorTime = displayedTime;
    m_havePlaybackCursor = true;
    m_playbackDisplayPending = false;
    setTime(static_cast<int>(displayedTime), /*blockSignals=*/true);
  }
}

QTimelineDockWidget::QTimelineDockWidget(QWidget* parent, QRenderSettings* qrs, TimeSeriesSettingsData* settings)
  : QDockWidget(parent)
  , m_TimelineWidget(this, qrs, settings)
{
  setWindowTitle(tr("Time"));

  m_TimelineWidget.setParent(this);

  setWidget(&m_TimelineWidget);
}
