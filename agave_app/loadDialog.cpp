#include "loadDialog.h"

#include "Controls.h"
#include "RangeWidget.h"
#include "Section.h"

#include "renderlib/Logging.h"
#include "renderlib/io/FileReader.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

LoadDialog::LoadDialog(std::string path, const std::vector<MultiscaleDims>& dims, uint32_t scene, QWidget* parent)
  : QDialog(parent)
{
  m_reader = FileReader::getReader(path);
  mPath = path;
  mDims = dims;
  mSelectedLevel = 0;
  mScene = scene;

  std::string title;
  QString pathString = QString::fromStdString(path);
  QString extension = pathString.sliced(pathString.lastIndexOf("."));
  if ((pathString.indexOf("http") == 0) || (pathString.indexOf("s3:") == 0) || (pathString.indexOf("gs:") == 0)) {
    title = "URL Load Settings";
  } else if (extension.indexOf(".zarr") == 0) {
    title = "Directory Load Settings";
  } else if (extension.indexOf(".json") == 0) {
    title = "JSON Load Settings";
  } else {
    title = "File Load Settings";
  }

  setWindowTitle(tr((title).c_str()));
  setFocusPolicy(Qt::StrongFocus);

  // get standard QLabel font size
  QFont f = QLabel("A").font();
  int standardPointSize = f.pointSize();

  QPushButton* cancelButton = new QPushButton(tr("Cancel"));
  QPushButton* openButton = new QPushButton(tr("Open"));
  QHBoxLayout* buttonBox = new QHBoxLayout();
  buttonBox->addWidget(cancelButton);
  buttonBox->addWidget(openButton);
  connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
  connect(openButton, &QPushButton::clicked, this, &LoadDialog::accept);

  mSceneInput = new QSpinBox(this);
  mSceneInput->setMinimum(0);
  mSceneInput->setMaximum(65536);
  mSceneInput->setValue(scene);
  mSceneInput->setVisible(false);

  mMultiresolutionInput = new QComboBox();
  updateMultiresolutionInput();

  m_TimeSlider = new QIntSlider();
  m_TimeSlider->setSpinnerKeyboardTracking(true);
  int maxt = dims[0].sizeT();
  if (maxt > 1) {
    m_TimeSlider->setRange(0, maxt - 1);
  } else {
    m_TimeSlider->setRange(0, 0);
  }
  m_TimeSlider->setValue(0);
  m_TimeSlider->setSingleStep(1);
  m_TimeSlider->setTickPosition(QSlider::TickPosition::TicksBelow);
  m_TimeSlider->setTickInterval((maxt - 1) / 10);
  m_TimeSlider->setEnabled(maxt > 1);

  mChannelsSection = new Section("Channels", 0);
  mChannels = new QListWidget(this);
  populateChannels(0);
  auto chseclo = new QVBoxLayout();
  chseclo->setContentsMargins(0, 0, 0, 0);
  chseclo->setSpacing(0);
  chseclo->addWidget(mChannels);
  mChannelsSection->setContentLayout(*chseclo);
  QObject::connect(mChannelsSection, &Section::collapsed, [this]() { this->adjustSize(); });

  connect(mMultiresolutionInput, SIGNAL(currentIndexChanged(int)), this, SLOT(updateMultiresolutionLevel(int)));

  connect(mChannels, SIGNAL(itemChanged(QListWidgetItem*)), this, SLOT(updateChannels()));

  m_roiX = new RangeWidget(Qt::Horizontal);
  m_roiX->setStatusTip(tr("Region to load: X axis"));
  m_roiX->setToolTip(tr("Region to load: X axis"));
  m_roiX->setBounds(0, dims[mSelectedLevel].sizeX());
  m_roiX->setFirstValue(0);
  m_roiX->setSecondValue(m_roiX->maxBound());
  QObject::connect(m_roiX, &RangeWidget::minValueChanged, this, &LoadDialog::updateMemoryEstimate);
  QObject::connect(m_roiX, &RangeWidget::maxValueChanged, this, &LoadDialog::updateMemoryEstimate);
  m_roiY = new RangeWidget(Qt::Horizontal);
  m_roiY->setStatusTip(tr("Region to load: Y axis"));
  m_roiY->setToolTip(tr("Region to load: Y axis"));
  m_roiY->setBounds(0, dims[mSelectedLevel].sizeY());
  m_roiY->setFirstValue(0);
  m_roiY->setSecondValue(m_roiY->maxBound());
  QObject::connect(m_roiY, &RangeWidget::minValueChanged, this, &LoadDialog::updateMemoryEstimate);
  QObject::connect(m_roiY, &RangeWidget::maxValueChanged, this, &LoadDialog::updateMemoryEstimate);
  m_roiZ = new RangeWidget(Qt::Horizontal);
  m_roiZ->setStatusTip(tr("Region to load: Z axis"));
  m_roiZ->setToolTip(tr("Region to load: Z axis"));
  m_roiZ->setBounds(0, dims[mSelectedLevel].sizeZ());
  m_roiZ->setFirstValue(0);
  m_roiZ->setSecondValue(m_roiZ->maxBound());
  QObject::connect(m_roiZ, &RangeWidget::minValueChanged, this, &LoadDialog::updateMemoryEstimate);
  QObject::connect(m_roiZ, &RangeWidget::maxValueChanged, this, &LoadDialog::updateMemoryEstimate);

  auto* roiLayout = new QGridLayout();
  roiLayout->addWidget(new QLabel("X"), 0, 0);
  roiLayout->addWidget(new QLabel("Y"), 1, 0);
  roiLayout->addWidget(new QLabel("Z"), 2, 0);
  roiLayout->addWidget(m_roiX, 0, 1);
  roiLayout->addWidget(m_roiY, 1, 1);
  roiLayout->addWidget(m_roiZ, 2, 1);
  roiLayout->setColumnStretch(0, 1);
  roiLayout->setColumnStretch(1, 3);

  m_roiSection = new Section("Region of Interest", 0);
  m_roiSection->setContentLayout(*roiLayout);
  m_roiSection->setEnabled(m_reader->supportChunkedLoading());

  QObject::connect(m_roiSection, &Section::collapsed, [this]() { this->adjustSize(); });

  mVolumeLabel = new QLabel("XxYxZ pixels");
  mMemoryEstimateLabel = new QLabel("Memory Estimate: 0 MB");
  QFont font = mMemoryEstimateLabel->font();
  font.setPointSize(font.pointSize() * 1.5);
  mMemoryEstimateLabel->setTextFormat(Qt::RichText);
  mMemoryEstimateLabel->setFont(font);

  updateMultiresolutionLevel(mSelectedLevel);

  m_keepSettingsCheckbox = new QCheckBox(this);
  m_keepSettingsCheckbox->setChecked(true);

  m_prefetchWholeSeriesCheckbox = new QCheckBox(this);
  m_prefetchWholeSeriesCheckbox->setChecked(false);
  m_prefetchWholeSeriesCheckbox->setToolTip(
    tr("Load every time step into the cache in the background, rather than only a few ahead of the current one.\n"
       "Loading stops when the memory cache is full."));

  QFormLayout* layout = new QFormLayout(this);
  layout->setLabelAlignment(Qt::AlignLeft);
  layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  static const int spacing = 4;
  // layout->addRow("Scene", mSceneInput);
  if (mMultiresolutionInput->count() > 1) {
    layout->addRow("Resolution Level", mMultiresolutionInput);
    layout->addItem(new QSpacerItem(0, spacing, QSizePolicy::Expanding, QSizePolicy::Expanding));
  }
  if (m_TimeSlider->isEnabled()) {
    layout->addRow("Time", m_TimeSlider);
    // Only offer prefetch when the file actually has a series.
    layout->addRow("Prefetch time series", m_prefetchWholeSeriesCheckbox);
    layout->addItem(new QSpacerItem(0, spacing, QSizePolicy::Expanding, QSizePolicy::Expanding));
  }
  layout->addRow(mChannelsSection);
  layout->addItem(new QSpacerItem(0, spacing, QSizePolicy::Expanding, QSizePolicy::Expanding));
  if (m_roiSection->isEnabled()) {
    layout->addRow(m_roiSection);
    layout->addItem(new QSpacerItem(0, spacing, QSizePolicy::Expanding, QSizePolicy::Expanding));
  }
  QFrame* hline = new QFrame();
  hline->setFrameShape(QFrame::HLine);
  layout->addRow(hline);
  layout->addItem(new QSpacerItem(0, spacing, QSizePolicy::Expanding, QSizePolicy::Expanding));
  layout->addRow(mVolumeLabel);
  layout->addItem(new QSpacerItem(0, spacing, QSizePolicy::Expanding, QSizePolicy::Expanding));
  layout->addRow(mMemoryEstimateLabel);
  layout->addItem(new QSpacerItem(0, spacing, QSizePolicy::Expanding, QSizePolicy::Expanding));
  layout->addRow("Keep current AGAVE settings", m_keepSettingsCheckbox);
  layout->addItem(new QSpacerItem(0, spacing, QSizePolicy::Expanding, QSizePolicy::Expanding));
  layout->addRow(buttonBox);

  setLayout(layout);
}

