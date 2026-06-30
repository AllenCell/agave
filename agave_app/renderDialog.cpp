#include "renderDialog.h"
#include "renderer.h"

#include "renderlib/gfxapi/IGLContext.h"
#include "renderlib/AppScene.h"
#include "renderlib/Logging.h"
#include "renderlib/RenderSettings.h"
#include "renderlib/ViewerWindow.h"
#include "renderlib/command.h"
#include "renderlib/gfxapi/IRenderWindow.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageWriter>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimeEdit>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

static const float ZOOM_STEP = 1.1f;
static const int TOOLBAR_INSET = 6;
static const char* kStopRenderText = "Are you sure you want to stop the render currently in progress?";

// find a subrectangle of fullTargetRect that fits the aspect ratio of srcRect
QRect
getFitTargetRect(QRect fullTargetRect, QRect srcRect)
{
  QRect targetRect = fullTargetRect;
  float srcaspect = (float)srcRect.width() / (float)srcRect.height();
  float targetaspect = (float)targetRect.width() / (float)targetRect.height();
  if (srcaspect > targetaspect) {
    targetRect.setHeight(targetRect.width() / srcaspect);
    targetRect.moveTop((fullTargetRect.height() - targetRect.height()) / 2);
  } else {
    targetRect.setWidth(targetRect.height() * srcaspect);
    targetRect.moveLeft((fullTargetRect.width() - targetRect.width()) / 2);
  }
  return targetRect;
}

QImage
makeCheckerboard(int w, int h)
{
  QImage destImage = QImage(w, h, QImage::Format_ARGB32);

  QPainter painter(&destImage);

  QPixmap pm(20, 20);
  QPainter pmp(&pm);
  pmp.fillRect(0, 0, 10, 10, Qt::lightGray);
  pmp.fillRect(10, 10, 10, 10, Qt::lightGray);
  pmp.fillRect(0, 10, 10, 10, Qt::darkGray);
  pmp.fillRect(10, 0, 10, 10, Qt::darkGray);
  pmp.end();

  QBrush brush(pm);
  painter.fillRect(0, 0, w, h, brush);
  painter.end();

  return destImage;
}

QImage
rescaleAndFitImage(QImage* image, int w, int h)
{
  QImage destImage = makeCheckerboard(w, h);

  QPainter painter(&destImage);
  // find the rectangle in destimage that will hold src image
  QRect destRect = getFitTargetRect(QRect(0, 0, w, h), QRect(0, 0, image->width(), image->height()));
  painter.drawImage(destRect, *image);
  painter.end();

  return destImage;
}

ImageDisplay::ImageDisplay(QWidget* parent)
  : QWidget(parent)
  , m_scale(1.0)
{
  m_pixmap = nullptr;
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  m_pixmap = new QPixmap(256, 256);
  m_pixmap->fill(Qt::black);
}

ImageDisplay::~ImageDisplay()
{
  delete m_pixmap;
}

void
ImageDisplay::setImage(QImage* image)
{
  if (image) {
    // TODO: sometimes we want to hold on to the transparent image with alphas, sometimes we don't!!!!
    // users may want to save the image with or without the background!
    // can this processing step be skipped at all?

    // remove alphas from image for display here
    QImage im = image->convertToFormat(QImage::Format_RGB32);

    m_pixmap->convertFromImage(im, Qt::ColorOnly);
    m_rect = m_pixmap->rect();
    m_rect.translate(-m_rect.center());
  }
  update();
}

void
ImageDisplay::save(const QString& filename)
{
  m_pixmap->save(filename);
}

void
ImageDisplay::paintEvent(QPaintEvent*)
{
  QPainter painter(this);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);

  painter.drawRect(0, 0, width(), height());
  painter.fillRect(0, 0, width(), height(), Qt::darkGray);

  if (!m_pixmap) {
    return;
  }
  painter.translate(rect().center());
  painter.scale(m_scale, m_scale);
  painter.translate(m_delta);
  painter.drawPixmap(m_rect.topLeft(), *m_pixmap);
}

void
ImageDisplay::mousePressEvent(QMouseEvent* event)
{
  m_reference = event->pos();
  qApp->setOverrideCursor(Qt::ClosedHandCursor);
  setMouseTracking(true);
}

void
ImageDisplay::mouseMoveEvent(QMouseEvent* event)
{
  m_delta += (event->pos() - m_reference) * 1.0 / m_scale;
  m_reference = event->pos();
  update();
}

void
ImageDisplay::mouseReleaseEvent(QMouseEvent*)
{
  qApp->restoreOverrideCursor();
  setMouseTracking(false);
}

void
ImageDisplay::scale(qreal s)
{
  m_scale *= s;
  repaint();
  // update();
}
void
ImageDisplay::setScale(qreal s)
{
  m_scale = s;
  repaint();
  // update();
}

void
ImageDisplay::fit(int w, int h)
{
  // fit image aspect ratio within the given widget rectangle.
  QRect targetRect = getFitTargetRect(rect(), QRect(0, 0, w, h));

  float imageaspect = (float)w / (float)h;
  float widgetaspect = (float)width() / (float)height();
  // targetRect will describe a sub-rectangle of the ImageDisplay's client rect
  if (imageaspect > widgetaspect) {
    // scale value from width!
    m_scale = ((float)targetRect.width() / (float)w);
  } else {
    // scale value from height!
    m_scale = ((float)targetRect.height() / (float)h);
  }

  // center image
  m_delta = QPointF();
  m_reference = QPointF();

  repaint();
  // update();
}

struct ResolutionPreset
{
  const char* label;
  int w;
  int h;
};
ResolutionPreset resolutionPresets[] = {
  { "4:3 640x480", 640, 480 },       { "4:3 1024x768", 1024, 768 },     { "4:3 1280x960", 1280, 960 },
  { "4:3 1600x1200", 1600, 1200 },   { "16:9 640x360", 640, 360 },      { "16:9 960x540", 960, 540 },
  { "16:9 1280x720", 1280, 720 },    { "16:9 1920x1080", 1920, 1080 },  { "16:9 3840x2160", 3840, 2160 },
  { "16:10 1920x1200", 1920, 1200 }, { "16:10 2560x1440", 2560, 1440 }, { "16:10 3840x2160", 3840, 2160 },
};

static QLabel*
makeGroupLabel(const std::string& text)
{
  QLabel* label = new QLabel(QString::fromStdString(text));
  label->setStyleSheet("font-size: 14px;");
  label->setTextFormat(Qt::RichText);
  return label;
}

