#pragma once

#include <QDockWidget>

#include "CacheSettingsWidget.h"

class CacheSettingsDockWidget : public QDockWidget
{
  Q_OBJECT

public:
  explicit CacheSettingsDockWidget(QWidget* parent = nullptr);

  CacheSettingsWidget* widget() { return &m_settingsWidget; }

private:
  CacheSettingsWidget m_settingsWidget;
};