LoadDialog::~LoadDialog()
{
  delete m_reader;
}

void
LoadDialog::updateScene(int value)
{
  mScene = value;
  mDims = m_reader->loadMultiscaleDims(mPath, mScene);
  updateMultiresolutionInput();
}

void
LoadDialog::updateChannels()
{
  std::vector<uint32_t> channels = getCheckedChannels();
  mChannelsSection->setTitle("Channels (" + QString::number(channels.size()) + "/" +
                             QString::number(mChannels->count()) + " selected)");
  updateMemoryEstimate();
}

std::vector<uint32_t>
LoadDialog::getCheckedChannels() const
{
  std::vector<uint32_t> channels;
  for (int i = 0; i < mChannels->count(); i++) {
    if (mChannels->item(i)->checkState() == Qt::Checked) {
      channels.push_back(i);
      // channels.push_back(mChannels->item(i)->data(Qt::UserRole).toUInt());
    }
  }
  return channels;
}

void
LoadDialog::updateMemoryEstimate()
{
  LoadSpec spec;
  spec.minx = m_roiX->minValue();
  spec.maxx = m_roiX->maxValue();
  spec.miny = m_roiY->minValue();
  spec.maxy = m_roiY->maxValue();
  spec.minz = m_roiZ->minValue();
  spec.maxz = m_roiZ->maxValue();
  spec.channels = getCheckedChannels();
  size_t mem = spec.getMemoryEstimate(mDims[mSelectedLevel].sizeC());
  std::string label = LoadSpec::bytesToStringLabel(mem);

  mVolumeLabel->setText(QString::number(spec.maxx - spec.minx) + " x " + QString::number(spec.maxy - spec.miny) +
                        " x " + QString::number(spec.maxz - spec.minz) + " pixels");

  mMemoryEstimateLabel->setText("Memory Estimate: <b>" + QString::fromStdString(label) + "</b>");
}

