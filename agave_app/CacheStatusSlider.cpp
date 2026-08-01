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

// Repaint coalescing interval. Prefetch emits one status change per timepoint, so
// without this a long series would repaint dozens of times in quick succession.
constexpr int kRepaintCoalesceMs = 50;

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
CacheStatusStrip::setDetailed(bool detailed)
{
  if (m_detailed == detailed) {
    return;
  }
  m_detailed = detailed;
  scheduleRepaint();
}

void
CacheStatusStrip::paintEvent(QPaintEvent* /*event*/)
{
  if (m_statuses.empty()) {
    return;
  }

  // Align horizontally with the groove of the slider we overlay, so a segment
  // sits under the handle position for its timepoint.
  int left = 0;
  int usableWidth = width();
  if (auto* slider = qobject_cast<QSlider*>(parentWidget())) {
    QStyleOptionSlider opt;
    opt.initFrom(slider);
    opt.orientation = slider->orientation();
    opt.minimum = slider->minimum();
    opt.maximum = slider->maximum();
    opt.sliderPosition = slider->sliderPosition();
    opt.sliderValue = slider->value();
    const QRect groove = slider->style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, slider);
    if (groove.width() > 0) {
      left = groove.left();
      usableWidth = groove.width();
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
  const QColor cached = palette().color(QPalette::Highlight);
  const QColor queued = QColor(cached.red(), cached.green(), cached.blue(), 70);
  const QColor loading = QColor(cached.red(), cached.green(), cached.blue(), 150);
  // Disk-resident: dimmer than in-memory, since it is available but not instant.
  // This is the normal resting state for a series larger than the memory budget,
  // so it must be visible rather than blank.
  const QColor onDisk = QColor(cached.red(), cached.green(), cached.blue(), 110);
  const QColor failed(200, 60, 60);

  for (int i = 0; i < count; ++i) {
    QColor color;
    switch (m_statuses[static_cast<size_t>(i)]) {
      case TimepointStatus::RamCached:
        color = cached;
        break;
      case TimepointStatus::DiskCached:
        // Shown in both modes: in the simple two-state view it still counts as
        // "you have this data", just not in memory.
        color = onDisk;
        break;
      case TimepointStatus::Queued:
        if (!m_detailed) {
          continue;
        }
        color = queued;
        break;
      case TimepointStatus::Loading:
        if (!m_detailed) {
          continue;
        }
        color = loading;
        break;
      case TimepointStatus::Failed:
        if (!m_detailed) {
          continue;
        }
        color = failed;
        break;
      case TimepointStatus::NotCached:
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

void
TimeSliderWithCacheStatus::setDetailedStatus(bool detailed)
{
  m_strip->setDetailed(detailed);
}
