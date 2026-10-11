#include "CacheSettingsWidget.h"

#include "renderlib/CacheManager.h"

#include <QFormLayout>

CacheSettingsWidget::CacheSettingsWidget(QWidget* parent)
  : QWidget(parent)
{
  auto* layout = new QFormLayout(this);

  m_enableCache = new QCheckBox(tr("Enable cache"), this);
  m_enableDisk = new QCheckBox(tr("Enable disk cache"), this);

  m_ramLimitMB = new QSpinBox(this);
  m_ramLimitMB->setRange(0, 1024 * 1024);
  m_ramLimitMB->setSuffix(tr(" MB"));
  m_ramLimitMB->setSingleStep(256);

  m_diskLimitGB = new QSpinBox(this);
  m_diskLimitGB->setRange(0, 1024 * 1024);
  m_diskLimitGB->setSuffix(tr(" GB"));
  m_diskLimitGB->setSingleStep(10);

  m_cacheDirLabel = new QLabel(this);
  // The cache directory is fixed (registered at startup); display it read-only.
  m_cacheDirLabel->setText(QString::fromStdString(CacheManager::instance().getCacheDirectory()));
  m_cacheDirLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_cacheDirLabel->setWordWrap(true);

  m_applyButton = new QPushButton(tr("Apply"), this);
  m_clearDiskButton = new QPushButton(tr("Clear disk cache"), this);

  layout->addRow(m_enableCache);
  layout->addRow(m_enableDisk);
  layout->addRow(tr("RAM limit"), m_ramLimitMB);
  layout->addRow(tr("Disk limit"), m_diskLimitGB);
  layout->addRow(tr("Cache directory"), m_cacheDirLabel);
  layout->addRow(QString(), m_applyButton);
  layout->addRow(QString(), m_clearDiskButton);
  setLayout(layout);
}

void
CacheSettingsWidget::setSettings(const CacheSettingsData& data)
{
  m_enableCache->setChecked(data.enabled);
  m_enableDisk->setChecked(data.enableDisk);
  m_ramLimitMB->setValue(static_cast<int>(data.maxRamBytes / (1024ULL * 1024ULL)));
  m_diskLimitGB->setValue(static_cast<int>(data.maxDiskBytes / (1024ULL * 1024ULL * 1024ULL)));
}

CacheSettingsData
CacheSettingsWidget::getSettings() const
{
  CacheSettingsData data;
  data.enabled = m_enableCache->isChecked();
  data.enableDisk = m_enableDisk->isChecked();
  data.maxRamBytes = static_cast<std::uint64_t>(m_ramLimitMB->value()) * 1024ULL * 1024ULL;
  data.maxDiskBytes = static_cast<std::uint64_t>(m_diskLimitGB->value()) * 1024ULL * 1024ULL * 1024ULL;
  return data;
}
