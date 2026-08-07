#include "AppearanceSettingsWidget.h"

#include "Controls.h"
#include "ObjectTransformMode.h"
#include "QRenderSettings.h"
#include "RangeWidget.h"
#include "Section.h"

#include "tfeditor/gradients.h"

#include "renderlib/ImageXYZC.h"
#include "renderlib/AppScene.h"
#include "renderlib/Colormap.h"
#include "renderlib/Light.h"
#include "renderlib/Logging.h"
#include "renderlib/RenderSettings.h"

#include <QFormLayout>
#include <QFrame>
#include <QAbstractItemDelegate>
#include <QItemDelegate>
#include <QLinearGradient>
#include <QPainter>
#include <QPointer>
#include <algorithm>

static QGradientStops
colormapToGradient(const std::vector<ColorControlPoint>& v)
{
  QGradientStops stops;
  for (auto i : v) {
    stops.push_back(QPair<qreal, QColor>(i.first, QColor::fromRgb(i.r, i.g, i.b, i.a)));
  }
  return stops;
}

// Returns gradient stops to use for displaying a channel's colormap as a
// background swatch. For "Labels" and the no-colormap entry we return an
// empty list so the swatch falls back to a solid diffuse color.
static QGradientStops
swatchStopsForColorRamp(const ColorRamp& cr)
{
  if (cr.m_name == "Labels" || cr.m_name == ColorRamp::NO_COLORMAP_NAME) {
    return QGradientStops();
  }
  return colormapToGradient(cr.m_stops);
}

// Item delegate for the colormap dropdown. Paints each item's BackgroundRole
// brush (the colormap gradient) edge-to-edge with no inner border. The hover
// / selected state is rendered as a translucent overlay plus a focus outline,
// so the underlying colormap remains visible.
class GradientItemDelegate : public QAbstractItemDelegate
{
public:
  explicit GradientItemDelegate(QComboBox* combo)
    : QAbstractItemDelegate(combo)
    , m_combo(combo)
  {
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
  {
    painter->save();
    painter->setPen(Qt::NoPen);
    QVariant bg = index.data(Qt::BackgroundRole);
    if (bg.canConvert<QBrush>()) {
      painter->setBrush(bg.value<QBrush>());
    } else {
      painter->setBrush(option.palette.base());
    }
    painter->drawRect(option.rect);

    QString text = index.data(Qt::DisplayRole).toString();
    if (!text.isEmpty()) {
      painter->setPen(option.palette.color(QPalette::Text));
      painter->drawText(option.rect, Qt::AlignCenter, text);
    }

    // Hover / selected state: translucent overlay + 1px outline so the
    // colormap underneath remains visible.
    if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
      QColor highlight = option.palette.color(QPalette::Highlight);
      QColor overlay = highlight;
      overlay.setAlpha(64);
      painter->setPen(Qt::NoPen);
      painter->setBrush(overlay);
      painter->drawRect(option.rect);

      painter->setPen(QPen(highlight, 1));
      painter->setBrush(Qt::NoBrush);
      painter->drawRect(option.rect.adjusted(0, 0, -1, -1));
    }
    painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
  {
    Q_UNUSED(index);
    int height = option.fontMetrics.height() + 4;
    if (m_combo) {
      height = std::max(height, m_combo->sizeHint().height());
    }
    return { option.fontMetrics.averageCharWidth() * 10, height };
  }

private:
  QPointer<QComboBox> m_combo;
};

class GradientCombo : public QComboBox
{
public:
  GradientCombo(QWidget* parent = nullptr)
    : QComboBox(parent)
  {
    setFocusPolicy(Qt::StrongFocus);
  }

  void wheelEvent(QWheelEvent* event) override
  {
    if (!hasFocus()) {
      event->ignore();
      return;
    }
    QComboBox::wheelEvent(event);
  }

  void paintEvent(QPaintEvent* e) override
  {
    QComboBox::paintEvent(e);

    QPainter painter(this);
    painter.setPen(Qt::NoPen);
    painter.setBrush(itemData(currentIndex(), Qt::BackgroundRole).value<QBrush>());
    QStyleOptionComboBox option;
    option.initFrom(this);
    option.rect = rect();
    QRect arrowRect = style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxArrow, this);

    // Fill the entire widget area to the left of the dropdown arrow.
    QRect fillRect = rect();
    fillRect.setRight(arrowRect.left() - 1);
    painter.drawRect(fillRect);
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(fillRect, Qt::AlignCenter, itemText(currentIndex()));

    // Draw a standard 1px border around the swatch using the palette's
    // mid color (matches other framed widgets in the theme).
    painter.setBrush(Qt::NoBrush);
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(fillRect.adjusted(0, 0, -1, -1));
  }
};

static QComboBox*
makeGradientCombo()
{
  QComboBox* cb = new GradientCombo();
  cb->setItemDelegate(new GradientItemDelegate(cb));
  const QStringList colorNames = QColor::colorNames();
  int index = 0;
  for (auto& gspec : getBuiltInGradients()) {
    QLinearGradient gradient;
    gradient.setStops(colormapToGradient(gspec.m_stops));
    gradient.setStart(0., 0.);     // top left
    gradient.setFinalStop(1., 0.); // bottom right
    gradient.setCoordinateMode(QGradient::ObjectMode);

    QString itemText;
    QBrush brush(gradient);
    if (gspec.m_name == "Labels" || gspec.m_name == ColorRamp::NO_COLORMAP_NAME) {
      // special case for Labels
      brush.setColor(QColor(255, 255, 255));
      brush.setStyle(Qt::SolidPattern);
      itemText = gspec.m_name.c_str();
    } else {
      brush.setStyle(Qt::LinearGradientPattern);
    }
    cb->addItem(itemText, QVariant(gspec.m_name.c_str()));
    cb->setItemData(index, QVariant(gspec.m_name.c_str()), Qt::ToolTipRole);
    cb->setItemData(index, brush, Qt::BackgroundRole);
    index++;
  }
  return cb;
}

static const int MAX_CHANNELS_CHECKED = 4;

