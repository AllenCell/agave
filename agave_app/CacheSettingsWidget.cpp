#include "CacheSettingsWidget.h"

#include "renderlib/CacheManager.h"

#include <QFormLayout>

CacheSettingsWidget::CacheSettingsWidget(QWidget* parent, AgaveSettingsData* settings)
  : QWidget(parent)
  , m_settings(settings)
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

  m_prefetchEnabled = new QCheckBox(tr("Prefetch time steps"), this);
  m_prefetchEnabled->setToolTip(
    tr("Fill memory and disk with time steps ahead of the current one, in the background. How far ahead is bounded by "
       "the RAM and disk limits above."));
  m_prefetchEnabled->setStatusTip(
    tr("Fill memory and disk with time steps ahead of the current one, in the background"));

  connect(m_enableCache, &QCheckBox::toggled, this, [this](bool) { writeToSettings(); });
  connect(m_enableDisk, &QCheckBox::toggled, this, [this](bool) { writeToSettings(); });
  connect(m_ramLimitMB, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { writeToSettings(); });
  connect(m_diskLimitGB, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { writeToSettings(); });
  connect(m_prefetchEnabled, &QCheckBox::toggled, this, [this](bool) { writeToSettings(); });

  layout->addRow(m_enableCache);
  layout->addRow(m_enableDisk);
  layout->addRow(tr("RAM limit"), m_ramLimitMB);
  layout->addRow(tr("Disk limit"), m_diskLimitGB);
  layout->addRow(tr("Cache directory"), m_cacheDirLabel);
  layout->addRow(new QLabel(tr("<b>Prefetch</b>"), this));
  layout->addRow(m_prefetchEnabled);
  layout->addRow(QString(), m_applyButton);
  layout->addRow(QString(), m_clearDiskButton);
  setLayout(layout);

  // Populate the fields from the already-loaded settings; otherwise the spin
  // boxes show their default 0 until something else triggers a refresh.
  updateUiFromSettings();
}

void
CacheSettingsWidget::updateUiFromSettings()
{
  if (!m_settings) {
    return;
  }
  m_refreshingSettings = true;
  m_enableCache->setChecked(m_settings->cache.enabled);
  m_enableDisk->setChecked(m_settings->cache.enableDisk);
  m_ramLimitMB->setValue(static_cast<int>(m_settings->cache.maxRamBytes / (1024ULL * 1024ULL)));
  m_diskLimitGB->setValue(static_cast<int>(m_settings->cache.maxDiskBytes / (1024ULL * 1024ULL * 1024ULL)));
  m_prefetchEnabled->setChecked(m_settings->timeSeries.prefetchEnabled);
  m_refreshingSettings = false;
}

void
CacheSettingsWidget::writeToSettings()
{
  if (!m_settings || m_refreshingSettings) {
    return;
  }
  m_settings->cache.enabled = m_enableCache->isChecked();
  m_settings->cache.enableDisk = m_enableDisk->isChecked();
  m_settings->cache.maxRamBytes = static_cast<std::uint64_t>(m_ramLimitMB->value()) * 1024ULL * 1024ULL;
  m_settings->cache.maxDiskBytes = static_cast<std::uint64_t>(m_diskLimitGB->value()) * 1024ULL * 1024ULL * 1024ULL;
  m_settings->timeSeries.prefetchEnabled = m_prefetchEnabled->isChecked();
}
