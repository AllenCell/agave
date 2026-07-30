#include "TimelineDockWidget.h"

#include "Controls.h"
#include "QRenderSettings.h"
#include "TimeSeriesLoaderBridge.h"

#include "renderlib/AppScene.h"
#include "renderlib/ImageXYZC.h"
#include "renderlib/Logging.h"
#include "renderlib/RenderSettings.h"
#include "renderlib/io/ApplyVolumeToScene.h"

#include <QScrollArea>

QTimelineWidget::QTimelineWidget(QWidget* pParent, QRenderSettings* qrs)
  : QWidget(pParent)
  , m_MainLayout()
  , m_qrendersettings(qrs)
  , m_scene(nullptr)
  , m_loader(std::make_unique<TimeSeriesLoader>())
  , m_bridge(new TimeSeriesLoaderBridge(this))
{
  // Create main layout
  m_MainLayout.setAlignment(Qt::AlignTop);
  setLayout(&m_MainLayout);

  QScrollArea* scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);

  auto* fullLayout = new QVBoxLayout();

  m_TimeSlider = new QIntSlider();
  m_TimeSlider->setStatusTip(tr("Set current time sample"));
  m_TimeSlider->setToolTip(tr("Set current time sample"));
  // Loading is asynchronous now, so live scrubbing is safe: dragging issues
  // interactive requests and the loader coalesces them, cancelling the load for
  // any position the user has already left.
  m_TimeSlider->setTracking(true);
  m_TimeSlider->setRange(0, 0);
  m_TimeSlider->setSingleStep(1);
  m_TimeSlider->setTickPosition(QSlider::TickPosition::TicksBelow);
  fullLayout->addWidget(m_TimeSlider);

  QObject::connect(m_TimeSlider, &QIntSlider::valueChanged, [this](int t) { this->OnTimeChanged(t); });

  connect(m_bridge,
          &TimeSeriesLoaderBridge::interactiveLoadComplete,
          this,
          [this](uint32_t time, std::shared_ptr<ImageXYZC> image, uint64_t seq) { onLoadComplete(time, image, seq); });
  connect(m_bridge, &TimeSeriesLoaderBridge::interactiveLoadFailed, this, [this](uint32_t time, uint64_t seq) {
    onLoadFailed(time, seq);
  });

  m_loader->addObserver(m_bridge);

  scrollArea->setLayout(fullLayout);

  m_MainLayout.addWidget(scrollArea, 1, 0);
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

  m_TimeSlider->setRange(minT, maxT);
  m_TimeSlider->setValue(currentT, true);
  m_TimeSlider->setTickInterval((maxT - minT) / 10);
  m_TimeSlider->setSingleStep(1);

  // disable the slider if there is less than 2 time samples.
  m_TimeSlider->setEnabled(maxT > minT);
  this->parentWidget()->setWindowTitle(maxT > minT ? tr("Time") : tr("Time (disabled)"));

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

  // Fire and forget. The load happens on the loader thread; onLoadComplete
  // installs the volume when it arrives. No wait cursor, because the UI is not
  // blocked any more.
  m_latestRequestSeq = m_loader->requestTime(static_cast<uint32_t>(std::max(0, newTime)));
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
  if (!applyVolumeToScene(m_scene, image, m_qrendersettings ? m_qrendersettings->renderSettings() : nullptr)) {
    // The volume was rejected (e.g. a channel-count mismatch, which should not
    // happen for time steps of one source). Treat it as a failed load so the
    // slider does not sit on a time the scene never reached.
    onLoadFailed(time, seq);
    return;
  }

  m_scene->m_timeLine.setCurrentTime(static_cast<int32_t>(time));

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
    setTime(m_scene->m_timeLine.currentTime(), /*blockSignals=*/true);
  }
}

QTimelineDockWidget::QTimelineDockWidget(QWidget* parent, QRenderSettings* qrs)
  : QDockWidget(parent)
  , m_TimelineWidget(this, qrs)
{
  setWindowTitle(tr("Time"));

  m_TimelineWidget.setParent(this);

  setWidget(&m_TimelineWidget);
}