QAppearanceSettingsWidget::QAppearanceSettingsWidget(QWidget* pParent,
                                                     QRenderSettings* qrs,
                                                     RenderSettings* rs,
                                                     QAction* pToggleRotateAction,
                                                     QAction* pToggleTranslateAction)
  : QGroupBox(pParent)
  , m_MainLayout(new QFormLayout())
  , m_DensityScaleSlider()
  , m_RendererType()
  , m_ShadingType()
  , m_GradientFactorSlider()
  , m_StepSizePrimaryRaySlider()
  , m_StepSizeSecondaryRaySlider()
  , m_qrendersettings(qrs)
  , m_transformMode(new ObjectTransformMode([this]() { return m_scene; }, qrs, this))
{
  Controls::initFormLayout(*m_MainLayout);
  setLayout(m_MainLayout);

  m_RendererType.addItem("Ray march blending", 0);
  m_RendererType.addItem("Path Traced", 1);

  m_RendererType.setCurrentIndex(1);
  Controls::addFormRow(
    m_MainLayout, "Renderer", &m_RendererType, tr("Select volume rendering type"), tr("Select volume rendering type"));

  m_DensityScaleSlider.setRange(0.001, 100.0);
  m_DensityScaleSlider.setDecimals(3);
  m_DensityScaleSlider.setValue(rs->m_RenderSettings.m_DensityScale);
  Controls::addFormRow(m_MainLayout,
                       "Scattering Density",
                       &m_DensityScaleSlider,
                       tr("Set scattering density for volume"),
                       tr("Set scattering density for volume"));

  m_ShadingType.addItem("BRDF Only", 0);
  m_ShadingType.addItem("Phase Function Only", 1);
  m_ShadingType.addItem("Mixed", 2);
  m_ShadingType.setCurrentIndex(rs->m_RenderSettings.m_ShadingType);
  Controls::addFormRow(
    m_MainLayout, "Shading Type", &m_ShadingType, tr("Select volume shading style"), tr("Select volume shading style"));

  m_GradientFactorSlider.setRange(0.0, 1.0);
  m_GradientFactorSlider.setDecimals(3);
  m_GradientFactorSlider.setValue(rs->m_RenderSettings.m_GradientFactor);
  Controls::addFormRow(m_MainLayout,
                       "Shading Type Mixture",
                       &m_GradientFactorSlider,
                       tr("Mix between BRDF and Phase shading"),
                       tr("Mix between BRDF and Phase shading"));

  QObject::connect(&m_DensityScaleSlider, SIGNAL(valueChanged(double)), this, SLOT(OnSetDensityScale(double)));
  QObject::connect(&m_GradientFactorSlider, SIGNAL(valueChanged(double)), this, SLOT(OnSetGradientFactor(double)));

  m_StepSizePrimaryRaySlider.setRange(1.0, 100.0);
  m_StepSizePrimaryRaySlider.setValue(rs->m_RenderSettings.m_StepSizeFactor);
  m_StepSizePrimaryRaySlider.setDecimals(3);
  Controls::addFormRow(m_MainLayout,
                       "Primary Ray Step Size",
                       &m_StepSizePrimaryRaySlider,
                       tr("Set volume ray march step size for camera rays"),
                       tr("Set volume ray march step size for camera rays"));

  QObject::connect(
    &m_StepSizePrimaryRaySlider, SIGNAL(valueChanged(double)), this, SLOT(OnSetStepSizePrimaryRay(double)));

  m_StepSizeSecondaryRaySlider.setRange(1.0, 100.0);
  m_StepSizeSecondaryRaySlider.setValue(rs->m_RenderSettings.m_StepSizeFactorShadow);
  m_StepSizeSecondaryRaySlider.setDecimals(3);
  Controls::addFormRow(m_MainLayout,
                       "Secondary Ray Step Size",
                       &m_StepSizeSecondaryRaySlider,
                       tr("Set volume ray march step size for scattered rays"),
                       tr("Set volume ray march step size for scattered rays"));

  QObject::connect(
    &m_StepSizeSecondaryRaySlider, SIGNAL(valueChanged(double)), this, SLOT(OnSetStepSizeSecondaryRay(double)));

  m_woodcockTrackingCheckBox.setChecked(rs->m_RenderSettings.m_UseWoodcockTracking);
  Controls::addFormRow(m_MainLayout,
                       "Woodcock Tracking",
                       &m_woodcockTrackingCheckBox,
                       tr("Use Woodcock tracking instead of fixed-step ray marching"),
                       tr("Use Woodcock tracking instead of fixed-step ray marching"));
  QObject::connect(&m_woodcockTrackingCheckBox, &QCheckBox::clicked, [this](const bool is_checked) {
    this->OnWoodcockTrackingChecked(is_checked);
  });

  m_multichannelBlendComboBox.addItem(tr("Weighted"), static_cast<int>(MultichannelBlendMode::Weighted));
  m_multichannelBlendComboBox.addItem(tr("Max"), static_cast<int>(MultichannelBlendMode::Max));
  m_multichannelBlendComboBox.setCurrentIndex(m_multichannelBlendComboBox.findData(
    static_cast<int>(rs->m_RenderSettings.m_MultichannelBlendMode)));
  Controls::addFormRow(m_MainLayout,
                       "Multichannel blend",
                       &m_multichannelBlendComboBox,
                       tr("Choose how enabled channels contribute at each path trace sample"),
                       tr("Choose how enabled channels contribute at each path trace sample"));
  QObject::connect(&m_multichannelBlendComboBox,
                   QOverload<int>::of(&QComboBox::currentIndexChanged),
                   this,
                   &QAppearanceSettingsWidget::OnMultichannelBlendChanged);

  m_interpolateCheckBox.setChecked(true);
  Controls::addFormRow(m_MainLayout,
                       "Interpolate",
                       &m_interpolateCheckBox,
                       tr("Interpolated volume sampling"),
                       tr("Interpolated volume sampling"));
  QObject::connect(&m_interpolateCheckBox, &QCheckBox::clicked, [this](const bool is_checked) {
    this->OnInterpolateChecked(is_checked);
  });

  m_backgroundColorButton.SetColor(QColor(0, 0, 0), true);
  Controls::addFormRow(
    m_MainLayout, "Background Color", &m_backgroundColorButton, tr("Set background color"), tr("Set background color"));

  QObject::connect(&m_backgroundColorButton, &QColorPushButton::currentColorChanged, [this](const QColor& c) {
    this->OnBackgroundColorChanged(c);
  });

  auto* bboxLayout = new QHBoxLayout();
  m_showBoundingBoxCheckBox.setChecked(false);
  m_showBoundingBoxCheckBox.setStatusTip(tr("Show/hide bounding box"));
  m_showBoundingBoxCheckBox.setToolTip(tr("Show/hide bounding box"));
  bboxLayout->addWidget(&m_showBoundingBoxCheckBox, 0);

  m_boundingBoxColorButton.setStatusTip(tr("Set bounding box color"));
  m_boundingBoxColorButton.setToolTip(tr("Set bounding box color"));
  m_boundingBoxColorButton.SetColor(QColor(255, 255, 255), true);
  bboxLayout->addWidget(&m_boundingBoxColorButton, 1);

  Controls::addFormRow(
    m_MainLayout, "Bounding Box", bboxLayout, tr("Show/hide bounding box"), tr("Show/hide bounding box"));

  QObject::connect(&m_showBoundingBoxCheckBox, &QCheckBox::clicked, [this](const bool is_checked) {
    this->OnShowBoundsChecked(is_checked);
  });
  QObject::connect(&m_boundingBoxColorButton, &QColorPushButton::currentColorChanged, [this](const QColor& c) {
    this->OnBoundingBoxColorChanged(c);
  });

  m_showScaleBarCheckBox.setChecked(false);
  Controls::addFormRow(
    m_MainLayout, "Scale Bar", &m_showScaleBarCheckBox, tr("Show/hide scale bar"), tr("Show/hide scale bar"));
  QObject::connect(&m_showScaleBarCheckBox, &QCheckBox::clicked, [this](const bool is_checked) {
    this->OnShowScaleBarChecked(is_checked);
  });

  m_showTimeStampCheckBox.setChecked(false);
  Controls::addFormRow(
    m_MainLayout, "Timestamps", &m_showTimeStampCheckBox, tr("Show/hide timestamps"), tr("Show/hide timestamps"));
  QObject::connect(&m_showTimeStampCheckBox, &QCheckBox::clicked, [this](const bool is_checked) {
    this->OnShowTimeStampChecked(is_checked);
  });

  m_timeStampFormatComboBox.addItem(tr("HH:MM:SS"));
  m_timeStampFormatComboBox.addItem(tr("Time Units"));
  m_timeStampFormatComboBox.setCurrentIndex(1);
  Controls::addFormRow(m_MainLayout,
                       "Timestamp Format",
                       &m_timeStampFormatComboBox,
                       tr("Choose timestamp display format"),
                       tr("Choose timestamp display format"));
  QObject::connect(&m_timeStampFormatComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
    this->OnTimeStampFormatChanged(index);
  });

  m_scaleSection = new Section("Volume Scale", 0);
  auto* scaleSectionLayout = new QGridLayout();
  scaleSectionLayout->addWidget(new QLabel("X"), 0, 0);
  m_xscaleSpinner = new QDoubleSpinner();
  m_xscaleSpinner->setStatusTip(tr("Scale volume in X dimension"));
  m_xscaleSpinner->setToolTip(tr("Scale volume in X dimension"));
  m_xscaleSpinner->setDecimals(6);
  m_xscaleSpinner->setMaximum(10000.0);
  m_xscaleSpinner->setValue(1.0);
  scaleSectionLayout->addWidget(m_xscaleSpinner, 0, 1);
  m_xFlipCheckBox = new QCheckBox("Flip");
  m_xFlipCheckBox->setStatusTip(tr("Invert volume in X dimension"));
  m_xFlipCheckBox->setToolTip(tr("Invert volume in X dimension"));
  scaleSectionLayout->addWidget(m_xFlipCheckBox, 0, 2);
  QObject::connect(m_xscaleSpinner,
                   QOverload<double>::of(&QDoubleSpinner::valueChanged),
                   this,
                   &QAppearanceSettingsWidget::OnSetScaleX);
  QObject::connect(
    m_xFlipCheckBox, &QCheckBox::clicked, [this](bool flipValue) { this->OnFlipAxis(Axis::X, flipValue); });
  scaleSectionLayout->addWidget(new QLabel("Y"), 1, 0);
  m_yscaleSpinner = new QDoubleSpinner();
  m_yscaleSpinner->setStatusTip(tr("Scale volume in Y dimension"));
  m_yscaleSpinner->setToolTip(tr("Scale volume in Y dimension"));
  m_yscaleSpinner->setDecimals(6);
  m_yscaleSpinner->setMaximum(10000.0);
  m_yscaleSpinner->setValue(1.0);
  scaleSectionLayout->addWidget(m_yscaleSpinner, 1, 1);
  m_yFlipCheckBox = new QCheckBox("Flip");
  m_yFlipCheckBox->setStatusTip(tr("Invert volume in Y dimension"));
  m_yFlipCheckBox->setToolTip(tr("Invert volume in Y dimension"));
  scaleSectionLayout->addWidget(m_yFlipCheckBox, 1, 2);
  QObject::connect(m_yscaleSpinner,
                   QOverload<double>::of(&QDoubleSpinner::valueChanged),
                   this,
                   &QAppearanceSettingsWidget::OnSetScaleY);
  QObject::connect(
    m_yFlipCheckBox, &QCheckBox::clicked, [this](bool flipValue) { this->OnFlipAxis(Axis::Y, flipValue); });
  scaleSectionLayout->addWidget(new QLabel("Z"), 2, 0);
  m_zscaleSpinner = new QDoubleSpinner();
  m_zscaleSpinner->setStatusTip(tr("Scale volume in Z dimension"));
  m_zscaleSpinner->setToolTip(tr("Scale volume in Z dimension"));
  m_zscaleSpinner->setDecimals(6);
  m_zscaleSpinner->setMaximum(10000.0);
  m_zscaleSpinner->setValue(1.0);
  scaleSectionLayout->addWidget(m_zscaleSpinner, 2, 1);
  m_zFlipCheckBox = new QCheckBox("Flip");
  m_zFlipCheckBox->setStatusTip(tr("Invert volume in Z dimension"));
  m_zFlipCheckBox->setToolTip(tr("Invert volume in Z dimension"));
  scaleSectionLayout->addWidget(m_zFlipCheckBox, 2, 2);
  QObject::connect(m_zscaleSpinner,
                   QOverload<double>::of(&QDoubleSpinner::valueChanged),
                   this,
                   &QAppearanceSettingsWidget::OnSetScaleZ);
  QObject::connect(
    m_zFlipCheckBox, &QCheckBox::clicked, [this](bool flipValue) { this->OnFlipAxis(Axis::Z, flipValue); });

  m_scaleSection->setContentLayout(*scaleSectionLayout);
  m_MainLayout->addRow(m_scaleSection);

  m_clipRoiSection = new Section("ROI", 0);
  auto* roiSectionLayout = new QGridLayout();
  roiSectionLayout->addWidget(new QLabel("X"), 0, 0);
  m_roiX = new RangeWidget(Qt::Horizontal);
  m_roiX->setStatusTip(tr("Set clip planes along X axis"));
  m_roiX->setToolTip(tr("Set clip planes along X axis"));
  m_roiX->setBounds(0, 100);
  m_roiX->setFirstValue(0);
  m_roiX->setSecondValue(100);
  roiSectionLayout->addWidget(m_roiX, 0, 1);
  QObject::connect(m_roiX, &RangeWidget::minValueChanged, this, &QAppearanceSettingsWidget::OnSetRoiXMin);
  QObject::connect(m_roiX, &RangeWidget::maxValueChanged, this, &QAppearanceSettingsWidget::OnSetRoiXMax);
  roiSectionLayout->addWidget(new QLabel("Y"), 1, 0);
  m_roiY = new RangeWidget(Qt::Horizontal);
  m_roiY->setStatusTip(tr("Set clip planes along Y axis"));
  m_roiY->setToolTip(tr("Set clip planes along Y axis"));
  m_roiY->setBounds(0, 100);
  m_roiY->setFirstValue(0);
  m_roiY->setSecondValue(100);
  roiSectionLayout->addWidget(m_roiY, 1, 1);
  QObject::connect(m_roiY, &RangeWidget::minValueChanged, this, &QAppearanceSettingsWidget::OnSetRoiYMin);
  QObject::connect(m_roiY, &RangeWidget::maxValueChanged, this, &QAppearanceSettingsWidget::OnSetRoiYMax);
  roiSectionLayout->addWidget(new QLabel("Z"), 2, 0);
  m_roiZ = new RangeWidget(Qt::Horizontal);
  m_roiZ->setStatusTip(tr("Set clip planes along Z axis"));
  m_roiZ->setToolTip(tr("Set clip planes along Z axis"));
  m_roiZ->setBounds(0, 100);
  m_roiZ->setFirstValue(0);
  m_roiZ->setSecondValue(100);
  roiSectionLayout->addWidget(m_roiZ, 2, 1);
  QObject::connect(m_roiZ, &RangeWidget::minValueChanged, this, &QAppearanceSettingsWidget::OnSetRoiZMin);
  QObject::connect(m_roiZ, &RangeWidget::maxValueChanged, this, &QAppearanceSettingsWidget::OnSetRoiZMax);

  roiSectionLayout->setColumnStretch(0, 1);
  roiSectionLayout->setColumnStretch(1, 3);

  m_clipRoiSection->setContentLayout(*roiSectionLayout);
  m_MainLayout->addRow(m_clipRoiSection);

  Section* sectionCP = createClipPlaneSection(pToggleRotateAction, pToggleTranslateAction);
  m_MainLayout->addRow(sectionCP);

  QFrame* lineB = new QFrame();
  lineB->setFrameShape(QFrame::HLine);
  lineB->setFrameShadow(QFrame::Sunken);
  m_MainLayout->addRow(lineB);

  // create a "lock lights to camera" checkbox
  m_lockLightsToCameraCheckBox = new QCheckBox("Lock Lights to Camera");
  m_lockLightsToCameraCheckBox->setStatusTip(tr("When checked, interactive volume rotation will not rotate lights"));
  m_lockLightsToCameraCheckBox->setToolTip(tr("When checked, interactive volume rotation will not rotate lights"));

  m_lockLightsToCameraCheckBox->setChecked(m_scene ? m_scene->m_lighting.lockToCamera : true);
  m_MainLayout->addRow(m_lockLightsToCameraCheckBox);
  QObject::connect(m_lockLightsToCameraCheckBox, &QCheckBox::clicked, [this](bool is_checked) {
    if (m_scene) {
      m_scene->setLockLightsToCamera(is_checked);
    }
  });

  Section* section = createAreaLightingControls(pToggleRotateAction);
  m_MainLayout->addRow(section);
  Section* section2 = createSkyLightingControls(pToggleRotateAction);
  m_MainLayout->addRow(section2);

  QFrame* lineA = new QFrame();
  lineA->setFrameShape(QFrame::HLine);
  lineA->setFrameShadow(QFrame::Sunken);
  m_MainLayout->addRow(lineA);

  QObject::connect(&m_RendererType, SIGNAL(currentIndexChanged(int)), this, SLOT(OnSetRendererType(int)));
  QObject::connect(&m_ShadingType, SIGNAL(currentIndexChanged(int)), this, SLOT(OnSetShadingType(int)));
  // QObject::connect(&gStatus, SIGNAL(RenderBegin()), this, SLOT(OnRenderBegin()));

  QObject::connect(m_qrendersettings, SIGNAL(Changed()), this, SLOT(OnTransferFunctionChanged()));
}