RenderDialog::RenderDialog(ViewerWindow* borrowedRenderer,
                           const RenderSettings& renderSettings,
                           const Scene& scene,
                           const CCamera& ccamera,
                           gfxApi::IGLContext* glContext,
                           const LoadSpec& loadSpec,
                           CaptureSettings* captureSettings,
                           int viewportWidth,
                           int viewportHeight,
                           QWidget* parent)
  : m_renderer(borrowedRenderer->m_renderer.get())
  , m_renderSettings(renderSettings)
  , m_scene(scene)
  , m_camera(ccamera)
  , m_glContext(glContext)
  , m_loadSpec(loadSpec)
  , m_renderThread(nullptr)
  , m_frameRenderTime(0)
  , m_uiUpdateTime(0)
  , mWidth(0)
  , mHeight(0)
  , mFrameNumber(0)
  , mCaptureSettings(captureSettings)
  , mTimeSeriesProgressLabel(nullptr)
  , mRenderProgressLabel(nullptr)
  , QDialog(parent)
{
  setWindowTitle(tr("AGAVE Render"));
  setStyleSheet(R"(
QGroupBox
{
    font-size: 12px;
}
)");
  mImageView = new ImageDisplay(this);
  mRenderButton = new QPushButton("Start Rendering", this);
  mRenderButton->setStyleSheet("font-size: 16px;");
  // mPauseRenderButton = new QPushButton("&Pause", this);
  mStopRenderButton = new QPushButton("Stop Rendering", this);
  mStopRenderButton->setStyleSheet("font-size: 16px;");
  // mSaveButton = new QPushButton("&Save", this);
  mCloseButton = new QPushButton("Close Render", this);
  mCloseButton->setStyleSheet("font-size: 16px;");

  bool isTimeSeries = scene.m_timeLine.maxTime() > 0;

  mFrameProgressBar = new QProgressBar(this);
  if (mCaptureSettings->renderDuration.durationType == eRenderDurationType::SAMPLES) {
    mFrameProgressBar->setRange(0, mCaptureSettings->renderDuration.samples);
  } else {
    mFrameProgressBar->setRange(0, mCaptureSettings->renderDuration.duration);
  }

  mRenderDurationEdit = new QButtonGroup(this);
  QPushButton* samplesButton = new QPushButton(tr("Samples"), this);
  // samplesButton->setStyleSheet(
  //   "border-top-right-radius: 1px; border-bottom-right-radius: 1px; margin-right:0px; padding-right:0px;");
  samplesButton->setToolTip(QString("<FONT>Render for a fixed number of samples per pixel</FONT>"));
  samplesButton->setCheckable(true);
  QPushButton* timeButton = new QPushButton(tr("Time"), this);
  // timeButton->setStyleSheet(
  //   "border-top-left-radius: 1px; border-bottom-left-radius: 1px; margin-left:0px; padding-left:0px;");
  timeButton->setToolTip(QString("<FONT>Render for a fixed amount of time</FONT>"));
  timeButton->setCheckable(true);
  mRenderDurationEdit->addButton(samplesButton, eRenderDurationType::SAMPLES);
  mRenderDurationEdit->addButton(timeButton, eRenderDurationType::TIME);
  auto mapDurationTypeToUIIndex = std::map<eRenderDurationType, int>{
    { eRenderDurationType::SAMPLES, 0 },
    { eRenderDurationType::TIME, 1 },
  };
  mRenderDurationEdit->button(mCaptureSettings->renderDuration.durationType)->setChecked(true);

  mRenderSamplesEdit = new QSpinBox(this);
  mRenderSamplesEdit->setMinimum(1);
  // arbitrarily chosen
  mRenderSamplesEdit->setMaximum(65536);
  mRenderSamplesEdit->setValue(mCaptureSettings->renderDuration.samples);
  mRenderTimeEdit = new QTimeEdit(this);
  mRenderTimeEdit->setDisplayFormat("hh:mm:ss");
  mRenderTimeEdit->setMinimumTime(QTime(0, 0, 1));
  int h = mCaptureSettings->renderDuration.duration / (60 * 60);
  int m = (mCaptureSettings->renderDuration.duration - h * 60 * 60) / 60;
  int s = (mCaptureSettings->renderDuration.duration - h * 60 * 60 - m * 60);
  mRenderTimeEdit->setTime(QTime(h, m, s));

  mMainViewWidth = viewportWidth;
  mMainViewHeight = viewportHeight;
  mWidth = mCaptureSettings->width;
  mHeight = mCaptureSettings->height;
  mAspectRatio = (float)mWidth / (float)mHeight;

  m_camera.m_Film.m_Resolution.SetResX(mWidth);
  m_camera.m_Film.m_Resolution.SetResY(mHeight);

  mWidthInput = new QLineEdit(QString::number(mWidth), this);
  mWidthInput->setValidator(new QIntValidator(2, 16384, this));
  mHeightInput = new QLineEdit(QString::number(mHeight), this);
  mHeightInput->setValidator(new QIntValidator(2, 16384, this));

  mLockAspectRatio = new QPushButton(QIcon(), "", this);
  mLockAspectRatio->setObjectName("lockAspectRatioBtn");
  mLockAspectRatio->setCheckable(true);
  mLockAspectRatio->setChecked(true);
  mLockAspectRatio->setToolTip(QString("<FONT>Lock/unlock aspect ratio when editing X and Y values</FONT>"));
  connect(mLockAspectRatio, &QPushButton::toggled, [this]() { mLockAspectRatio->style()->polish(mLockAspectRatio); });

  mResolutionPresets = new QComboBox(this);
  mResolutionPresets->addItem("Resolution Presets...");
  mResolutionPresets->addItem(QString::fromStdString("Main window (" + std::to_string(mMainViewWidth) + "x" +
                                                     std::to_string(mMainViewHeight) + ")"));
  for (int i = 0; i < sizeof(resolutionPresets) / sizeof(ResolutionPreset); i++) {
    mResolutionPresets->addItem(resolutionPresets[i].label);
  }

  mStartTimeInput = new QSpinBox(this);
  mStartTimeInput->setMinimum(0);
  mStartTimeInput->setMaximum(scene.m_timeLine.maxTime());
  mStartTimeInput->setValue(mCaptureSettings->startTime);
  mStartTimeInput->setToolTip(QString("<FONT>First time index of time series to render.</FONT>"));
  mEndTimeInput = new QSpinBox(this);
  mEndTimeInput->setMinimum(0);
  mEndTimeInput->setMaximum(scene.m_timeLine.maxTime());
  mEndTimeInput->setValue(mCaptureSettings->endTime);
  mEndTimeInput->setToolTip(QString("<FONT>Last time index of time series to render.</FONT>"));

  mTimeSeriesProgressBar = new QProgressBar(this);
  mTimeSeriesProgressBar->setRange(0, abs(mEndTimeInput->value() - mStartTimeInput->value()) + 1);
  mTimeSeriesProgressBar->setValue(0);

  mSelectSaveDirectoryButton = new QPushButton("...", this);
  mSelectSaveDirectoryButton->setToolTip(QString("<FONT>Select directory where rendered images will be saved.</FONT>"));

  mAutosaveCheckbox = new QCheckBox("Autosave", this);
  mAutosaveCheckbox->setChecked(true);
  mAutosaveCheckbox->setVisible(false);
  mAutosaveCheckbox->setEnabled(false);

  mSaveDirectoryLabel = new QLabel(QString::fromStdString(mCaptureSettings->outputDir), this);
  mSaveFilePrefix = new QLineEdit(QString::fromStdString(mCaptureSettings->filenamePrefix), this);
  mSaveFilePrefix->setToolTip(QString("<FONT>All images saved as PNG files.  If you are rendering a time series, each "
                                      "image from the series will be saved "
                                      "individually with suffix _0000, _0001, etc.</FONT>"));

  mImagePreviewLabel = new QLabel("Image will populate once you start rendering.", mImageView);
  mImagePreviewLabel->setAlignment(Qt::AlignCenter);
  mImagePreviewLabel->setStyleSheet("QLabel { background-color : silver; color : black; }");
  mImagePreviewLabel->setMargin(12);

  mToolbar = new QWidget(mImageView);
  QHBoxLayout* toolbarLayout = new QHBoxLayout(mToolbar);
  toolbarLayout->setSpacing(0);
  toolbarLayout->setContentsMargins(0, 0, 0, 0);
  // toolbarLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
  QPushButton* zoomInButton = new QPushButton(" + ", mToolbar);
  zoomInButton->setToolTip(QString("<FONT>Zoom in</FONT>"));
  zoomInButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  zoomInButton->adjustSize();
  QPushButton* zoomOutButton = new QPushButton(" - ", mToolbar);
  zoomOutButton->setToolTip(QString("<FONT>Zoom out</FONT>"));
  zoomOutButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  zoomOutButton->adjustSize();
  QPushButton* zoomFitButton = new QPushButton("[ ]", mToolbar);
  zoomFitButton->setToolTip(QString("<FONT>Zoom to fit</FONT>"));
  zoomFitButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  zoomFitButton->adjustSize();
  int width = zoomFitButton->fontMetrics().boundingRect(zoomFitButton->text()).width() * 2 + 7;
  zoomInButton->setMaximumWidth(width);
  zoomOutButton->setMaximumWidth(width);
  zoomFitButton->setMaximumWidth(width);
  zoomInButton->setFocusPolicy(Qt::NoFocus);
  zoomOutButton->setFocusPolicy(Qt::NoFocus);
  zoomFitButton->setFocusPolicy(Qt::NoFocus);
  toolbarLayout->addWidget(zoomInButton);
  toolbarLayout->addWidget(zoomOutButton);
  toolbarLayout->addWidget(zoomFitButton);
  connect(zoomInButton, &QPushButton::clicked, this, &RenderDialog::onZoomInClicked);
  connect(zoomOutButton, &QPushButton::clicked, this, &RenderDialog::onZoomOutClicked);
  connect(zoomFitButton, &QPushButton::clicked, this, &RenderDialog::onZoomFitClicked);
  // mToolbar->addAction("+", this, &RenderDialog::onZoomInClicked);
  // mToolbar->addSeparator();
  // mToolbar->addAction("-", this, &RenderDialog::onZoomOutClicked);
  // mToolbar->addSeparator();
  // mToolbar->addAction("[ ]", this, &RenderDialog::onZoomFitClicked);
  mToolbar->setLayout(toolbarLayout);
  mToolbar->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);

  connect(mRenderButton, &QPushButton::clicked, this, &RenderDialog::render);
  // connect(mPauseRenderButton, &QPushButton::clicked, this, &RenderDialog::pauseRendering);
  connect(mStopRenderButton, &QPushButton::clicked, this, &RenderDialog::onStopButtonClick);
  connect(mCloseButton, &QPushButton::clicked, this, &RenderDialog::close);
  // connect(mSaveButton, &QPushButton::clicked, this, &RenderDialog::save);
  connect(mResolutionPresets, SIGNAL(currentIndexChanged(int)), this, SLOT(onResolutionPreset(int)));
  connect(mWidthInput, &QLineEdit::textChanged, this, &RenderDialog::updateWidth);
  connect(mHeightInput, &QLineEdit::textChanged, this, &RenderDialog::updateHeight);
  connect(mRenderSamplesEdit, SIGNAL(valueChanged(int)), this, SLOT(updateRenderSamples(int)));
  connect(mRenderTimeEdit, SIGNAL(timeChanged(const QTime&)), this, SLOT(updateRenderTime(const QTime&)));
  connect(mRenderDurationEdit, SIGNAL(idClicked(int)), this, SLOT(onRenderDurationTypeChanged(int)));
  //  connect(mRenderDurationEdit, SIGNAL(currentIndexChanged(int)), this, SLOT(onRenderDurationTypeChanged(int)));
  connect(mStartTimeInput, QOverload<int>::of(&QSpinBox::valueChanged), this, &RenderDialog::onStartTimeChanged);
  connect(mEndTimeInput, QOverload<int>::of(&QSpinBox::valueChanged), this, &RenderDialog::onEndTimeChanged);
  connect(mSelectSaveDirectoryButton, &QPushButton::clicked, this, &RenderDialog::onSelectSaveDirectoryClicked);
  connect(mSaveFilePrefix, &QLineEdit::textChanged, this, &RenderDialog::onSaveFilePrefixChanged);

  QVBoxLayout* outputResolutionLayout = new QVBoxLayout();
  QHBoxLayout* topButtonsLayout = new QHBoxLayout();
  topButtonsLayout->addWidget(new QLabel(tr("X:")), 0);
  topButtonsLayout->addWidget(mWidthInput, 1);
  topButtonsLayout->addWidget(new QLabel(tr("Y:")), 0);
  topButtonsLayout->addWidget(mHeightInput, 1);
  topButtonsLayout->addWidget(mLockAspectRatio, 0);
  outputResolutionLayout->addWidget(makeGroupLabel("<b>Output Resolution</b>"));
  outputResolutionLayout->addLayout(topButtonsLayout);
  outputResolutionLayout->addWidget(mResolutionPresets);

  QHBoxLayout* timeHLayout = new QHBoxLayout();
  timeHLayout->addWidget(new QLabel(tr("Start:")), 0);
  timeHLayout->addWidget(mStartTimeInput, 1);
  timeHLayout->addWidget(new QLabel(tr("End:")), 0);
  timeHLayout->addWidget(mEndTimeInput, 1);
  QVBoxLayout* timeLayout = new QVBoxLayout();
  QLabel* timeLabel = makeGroupLabel("<b>Time Series</b>");
  timeLabel->setToolTip(
    "<FONT>Each image from the series will be saved individually with suffix _0000, _0001, etc.<FONT>");
  timeLayout->addWidget(timeLabel);
  timeLayout->addLayout(timeHLayout);

  //  QHBoxLayout* saveFileLayout = new QHBoxLayout();
  // saveFileLayout->addWidget(mSaveFilePrefix, 1);
  QHBoxLayout* saveDirLayout = new QHBoxLayout();
  saveDirLayout->addWidget(mSaveDirectoryLabel, 2);
  saveDirLayout->addWidget(mSelectSaveDirectoryButton, 1);
  QFormLayout* saveSettingsLayout = new QFormLayout();
  saveSettingsLayout->setLabelAlignment(Qt::AlignLeft);
  saveSettingsLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  saveSettingsLayout->addRow(makeGroupLabel("<b>Output File</b>"));
  saveSettingsLayout->addRow(tr("File Name:"), mSaveFilePrefix);
  saveSettingsLayout->addRow(tr("Location:"), saveDirLayout);

  int ml, mt, mr, mb;

  QHBoxLayout* durationsHLayout = new QHBoxLayout();
  // durationsHLayout->addStretch();
  durationsHLayout->addWidget(mRenderDurationEdit->button(eRenderDurationType::SAMPLES));
  durationsHLayout->addWidget(mRenderDurationEdit->button(eRenderDurationType::TIME));
  // durationsHLayout->addStretch();
  durationsHLayout->getContentsMargins(&ml, &mt, &mr, &mb);
  durationsHLayout->setContentsMargins(0, mt, 0, mb);
  durationsHLayout->setSpacing(0);

  QFormLayout* durationsHLayoutTime = new QFormLayout();
  durationsHLayoutTime->getContentsMargins(&ml, &mt, &mr, &mb);
  durationsHLayoutTime->setContentsMargins(0, mt, 0, mb);
  durationsHLayoutTime->setLabelAlignment(Qt::AlignLeft);
  durationsHLayoutTime->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  durationsHLayoutTime->addRow(tr("Time:"), mRenderTimeEdit);
  QWidget* durationSettingsTime = new QWidget();
  durationSettingsTime->setLayout(durationsHLayoutTime);

  QFormLayout* durationsHLayoutSamples = new QFormLayout();
  durationsHLayoutSamples->getContentsMargins(&ml, &mt, &mr, &mb);
  durationsHLayoutSamples->setContentsMargins(0, mt, 0, mb);
  durationsHLayoutSamples->setLabelAlignment(Qt::AlignLeft);
  durationsHLayoutSamples->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  durationsHLayoutSamples->addRow(tr("Samples:"), mRenderSamplesEdit);
  QWidget* durationSettingsSamples = new QWidget();
  durationSettingsSamples->setLayout(durationsHLayoutSamples);

  mRenderDurationSettings = new QStackedWidget(this);
  mRenderDurationSettings->addWidget(durationSettingsTime);
  mRenderDurationSettings->addWidget(durationSettingsSamples);
  // initialize
  setRenderDurationType(mCaptureSettings->renderDuration.durationType);

  QWidget* durationsWidget = new QWidget();
  durationsWidget->setLayout(durationsHLayout);

  QVBoxLayout* durationsLayout = new QVBoxLayout();
  durationsLayout->addWidget(makeGroupLabel("<b>Image Quality</b>"));
  durationsLayout->addWidget(durationsWidget);
  durationsLayout->addWidget(mRenderDurationSettings);

  QHBoxLayout* bottomButtonsLayout = new QHBoxLayout();
  bottomButtonsLayout->addWidget(mRenderButton);
  // bottomButtonslayout->addWidget(mPauseRenderButton);
  bottomButtonsLayout->addWidget(mStopRenderButton);
  bottomButtonsLayout->addWidget(mCloseButton);
  mStopRenderButton->setVisible(false);
  mCloseButton->setVisible(false);
  // bottomButtonslayout->addWidget(mSaveButton);

  static const int MAX_CONTROLS_WIDTH = 400;

  QGroupBox* groupBox0 = new QGroupBox();
  groupBox0->setMaximumWidth(MAX_CONTROLS_WIDTH);
  groupBox0->setLayout(outputResolutionLayout);

  QGroupBox* groupBox1 = new QGroupBox();
  groupBox1->setMaximumWidth(MAX_CONTROLS_WIDTH);
  groupBox1->setLayout(timeLayout);
  groupBox1->setVisible(isTimeSeries);

  QGroupBox* groupBox2 = new QGroupBox();
  groupBox2->setMaximumWidth(MAX_CONTROLS_WIDTH);
  groupBox2->setLayout(durationsLayout);

  QGroupBox* groupBox3 = new QGroupBox();
  groupBox3->setMaximumWidth(MAX_CONTROLS_WIDTH);
  groupBox3->setLayout(saveSettingsLayout);

  QGroupBox* groupBox4 = new QGroupBox();
  groupBox4->setMaximumWidth(MAX_CONTROLS_WIDTH);
  groupBox4->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  groupBox4->setLayout(bottomButtonsLayout);

  QVBoxLayout* controlsLayout = new QVBoxLayout();
  controlsLayout->addWidget(groupBox0);
  controlsLayout->addWidget(groupBox1);
  controlsLayout->addWidget(groupBox2);
  controlsLayout->addWidget(groupBox3);
  controlsLayout->addWidget(groupBox4);
  controlsLayout->addStretch(1);
  controlsLayout->setSpacing(0);
  controlsLayout->setContentsMargins(0, 0, 0, 0);

  mWidgetsToDisableWhileRendering.push_back(groupBox0);
  mWidgetsToDisableWhileRendering.push_back(groupBox1);
  mWidgetsToDisableWhileRendering.push_back(groupBox2);
  mWidgetsToDisableWhileRendering.push_back(groupBox3);

  QGroupBox* controlsGroupBox = new QGroupBox();
  controlsGroupBox->setLayout(controlsLayout);
  controlsGroupBox->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);
  controlsGroupBox->setMaximumWidth(MAX_CONTROLS_WIDTH);

  QVBoxLayout* viewLayout = new QVBoxLayout();
  viewLayout->addWidget(mImageView);
  // viewLayout->addWidget(mToolbar);
  mToolbar->setParent(mImageView);
  mImagePreviewLabel->setParent(mImageView);

  QHBoxLayout* mainDialogLayout = new QHBoxLayout();
  mainDialogLayout->addWidget(controlsGroupBox, 1);
  mainDialogLayout->addLayout(viewLayout, 3);

  QGroupBox* progressGroup = new QGroupBox();
  QFormLayout* progressLayout = new QFormLayout();
  progressLayout->setLabelAlignment(Qt::AlignLeft);
  progressLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  mRenderProgressLabel = makeGroupLabel("<b>Render</b> " + loadSpec.getFilename());
  progressLayout->addRow(mRenderProgressLabel);

  // do we have a potential time series?
  if (scene.m_timeLine.maxTime() > 0) {
    progressLayout->addRow(tr("Frame Progress"), mFrameProgressBar);
    mTimeSeriesProgressLabel = new QLabel("Total Progress");
    updateTimeSeriesProgressLabel();
    progressLayout->addRow(mTimeSeriesProgressLabel, mTimeSeriesProgressBar);
  } else {
    progressLayout->addRow(tr("Total Progress"), mFrameProgressBar);
    mTimeSeriesProgressBar->setVisible(false);
  }

  // progressLayout->setContentsMargins(0, 0, 0, 0);
  progressLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
  progressGroup->setLayout(progressLayout);

  QVBoxLayout* reallyMainDialogLayout = new QVBoxLayout();
  reallyMainDialogLayout->addLayout(mainDialogLayout);
  reallyMainDialogLayout->addWidget(progressGroup);

  setLayout(reallyMainDialogLayout);
  positionToolbar();
  updatePreviewImage();
}

