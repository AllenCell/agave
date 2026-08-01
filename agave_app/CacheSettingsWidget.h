#pragma once

#include "CacheSettings.h"

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

  void setSettings(const CacheSettingsData& data);
  CacheSettingsData getSettings() const;

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
  QCheckBox* m_showDetailedCacheStatus = nullptr;

  // Last value passed to setSettings. getSettings() starts from this so fields
  // this widget does not present -- the playback settings, which live on the
  // timeline dock -- survive a round trip instead of being reset to defaults.
  CacheSettingsData m_lastSet;
};