Section*
QAppearanceSettingsWidget::createClipPlaneSection(QAction* pToggleRotateAction, QAction* pToggleTranslateAction)
{
  Section::CheckBoxInfo checkBoxInfo = { this->m_scene ? this->m_scene->m_clipPlane->m_enabled : false,
                                         "Enable/disable clip plane",
                                         "Enable/disable clip plane" };
  m_clipPlaneSection = new Section("Clip Plane", 0, &checkBoxInfo);
  // section checkbox turns clip plane on or off
  QObject::connect(m_clipPlaneSection, &Section::checked, [this](bool is_checked) {
    if (this->m_scene && this->m_scene->m_clipPlane) {
      this->m_scene->m_clipPlane->m_enabled = is_checked;
      m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(RoiDirty);
    }
  });

  auto* sectionLayout = Controls::createAgaveFormLayout();

  auto btnLayout = new QHBoxLayout();

  m_clipPlaneRotateButton = new QPushButton("Rotate");
  m_clipPlaneRotateButton->setStatusTip(tr("Show interactive controls in viewport for clip plane rotation angle"));
  m_clipPlaneRotateButton->setToolTip(tr("Show interactive controls in viewport for clip plane rotation angle"));
  btnLayout->addWidget(m_clipPlaneRotateButton);
  m_transformMode->registerButton(m_clipPlaneRotateButton, pToggleRotateAction, [this]() -> SceneObject* {
    return this->m_scene ? this->m_scene->m_clipPlane.get() : nullptr;
  });

  m_clipPlaneTranslateButton = new QPushButton("Translate");
  m_clipPlaneTranslateButton->setStatusTip(tr("Show interactive controls in viewport for clip plane translation"));
  m_clipPlaneTranslateButton->setToolTip(tr("Show interactive controls in viewport for clip plane translation"));
  btnLayout->addWidget(m_clipPlaneTranslateButton);
  m_transformMode->registerButton(m_clipPlaneTranslateButton, pToggleTranslateAction, [this]() -> SceneObject* {
    return this->m_scene ? this->m_scene->m_clipPlane.get() : nullptr;
  });

  sectionLayout->addLayout(btnLayout, sectionLayout->rowCount(), 0, 1, 2);

  m_hideUserClipPlane = new QCheckBox();
  m_hideUserClipPlane->setChecked(false);
  QObject::connect(
    m_hideUserClipPlane, &QCheckBox::clicked, [this, pToggleRotateAction, pToggleTranslateAction](bool toggled) {
      if (!this->m_scene) {
        return;
      }

      ScenePlane* p = this->m_scene->m_clipPlane.get();
      p->setVisible(!toggled);
    });

  Controls::addFormRow(sectionLayout,
                       "Hide",
                       m_hideUserClipPlane,
                       tr("Hide clip plane grid in viewport"),
                       tr("Hide clip plane grid in viewport"));

  // Add the Reset button
  m_clipPlaneResetButton = new QPushButton("Reset");
  m_clipPlaneResetButton->setStatusTip(tr("Reset clip plane to 0,0,0,0"));
  m_clipPlaneResetButton->setToolTip(tr("Reset clip plane to 0,0,0,0"));
  QObject::connect(m_clipPlaneResetButton, &QPushButton::clicked, [this]() {
    if (!this->m_scene || !this->m_scene->m_clipPlane) {
      return;
    }
    glm::vec3 c = this->m_scene->m_boundingBox.GetCenter();
    this->m_scene->m_clipPlane->resetTo(c);
    m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(RoiDirty);
    // re-select to cause Origins.update to reset the translate or rotate tools
    if (this->m_scene->m_selection == this->m_scene->m_clipPlane.get()) {
      emit this->m_qrendersettings->Selected(this->m_scene->m_clipPlane.get());
    }
  });

  auto* resetbtnLayout = new QHBoxLayout();
  resetbtnLayout->addWidget(new QWidget());
  resetbtnLayout->addWidget(m_clipPlaneResetButton);
  sectionLayout->addLayout(resetbtnLayout, sectionLayout->rowCount(), 0, 1, 2);

  m_clipPlaneSection->setContentLayout(*sectionLayout);
  return m_clipPlaneSection;
}

