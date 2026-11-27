#pragma once

#include "renderlib/SliceViewState.h"
#include "renderlib/io/TimeSeriesPlayer.h"

#include <QDockWidget>

#include <chrono>

class QCheckBox;
class QIntSlider;
class QSpinBox;
class QTimer;
class QToolButton;

class SliceDockWidget : public QDockWidget
{
  Q_OBJECT

public:
  explicit SliceDockWidget(QWidget* parent = nullptr);

  void setSliceState(const SliceViewState& state);
  void stopPlayback();

signals:
  void sliceChanged(SliceViewMode mode, int index);

private:
  void togglePlayPause();
  void onPlaybackTick();
  void syncPlaybackUi();
  uint64_t nowMs() const;

  SliceViewMode m_mode = SliceViewMode::Z;
  QIntSlider* m_sliceSlider = nullptr;
  QToolButton* m_playPauseButton = nullptr;
  QSpinBox* m_fpsSpinner = nullptr;
  QCheckBox* m_loopCheckbox = nullptr;
  QTimer* m_playbackTimer = nullptr;
  TimeSeriesPlayer m_player;
  std::chrono::steady_clock::time_point m_clockOrigin;
};