void
RenderDialog::save()
{
  pauseRendering();

  QFileDialog::Options options;
#ifdef __linux__
  options |= QFileDialog::DontUseNativeDialog;
#endif

  const QByteArrayList supportedFormats = QImageWriter::supportedImageFormats();
  QStringList supportedFormatStrings;
  foreach (const QByteArray& item, supportedFormats) {
    supportedFormatStrings.append(QString::fromLocal8Bit(item)); // Assuming local 8-bit.
  }

  static const QStringList desiredFormats = { "png", "jpg", "tif" };

  QStringList formatFilters;
  foreach (const QString& desiredFormatName, desiredFormats) {
    if (supportedFormatStrings.contains(desiredFormatName, Qt::CaseInsensitive)) {
      formatFilters.append(desiredFormatName.toUpper() + " (*." + desiredFormatName + ")");
    }
  }
  QString allSupportedFormatsFilter = formatFilters.join(";;");

  QString fileName =
    QFileDialog::getSaveFileName(this, tr("Save Image"), QString(), allSupportedFormatsFilter, nullptr, options);

  if (fileName.isEmpty()) {
    return;
  }
  mImageView->save(fileName);
}

void
RenderDialog::setImage(QImage* image)
{
  if (mWidth == image->width() && mHeight == image->height()) {
    mImageView->setImage(image);
    return;
  }

  // resize and letterbox the image to match our current render resolution.
  QImage destImage = rescaleAndFitImage(image, mWidth, mHeight);
  mImageView->setImage(&destImage);
}