// TODO App really needs to be architected to let the tool's visibility state be independent of whether it's selected
// for manipulation.  Right now, selection of an object is the only thing that shows/hides the tool.
bool
QAppearanceSettingsWidget::shouldClipPlaneShow()
{
  return m_scene && !m_hideUserClipPlane->isChecked() && this->m_scene->m_clipPlane.get()->m_enabled;
}

Section*
QAppearanceSettingsWidget::createAreaLightingControls(QAction* pRotationAction)
{
  Section* section = new Section("Area Light", 0);
  auto* sectionLayout = Controls::createAgaveFormLayout();

  auto btnLayout = new QHBoxLayout();

  m_lt0gui.m_RotateButton = new QPushButton("Rotate");
  m_lt0gui.m_RotateButton->setStatusTip(tr("Show interactive controls in viewport for area light rotation angle"));
  m_lt0gui.m_RotateButton->setToolTip(tr("Show interactive controls in viewport for area light rotation angle"));
  btnLayout->addWidget(m_lt0gui.m_RotateButton);
  m_transformMode->registerButton(m_lt0gui.m_RotateButton, pRotationAction, [this]() -> SceneObject* {
    return (this->m_scene && this->m_scene->m_volume) ? this->m_scene->SceneAreaLight() : nullptr;
  });
  // dummy widget to fill space (TODO: Translate button?)
  btnLayout->addWidget(new QWidget());
  sectionLayout->addLayout(btnLayout, sectionLayout->rowCount(), 0, 1, 2);

  m_lt0gui.m_thetaSlider = new QNumericSlider();
  m_lt0gui.m_thetaSlider->setRange(0.0, TWO_PI_F);
  m_lt0gui.m_thetaSlider->setSingleStep(TWO_PI_F / 100.0);
  m_lt0gui.m_thetaSlider->setValue(0.0);
  Controls::addFormRow(sectionLayout,
                       "Theta",
                       m_lt0gui.m_thetaSlider,
                       tr("Set angle theta for area light"),
                       tr("Set angle theta for area light"));
  QObject::connect(
    m_lt0gui.m_thetaSlider, &QNumericSlider::valueChanged, this, &QAppearanceSettingsWidget::OnSetAreaLightTheta);

  m_lt0gui.m_phiSlider = new QNumericSlider();
  m_lt0gui.m_phiSlider->setRange(0.0, PI_F);
  m_lt0gui.m_phiSlider->setSingleStep(PI_F / 100.0);
  m_lt0gui.m_phiSlider->setValue(HALF_PI_F);
  Controls::addFormRow(
    sectionLayout, "Phi", m_lt0gui.m_phiSlider, tr("Set angle phi for area light"), tr("Set angle phi for area light"));
  QObject::connect(
    m_lt0gui.m_phiSlider, &QNumericSlider::valueChanged, this, &QAppearanceSettingsWidget::OnSetAreaLightPhi);

  m_lt0gui.m_sizeSlider = new QNumericSlider();
  m_lt0gui.m_sizeSlider->setRange(0.1, 5.0);
  m_lt0gui.m_sizeSlider->setSingleStep(5.0 / 100.0);
  m_lt0gui.m_sizeSlider->setValue(1.0);
  Controls::addFormRow(
    sectionLayout, "Size", m_lt0gui.m_sizeSlider, tr("Set size for area light"), tr("Set size for area light"));
  QObject::connect(
    m_lt0gui.m_sizeSlider, &QNumericSlider::valueChanged, this, &QAppearanceSettingsWidget::OnSetAreaLightSize);

  m_lt0gui.m_distSlider = new QNumericSlider();
  m_lt0gui.m_distSlider->setRange(0.1, 10.0);
  m_lt0gui.m_distSlider->setSingleStep(1.0);
  m_lt0gui.m_distSlider->setValue(10.0);
  Controls::addFormRow(sectionLayout,
                       "Distance",
                       m_lt0gui.m_distSlider,
                       tr("Set distance for area light"),
                       tr("Set distance for area light"));
  QObject::connect(
    m_lt0gui.m_distSlider, &QNumericSlider::valueChanged, this, &QAppearanceSettingsWidget::OnSetAreaLightDistance);

  auto* arealightLayout = new QHBoxLayout();
  m_lt0gui.m_intensitySlider = new QNumericSlider();
  m_lt0gui.m_intensitySlider->setStatusTip(tr("Set intensity for area light"));
  m_lt0gui.m_intensitySlider->setToolTip(tr("Set intensity for area light"));
  m_lt0gui.m_intensitySlider->setRange(0.0, 1000.0);
  m_lt0gui.m_intensitySlider->setSingleStep(10.0);
  m_lt0gui.m_intensitySlider->setValue(100.0);
  m_lt0gui.m_intensitySlider->setDecimals(1);
  arealightLayout->addWidget(m_lt0gui.m_intensitySlider, 1);
  m_lt0gui.m_areaLightColorButton = new QColorPushButton();
  m_lt0gui.m_areaLightColorButton->setStatusTip(tr("Set color for area light"));
  m_lt0gui.m_areaLightColorButton->setToolTip(tr("Set color for area light"));
  arealightLayout->addWidget(m_lt0gui.m_areaLightColorButton, 0);
  arealightLayout->setContentsMargins(0, 0, 0, 0);
  Controls::addFormRow(sectionLayout,
                       "Intensity",
                       arealightLayout,
                       tr("Set intensity for area light"),
                       tr("Set intensity for area light"));
  QObject::connect(m_lt0gui.m_areaLightColorButton, &QColorPushButton::currentColorChanged, [this](const QColor& c) {
    this->OnSetAreaLightColor(this->m_lt0gui.m_intensitySlider->value(), c);
  });
  QObject::connect(m_lt0gui.m_intensitySlider, &QNumericSlider::valueChanged, [this](double v) {
    this->OnSetAreaLightColor(v, this->m_lt0gui.m_areaLightColorButton->GetColor());
  });

  auto* resetBtn = new QPushButton("Reset");
  resetBtn->setStatusTip(tr("Reset area light to default orientation and settings"));
  resetBtn->setToolTip(tr("Reset area light to default orientation and settings"));
  QObject::connect(resetBtn, &QPushButton::clicked, [this]() {
    if (!this->m_scene) {
      return;
    }
    SceneLight* sl = this->m_scene->SceneAreaLight();
    sl->reset();
    updateLightingControlsFromScene();

    m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(LightsDirty);
    if (this->m_scene->m_selection == sl) {
      emit this->m_qrendersettings->Selected(sl);
    }
  });
  auto* resetLayout = new QHBoxLayout();
  resetLayout->addWidget(new QWidget());
  resetLayout->addWidget(resetBtn);
  sectionLayout->addLayout(resetLayout, sectionLayout->rowCount(), 0, 1, 2);

  section->setContentLayout(*sectionLayout);
  return section;
}

