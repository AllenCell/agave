#include "CacheStatusSlider.h"

#include <QEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTimer>

#include <algorithm>

namespace {

// Height of the painted strip, and how far above the slider's bottom edge it
// sits. Small enough to read as an annotation rather than a second control.
constexpr int kStripHeight = 3;
constexpr int kStripBottomMargin = 1;
// QStyle::SC_SliderGroove reports the outer groove. The dark Windows/Qt style
// paints the visible rail inset from that rectangle; match the painted pixels so
// the colored strip ends line up with the track.
constexpr int kGroovePaintInset = 2;

// Repaint coalescing interval. Prefetch emits one status change per timepoint, so
// without this a long series would repaint dozens of times in quick succession.
constexpr int kRepaintCoalesceMs = 50;

void
initSliderStyleOption(const QSlider& slider, QStyleOptionSlider& opt)
{
  opt.initFrom(&slider);
  opt.rect = slider.rect();
  opt.orientation = slider.orientation();
  opt.minimum = slider.minimum();
  opt.maximum = slider.maximum();
  opt.sliderPosition = slider.sliderPosition();
  opt.sliderValue = slider.value();
  opt.singleStep = slider.singleStep();
  opt.pageStep = slider.pageStep();
  opt.tickPosition = slider.tickPosition();
  opt.tickInterval = slider.tickInterval();
  opt.upsideDown = slider.invertedAppearance();
  opt.subControls = QStyle::SC_SliderGroove;
}

} // namespace

CacheStatusStrip::CacheStatusStrip(QWidget* parent)
  : QWidget(parent)
  , m_repaintTimer(new QTimer(this))
{
  // Purely decorative: never intercept clicks meant for the slider underneath.
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_NoSystemBackground);
  setFocusPolicy(Qt::NoFocus);

  m_repaintTimer->setSingleShot(true);
  m_repaintTimer->setInterval(kRepaintCoalesceMs);
  connect(m_repaintTimer, &QTimer::timeout, this, [this]() { update(); });

  if (parent) {
    parent->installEventFilter(this);
    setGeometry(parent->rect());
  }
}

bool
CacheStatusStrip::eventFilter(QObject* watched, QEvent* event)
{
  if (watched == parentWidget() && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
    setGeometry(parentWidget()->rect());
  }
  return QWidget::eventFilter(watched, event);
}

void
CacheStatusStrip::scheduleRepaint()
{
  if (!m_repaintTimer->isActive()) {
    m_repaintTimer->start();
  }
}

void
CacheStatusStrip::setStatuses(uint32_t minTime, const std::vector<TimepointStatus>& statuses)
{
  m_minTime = minTime;
  m_statuses = statuses;
  scheduleRepaint();
}

void
CacheStatusStrip::setStatus(uint32_t time, TimepointStatus status)
{
  if (time < m_minTime) {
    return;
  }
  const size_t index = static_cast<size_t>(time - m_minTime);
  if (index >= m_statuses.size()) {
    return;
  }
  if (m_statuses[index] == status) {
    return;
  }
  m_statuses[index] = status;
  scheduleRepaint();
}

void
CacheStatusStrip::clearStatuses()
{
  m_statuses.clear();
  scheduleRepaint();
}

void
CacheStatusStrip::paintEvent(QPaintEvent* /*event*/)
{
  if (m_statuses.empty()) {
    return;
  }

  // Align horizontally with the same groove/track sub-control that Qt asks the
  // active style to draw for the slider underneath.
  int left = 0;
  int usableWidth = width();
  if (auto* slider = qobject_cast<QSlider*>(parentWidget())) {
    QStyleOptionSlider opt;
    initSliderStyleOption(*slider, opt);
    const QRect groove = slider->style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, slider);
    if (groove.isValid() && groove.width() > 0) {
      const int inset = groove.width() > 2 * kGroovePaintInset ? kGroovePaintInset : 0;
      left = groove.left() + inset;
      usableWidth = groove.width() - 2 * inset;
    }
  }

  const int count = static_cast<int>(m_statuses.size());
  if (count <= 0 || usableWidth <= 0) {
    return;
  }

  const int top = height() - kStripHeight - kStripBottomMargin;
  if (top < 0) {
    return;
  }

  QPainter painter(this);
  // Traffic-light palette: at a glance the strip tells you what you have in RAM
  // (green), what is on disk (cyan -- cached but a tier away), what is on its way
  // (yellow), and what is not there yet (red). Queued and Loading share a color
  // because the user's question ("is it working?") is answered by either state.
  const QColor ramCached(60, 180, 75);   // in-memory
  const QColor diskCached(60, 180, 200); // on-disk, cheap to reload
  const QColor inFlight(240, 210, 60);   // Queued or Loading
  const QColor notCached(200, 70, 60);   // not fetched
  // Failed gets a darker red so a permanent failure is visually distinct from a
  // frame that just has not been fetched yet.
  const QColor failedDetailed(120, 30, 30);

  for (int i = 0; i < count; ++i) {
    QColor color;
    switch (m_statuses[static_cast<size_t>(i)]) {
      case TimepointStatus::RamCached:
        color = ramCached;
        break;
      case TimepointStatus::DiskCached:
        color = diskCached;
        break;
      case TimepointStatus::Queued:
      case TimepointStatus::Loading:
        color = inFlight;
        break;
      case TimepointStatus::Failed:
        color = failedDetailed;
        break;
      case TimepointStatus::NotCached:
        color = notCached;
        break;
      default:
        continue;
    }

    // Compute both edges from the same mapping so segments tile without gaps or
    // overlaps, whatever the rounding.
    const int x0 = left + (i * usableWidth) / count;
    const int x1 = left + ((i + 1) * usableWidth) / count;
    const int segmentWidth = std::max(1, x1 - x0);
    painter.fillRect(QRect(x0, top, segmentWidth, kStripHeight), color);
  }
}

TimeSliderWithCacheStatus::TimeSliderWithCacheStatus(QWidget* parent)
  : QIntSlider(parent)
  , m_strip(new CacheStatusStrip(&sliderWidget()))
{
  layoutStrip();
  m_strip->show();

  connect(&sliderWidget(), &QSlider::sliderPressed, this, &TimeSliderWithCacheStatus::sliderPressed);
  connect(&sliderWidget(), &QSlider::sliderReleased, this, &TimeSliderWithCacheStatus::sliderReleased);
}

bool
TimeSliderWithCacheStatus::isSliderDown() const
{
  return sliderWidget().isSliderDown();
}

void
TimeSliderWithCacheStatus::layoutStrip()
{
  m_strip->setGeometry(sliderWidget().rect());
}

void
TimeSliderWithCacheStatus::resizeEvent(QResizeEvent* event)
{
  QIntSlider::resizeEvent(event);
  layoutStrip();
}

void
TimeSliderWithCacheStatus::setStatuses(uint32_t minTime, const std::vector<TimepointStatus>& statuses)
{
  layoutStrip();
  m_strip->setStatuses(minTime, statuses);
}

void
TimeSliderWithCacheStatus::setStatus(uint32_t time, TimepointStatus status)
{
  m_strip->setStatus(time, status);
}

void
TimeSliderWithCacheStatus::clearStatuses()
{
  m_strip->clearStatuses();
}