void
RenderDialog::updateUIReadyToRender()
{
  if (mRenderProgressLabel) {
    mRenderProgressLabel->setText(QString::fromStdString("<b>Render</b> " + m_loadSpec.getFilename()));
  }
}

void
RenderDialog::updateUIStartRendering()
{
  mImagePreviewLabel->hide();

  mRenderButton->setVisible(false);
  mStopRenderButton->setVisible(true);
  mCloseButton->setVisible(false);
  mRenderProgressLabel->setText(QString::fromStdString("<b>Rendering</b> " + m_loadSpec.getFilename()));
  for (auto w : mWidgetsToDisableWhileRendering) {
    w->setEnabled(false);
  }
}

QString
RenderDialog::getUniqueNextFilename(QString path)
{
  QFileInfo fileInfo(path);

  while (fileInfo.exists()) {
    QString baseName = fileInfo.baseName();
    QString suffix = fileInfo.completeSuffix();
    QString dirPath = fileInfo.dir().path();
    QRegularExpression re("^(?<baseName>.*)\\((?<number>\\d+)\\)$");
    QRegularExpressionMatch match = re.match(baseName);
    if (match.hasMatch()) {
      baseName = match.captured("baseName");
      int number = match.captured("number").toInt();
      number++;
      baseName = baseName + "(" + QString::number(number) + ")";
    } else {
      baseName = baseName + "(1)";
    }

    path = dirPath + "/" + baseName + "." + suffix;
    fileInfo = QFileInfo(path);
  }

  return path;
}