Section*
QAppearanceSettingsWidget::createSkyLightingControls(QAction* pRotationAction)
{
  Section* section = new Section("Sky Light", 0);
  auto* sectionLayout = Controls::createAgaveFormLayout();

  auto btnLayout = new QHBoxLayout();

  m_lt1gui.m_RotateButton = new QPushButton("Rotate");
  m_lt1gui.m_RotateButton->setStatusTip(tr("Show interactive controls in viewport for sky light rotation angle"));
  m_lt1gui.m_RotateButton->setToolTip(tr("Show interactive controls in viewport for sky light rotation angle"));
  btnLayout->addWidget(m_lt1gui.m_RotateButton);
  m_transformMode->registerButton(m_lt1gui.m_RotateButton, pRotationAction, [this]() -> SceneObject* {
    return (this->m_scene && this->m_scene->m_volume) ? this->m_scene->SceneSphereLight() : nullptr;
  });
  // dummy widget to fill space (TODO: Translate button?)
  btnLayout->addWidget(new QWidget());
  sectionLayout->addLayout(btnLayout, sectionLayout->rowCount(), 0, 1, 2);

  auto* skylightTopLayout = new QHBoxLayout();
  m_lt1gui.m_stintensitySlider = new QNumericSlider();
  m_lt1gui.m_stintensitySlider->setStatusTip(tr("Set intensity for top of skylight sphere"));
  m_lt1gui.m_stintensitySlider->setToolTip(tr("Set intensity for top of skylight sphere"));
  m_lt1gui.m_stintensitySlider->setRange(0.0, 10.0);
  m_lt1gui.m_stintensitySlider->setValue(1.0);
  skylightTopLayout->addWidget(m_lt1gui.m_stintensitySlider, 1);
  m_lt1gui.m_stColorButton = new QColorPushButton();
  m_lt1gui.m_stColorButton->setStatusTip(tr("Set color for top of skylight sphere"));
  m_lt1gui.m_stColorButton->setToolTip(tr("Set color for top of skylight sphere"));
  skylightTopLayout->addWidget(m_lt1gui.m_stColorButton);
  Controls::addFormRow(sectionLayout,
                       "Top",
                       skylightTopLayout,
                       tr("Set intensity/color for top of skylight sphere"),
                       tr("Set intensity/color for top of skylight sphere"));
  QObject::connect(m_lt1gui.m_stColorButton, &QColorPushButton::currentColorChanged, [this](const QColor& c) {
    this->OnSetSkyLightTopColor(this->m_lt1gui.m_stintensitySlider->value(), c);
  });
  QObject::connect(m_lt1gui.m_stintensitySlider, &QNumericSlider::valueChanged, [this](double v) {
    this->OnSetSkyLightTopColor(v, this->m_lt1gui.m_stColorButton->GetColor());
  });

  auto* skylightMidLayout = new QHBoxLayout();
  m_lt1gui.m_smintensitySlider = new QNumericSlider();
  m_lt1gui.m_smintensitySlider->setStatusTip(tr("Set intensity for middle of skylight sphere"));
  m_lt1gui.m_smintensitySlider->setToolTip(tr("Set intensity for middle of skylight sphere"));
  m_lt1gui.m_smintensitySlider->setRange(0.0, 10.0);
  m_lt1gui.m_smintensitySlider->setValue(1.0);
  skylightMidLayout->addWidget(m_lt1gui.m_smintensitySlider, 1);
  m_lt1gui.m_smColorButton = new QColorPushButton();
  m_lt1gui.m_smColorButton->setStatusTip(tr("Set color for middle of skylight sphere"));
  m_lt1gui.m_smColorButton->setToolTip(tr("Set color for middle of skylight sphere"));
  skylightMidLayout->addWidget(m_lt1gui.m_smColorButton);
  Controls::addFormRow(sectionLayout,
                       "Mid",
                       skylightMidLayout,
                       tr("Set intensity/color for middle of skylight sphere"),
                       tr("Set intensity/color for middle of skylight sphere"));
  QObject::connect(m_lt1gui.m_smColorButton, &QColorPushButton::currentColorChanged, [this](const QColor& c) {
    this->OnSetSkyLightMidColor(this->m_lt1gui.m_smintensitySlider->value(), c);
  });
  QObject::connect(m_lt1gui.m_smintensitySlider, &QNumericSlider::valueChanged, [this](double v) {
    this->OnSetSkyLightMidColor(v, this->m_lt1gui.m_smColorButton->GetColor());
  });

  auto* skylightBotLayout = new QHBoxLayout();
  m_lt1gui.m_sbintensitySlider = new QNumericSlider();
  m_lt1gui.m_sbintensitySlider->setStatusTip(tr("Set intensity for bottom of skylight sphere"));
  m_lt1gui.m_sbintensitySlider->setToolTip(tr("Set intensity for bottom of skylight sphere"));
  m_lt1gui.m_sbintensitySlider->setRange(0.0, 10.0);
  m_lt1gui.m_sbintensitySlider->setValue(1.0);
  skylightBotLayout->addWidget(m_lt1gui.m_sbintensitySlider, 1);
  m_lt1gui.m_sbColorButton = new QColorPushButton();
  m_lt1gui.m_sbColorButton->setStatusTip(tr("Set color for bottom of skylight sphere"));
  m_lt1gui.m_sbColorButton->setToolTip(tr("Set color for bottom of skylight sphere"));
  skylightBotLayout->addWidget(m_lt1gui.m_sbColorButton);
  Controls::addFormRow(sectionLayout,
                       "Bot",
                       skylightBotLayout,
                       tr("Set intensity/color for bottom of skylight sphere"),
                       tr("Set intensity/color for bottom of skylight sphere"));
  QObject::connect(m_lt1gui.m_sbColorButton, &QColorPushButton::currentColorChanged, [this](const QColor& c) {
    this->OnSetSkyLightBotColor(this->m_lt1gui.m_sbintensitySlider->value(), c);
  });
  QObject::connect(m_lt1gui.m_sbintensitySlider, &QNumericSlider::valueChanged, [this](double v) {
    this->OnSetSkyLightBotColor(v, this->m_lt1gui.m_sbColorButton->GetColor());
  });

  auto* resetBtn = new QPushButton("Reset");
  resetBtn->setStatusTip(tr("Reset sky light to default orientation and colors"));
  resetBtn->setToolTip(tr("Reset sky light to default orientation and colors"));
  QObject::connect(resetBtn, &QPushButton::clicked, [this]() {
    if (!this->m_scene) {
      return;
    }
    SceneLight* sl = this->m_scene->SceneSphereLight();
    sl->reset();
    updateLightingControlsFromScene();

    m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(LightsDirty);
    if (this->m_scene->m_selection == sl) {
      emit this->m_qrendersettings->Selected(sl);
    }
  });
  auto* resetLayout = new QHBoxLayout();
  resetLayout->addWidget(new QWidget());
  resetLayout->addWidget(resetBtn);
  sectionLayout->addLayout(resetLayout, sectionLayout->rowCount(), 0, 1, 2);

  section->setContentLayout(*sectionLayout);
  return section;
}

void
QAppearanceSettingsWidget::OnFlipAxis(Axis axis, bool value)
{
  if (!m_scene)
    return;
  int flipValue = value ? -1 : 1;
  glm::ivec3 v = m_scene->m_volume->getVolumeAxesFlipped();
  m_scene->m_volume->setVolumeAxesFlipped(
    axis == Axis::X ? flipValue : v.x, axis == Axis::Y ? flipValue : v.y, axis == Axis::Z ? flipValue : v.z);
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(RenderParamsDirty);
}

void
QAppearanceSettingsWidget::OnSetScaleX(double value)
{
  if (!m_scene)
    return;
  m_scene->m_volume->setPhysicalSize(
    static_cast<float>(value), m_scene->m_volume->physicalSizeY(), m_scene->m_volume->physicalSizeZ());
  m_scene->initBoundsFromImg(m_scene->m_volume);
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(CameraDirty);
}

void
QAppearanceSettingsWidget::OnSetScaleY(double value)
{
  if (!m_scene)
    return;
  m_scene->m_volume->setPhysicalSize(
    m_scene->m_volume->physicalSizeX(), static_cast<float>(value), m_scene->m_volume->physicalSizeZ());
  m_scene->initBoundsFromImg(m_scene->m_volume);
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(CameraDirty);
}

void
QAppearanceSettingsWidget::OnSetScaleZ(double value)
{
  if (!m_scene)
    return;
  m_scene->m_volume->setPhysicalSize(
    m_scene->m_volume->physicalSizeX(), m_scene->m_volume->physicalSizeY(), static_cast<float>(value));
  m_scene->initBoundsFromImg(m_scene->m_volume);
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(CameraDirty);
}

void
QAppearanceSettingsWidget::OnSetRoiXMin(int value)
{
  if (!m_scene)
    return;
  glm::vec3 v = m_scene->m_roi.GetMinP();
  v.x = (float)value / (static_cast<float>(m_scene->m_volume->sizeX()) - 1);
  m_scene->m_roi.SetMinP(v);
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(RoiDirty);
}

void
QAppearanceSettingsWidget::OnSetRoiYMin(int value)
{
  if (!m_scene)
    return;
  glm::vec3 v = m_scene->m_roi.GetMinP();
  v.y = (float)value / (static_cast<float>(m_scene->m_volume->sizeY()) - 1);
  m_scene->m_roi.SetMinP(v);
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(RoiDirty);
}

void
QAppearanceSettingsWidget::OnSetRoiZMin(int value)
{
  if (!m_scene)
    return;
  glm::vec3 v = m_scene->m_roi.GetMinP();
  v.z = (float)value / (static_cast<float>(m_scene->m_volume->sizeZ()) - 1);
  m_scene->m_roi.SetMinP(v);
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(RoiDirty);
}
void
QAppearanceSettingsWidget::OnSetRoiXMax(int value)
{
  if (!m_scene)
    return;
  glm::vec3 v = m_scene->m_roi.GetMaxP();
  v.x = (float)value / (static_cast<float>(m_scene->m_volume->sizeX()) - 1);
  m_scene->m_roi.SetMaxP(v);
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(RoiDirty);
}
void
QAppearanceSettingsWidget::OnSetRoiYMax(int value)
{
  if (!m_scene)
    return;
  glm::vec3 v = m_scene->m_roi.GetMaxP();
  v.y = (float)value / (static_cast<float>(m_scene->m_volume->sizeY()) - 1);
  m_scene->m_roi.SetMaxP(v);
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(RoiDirty);
}
void
QAppearanceSettingsWidget::OnSetRoiZMax(int value)
{
  if (!m_scene)
    return;
  glm::vec3 v = m_scene->m_roi.GetMaxP();
  v.z = (float)value / (static_cast<float>(m_scene->m_volume->sizeZ()) - 1);
  m_scene->m_roi.SetMaxP(v);
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(RoiDirty);
}