void
LoadDialog::updateMultiresolutionLevel(int level)
{
  mSelectedLevel = level;
  MultiscaleDims d = mDims[level];

  // update the time slider

  int t = m_TimeSlider->value();
  // maintain a t value at same percentage of total.
  float pct = (float)t / (float)m_TimeSlider->maximum();
  int maxt = d.sizeT();
  m_TimeSlider->setRange(0, maxt - 1);
  m_TimeSlider->setValue(pct * (maxt - 1));
  m_TimeSlider->setEnabled(maxt > 1);

  // update the channels
  populateChannels(level);
  updateChannels();

  // update the xyz sliders

  float pct0x = m_roiX->minValuePercent();
  float pct1x = m_roiX->maxValuePercent();
  float pct0y = m_roiY->minValuePercent();
  float pct1y = m_roiY->maxValuePercent();
  float pct0z = m_roiZ->minValuePercent();
  float pct1z = m_roiZ->maxValuePercent();
  m_roiX->setBounds(0, d.sizeX());
  m_roiX->setFirstValue(0 + pct0x * m_roiX->boundsRange());
  m_roiX->setSecondValue(0 + pct1x * m_roiX->boundsRange());
  m_roiY->setBounds(0, d.sizeY());
  m_roiY->setFirstValue(0 + pct0y * m_roiY->boundsRange());
  m_roiY->setSecondValue(0 + pct1y * m_roiY->boundsRange());
  m_roiZ->setBounds(0, d.sizeZ());
  m_roiZ->setFirstValue(0 + pct0z * m_roiZ->boundsRange());
  m_roiZ->setSecondValue(0 + pct1z * m_roiZ->boundsRange());

  updateMemoryEstimate();
}

