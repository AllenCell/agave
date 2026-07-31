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

  m_prefetchEnabled = new QCheckBox(tr("Prefetch time steps"), this);
  m_prefetchEnabled->setToolTip(tr("Load time steps ahead of the current one in the background"));
  m_prefetchEnabled->setStatusTip(tr("Load time steps ahead of the current one in the background"));

  m_prefetchDepth = new QSpinBox(this);
  m_prefetchDepth->setRange(1, 1000);
  m_prefetchDepth->setSuffix(tr(" steps"));
  m_prefetchDepth->setToolTip(tr("How many time steps ahead to keep loaded"));
  m_prefetchDepth->setStatusTip(tr("How many time steps ahead to keep loaded"));

  m_prefetchFillCache = new QCheckBox(tr("Fill available cache"), this);
  m_prefetchFillCache->setToolTip(tr("Keep loading forward until the cache is full, ignoring the step count"));
  m_prefetchFillCache->setStatusTip(tr("Keep loading forward until the cache is full, ignoring the step count"));

  m_showDetailedCacheStatus = new QCheckBox(tr("Detailed cache status (debug)"), this);
  m_showDetailedCacheStatus->setToolTip(
    tr("Show queued, loading and failed time steps on the time slider, not just cached ones"));
  m_showDetailedCacheStatus->setStatusTip(
    tr("Show queued, loading and failed time steps on the time slider, not just cached ones"));

  layout->addRow(m_enableCache);
  layout->addRow(m_enableDisk);
  layout->addRow(tr("RAM limit"), m_ramLimitMB);
  layout->addRow(tr("Disk limit"), m_diskLimitGB);
  layout->addRow(tr("Cache directory"), m_cacheDirLabel);
  layout->addRow(new QLabel(tr("<b>Prefetch</b>"), this));
  layout->addRow(m_prefetchEnabled);
  layout->addRow(tr("Prefetch depth"), m_prefetchDepth);
  layout->addRow(m_prefetchFillCache);
  layout->addRow(m_showDetailedCacheStatus);
  layout->addRow(QString(), m_applyButton);
  layout->addRow(QString(), m_clearDiskButton);
  setLayout(layout);

  // Depth is meaningless while "fill available cache" is on, and the whole group
  // is meaningless with prefetch off. Keep the enabled states honest so the UI
  // does not imply a setting is in effect when it is not.
  auto updateEnabledStates = [this]() {
    const bool prefetching = m_prefetchEnabled->isChecked();
    m_prefetchFillCache->setEnabled(prefetching);
    m_prefetchDepth->setEnabled(prefetching && !m_prefetchFillCache->isChecked());
  };
  connect(m_prefetchEnabled, &QCheckBox::toggled, this, [updateEnabledStates](bool) { updateEnabledStates(); });
  connect(m_prefetchFillCache, &QCheckBox::toggled, this, [updateEnabledStates](bool) { updateEnabledStates(); });
  updateEnabledStates();
}

void
CacheSettingsWidget::setSettings(const CacheSettingsData& data)
{
  m_lastSet = data;
  m_enableCache->setChecked(data.enabled);
  m_enableDisk->setChecked(data.enableDisk);
  m_ramLimitMB->setValue(static_cast<int>(data.maxRamBytes / (1024ULL * 1024ULL)));
  m_diskLimitGB->setValue(static_cast<int>(data.maxDiskBytes / (1024ULL * 1024ULL * 1024ULL)));
  m_prefetchEnabled->setChecked(data.prefetchEnabled);
  m_prefetchDepth->setValue(static_cast<int>(data.prefetchDepth));
  m_prefetchFillCache->setChecked(data.prefetchFillCache);
  m_showDetailedCacheStatus->setChecked(data.showDetailedCacheStatus);
}

CacheSettingsData
CacheSettingsWidget::getSettings() const
{
  // Start from what we were given so unpresented fields are preserved.
  CacheSettingsData data = m_lastSet;
  data.enabled = m_enableCache->isChecked();
  data.enableDisk = m_enableDisk->isChecked();
  data.maxRamBytes = static_cast<std::uint64_t>(m_ramLimitMB->value()) * 1024ULL * 1024ULL;
  data.maxDiskBytes = static_cast<std::uint64_t>(m_diskLimitGB->value()) * 1024ULL * 1024ULL * 1024ULL;
  data.prefetchEnabled = m_prefetchEnabled->isChecked();
  data.prefetchDepth = static_cast<std::uint32_t>(m_prefetchDepth->value());
  data.prefetchFillCache = m_prefetchFillCache->isChecked();
  data.showDetailedCacheStatus = m_showDetailedCacheStatus->isChecked();
  return data;
}