void
QAppearanceSettingsWidget::OnSetAreaLightTheta(double value)
{
  if (!m_scene)
    return;
  m_scene->AreaLight().m_Theta = static_cast<float>(value);
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(LightsDirty);
}
void
QAppearanceSettingsWidget::OnSetAreaLightPhi(double value)
{
  if (!m_scene)
    return;
  m_scene->AreaLight().m_Phi = static_cast<float>(value);
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(LightsDirty);
}
void
QAppearanceSettingsWidget::OnSetAreaLightSize(double value)
{
  if (!m_scene)
    return;
  m_scene->AreaLight().m_Width = static_cast<float>(value);
  m_scene->AreaLight().m_Height = static_cast<float>(value);
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(LightsDirty);
}
void
QAppearanceSettingsWidget::OnSetAreaLightDistance(double value)
{
  if (!m_scene)
    return;
  m_scene->AreaLight().m_Distance = static_cast<float>(value);
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(LightsDirty);
}
void
QAppearanceSettingsWidget::OnSetAreaLightColor(double intensity, const QColor& color)
{
  if (!m_scene)
    return;
  float rgba[4];
  color.getRgbF(&rgba[0], &rgba[1], &rgba[2], &rgba[3]);

  m_scene->AreaLight().m_Color = glm::vec3(rgba[0], rgba[1], rgba[2]);
  m_scene->AreaLight().m_ColorIntensity = static_cast<float>(intensity);
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(LightsDirty);
}

void
QAppearanceSettingsWidget::OnSetSkyLightTopColor(double intensity, const QColor& color)
{
  if (!m_scene)
    return;
  float rgba[4];
  color.getRgbF(&rgba[0], &rgba[1], &rgba[2], &rgba[3]);

  m_scene->SphereLight().m_ColorTop = glm::vec3(rgba[0], rgba[1], rgba[2]);
  m_scene->SphereLight().m_ColorTopIntensity = static_cast<float>(intensity);
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(LightsDirty);
}
void
QAppearanceSettingsWidget::OnSetSkyLightMidColor(double intensity, const QColor& color)
{
  if (!m_scene)
    return;
  float rgba[4];
  color.getRgbF(&rgba[0], &rgba[1], &rgba[2], &rgba[3]);

  m_scene->SphereLight().m_ColorMiddle = glm::vec3(rgba[0], rgba[1], rgba[2]);
  m_scene->SphereLight().m_ColorMiddleIntensity = static_cast<float>(intensity);
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(LightsDirty);
}
void
QAppearanceSettingsWidget::OnSetSkyLightBotColor(double intensity, const QColor& color)
{
  if (!m_scene)
    return;
  float rgba[4];
  color.getRgbF(&rgba[0], &rgba[1], &rgba[2], &rgba[3]);

  m_scene->SphereLight().m_ColorBottom = glm::vec3(rgba[0], rgba[1], rgba[2]);
  m_scene->SphereLight().m_ColorBottomIntensity = static_cast<float>(intensity);
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(LightsDirty);
}

void
QAppearanceSettingsWidget::OnRenderBegin()
{
  m_DensityScaleSlider.setValue(m_qrendersettings->GetDensityScale());
  m_ShadingType.setCurrentIndex(m_qrendersettings->GetShadingType());
  m_GradientFactorSlider.setValue(m_qrendersettings->renderSettings()->m_RenderSettings.m_GradientFactor);

  m_StepSizePrimaryRaySlider.setValue(m_qrendersettings->renderSettings()->m_RenderSettings.m_StepSizeFactor, true);
  m_StepSizeSecondaryRaySlider.setValue(m_qrendersettings->renderSettings()->m_RenderSettings.m_StepSizeFactorShadow,
                                        true);
  m_woodcockTrackingCheckBox.setChecked(m_qrendersettings->renderSettings()->m_RenderSettings.m_UseWoodcockTracking);
  m_interpolateCheckBox.setChecked(m_qrendersettings->renderSettings()->m_RenderSettings.m_InterpolatedVolumeSampling);
  m_multichannelBlendComboBox.setCurrentIndex(m_multichannelBlendComboBox.findData(
    static_cast<int>(m_qrendersettings->renderSettings()->m_RenderSettings.m_MultichannelBlendMode)));
}

void
QAppearanceSettingsWidget::OnSetDensityScale(double DensityScale)
{
  m_qrendersettings->SetDensityScale(static_cast<float>(DensityScale));
}

void
QAppearanceSettingsWidget::OnSetShadingType(int Index)
{
  m_qrendersettings->SetShadingType(Index);
  m_GradientFactorSlider.setEnabled(Index == 2);
}

void
QAppearanceSettingsWidget::OnSetRendererType(int Index)
{
  m_qrendersettings->SetRendererType(Index);
}

void
QAppearanceSettingsWidget::OnSetGradientFactor(double GradientFactor)
{
  m_qrendersettings->SetGradientFactor(static_cast<float>(GradientFactor));
}

void
QAppearanceSettingsWidget::OnSetStepSizePrimaryRay(const double& StepSizePrimaryRay)
{
  m_qrendersettings->renderSettings()->m_RenderSettings.m_StepSizeFactor = (float)StepSizePrimaryRay;
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(RenderParamsDirty);
}

void
QAppearanceSettingsWidget::OnSetStepSizeSecondaryRay(const double& StepSizeSecondaryRay)
{
  m_qrendersettings->renderSettings()->m_RenderSettings.m_StepSizeFactorShadow = (float)StepSizeSecondaryRay;
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(RenderParamsDirty);
}

void
QAppearanceSettingsWidget::OnTransferFunctionChanged()
{
  m_DensityScaleSlider.setValue(m_qrendersettings->GetDensityScale(), true);
  m_ShadingType.setCurrentIndex(m_qrendersettings->GetShadingType());
  m_GradientFactorSlider.setValue(m_qrendersettings->GetGradientFactor(), true);
  m_multichannelBlendComboBox.setCurrentIndex(m_multichannelBlendComboBox.findData(
    static_cast<int>(m_qrendersettings->renderSettings()->m_RenderSettings.m_MultichannelBlendMode)));
}

void
QAppearanceSettingsWidget::OnBackgroundColorChanged(const QColor& color)
{
  if (!m_scene)
    return;
  float rgba[4];
  color.getRgbF(&rgba[0], &rgba[1], &rgba[2], &rgba[3]);
  m_scene->m_material.m_backgroundColor[0] = rgba[0];
  m_scene->m_material.m_backgroundColor[1] = rgba[1];
  m_scene->m_material.m_backgroundColor[2] = rgba[2];
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(RenderParamsDirty);
}

void
QAppearanceSettingsWidget::OnBoundingBoxColorChanged(const QColor& color)
{
  if (!m_scene)
    return;
  float rgba[4];
  color.getRgbF(&rgba[0], &rgba[1], &rgba[2], &rgba[3]);
  m_scene->m_material.m_boundingBoxColor[0] = rgba[0];
  m_scene->m_material.m_boundingBoxColor[1] = rgba[1];
  m_scene->m_material.m_boundingBoxColor[2] = rgba[2];
}

void
QAppearanceSettingsWidget::OnShowBoundsChecked(bool isChecked)
{
  if (!m_scene)
    return;
  m_scene->m_material.m_showBoundingBox = isChecked;
}

void
QAppearanceSettingsWidget::OnShowScaleBarChecked(bool isChecked)
{
  if (!m_scene)
    return;
  m_scene->m_showScaleBar = isChecked;
}

void
QAppearanceSettingsWidget::OnShowTimeStampChecked(bool isChecked)
{
  if (!m_scene)
    return;
  m_scene->m_showTimeStamp = isChecked;
}

void
QAppearanceSettingsWidget::OnTimeStampFormatChanged(int index)
{
  if (!m_scene)
    return;
  m_scene->m_timeStampDisplayMode = static_cast<Scene::TimeStampDisplayMode>(index);
}

void
QAppearanceSettingsWidget::OnInterpolateChecked(bool isChecked)
{
  if (!m_scene)
    return;
  m_qrendersettings->renderSettings()->m_RenderSettings.m_InterpolatedVolumeSampling = isChecked;
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(RenderParamsDirty);
}

void
QAppearanceSettingsWidget::OnWoodcockTrackingChecked(bool isChecked)
{
  if (!m_scene)
    return;
  m_qrendersettings->renderSettings()->m_RenderSettings.m_UseWoodcockTracking = isChecked;
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(RenderParamsDirty);
}

void
QAppearanceSettingsWidget::OnMultichannelBlendChanged(int index)
{
  if (!m_scene || index < 0)
    return;
  m_qrendersettings->renderSettings()->m_RenderSettings.m_MultichannelBlendMode =
    static_cast<MultichannelBlendMode>(m_multichannelBlendComboBox.itemData(index).toInt());
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(RenderParamsDirty);
}

void
QAppearanceSettingsWidget::OnDiffuseColorChanged(int i, const QColor& color)
{
  if (!m_scene)
    return;
  float rgba[4];
  color.getRgbF(&rgba[0], &rgba[1], &rgba[2], &rgba[3]);
  m_scene->m_material.m_diffuse[i * 3 + 0] = rgba[0];
  m_scene->m_material.m_diffuse[i * 3 + 1] = rgba[1];
  m_scene->m_material.m_diffuse[i * 3 + 2] = rgba[2];
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(RenderParamsDirty);
}

void
QAppearanceSettingsWidget::OnSpecularColorChanged(int i, const QColor& color)
{
  if (!m_scene)
    return;
  float rgba[4];
  color.getRgbF(&rgba[0], &rgba[1], &rgba[2], &rgba[3]);
  m_scene->m_material.m_specular[i * 3 + 0] = rgba[0];
  m_scene->m_material.m_specular[i * 3 + 1] = rgba[1];
  m_scene->m_material.m_specular[i * 3 + 2] = rgba[2];
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(RenderParamsDirty);
}