bool
RenderDialog::getOverwriteConfirmation()
{
  QString path = getFullSavePath();

  QFileInfo fileInfo(path);
  if (!fileInfo.exists()) {
    return true;
  }

  QMessageBox msgBox;
  msgBox.setWindowTitle("Overwrite Existing File?");
  msgBox.setText(fileInfo.fileName() + " : A file with this name exists at this location already.");
  msgBox.setInformativeText("Do you want to overwrite it?");
  msgBox.setDefaultButton(msgBox.addButton(tr("Cancel"), QMessageBox::NoRole));
  auto overwriteButton = msgBox.addButton(tr("Overwrite"), QMessageBox::YesRole);
  int ret = msgBox.exec();

  return msgBox.clickedButton() == overwriteButton;
}

void
RenderDialog::render()
{

  if (!this->m_renderThread || m_renderThread->isFinished()) {

    resetProgress();

    // for time series, we will try to get overwrite confirmation.
    // for single frames, we will rely on generated unique filenames.
    if (mTimeSeriesProgressLabel && !getOverwriteConfirmation()) {
      return;
    }

    updateUIStartRendering();

    if (!m_glContext->isValid() && !m_glContext->create()) {
      LOG_ERROR << "RenderDialog: failed to prepare the Qt GL context";
      updateUIStopRendering(false);
      return;
    }

    if (!m_renderThread) {

      m_renderThread = new Renderer("Render dialog render thread ", this, m_mutex);

      // queued across thread boundary.  requestProcessed is called from another thread, asynchronously.
      connect(m_renderThread, &Renderer::requestProcessed, this, &RenderDialog::onRenderRequestProcessed);
      connect(m_renderThread, &Renderer::frameDone, this, &RenderDialog::onFrameDone);
      connect(m_renderThread, &Renderer::finished, this, &RenderDialog::onRenderThreadFinished);
    }

    // now get our rendering resources into this Renderer object
    m_renderThread->configure(m_renderer,
                              m_renderSettings,
                              m_scene,
                              m_camera,
                              m_loadSpec,
                              renderlib::RendererType_Pathtrace,
                              m_glContext,
                              mCaptureSettings);

    onZoomFitClicked();
    // first time in, set up stream mode and give the first draw request
    resumeRendering();

    LOG_INFO << "Starting render thread...";
    m_glContext->moveToThread(m_renderThread);
    m_renderThread->start();
  } else {
    resumeRendering();
  }
}

