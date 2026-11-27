#include "CacheSettingsDockWidget.h"

CacheSettingsDockWidget::CacheSettingsDockWidget(QWidget* parent, AgaveSettingsData* settings)
  : QDockWidget(parent)
  , m_settingsWidget(this, settings)
{
  setWindowTitle(tr("Advanced Cache Settings"));
  setWidget(&m_settingsWidget);
}
