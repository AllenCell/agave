#include "TimelineDockWidget.h"

#include "Controls.h"
#include "QRenderSettings.h"

#include "renderlib/AppScene.h"
#include "renderlib/ImageXYZC.h"
#include "renderlib/Logging.h"
#include "renderlib/RenderSettings.h"

#include <QApplication>
#include <QScrollArea>

QTimelineWidget::QTimelineWidget(QWidget* pParent, QRenderSettings* qrs)
  : QWidget(pParent)
  , m_MainLayout()
  , m_qrendersettings(qrs)
  , m_scene(nullptr)
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
  m_TimeSlider->setTracking(false);
  m_TimeSlider->setRange(0, 0);
  m_TimeSlider->setSingleStep(1);
  m_TimeSlider->setTickPosition(QSlider::TickPosition::TicksBelow);
  fullLayout->addWidget(m_TimeSlider);

  QObject::connect(m_TimeSlider, &QIntSlider::valueChanged, [this](int t) { this->OnTimeChanged(t); });

  scrollArea->setLayout(fullLayout);

  m_MainLayout.addWidget(scrollArea, 1, 0);
}

void
QTimelineWidget::onNewImage(Scene* s, const LoadSpec& loadSpec, std::shared_ptr<IFileReader> reader)
{
  m_reader = reader;
  m_loadSpec = loadSpec;
  m_scene = s;

  int32_t minT = m_scene ? m_scene->m_timeLine.minTime() : 0;
  int32_t maxT = m_scene ? m_scene->m_timeLine.maxTime() : 0;

  m_TimeSlider->setTracking(false);
  m_TimeSlider->setRange(minT, maxT);
  m_TimeSlider->setValue(m_scene->m_timeLine.currentTime(), true);
  m_TimeSlider->setTickInterval((maxT - minT) / 10);
  m_TimeSlider->setSingleStep(1);

  // disable the slider if there is less than 2 time samples.
  m_TimeSlider->setEnabled(maxT > minT);
  this->parentWidget()->setWindowTitle(maxT > minT ? tr("Time") : tr("Time (disabled)"));
}

void
QTimelineWidget::setTime(int t)
{
  m_TimeSlider->setValue(t);
}

void
QTimelineWidget::OnTimeChanged(int newTime)
{
  if (!m_scene) {
    return;
  }
  if (m_scene->m_timeLine.currentTime() != newTime) {
    // assume (require!) that a new time sample will have same exact
    // channel configuration and dimensions as previous time.
    // we are just updating volume data.
    LoadSpec loadSpec = m_loadSpec;
    loadSpec.time = newTime;

    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    // Reuse the reader created at file-open time so we skip re-opening the file
    // and re-parsing metadata on every time-step.
    std::shared_ptr<ImageXYZC> image = FileReader::loadAndCache(loadSpec, m_reader);
    QApplication::restoreOverrideCursor();
    if (!image) {
      // TODO FIXME if we fail to set the new time, then reset the GUI to previous time
      LOG_DEBUG << "Failed to open " << loadSpec.filepath << " at scene " << loadSpec.scene << " at time " << newTime;
      return;
    }

    // remap LUTs to preserve absolute thresholding
    for (uint32_t i = 0; i < image->sizeC(); ++i) {
      GradientData& lutInfo = m_scene->m_material.m_gradientData[i];
      lutInfo.convert(m_scene->m_volume->channel(i)->m_histogram, image->channel(i)->m_histogram);
      image->channel(i)->generateFromGradientData(lutInfo);
    }

    m_scene->m_timeLine.setCurrentTime(newTime);
    m_scene->m_volume = image;

    // keep the local loadSpec up to date if successful load
    m_loadSpec = loadSpec;

    // update the AppearanceSettings channel gui with new Histograms
    emit timeChanged(newTime);

    m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(VolumeDataDirty);
    m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(TransferFunctionDirty);
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