void
QAppearanceSettingsWidget::OnEmissiveColorChanged(int i, const QColor& color)
{
  if (!m_scene)
    return;
  float rgba[4];
  color.getRgbF(&rgba[0], &rgba[1], &rgba[2], &rgba[3]);
  m_scene->m_material.m_emissive[i * 3 + 0] = rgba[0];
  m_scene->m_material.m_emissive[i * 3 + 1] = rgba[1];
  m_scene->m_material.m_emissive[i * 3 + 2] = rgba[2];
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(RenderParamsDirty);
}

void
QAppearanceSettingsWidget::OnUpdateLut(int i, const std::vector<LutControlPoint>& stops)
{
  if (!m_scene)
    return;
  m_scene->m_volume->channel((uint32_t)i)->generateFromGradientData(m_scene->m_material.m_gradientData[i]);

  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(TransferFunctionDirty);
}

void
QAppearanceSettingsWidget::OnOpacityChanged(int i, double opacity)
{
  if (!m_scene)
    return;
  // LOG_DEBUG << "window/level: " << window << ", " << level;
  //_scene->_volume->channel((uint32_t)i)->setOpacity(opacity);
  m_scene->m_material.m_opacity[i] = static_cast<float>(opacity);
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(TransferFunctionDirty);
}

void
QAppearanceSettingsWidget::OnRoughnessChanged(int i, double roughness)
{
  if (!m_scene)
    return;
  m_scene->m_material.m_roughness[i] = static_cast<float>(roughness);
  m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(TransferFunctionDirty);
}

void
QAppearanceSettingsWidget::OnChannelChecked(int i, bool is_checked)
{
  if (!m_scene) {
    return;
  }
  // if we are switching one on, count how many sections are checked.
  // if more than 4, then switch this one back off
  if (is_checked) {
    int count = 0;
    for (auto& m_channelSection : m_channelSections) {
      if (m_channelSection->isChecked())
        count++;
    }
    if (count > MAX_CHANNELS_CHECKED) {
      // uncheck the one that was just checked
      m_channelSections[i]->setChecked(false);
      return;
    }
  }

  // now we can actually update the state
  bool old_value = m_scene->m_material.m_enabled[i];
  if (old_value != is_checked) {
    m_scene->m_material.m_enabled[i] = is_checked;
    m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(VolumeDataDirty | TransferFunctionDirty);
  }
}

// split color into color and intensity.
static inline void
normalizeColorForGui(const glm::vec3& incolor, QColor& outcolor, float& outintensity)
{
  // if any r,g,b is greater than 1, take max value as intensity, else intensity = 1
  float i = std::max({ incolor.x, incolor.y, incolor.z });
  outintensity = (i > 1.0f) ? i : 1.0f;
  glm::vec3 voutcolor = incolor / i;
  outcolor = QColor::fromRgbF(voutcolor.x, voutcolor.y, voutcolor.z);
}

void
QAppearanceSettingsWidget::initClipPlaneControls(Scene* scene)
{
  const ScenePlane* clipPlane = scene->m_clipPlane.get();
  m_clipPlaneSection->setChecked(clipPlane->m_enabled);
}

void
QAppearanceSettingsWidget::updateLightingControlsFromScene()
{
  if (!m_scene) {
    return;
  }
  QColor c;
  float i;
  normalizeColorForGui(m_scene->AreaLight().m_Color, c, i);
  m_lt0gui.m_thetaSlider->setValue(m_scene->AreaLight().m_Theta);
  m_lt0gui.m_phiSlider->setValue(m_scene->AreaLight().m_Phi);
  m_lt0gui.m_sizeSlider->setValue(m_scene->AreaLight().m_Width);
  m_lt0gui.m_distSlider->setValue(m_scene->AreaLight().m_Distance);
  m_lt0gui.m_intensitySlider->setValue(i * m_scene->AreaLight().m_ColorIntensity);
  m_lt0gui.m_areaLightColorButton->SetColor(c);

  normalizeColorForGui(m_scene->SphereLight().m_ColorTop, c, i);
  m_lt1gui.m_stintensitySlider->setValue(i * m_scene->SphereLight().m_ColorTopIntensity);
  m_lt1gui.m_stColorButton->SetColor(c);
  normalizeColorForGui(m_scene->SphereLight().m_ColorMiddle, c, i);
  m_lt1gui.m_smintensitySlider->setValue(i * m_scene->SphereLight().m_ColorMiddleIntensity);
  m_lt1gui.m_smColorButton->SetColor(c);
  normalizeColorForGui(m_scene->SphereLight().m_ColorBottom, c, i);
  m_lt1gui.m_sbintensitySlider->setValue(i * m_scene->SphereLight().m_ColorBottomIntensity);
  m_lt1gui.m_sbColorButton->SetColor(c);
}

void
QAppearanceSettingsWidget::initLightingControls(Scene* scene)
{
  updateLightingControlsFromScene();

  // attach light observer to scene's area light source, to receive updates from viewport controls
  // TODO FIXME clean this up - it's not removed anywhere so if light(i.e. scene) outlives "this" then we have problems.
  // Currently in AGAVE this is not an issue..
  scene->SceneAreaLight()->m_observers.emplace_back([this](const Light& light) { updateLightingControlsFromScene(); });
}

