#pragma once

#include "Controls.h"
#include "Section.h"

#include "renderlib/VolumeDimensions.h"
#include "renderlib/io/FileReader.h"

#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QLineEdit>
#include <QListView>
#include <QStandardItemModel>
#include <QStyledItemDelegate>

class QIntSlider;
class QLabel;
class QListWidget;
class QSpinBox;
class QTreeWidget;

class IFileReader;
class RangeWidget;
class Section;

class LoadDialog : public QDialog
{
  Q_OBJECT

public:
  LoadDialog(std::string path, const std::vector<MultiscaleDims>& dims, uint32_t scene, QWidget* parent = Q_NULLPTR);
  ~LoadDialog() override;

  LoadSpec getLoadSpec() const;
  int getMultiscaleLevelIndex() const { return mSelectedLevel; }
  bool getKeepSettings() const { return m_keepSettingsCheckbox->isChecked(); }
  // "Prefetch whole time series": warm every time step in the background rather
  // than only a window ahead of the playhead. Only meaningful when the file has
  // more than one time step; false when the checkbox was not shown.
  bool getPrefetchWholeTimeSeries() const;

  QSize sizeHint() const override { return QSize(400, 100); }

private slots:
  void updateScene(int value);
  void updateMultiresolutionLevel(int level);
  void updateChannels();

  void accept() override;

private:
  std::string mPath;
  int mScene;
  int mTime;
  std::vector<MultiscaleDims> mDims;
  int mSelectedLevel;

  QSpinBox* mSceneInput;
  // show multiresolutions
  QComboBox* mMultiresolutionInput;
  // start with a single timepoint
  QIntSlider* m_TimeSlider;
  // select any set of channels
  QListWidget* mChannels;
  Section* mChannelsSection;
  QTreeWidget* mMetadataTree;
  QLabel* mVolumeLabel;
  QLabel* mMemoryEstimateLabel;
  // select region of interest in zyx
  RangeWidget* m_roiX;
  RangeWidget* m_roiY;
  RangeWidget* m_roiZ;
  Section* m_roiSection;
  QCheckBox* m_keepSettingsCheckbox;
  QCheckBox* m_prefetchWholeSeriesCheckbox = nullptr;

  void updateMemoryEstimate();
  void updateMultiresolutionInput();
  std::vector<uint32_t> getCheckedChannels() const;
  void populateChannels(int level);

  IFileReader* m_reader;
};
