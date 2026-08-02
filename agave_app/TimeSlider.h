#pragma once

#include "Controls.h"

#include "renderlib/io/TimeSeriesLoader.h"

#include <QWidget>

#include <vector>

class QTimer;

// Thin strip drawn along the bottom of a slider showing per-timepoint fetch
// state as a traffic light: green in-memory, cyan on-disk, yellow being
// fetched, red not fetched.
//
// Implemented as a mouse-transparent overlay parented to the slider rather than
// by subclassing QSlider, so QIntSlider's composite structure is left alone and
// the 20-odd other sliders in the app are unaffected. It paints only in a few
// pixels at the bottom of the slider's rect, horizontally aligned to the groove,
// so it never collides with the handle.
class CacheStatusStrip : public QWidget
{
  Q_OBJECT

public:
  explicit CacheStatusStrip(QWidget* parent);

  // `statuses` is indexed from `minTime`.
  void setStatuses(uint32_t minTime, const std::vector<TimepointStatus>& statuses);
  void setStatus(uint32_t time, TimepointStatus status);
  void clearStatuses();

  // Failed paints with its own distinct color so a permanent failure is
  // visually distinct from a frame that just has not been fetched yet.

protected:
  void paintEvent(QPaintEvent* event) override;
  // Watches the slider we overlay so the strip follows its geometry. Relying on
  // the parent QIntSlider's resizeEvent would depend on layout activation order.
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  // Repaints are coalesced: a long series produces one status change per
  // timepoint and repainting per change would be wasteful during prefetch.
  void scheduleRepaint();

  uint32_t m_minTime = 0;
  std::vector<TimepointStatus> m_statuses;
  QTimer* m_repaintTimer;
};

// QIntSlider plus a cache status strip.
class TimeSlider : public QIntSlider
{
  Q_OBJECT

public:
  explicit TimeSlider(QWidget* parent = nullptr);

  void setStatuses(uint32_t minTime, const std::vector<TimepointStatus>& statuses);
  void setStatus(uint32_t time, TimepointStatus status);
  void clearStatuses();

  // True while the user is actively dragging the slider handle with the mouse.
  // Keyboard, wheel, spinner, and programmatic changes leave this false.
  bool isSliderDown() const;

signals:
  // Forwarded from the inner QSlider so callers can react to drag start/end
  // without reaching through the protected sliderWidget() accessor.
  void sliderPressed();
  void sliderReleased();

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  void layoutStrip();

  CacheStatusStrip* m_strip;
};