QString
RenderDialog::getFullSavePath()
{
  QString autosavePath = mSaveDirectoryLabel->text();
  QDir d(autosavePath);
  // TODO set up autosave path when we start rendering
  bool pathOk = d.mkpath(autosavePath);
  if (!pathOk) {
    LOG_ERROR << "Failed to make path " << autosavePath.toStdString();
  }

  // if not time series, then don't add the frame number to the filename
  QString frameSuffix;
  if (mTimeSeriesProgressLabel) {
    frameSuffix = QString("_%1").arg(mFrameNumber, 4, 10, QChar('0'));
  }
  QString filename = mSaveFilePrefix->text() + frameSuffix + QString(".png");
  QFileInfo fileInfo(d, filename);
  QString saveFilePath = fileInfo.absoluteFilePath();
  return saveFilePath;
}

void
RenderDialog::onFrameDone(QImage image)
{
  // note that every render request that comes thru here sends a
  // whole new image.
  // this is likely much less efficient than writing the image in-place

  // this is called after the render thread has completed a render request
  // the QImage is sent here from the thread.
  // this is an incremental update of a render and our chance to update the GUI and state of our processing

  if (!m_renderThread || m_renderThread->isFinished() || m_renderThread->isInterruptionRequested()) {
    LOG_DEBUG << "received frame after render terminated";
  }
  if (mTimeSeriesProgressBar->value() >= mTimeSeriesProgressBar->maximum()) {
    LOG_DEBUG << "received frame after timeline completed";
    return;
  }

  mFrameProgressBar->setValue(mFrameProgressBar->maximum());
  // stop streaming mode while we deal with a completed frame
  m_renderThread->setStreamMode(0);

  // we just received the last sample.
  // however, another sample was already enqueued!!!!
  // so we know we will have one sample to discard.

  LOG_DEBUG << "frame " << mFrameNumber << " progress completed";

  // update display with finished frame
  this->setImage(&image);

  // save image
  if (mAutosaveCheckbox->isChecked()) {
    QString saveFilePath = getFullSavePath();
    // if not in time series, then unique-ify the filename
    if (!mTimeSeriesProgressLabel) {
      saveFilePath = getUniqueNextFilename(saveFilePath);
    }

    // TODO don't throw away alpha - rethink how image is composited with background color
    QImage im = image.convertToFormat(QImage::Format_RGB32);
    bool ok = im.save(saveFilePath);
    if (!ok) {
      LOG_ERROR << "Failed to save render " << saveFilePath.toStdString();
    } else {
      LOG_INFO << "Saved " << saveFilePath.toStdString();
    }
  }

  // increment frame
  mFrameNumber += 1;
  mTimeSeriesProgressBar->setValue(mTimeSeriesProgressBar->value() + 1);
  updateTimeSeriesProgressLabel();
  LOG_DEBUG << "Total Progress " << mTimeSeriesProgressBar->value() << " / " << mTimeSeriesProgressBar->maximum();

  // done with LAST frame? halt everything.
  if (mTimeSeriesProgressBar->value() >= mTimeSeriesProgressBar->maximum()) {
    LOG_DEBUG << "all frames completed.  ending render";
    stopRendering();
    updateUIStopRendering(true);
  } else {
    LOG_DEBUG << "reset frame progress for next frame";
    // reset frame progress and render time
    mFrameProgressBar->setValue(0);
    m_frameRenderTime = 0;
    m_uiUpdateTime = 0;

    // force a redraw of this dialog
    repaint();

    // set up for next frame!
    // SetTimeCommand should result in this anyway, so we don't need to do it here.
    // m_renderer->renderSettings().SetNoIterations(0);

    LOG_DEBUG << "queueing setTime " << mFrameNumber << " command ";
    std::vector<Command*> cmd;
    SetTimeCommandD timedata;
    timedata.m_time = mFrameNumber;
    cmd.push_back(new SetTimeCommand(timedata));
    m_renderThread->addRequest(new RenderRequest(nullptr, cmd, false));
    // re-enable streamed progressive render frames
    m_renderThread->setStreamMode(1);
  }
}

