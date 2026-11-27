#pragma once

#include "Defines.h"

#include <stdio.h>
#include <string.h> //for memset etc

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>

#define MAX_NO_DURATIONS 30

// Format a millisecond duration as a fixed-point string rounded to the nearest
// 1/100th of a millisecond. Shared by the OpenGL and Vulkan render paths so
// their Performance statistics are formatted identically.
inline std::string
formatDurationMs(double milliseconds)
{
  std::ostringstream ss;
  ss << std::fixed << std::setprecision(2) << milliseconds;
  return ss.str();
}

class Timing
{
public:
  Timing()
    : m_NoDurations(MAX_NO_DURATIONS)
    , m_FilteredDuration(0.0f)
  {
    for (int i = 0; i < MAX_NO_DURATIONS; i++) {
      m_Durations[i] = 0.0f;
    }
  };

  Timing(std::string name)
  {
    m_Name = name;
    memset(m_Durations, 0, MAX_NO_DURATIONS * sizeof(float));

    m_NoDurations = 0;
    m_FilteredDuration = 0.0f;
  }

  virtual ~Timing() {};

  Timing& operator=(const Timing& Other)
  {
    if (this == &Other) {
      return *this;
    }

    m_Name = Other.m_Name;

    for (int i = 0; i < MAX_NO_DURATIONS; i++) {
      m_Durations[i] = Other.m_Durations[i];
    }

    m_NoDurations = Other.m_NoDurations;
    m_FilteredDuration = Other.m_FilteredDuration;

    return *this;
  }

  void AddDuration(const float& Duration)
  {
    float TempDurations[MAX_NO_DURATIONS];

    memcpy(TempDurations, m_Durations, MAX_NO_DURATIONS * sizeof(float));

    m_Durations[0] = Duration;

    //		m_FilteredDuration = Duration;
    //		return;

    float SumDuration = Duration;

    for (int i = 0; i < m_NoDurations - 1; i++) {
      m_Durations[i + 1] = TempDurations[i];
      SumDuration += TempDurations[i];
    }

    m_FilteredDuration = SumDuration / (float)m_NoDurations;

    m_NoDurations = std::min<int>(MAX_NO_DURATIONS, m_NoDurations + 1);
  }

  std::string filteredDurationAsString() { return formatDurationMs(m_FilteredDuration); }

  std::string m_Name;
  float m_Durations[MAX_NO_DURATIONS];
  int m_NoDurations;
  float m_FilteredDuration;
};

// on main thread, have one clock instance that calls tick on every iteration of main event loop
// then call mainwindow.gesture.setTimeIncrement(clock.timeIncrement);
struct Clock
{
  Clock()
    : time(0)
    , timeIncrement(0)
  {
    time = Clock::now();
  }

  static double now()
  {
    auto currentDateTime = std::chrono::system_clock::now();
    const auto ms = std::chrono::time_point_cast<std::chrono::milliseconds>(currentDateTime).time_since_epoch().count();
    return ms / 1000.0;
  }

  double tick()
  {
    double currentTime = Clock::now();

    timeIncrement = currentTime - time;
    time = currentTime;
    return timeIncrement;
  }

  double time;
  double timeIncrement;
};
