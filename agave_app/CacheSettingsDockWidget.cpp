#include "CacheSettingsDockWidget.h"

CacheSettingsDockWidget::CacheSettingsDockWidget(QWidget* parent)
  : QDockWidget(parent)
{
  setWindowTitle(tr("Advanced Cache Settings"));
  setWidget(&m_settingsWidget);
}
