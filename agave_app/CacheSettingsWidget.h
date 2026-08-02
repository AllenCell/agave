#pragma once

#include "AgaveSettings.h"

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QWidget>

class CacheSettingsWidget : public QWidget
{
  Q_OBJECT

public:
  explicit CacheSettingsWidget(QWidget* parent = nullptr);

  void setSettingsData(AgaveSettingsData* settings);
  void refreshFromSettings();
  void writeToSettings();

  QPushButton* applyButton() const { return m_applyButton; }
  QPushButton* clearDiskButton() const { return m_clearDiskButton; }

private:
  QCheckBox* m_enableCache = nullptr;
  QCheckBox* m_enableDisk = nullptr;
  QSpinBox* m_ramLimitMB = nullptr;
  QSpinBox* m_diskLimitGB = nullptr;
  QLabel* m_cacheDirLabel = nullptr;
  QPushButton* m_applyButton = nullptr;
  QPushButton* m_clearDiskButton = nullptr;

  QCheckBox* m_prefetchEnabled = nullptr;

  AgaveSettingsData* m_settings = nullptr;
};
