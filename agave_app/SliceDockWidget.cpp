#include "SliceDockWidget.h"

#include "Controls.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QSlider>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace
{
QString
axisName(SliceViewMode mode)
{
  switch (mode) {
    case SliceViewMode::X:
      return QStringLiteral("X");
    case SliceViewMode::Y:
      return QStringLiteral("Y");
    case SliceViewMode::Z:
      return QStringLiteral("Z");
    default:
      return QString();
  }
}
}

SliceDockWidget::SliceDockWidget(QWidget* parent)
  : QDockWidget(parent)
  , m_clockOrigin(std::chrono::steady_clock::now())
{
  setWindowTitle(tr("Slice"));

  auto* contents = new QWidget(this);
  auto* layout = new QVBoxLayout(contents);
  layout->setContentsMargins(6, 6, 6, 6);

  m_sliceSlider = new QIntSlider(contents);
  m_sliceSlider->setRange(0, 0);
  m_sliceSlider->setSingleStep(1);
  m_sliceSlider->setTracking(true);
  m_sliceSlider->setTickPosition(QSlider::TicksBelow);
  m_sliceSlider->setToolTip(tr("Set the displayed slice"));
  m_sliceSlider->setStatusTip(tr("Set the displayed slice"));
  layout->addWidget(m_sliceSlider);

  auto* playbackRow = new QHBoxLayout();
  m_playPauseButton = new QToolButton(contents);
  m_playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
  m_playPauseButton->setToolTip(tr("Play or pause through slices"));
  m_playPauseButton->setStatusTip(tr("Play or pause through slices"));
  playbackRow->addWidget(m_playPauseButton);

  m_fpsSpinner = new QSpinBox(contents);
  m_fpsSpinner->setRange(1, 120);
  m_fpsSpinner->setValue(10);
  m_fpsSpinner->setSuffix(tr(" fps"));
  m_fpsSpinner->setToolTip(tr("Target slice playback frame rate"));
  playbackRow->addWidget(m_fpsSpinner);

  m_loopCheckbox = new QCheckBox(tr("Loop"), contents);
  m_loopCheckbox->setChecked(true);
  m_loopCheckbox->setToolTip(tr("Wrap to the first slice after the last slice"));
  playbackRow->addWidget(m_loopCheckbox);
  playbackRow->addStretch(1);
  layout->addLayout(playbackRow);
  setWidget(contents);

  TimeSeriesPlayer::Config config;
  config.mode = TimeSeriesPlayer::Mode::ShowEveryFrame;
  config.fps = static_cast<float>(m_fpsSpinner->value());
  config.loop = m_loopCheckbox->isChecked();
  m_player.setConfig(config);
  m_player.setRange(0, 0);

  m_playbackTimer = new QTimer(this);
  m_playbackTimer->setTimerType(Qt::PreciseTimer);
  m_playbackTimer->setInterval(5);

  connect(m_sliceSlider, &QIntSlider::valueChanged, this, [this](int index) {
    emit sliceChanged(m_mode, index);
  });
  connect(m_playPauseButton, &QToolButton::pressed, this, [this]() { togglePlayPause(); });
  connect(m_playbackTimer, &QTimer::timeout, this, [this]() { onPlaybackTick(); });

  const auto updateConfig = [this]() {
    TimeSeriesPlayer::Config next = m_player.config();
    next.fps = static_cast<float>(m_fpsSpinner->value());
    next.loop = m_loopCheckbox->isChecked();
    m_player.setConfig(next);
  };
  connect(m_fpsSpinner, QOverload<int>::of(&QSpinBox::valueChanged), this, [updateConfig](int) { updateConfig(); });
  connect(m_loopCheckbox, &QCheckBox::toggled, this, [updateConfig](bool) { updateConfig(); });
  syncPlaybackUi();
}

void
SliceDockWidget::setSliceState(const SliceViewState& state)
{
  if (!state.isSingleSlice()) {
    stopPlayback();
    return;
  }

  const bool rangeChanged = m_mode != state.mode || m_sliceSlider->maximum() != state.activeDimension() - 1;
  m_mode = state.mode;
  setWindowTitle(tr("%1 Slice").arg(axisName(m_mode)));
  const QSignalBlocker sliderBlocker(m_sliceSlider);
  m_sliceSlider->setSuffix(tr(" / %1").arg(state.activeDimension() - 1));
  m_sliceSlider->setRange(0, std::max(0, state.activeDimension() - 1));
  m_sliceSlider->setTickInterval(std::max(1, state.activeDimension() / 10));
  m_sliceSlider->setValue(state.activeIndex(), true);
  m_player.setRange(0, static_cast<uint32_t>(std::max(0, state.activeDimension() - 1)));

  if (rangeChanged) {
    stopPlayback();
  }
  const bool canPlay = state.activeDimension() > 1;
  m_sliceSlider->setEnabled(canPlay);
  m_playPauseButton->setEnabled(canPlay);
  m_fpsSpinner->setEnabled(canPlay);
  m_loopCheckbox->setEnabled(canPlay);
}

void
SliceDockWidget::stopPlayback()
{
  m_player.stop();
  syncPlaybackUi();
}

uint64_t
SliceDockWidget::nowMs() const
{
  return static_cast<uint64_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_clockOrigin).count());
}

void
SliceDockWidget::togglePlayPause()
{
  if (!m_sliceSlider || m_sliceSlider->maximum() <= 0) {
    return;
  }
  if (m_player.isPlaying()) {
    m_player.pause();
  } else {
    m_player.play(static_cast<uint32_t>(std::max(0, m_sliceSlider->value())), nowMs());
  }
  syncPlaybackUi();
}

void
SliceDockWidget::onPlaybackTick()
{
  if (!m_sliceSlider) {
    return;
  }
  const uint32_t current = static_cast<uint32_t>(std::max(0, m_sliceSlider->value()));
  const std::optional<uint32_t> next = m_player.advance(nowMs(), current, [](uint32_t) { return true; });
  if (next) {
    m_sliceSlider->setValue(static_cast<int>(*next), true);
    emit sliceChanged(m_mode, static_cast<int>(*next));
  }
  if (!m_player.isPlaying()) {
    syncPlaybackUi();
  }
}

void
SliceDockWidget::syncPlaybackUi()
{
  const bool playing = m_player.isPlaying();
  m_playPauseButton->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
  if (playing && !m_playbackTimer->isActive()) {
    m_playbackTimer->start();
  } else if (!playing && m_playbackTimer->isActive()) {
    m_playbackTimer->stop();
  }
}

