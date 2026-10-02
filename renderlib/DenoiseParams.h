#pragma once

enum class MultichannelBlendMode
{
  Max = 0,
  Weighted = 1
};

class DenoiseParams
{
public:
  bool m_Enabled;
  float m_Noise;
  float m_LerpC;
  int m_WindowRadius;
  float m_WindowArea;
  float m_InvWindowArea;
  float m_WeightThreshold;
  float m_LerpThreshold;

public:
  DenoiseParams()
  {
    m_Enabled = true;
    m_Noise = 0.05f;
    m_LerpC = 0.01f;
    m_WindowRadius = 6;
    m_WindowArea = (2.0f * m_WindowRadius + 1.0f) * (2.0f * m_WindowRadius + 1.0f);
    m_InvWindowArea = 1.0f / m_WindowArea;
    m_WeightThreshold = 0.1f;
    m_LerpThreshold = 0.0f;
  }

  DenoiseParams& operator=(const DenoiseParams& Other)
  {
    m_Enabled = Other.m_Enabled;
    m_Noise = Other.m_Noise;
    m_LerpC = Other.m_LerpC;
    m_WindowRadius = Other.m_WindowRadius;
    m_WindowArea = Other.m_WindowArea;
    m_InvWindowArea = Other.m_InvWindowArea;
    m_WeightThreshold = Other.m_WeightThreshold;
    m_LerpThreshold = Other.m_LerpThreshold;

    return *this;
  }

  void SetWindowRadius(const int& WindowRadius)
  {
    m_WindowRadius = WindowRadius;
    m_WindowArea = (2.0f * m_WindowRadius + 1.0f) * (2.0f * m_WindowRadius + 1.0f);
    m_InvWindowArea = 1.0f / m_WindowArea;
  }
};

struct PathTraceRenderSettings
{
  float m_DensityScale;
  int m_ShadingType;
  float m_StepSizeFactor;
  float m_StepSizeFactorShadow;
  float m_GradientDelta;
  float m_GradientFactor;
  bool m_ShowLightsBackground;
  bool m_InterpolatedVolumeSampling;
  bool m_UseWoodcockTracking;
  MultichannelBlendMode m_MultichannelBlendMode;

  PathTraceRenderSettings()
    : m_DensityScale(8.5f)
    , m_ShadingType(2)
    , m_StepSizeFactor(4.0f)
    , m_StepSizeFactorShadow(4.0f)
    , m_GradientDelta(4.0f)
    , m_GradientFactor(0.5f)
    , m_ShowLightsBackground(false)
    , m_InterpolatedVolumeSampling(true)
    , m_UseWoodcockTracking(true)
    , m_MultichannelBlendMode(MultichannelBlendMode::Max)
  {
  }
};
