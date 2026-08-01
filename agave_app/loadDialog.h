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

  // "Prefetch time series": fill memory and disk with time steps in the
  // background. How far ahead is bounded by the RAM and disk cache limits, not by
  // this choice -- it only turns prefetching on or off.
  //
  // Seed this from the persisted setting before exec(), so the box shows the
  // current state instead of always starting unchecked. The caller then applies
  // getPrefetchTimeSeries() in BOTH directions, which is only safe because the box
  // round-trips the setting rather than defaulting over the top of it.
  void setPrefetchTimeSeries(bool enabled);
  bool getPrefetchTimeSeries() const;

  // Whether the prefetch choice was actually offered, i.e. the file has more than
  // one time step. Callers must check this before applying getPrefetchTimeSeries():
  // a single-timepoint load would otherwise persist "prefetch off" and silently
  // disable it for the next series.
  bool hasTimeSeriesChoice() const;

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
