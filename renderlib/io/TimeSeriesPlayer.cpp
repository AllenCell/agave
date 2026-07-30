#include "TimeSeriesPlayer.h"

#include <algorithm>

void
TimeSeriesPlayer::setConfig(const Config& config)
{
  m_config = config;
  // Guard against a zero or negative rate turning frameIntervalMs into a
  // division by zero or an absurd interval.
  if (!(m_config.fps > 0.0f)) {
    m_config.fps = 1.0f;
  }
}

void
TimeSeriesPlayer::setRange(uint32_t minTime, uint32_t maxTime)
{
  m_minTime = minTime;
  m_maxTime = std::max(minTime, maxTime);
}

void
TimeSeriesPlayer::play(uint32_t fromTime, uint64_t nowMs)
{
  m_originTime = std::clamp(fromTime, m_minTime, m_maxTime);
  m_state = State::Playing;
  m_lastFrameMs = nowMs;
  m_droppedFrames = 0;
}

void
TimeSeriesPlayer::pause()
{
  if (m_state == State::Playing) {
    m_state = State::Paused;
  }
}

std::optional<uint32_t>
TimeSeriesPlayer::stop()
{
  if (m_state == State::Stopped) {
    return std::nullopt;
  }
  m_state = State::Stopped;
  return m_originTime;
}

uint64_t
TimeSeriesPlayer::frameIntervalMs() const
{
  const double interval = 1000.0 / static_cast<double>(m_config.fps);
  // Never report a zero interval: advance() would then fire every tick and
  // playback rate would be bounded only by the caller's timer.
  return static_cast<uint64_t>(std::max(1.0, interval));
}

std::optional<uint32_t>
TimeSeriesPlayer::nextTime(uint32_t time) const
{
  if (m_minTime == m_maxTime) {
    // Single-timepoint series: nothing to advance to. Loop would spin on the
    // same frame, so treat it as the end either way.
    return std::nullopt;
  }
  if (time >= m_maxTime) {
    if (m_config.loop) {
      return m_minTime;
    }
    return std::nullopt;
  }
  return time + 1;
}

std::optional<uint32_t>
TimeSeriesPlayer::advance(uint64_t nowMs, uint32_t currentTime, const ReadyPredicate& isReady)
{
  if (m_state != State::Playing) {
    return std::nullopt;
  }

  // Guard against a clock that went backwards (or a caller passing a stale
  // timestamp) leaving m_lastFrameMs in the future forever.
  if (nowMs < m_lastFrameMs) {
    m_lastFrameMs = nowMs;
    return std::nullopt;
  }

  const uint64_t interval = frameIntervalMs();
  if (nowMs - m_lastFrameMs < interval) {
    return std::nullopt;
  }

  std::optional<uint32_t> candidate = nextTime(currentTime);
  if (!candidate) {
    // End of the range with looping off.
    m_state = State::Stopped;
    return std::nullopt;
  }

  if (m_config.mode == Mode::ShowEveryFrame) {
    if (isReady && !isReady(*candidate)) {
      // Hold the current frame. Note we do NOT advance m_lastFrameMs here, so
      // the frame becomes due the moment it is ready.
      return std::nullopt;
    }
    m_lastFrameMs = nowMs;
    return candidate;
  }

  // RealTime: keep wall-clock rate by skipping frames that are not ready. Walk
  // forward at most one full pass so an entirely uncached series cannot spin
  // here, and so looping cannot cycle indefinitely within a single tick.
  const uint64_t span = static_cast<uint64_t>(m_maxTime - m_minTime) + 1;
  uint32_t probe = *candidate;
  for (uint64_t step = 0; step < span; ++step) {
    if (!isReady || isReady(probe)) {
      m_lastFrameMs = nowMs;
      return probe;
    }
    ++m_droppedFrames;
    std::optional<uint32_t> following = nextTime(probe);
    if (!following) {
      m_state = State::Stopped;
      return std::nullopt;
    }
    probe = *following;
  }

  // Nothing in the whole series is ready. Charge the elapsed slot so we do not
  // re-scan on every tick, and hold the current frame.
  m_lastFrameMs = nowMs;
  return std::nullopt;
}
