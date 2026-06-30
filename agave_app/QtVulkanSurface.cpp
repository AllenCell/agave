#include "QtVulkanSurface.h"

#if AGAVE_HAS_VULKAN

#include <QSize>
#include <QWidget>

#include <algorithm>
#include <cmath>

QtVulkanSurface::QtVulkanSurface(QWidget* widget)
  : m_widget(widget)
{
}

void*
QtVulkanSurface::nativeHandle() const
{
  if (!m_widget) {
    return nullptr;
  }
  return reinterpret_cast<void*>(m_widget->winId());
}

bool
QtVulkanSurface::isExposed() const
{
  return m_widget && m_widget->isVisible();
}

void
QtVulkanSurface::pixelSize(uint32_t& width, uint32_t& height) const
{
  const QSize size = m_widget ? m_widget->size() : QSize();
  const double scale = m_widget ? m_widget->devicePixelRatioF() : 1.0;
  width = static_cast<uint32_t>(std::max(1.0, std::round(static_cast<double>(size.width()) * scale)));
  height = static_cast<uint32_t>(std::max(1.0, std::round(static_cast<double>(size.height()) * scale)));
}

double
QtVulkanSurface::contentScale() const
{
  return m_widget ? m_widget->devicePixelRatioF() : 1.0;
}

#endif // AGAVE_HAS_VULKAN
