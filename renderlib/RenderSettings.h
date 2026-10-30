#pragma once

#include "DenoiseParams.h"
#include "Flags.h"
#include "SliceViewState.h"

class RenderSettings
{
public:
  RenderSettings();
  RenderSettings(const RenderSettings& Other);
  RenderSettings& operator=(const RenderSettings& Other);

  Flags m_DirtyFlags;
  PathTraceRenderSettings m_RenderSettings;
  DenoiseParams m_DenoiseParams;
  SliceViewState m_SliceView;

  int GetNoIterations() const { return m_NoIterations; }
  void SetNoIterations(const int& NoIterations) { m_NoIterations = NoIterations; }

private:
  int m_NoIterations;
};