void
RenderDialog::onRenderRequestProcessed(RenderRequest* req, QImage image)
{
  // note that every render request that comes thru here sends a
  // whole new image.
  // this is likely much less efficient than writing the image in-place

  // this is called after the render thread has completed a render request
  // the QImage is sent here from the thread.
  // this is an incremental update of a render and our chance to update the GUI and state of our processing

  if (req == nullptr) {
    LOG_DEBUG << "render callback after no request processed";
    return;
  }
  if (!m_renderThread || m_renderThread->isFinished() || m_renderThread->isInterruptionRequested()) {
    LOG_DEBUG << "received sample after render terminated";
  }
  if (mTimeSeriesProgressBar->value() >= mTimeSeriesProgressBar->maximum()) {
    LOG_DEBUG << "received frame after timeline completed";
    return;
  }

  // increment our time counter
  this->m_frameRenderTime += req->getActualDuration();
  m_uiUpdateTime += req->getActualDuration();

  // increment progress
  if (mRenderDurationType == eRenderDurationType::SAMPLES) {
    mFrameProgressBar->setValue(mFrameProgressBar->value() + 1);
  } else {
    // nano to seconds.  render durations in the dialog are specified in seconds.
    mFrameProgressBar->setValue(m_frameRenderTime / (1000 * 1000 * 1000));
  }

  // did a frame finish?
  {
    // update display if it's time to do so.
    static constexpr float IMAGE_UPDATE_INTERVAL_SECONDS = 1.0;
    if (m_uiUpdateTime / (1000 * 1000 * 1000) > IMAGE_UPDATE_INTERVAL_SECONDS) {
      m_uiUpdateTime = 0;
      this->setImage(&image);
    }
  }
}

void
RenderDialog::pauseRendering()
{
  // note this leaves the render thread alive and well
  if (m_renderThread) {
    m_renderThread->setStreamMode(0);
  }
}

void
RenderDialog::stopRendering()
{
  if (m_renderThread) {
    endRenderThread();
  }
}

void
RenderDialog::endRenderThread()
{
  pauseRendering();
  if (m_renderThread && m_renderThread->isRunning()) {
    m_renderThread->requestInterruption();
    m_renderThread->wakeUp();
    // we need to ensure that the render thread is not trying to make calls back into this thread
    m_renderThread->wait();
    if (m_glContext) {
      m_glContext->moveToThread(qApp->thread());
    }
  }
}

void
RenderDialog::resumeRendering()
{
  if (m_renderThread) {
    m_renderThread->setStreamMode(1);

    std::vector<Command*> cmd;

    // 1. pick up any resolution changes?
    SetResolutionCommandD resdata;
    resdata.m_x = mWidth;
    resdata.m_y = mHeight;
    cmd.push_back(new SetResolutionCommand(resdata));

    // 2. make sure we set the right time
    SetTimeCommandD timedata;
    timedata.m_time = mFrameNumber;
    cmd.push_back(new SetTimeCommand(timedata));

    // 3. the redraw request
    RequestRedrawCommandD data;
    cmd.push_back(new RequestRedrawCommand(data));

    m_renderThread->addRequest(new RenderRequest(nullptr, cmd, false));
  }
}

void
RenderDialog::done(int r)
{
  this->m_renderer = nullptr;
  endRenderThread();
  if (m_renderThread) {
    m_renderThread->deleteLater();
    m_renderThread = nullptr;
  }
  QDialog::done(r);
}

void
RenderDialog::onResolutionPreset(int index)
{
  if (index > 1) {
    // find preset res and set w/h
    const ResolutionPreset& preset = resolutionPresets[index - 2];
    mAspectRatio = (float)preset.w / (float)preset.h;
    mWidthInput->setText(QString::number(preset.w));
    mHeightInput->setText(QString::number(preset.h));
  } else if (index == 1) {
    // get xy from the main window size.
    mAspectRatio = (float)mMainViewWidth / (float)mMainViewHeight;
    mWidthInput->setText(QString::number(mMainViewWidth));
    mHeightInput->setText(QString::number(mMainViewHeight));
  }
  // restore index 0
  mResolutionPresets->setCurrentIndex(0);
}

void
RenderDialog::updateWidth(const QString& w)
{
  mWidth = w.toInt();
  m_camera.m_Film.m_Resolution.SetResX(mWidth);
  mCaptureSettings->width = mWidth;

  if (mLockAspectRatio->isChecked()) {
    mHeight = (int)(mWidth / mAspectRatio);
    mHeightInput->blockSignals(true);
    mHeightInput->setText(QString::number(mHeight));
    mHeightInput->blockSignals(false);
    m_camera.m_Film.m_Resolution.SetResY(mHeight);
    mCaptureSettings->height = mHeight;
  } else {
    mAspectRatio = (float)mWidth / (float)mHeight;
  }

  updatePreviewImage();
  resetProgress();
  updateUIReadyToRender();
}

void
RenderDialog::updateHeight(const QString& h)
{
  mHeight = h.toInt();
  m_camera.m_Film.m_Resolution.SetResY(mHeight);
  mCaptureSettings->height = mHeight;

  if (mLockAspectRatio->isChecked()) {
    mWidth = (int)(mHeight * mAspectRatio);
    mWidthInput->blockSignals(true);
    mWidthInput->setText(QString::number(mWidth));
    mWidthInput->blockSignals(false);
    m_camera.m_Film.m_Resolution.SetResX(mWidth);
    mCaptureSettings->width = mWidth;
  } else {
    mAspectRatio = (float)mWidth / (float)mHeight;
  }

  updatePreviewImage();
  resetProgress();
  updateUIReadyToRender();
}

int
RenderDialog::getXResolution()
{
  return m_camera.m_Film.m_Resolution.GetResX();
}

int
RenderDialog::getYResolution()
{
  return m_camera.m_Film.m_Resolution.GetResY();
}

void
RenderDialog::setRenderDurationType(eRenderDurationType type)
{
  mCaptureSettings->renderDuration.durationType = type;

  mRenderDurationType = type;

  if (mRenderDurationType == eRenderDurationType::SAMPLES) {
    mRenderTimeEdit->setEnabled(false);
    mRenderSamplesEdit->setEnabled(true);
  } else {
    mRenderTimeEdit->setEnabled(true);
    mRenderSamplesEdit->setEnabled(false);
  }
  mRenderDurationSettings->setCurrentIndex(type);
  resetProgress();
  updateUIReadyToRender();
}

void
RenderDialog::updateRenderSamples(int s)
{
  mCaptureSettings->renderDuration.samples = s;
  if (mRenderDurationType == eRenderDurationType::SAMPLES) {
    mFrameProgressBar->setMaximum(s);
  }
  resetProgress();
  updateUIReadyToRender();
}

void
RenderDialog::updateRenderTime(const QTime& t)
{
  mCaptureSettings->renderDuration.duration = t.hour() * 60 * 60 + t.minute() * 60 + t.second();

  if (mRenderDurationType == eRenderDurationType::TIME) {
    mFrameProgressBar->setMaximum(mCaptureSettings->renderDuration.duration);
  }
  resetProgress();
  updateUIReadyToRender();
}

