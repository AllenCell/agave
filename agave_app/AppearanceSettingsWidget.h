#pragma once

#include "Controls.h"
#include "ObjectTransformMode.h"
#include "renderlib/Colormap.h"
#include "renderlib/GradientData.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>

#include <memory>

class QRenderSettings;
class ImageXYZC;
class RangeWidget;
class RenderSettings;
class Scene;
class SceneObject;
class Section;

enum class Axis : std::uint8_t
{
  X = 0,
  Y = 1,
  Z = 2
};

class QAppearanceSettingsWidget : public QGroupBox
{
  Q_OBJECT

public:
  QAppearanceSettingsWidget(QWidget* pParent = nullptr,
                            QRenderSettings* qrs = nullptr,
                            RenderSettings* rs = nullptr,
                            QAction* pToggleRotateAction = nullptr,
                            QAction* pToggleTranslateAction = nullptr);

  void onNewImage(Scene* scene);
  void onTimeChanged(int newTime);

public slots:
  void OnRenderBegin();
  void OnSetDensityScale(double DensityScale);
  void OnTransferFunctionChanged();
  void OnSetRendererType(int Index);
  void OnSetShadingType(int Index);
  void OnSetGradientFactor(double GradientFactor);
  void OnSetStepSizePrimaryRay(const double& StepSizePrimaryRay);
  void OnSetStepSizeSecondaryRay(const double& StepSizeSecondaryRay);

public:
  void OnBackgroundColorChanged(const QColor& color);
  void OnBoundingBoxColorChanged(const QColor& color);
  void OnShowBoundsChecked(bool isChecked);
  void OnShowScaleBarChecked(bool isChecked);
  void OnShowTimeStampChecked(bool isChecked);
  void OnTimeStampFormatChanged(int index);
  void OnInterpolateChecked(bool isChecked);
  void OnWoodcockTrackingChecked(bool isChecked);
  void OnMultichannelBlendChanged(int index);
  void OnDiffuseColorChanged(int i, const QColor& color);
  void OnSpecularColorChanged(int i, const QColor& color);
  void OnEmissiveColorChanged(int i, const QColor& color);
  void OnUpdateLut(int i, const std::vector<LutControlPoint>& stops);

  void OnOpacityChanged(int i, double opacity);
  void OnRoughnessChanged(int i, double roughness);
  void OnChannelChecked(int i, bool is_checked);

  void OnSetAreaLightTheta(double value);
  void OnSetAreaLightPhi(double value);
  void OnSetAreaLightSize(double value);
  void OnSetAreaLightDistance(double value);
  void OnSetAreaLightColor(double intensity, const QColor& color);
  void OnSetSkyLightTopColor(double intensity, const QColor& color);
  void OnSetSkyLightMidColor(double intensity, const QColor& color);
  void OnSetSkyLightBotColor(double intensity, const QColor& color);

  void OnSetRoiXMax(int value);
  void OnSetRoiYMax(int value);
  void OnSetRoiZMax(int value);
  void OnSetRoiXMin(int value);
  void OnSetRoiYMin(int value);
  void OnSetRoiZMin(int value);

  void OnSetScaleX(double value);
  void OnSetScaleY(double value);
  void OnSetScaleZ(double value);
  void OnFlipAxis(Axis axis, bool value);

private:
  Scene* m_scene{ nullptr };

  QFormLayout* m_MainLayout;
  QNumericSlider m_DensityScaleSlider;
  QComboBox m_RendererType;
  QComboBox m_ShadingType;
  QNumericSlider m_GradientFactorSlider;
  QNumericSlider m_StepSizePrimaryRaySlider;
  QNumericSlider m_StepSizeSecondaryRaySlider;
  QCheckBox m_woodcockTrackingCheckBox;
  QCheckBox m_interpolateCheckBox;
  QComboBox m_multichannelBlendComboBox;
  QColorPushButton m_backgroundColorButton;

  QRenderSettings* m_qrendersettings;

  Section* m_clipRoiSection;
  RangeWidget* m_roiX;
  RangeWidget* m_roiY;
  RangeWidget* m_roiZ;

  Section* m_clipPlaneSection;
  QCheckBox* m_hideUserClipPlane;
  QPushButton* m_clipPlaneRotateButton;
  QPushButton* m_clipPlaneTranslateButton;
  QPushButton* m_clipPlaneResetButton;

  Section* m_scaleSection;
  QDoubleSpinner* m_xscaleSpinner;
  QCheckBox* m_xFlipCheckBox;
  QDoubleSpinner* m_yscaleSpinner;
  QCheckBox* m_yFlipCheckBox;
  QDoubleSpinner* m_zscaleSpinner;
  QCheckBox* m_zFlipCheckBox;
  QCheckBox m_showBoundingBoxCheckBox;
  QColorPushButton m_boundingBoxColorButton;
  QCheckBox m_showScaleBarCheckBox;
  QCheckBox m_showTimeStampCheckBox;
  QComboBox m_timeStampFormatComboBox;

  std::vector<Section*> m_channelSections;
  std::vector<class GradientWidget*> m_gradientWidgets;

  struct lt0
  {
    QPushButton* m_RotateButton;
    QNumericSlider* m_thetaSlider;
    QNumericSlider* m_phiSlider;
    QNumericSlider* m_sizeSlider;
    QNumericSlider* m_distSlider;
    QNumericSlider* m_intensitySlider;
    QColorPushButton* m_areaLightColorButton;
  } m_lt0gui;

  struct lt1
  {
    QPushButton* m_RotateButton;
    QNumericSlider* m_stintensitySlider;
    QColorPushButton* m_stColorButton;
    QNumericSlider* m_smintensitySlider;
    QColorPushButton* m_smColorButton;
    QNumericSlider* m_sbintensitySlider;
    QColorPushButton* m_sbColorButton;
  } m_lt1gui;

  QCheckBox* m_lockLightsToCameraCheckBox;

  Section* createSkyLightingControls(QAction* pRotationAction);
  Section* createAreaLightingControls(QAction* pLightRotationAction);
  Section* createClipPlaneSection(QAction* rotation, QAction* translation);
  void initLightingControls(Scene* scene);
  void updateLightingControlsFromScene();
  void initClipPlaneControls(Scene* scene);
  bool shouldClipPlaneShow();

  ObjectTransformMode* m_transformMode;
};
