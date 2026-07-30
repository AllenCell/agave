#pragma once

#include <cstdint>
#include <functional>
#include <optional>

// Playback state machine for a time series.
//
// Deliberately Qt-free and clock-injected: the caller supplies "now" and a
// predicate answering whether a candidate frame is ready to display. That keeps
// the whole stall-versus-drop-frames decision, loop wrapping and stop-origin
// behaviour unit-testable with no timers, no threads and no I/O. The Qt side
// owns only a QTimer that calls advance() and acts on the result.
class TimeSeriesPlayer
{
public:
  enum class Mode
  {
    // Never skip a timepoint: if the next frame is not ready, hold the current
    // one and try again. Effective frame rate is whatever loading sustains.
    ShowEveryFrame,
    // Keep wall-clock rate: if the next frame is not ready when its slot comes
    // up, skip it. Smooth, but some timepoints may never be displayed.
    RealTime,
  };

  enum class State
  {
    Stopped,
    Playing,
    Paused,
  };

  struct Config
  {
    Mode mode = Mode::ShowEveryFrame;
    float fps = 10.0f;
    bool loop = true;
  };

  // True if the candidate timepoint can be displayed right now.
  using ReadyPredicate = std::function<bool(uint32_t)>;

  void setConfig(const Config& config);
  Config config() const { return m_config; }

  // Inclusive range of valid timepoints.
  void setRange(uint32_t minTime, uint32_t maxTime);

  // Begin playing from `fromTime`. Records it as the origin that stop() returns
  // to. `nowMs` seeds the frame clock.
  void play(uint32_t fromTime, uint64_t nowMs);
  void pause();
  // Halt and return the timepoint playback started from, so the caller can
  // restore it. Returns nullopt if playback was not running.
  std::optional<uint32_t> stop();

  State state() const { return m_state; }
  bool isPlaying() const { return m_state == State::Playing; }

  // Called from the caller's clock tick. Returns the timepoint to display next,
  // or nullopt to hold the current frame.
  //
  // Behaviour by mode when the next frame is not ready:
  //   ShowEveryFrame - returns nullopt until it becomes ready, then advances.
  //                    The frame interval is measured from the moment a frame is
  //                    actually shown, so a slow load does not cause a burst of
  //                    catch-up frames afterwards.
  //   RealTime       - once the slot elapses, advances regardless, skipping past
  //                    frames that are not ready.
  //
  // Stops (returning nullopt and entering Stopped) when it reaches the end of
  // the range with looping disabled.
  std::optional<uint32_t> advance(uint64_t nowMs, uint32_t currentTime, const ReadyPredicate& isReady);

  // Number of frames skipped because they were not ready in RealTime mode.
  // Useful for telling the user that playback is outrunning loading.
  uint64_t droppedFrames() const { return m_droppedFrames; }
  void resetDroppedFrames() { m_droppedFrames = 0; }

private:
  uint64_t frameIntervalMs() const;
  // Next timepoint after `time`, honouring loop/clamp. Returns nullopt when
  // playback should stop at the end.
  std::optional<uint32_t> nextTime(uint32_t time) const;

  Config m_config;
  State m_state = State::Stopped;
  uint32_t m_minTime = 0;
  uint32_t m_maxTime = 0;
  uint32_t m_originTime = 0;
  uint64_t m_lastFrameMs = 0;
  uint64_t m_droppedFrames = 0;
};