void
RenderDialog::resetProgress()
{
  m_renderer->renderSettings().SetNoIterations(0);

  mFrameProgressBar->reset();
  mFrameProgressBar->setValue(0);

  m_frameRenderTime = 0; // FIX per frame render time vs total render time elapsed

  mTimeSeriesProgressBar->reset();
  mTimeSeriesProgressBar->setValue(0);
  mFrameNumber = mStartTimeInput->value();
}

void
RenderDialog::onRenderDurationTypeChanged(int index)
{
  // get userdata from value
  eRenderDurationType type = (eRenderDurationType)index;
  setRenderDurationType(type);
  if (type == eRenderDurationType::SAMPLES) {
    updateRenderSamples(mRenderSamplesEdit->value());
  } else {
    updateRenderTime(mRenderTimeEdit->time());
  }
}

void
RenderDialog::onZoomInClicked()
{
  mImageView->scale(ZOOM_STEP);
}

void
RenderDialog::onZoomOutClicked()
{
  mImageView->scale(1.0 / ZOOM_STEP);
}

void
RenderDialog::onZoomFitClicked()
{
  // fit image aspect ratio within the given widget rectangle.
  int w = mWidth;
  int h = mHeight;
  mImageView->fit(w, h);
}

void
RenderDialog::onStartTimeChanged(int t)
{
  // automatically update endtime if starttime was set larger than endtime
  if (mEndTimeInput->value() < t) {
    mEndTimeInput->setValue(t);
  }

  mTimeSeriesProgressBar->setRange(0, abs(mEndTimeInput->value() - mStartTimeInput->value()) + 1);

  mCaptureSettings->startTime = t;

  updateTimeSeriesProgressLabel();
  resetProgress();
  updateUIReadyToRender();
}

void
RenderDialog::onEndTimeChanged(int t)
{
  // automatically update starttime if endtime was set smaller than starttime
  if (mStartTimeInput->value() > t) {
    mStartTimeInput->setValue(t);
  }

  mTimeSeriesProgressBar->setRange(0, abs(mEndTimeInput->value() - mStartTimeInput->value()) + 1);

  mCaptureSettings->endTime = t;

  updateTimeSeriesProgressLabel();
  resetProgress();
  updateUIReadyToRender();
}

void
RenderDialog::onSelectSaveDirectoryClicked()
{
  QString dir = QFileDialog::getExistingDirectory(this,
                                                  tr("Select Directory"),
                                                  mSaveDirectoryLabel->text(),
                                                  QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  if (!dir.isEmpty()) {
    mSaveDirectoryLabel->setText(dir);
    mCaptureSettings->outputDir = dir.toStdString();
  }
}

void
RenderDialog::onSaveFilePrefixChanged(const QString& value)
{
  mCaptureSettings->filenamePrefix = value.toStdString();
}

void
RenderDialog::onRenderThreadFinished()
{
  m_renderThread->deleteLater();
  m_renderThread = nullptr;
}

bool
RenderDialog::getUserCancelConfirmation()
{
  QMessageBox::StandardButton btn = QMessageBox::question(this, "Stop Render?", QString(kStopRenderText));
  if (btn == QMessageBox::Yes) {
    return true;
  } else {
    return false;
  }
}

bool
RenderDialog::isRenderInProgress()
{
  if (!m_renderThread || m_renderThread->isFinished() || m_renderThread->isInterruptionRequested()) {
    return false;
  }
  if (mTimeSeriesProgressBar->value() >= mTimeSeriesProgressBar->maximum()) {
    return false;
  }
  if (mFrameProgressBar->value() >= mFrameProgressBar->maximum()) {
    return false;
  }
  return true;
}

void
RenderDialog::closeEvent(QCloseEvent* event)
{
  LOG_DEBUG << "closeEvent()";

  if (isRenderInProgress()) {
    if (getUserCancelConfirmation()) {
      event->accept();
      QDialog::closeEvent(event);
    } else {
      event->ignore();
      // do not close the dialog!
    }
  } else {
    event->accept();
    QDialog::closeEvent(event);
  }
}

void
RenderDialog::updateUIStopRendering(bool completed)
{
  mRenderButton->setVisible(true);
  mStopRenderButton->setVisible(false);
  mCloseButton->setVisible(completed);

  mRenderProgressLabel->setText(completed ? "<b>Render Complete!</b>" : "<b>Render Stopped</b>");

  if (completed) {
    // close all messageboxes?
    QWidgetList topWidgets = QApplication::topLevelWidgets();
    foreach (QWidget* w, topWidgets) {
      if (QMessageBox* mb = qobject_cast<QMessageBox*>(w)) {
        if (mb->text() == QString(kStopRenderText)) {
          mb->close();
        }
      }
    }
  }

  for (auto w : mWidgetsToDisableWhileRendering) {
    w->setEnabled(true);
  }
}

void
RenderDialog::onStopButtonClick()
{
  // note this does not pause/resume while waiting for confirmation
  if (isRenderInProgress()) {
    if (getUserCancelConfirmation()) {
      stopRendering();
      updateUIStopRendering(false);
    }
  } else {
    stopRendering();
    updateUIStopRendering(false);
  }
}

void
RenderDialog::positionToolbar()
{
  auto s = mImageView->size();
  mToolbar->move(s.width() - mToolbar->width() - TOOLBAR_INSET, s.height() - mToolbar->height() - TOOLBAR_INSET);
  mImagePreviewLabel->move(s.width() / 2 - mImagePreviewLabel->width() / 2,
                           s.height() / 2 - mImagePreviewLabel->height() / 2);
}

void
RenderDialog::resizeEvent(QResizeEvent* event)
{
  positionToolbar();
}
void
RenderDialog::showEvent(QShowEvent* event)
{
  positionToolbar();
}

void
RenderDialog::updatePreviewImage()
{
  mImagePreviewLabel->show();

  QImage img = makeCheckerboard(mWidth, mHeight);
  mImageView->setImage(&img);
  mImageView->fit(mWidth, mHeight);
}

void
RenderDialog::updateTimeSeriesProgressLabel()
{
  if (mTimeSeriesProgressLabel) {
    mTimeSeriesProgressLabel->setText("Total Progress (" + QString::number(mTimeSeriesProgressBar->value()) + "/" +
                                      QString::number(mTimeSeriesProgressBar->maximum()) + ")");
  }
}