void
QAppearanceSettingsWidget::onNewImage(Scene* scene)
{
  m_transformMode->clearMode();

  // Don't forget that most ui updating triggered in this function should
  // NOT signal changes to the scene.

  // remove the previous per-channel ui
  for (auto s : m_channelSections) {
    delete s;
  }
  m_channelSections.clear();
  m_gradientWidgets.clear();

  // I don't own this.
  m_scene = scene;

  if (!scene->m_volume) {
    return;
  }

  m_DensityScaleSlider.setValue(m_qrendersettings->renderSettings()->m_RenderSettings.m_DensityScale);
  m_ShadingType.setCurrentIndex(m_qrendersettings->renderSettings()->m_RenderSettings.m_ShadingType);
  m_GradientFactorSlider.setValue(m_qrendersettings->renderSettings()->m_RenderSettings.m_GradientFactor);

  m_StepSizePrimaryRaySlider.setValue(m_qrendersettings->renderSettings()->m_RenderSettings.m_StepSizeFactor);
  m_StepSizeSecondaryRaySlider.setValue(m_qrendersettings->renderSettings()->m_RenderSettings.m_StepSizeFactorShadow);
  m_woodcockTrackingCheckBox.setChecked(m_qrendersettings->renderSettings()->m_RenderSettings.m_UseWoodcockTracking);
  m_interpolateCheckBox.setChecked(m_qrendersettings->renderSettings()->m_RenderSettings.m_InterpolatedVolumeSampling);
  m_multichannelBlendComboBox.setCurrentIndex(m_multichannelBlendComboBox.findData(
    static_cast<int>(m_qrendersettings->renderSettings()->m_RenderSettings.m_MultichannelBlendMode)));

  QColor cbg = QColor::fromRgbF(m_scene->m_material.m_backgroundColor[0],
                                m_scene->m_material.m_backgroundColor[1],
                                m_scene->m_material.m_backgroundColor[2]);
  m_backgroundColorButton.SetColor(cbg);

  int xmax = static_cast<int>(m_scene->m_volume->sizeX() - 1);
  int ymax = static_cast<int>(m_scene->m_volume->sizeY() - 1);
  int zmax = static_cast<int>(m_scene->m_volume->sizeZ() - 1);

  m_roiX->setBounds(0, xmax, true);
  m_roiY->setBounds(0, ymax, true);
  m_roiZ->setBounds(0, zmax, true);

  m_roiX->setFirstValue(static_cast<int>(m_scene->m_roi.GetMinP().x * static_cast<float>(xmax)), true);
  m_roiX->setSecondValue(static_cast<int>(m_scene->m_roi.GetMaxP().x * static_cast<float>(xmax)), true);
  m_roiY->setFirstValue(static_cast<int>(m_scene->m_roi.GetMinP().y * static_cast<float>(ymax)), true);
  m_roiY->setSecondValue(static_cast<int>(m_scene->m_roi.GetMaxP().y * static_cast<float>(ymax)), true);
  m_roiZ->setFirstValue(static_cast<int>(m_scene->m_roi.GetMinP().z * static_cast<float>(zmax)), true);
  m_roiZ->setSecondValue(static_cast<int>(m_scene->m_roi.GetMaxP().z * static_cast<float>(zmax)), true);

  m_xscaleSpinner->setValue(m_scene->m_volume->physicalSizeX());
  m_yscaleSpinner->setValue(m_scene->m_volume->physicalSizeY());
  m_zscaleSpinner->setValue(m_scene->m_volume->physicalSizeZ());
  glm::ivec3 v = m_scene->m_volume->getVolumeAxesFlipped();
  m_xFlipCheckBox->setChecked(v.x < 0);
  m_yFlipCheckBox->setChecked(v.y < 0);
  m_zFlipCheckBox->setChecked(v.z < 0);

  QColor cbbox = QColor::fromRgbF(m_scene->m_material.m_boundingBoxColor[0],
                                  m_scene->m_material.m_boundingBoxColor[1],
                                  m_scene->m_material.m_boundingBoxColor[2]);
  m_boundingBoxColorButton.SetColor(cbbox);
  m_showBoundingBoxCheckBox.setChecked(m_scene->m_material.m_showBoundingBox);
  m_showScaleBarCheckBox.setChecked(m_scene->m_showScaleBar);
  m_lockLightsToCameraCheckBox->setChecked(m_scene->m_lighting.lockToCamera);
  m_showTimeStampCheckBox.setChecked(m_scene->m_showTimeStamp);
  m_timeStampFormatComboBox.setCurrentIndex(static_cast<int>(m_scene->m_timeStampDisplayMode));

  initLightingControls(scene);
  initClipPlaneControls(scene);

  int numEnabled = 0;
  for (int i = 0; i < scene->m_volume->sizeC(); ++i) {
    bool channelenabled = m_scene->m_material.m_enabled[i];
    // only really allow the first 4 enabled
    if (channelenabled) {
      numEnabled++;
      if (numEnabled > MAX_CHANNELS_CHECKED) {
        channelenabled = false;
        // disable for real!
        m_scene->m_material.m_enabled[i] = false;
      }
    }

    std::string tip = "Enable/disable channel " + scene->m_volume->channel(i)->m_name;
    Section::CheckBoxInfo cbinfo = { channelenabled, tip, tip };
    QColor cdiff = QColor::fromRgbF(scene->m_material.m_diffuse[i * 3 + 0],
                                    scene->m_material.m_diffuse[i * 3 + 1],
                                    scene->m_material.m_diffuse[i * 3 + 2]);
    Section::ColorBoxInfo colorinfo = { cdiff, "Channel color", "Channel color" };
    Section* section = new Section(QString::fromStdString(scene->m_volume->channel(i)->m_name), 0, &cbinfo, &colorinfo);

    auto* fullLayout = new QVBoxLayout();

    auto* sectionLayout = Controls::createAgaveFormLayout();

    GradientWidget* editor =
      new GradientWidget(scene->m_volume->channel(i)->m_histogram, &scene->m_material.m_gradientData[i]);
    fullLayout->addWidget(editor);
    // sectionLayout->addRow("Gradient", editor);
    fullLayout->addLayout(sectionLayout);

    m_gradientWidgets.push_back(editor);

    QObject::connect(editor, &GradientWidget::gradientStopsChanged, [i, this](const QGradientStops& stops) {
      // convert stops to control points
      std::vector<LutControlPoint> pts;
      pts.reserve(stops.size());
      for (const auto& stop : stops) {
        pts.emplace_back(stop.first, stop.second.alphaF());
      }

      this->OnUpdateLut(i, pts);
    });
    this->OnUpdateLut(i, std::vector<LutControlPoint>());

    QNumericSlider* opacitySlider = new QNumericSlider();
    opacitySlider->setRange(0.0, 100.0);
    opacitySlider->setSingleStep(0.01);
    opacitySlider->setValue(scene->m_material.m_opacity[i], true);
    Controls::addFormRow(
      sectionLayout, "Opacity", opacitySlider, tr("Set opacity for channel"), tr("Set opacity for channel"));

    QObject::connect(
      opacitySlider, &QNumericSlider::valueChanged, [i, this](double d) { this->OnOpacityChanged(i, d); });
    // init
    this->OnOpacityChanged(i, scene->m_material.m_opacity[i]);

    auto separator = new QFrame();
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    sectionLayout->addWidget(separator, sectionLayout->rowCount(), 0, 1, 2);

    // get color ramp from scene
    const ColorRamp& cr = scene->m_material.m_colormap[i];
    QComboBox* gradients = makeGradientCombo();
    int idx = gradients->findData(QVariant(cr.m_name.c_str()), Qt::UserRole);

    // Reflect the current colormap in the section's swatch. The combo's
    // currentIndexChanged handler (registered below) keeps it in sync after
    // user interaction.
    section->setColormapStops(swatchStopsForColorRamp(cr));

    Controls::addFormRow(
      sectionLayout,
      "ColorMap",
      gradients,
      tr("Set colormap for channel. ColorMap will be multiplied with Color. To use ColorMap only, set Color to white."),
      tr("Set colormap for channel. ColorMap will be multiplied with Color. To use ColorMap only, set Color to "
         "white."));
    QObject::connect(gradients, &QComboBox::currentIndexChanged, [i, gradients, section, this](int index) {
      // get string from userdata
      std::string name = gradients->itemData(index).toString().toStdString();

      ColorRamp ramp = ColorRamp::colormapFromName(name);
      if (name == "Labels") {
        if (m_scene) {
          m_scene->m_material.m_colormap[i] = ramp;
          m_scene->m_material.m_labels[i] = 1.0;
          m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(TransferFunctionDirty);
        }

      } else {
        m_scene->m_material.m_colormap[i] = ramp;
        m_scene->m_material.m_labels[i] = 0.0;
        m_qrendersettings->renderSettings()->m_DirtyFlags.SetFlag(TransferFunctionDirty);
      }
      section->setColormapStops(swatchStopsForColorRamp(ramp));
    });
    // init after creating the callback, so that we can reinit a named colormap properly.
    gradients->setCurrentIndex(idx);

    QColorPushButton* diffuseColorButton = new QColorPushButton();
    diffuseColorButton->SetColor(cdiff, true);
    Controls::addFormRow(
      sectionLayout, "Color", diffuseColorButton, tr("Set color for channel"), tr("Set color for channel"));
    QObject::connect(diffuseColorButton, &QColorPushButton::currentColorChanged, [i, this, section](const QColor& c) {
      this->OnDiffuseColorChanged(i, c);
      section->setColor(c);
    });
    QObject::connect(section, &Section::colorChanged, [i, this, diffuseColorButton](const QColor& c) {
      this->OnDiffuseColorChanged(i, c);
      diffuseColorButton->SetColor(c, true);
    });

    // init
    this->OnDiffuseColorChanged(i, cdiff);

    auto separator2 = new QFrame();
    separator2->setFrameShape(QFrame::HLine);
    separator2->setFrameShadow(QFrame::Sunken);
    sectionLayout->addWidget(separator2, sectionLayout->rowCount(), 0, 1, 2);

    QColorPushButton* specularColorButton = new QColorPushButton();
    QColor cspec = QColor::fromRgbF(scene->m_material.m_specular[i * 3 + 0],
                                    scene->m_material.m_specular[i * 3 + 1],
                                    scene->m_material.m_specular[i * 3 + 2]);
    specularColorButton->SetColor(cspec, true);
    Controls::addFormRow(sectionLayout,
                         "SpecularColor",
                         specularColorButton,
                         tr("Set specular color for channel"),
                         tr("Set specular color for channel"));
    QObject::connect(specularColorButton, &QColorPushButton::currentColorChanged, [i, this](const QColor& c) {
      this->OnSpecularColorChanged(i, c);
    });
    // init
    this->OnSpecularColorChanged(i, cspec);

    QColorPushButton* emissiveColorButton = new QColorPushButton();
    QColor cemis = QColor::fromRgbF(scene->m_material.m_emissive[i * 3 + 0],
                                    scene->m_material.m_emissive[i * 3 + 1],
                                    scene->m_material.m_emissive[i * 3 + 2]);
    emissiveColorButton->SetColor(cemis, true);
    Controls::addFormRow(sectionLayout,
                         "EmissiveColor",
                         emissiveColorButton,
                         tr("Set emissive color for channel"),
                         tr("Set emissive color for channel"));
    QObject::connect(emissiveColorButton, &QColorPushButton::currentColorChanged, [i, this](const QColor& c) {
      this->OnEmissiveColorChanged(i, c);
    });
    // init
    this->OnEmissiveColorChanged(i, cemis);

    QNumericSlider* roughnessSlider = new QNumericSlider();
    roughnessSlider->setRange(0.0, 100.0);
    roughnessSlider->setSingleStep(0.01);
    roughnessSlider->setValue(scene->m_material.m_roughness[i]);
    Controls::addFormRow(sectionLayout,
                         "Glossiness",
                         roughnessSlider,
                         tr("Set specular glossiness for channel"),
                         tr("Set specular glossiness for channel"));
    QObject::connect(
      roughnessSlider, &QNumericSlider::valueChanged, [i, this](double d) { this->OnRoughnessChanged(i, d); });
    this->OnRoughnessChanged(i, scene->m_material.m_roughness[i]);

    QObject::connect(section, &Section::checked, [i, this](bool is_checked) { this->OnChannelChecked(i, is_checked); });
    this->OnChannelChecked(i, channelenabled);

    section->setContentLayout(*fullLayout);
    // assumes per-channel sections are at the very end of the m_MainLayout
    m_MainLayout->addRow(section);
    m_channelSections.push_back(section);
  }
}

void
QAppearanceSettingsWidget::onTimeChanged(int timePoint)
{
  // update all gradient widgets with new histogram for time point
  if (!m_scene || !m_scene->m_volume) {
    return;
  }

  for (size_t i = 0; i < m_gradientWidgets.size(); ++i) {
    GradientWidget* gradientWidget = m_gradientWidgets[i];
    if (gradientWidget && i < m_scene->m_volume->sizeC()) {
      // Update histogram from current channel
      gradientWidget->setHistogram(m_scene->m_volume->channel(i)->m_histogram);
    }
  }
}