LoadSpec
LoadDialog::getLoadSpec() const
{
  LoadSpec spec;
  spec.filepath = mPath;
  spec.scene = mScene;
  spec.time = m_TimeSlider->value();
  spec.channels = getCheckedChannels();

  spec.maxx = m_roiX->maxValue();
  spec.minx = m_roiX->minValue();
  spec.maxy = m_roiY->maxValue();
  spec.miny = m_roiY->minValue();
  spec.maxz = m_roiZ->maxValue();
  spec.minz = m_roiZ->minValue();

  spec.subpath = mDims[mSelectedLevel].path;

  return spec;
}

void
LoadDialog::updateMultiresolutionInput()
{
  mMultiresolutionInput->clear();
  for (auto d : mDims) {
    LoadSpec spec;
    spec.maxx = d.sizeX();
    spec.maxy = d.sizeY();
    spec.maxz = d.sizeZ();
    size_t mem = spec.getMemoryEstimate(d.sizeC());
    std::string label = LoadSpec::bytesToStringLabel(mem);

    mMultiresolutionInput->addItem(QString::fromStdString(d.path + " (" + label + " max.)"));
  }
  mMultiresolutionInput->setEnabled(mDims.size() > 1);
}

void
LoadDialog::accept()
{
  // validate inputs.
  std::vector<uint32_t> channels = getCheckedChannels();
  if (channels.empty()) {
    QMessageBox::warning(this, "No Channels Selected", "Please select at least one channel.");
    return;
  }

  QDialog::accept();
}

void
LoadDialog::populateChannels(int level)
{
  int nch = mDims[level].sizeC();
  int oldnch = mChannels->count();

  std::vector<uint32_t> channels = getCheckedChannels();

  mChannels->clear();
  for (int i = 0; i < nch; ++i) {
    std::string channelName = "Ch " + std::to_string(i);
    if (mDims[level].channelNames.size() > i) {
      channelName = mDims[level].channelNames[i];
    }
    QListWidgetItem* listItem = new QListWidgetItem(QString::fromStdString(channelName), mChannels);

    // if we are within the previous number of channels, then we can use the previous selection.
    if (i < oldnch) {
      if (std::find(channels.begin(), channels.end(), i) != channels.end()) {
        listItem->setCheckState(Qt::Checked);
      } else {
        listItem->setCheckState(Qt::Unchecked);
      }
    } else {
      // if we are at a greater value then include channel by default.
      listItem->setCheckState(Qt::Checked);
    }

    listItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    listItem->setData(Qt::UserRole, QVariant::fromValue(i));
    mChannels->addItem(listItem);
  }
}

bool
LoadDialog::getPrefetchWholeTimeSeries() const
{
  // The checkbox is only added to the layout when the file has a time series, so
  // gate on the same condition rather than returning a stale default.
  if (!m_prefetchWholeSeriesCheckbox || !m_TimeSlider || !m_TimeSlider->isEnabled()) {
    return false;
  }
  return m_prefetchWholeSeriesCheckbox->isChecked();
}
