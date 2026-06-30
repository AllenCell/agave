#include "QtVulkanSurface.h"

#if AGAVE_HAS_VULKAN

#include <QSize>
#include <QWindow>

#include <algorithm>
#include <cmath>

QtVulkanSurface::QtVulkanSurface(QWindow* window)
  : m_window(window)
{
}

void*
QtVulkanSurface::nativeHandle() const
{
  if (!m_window) {
    return nullptr;
  }
  return reinterpret_cast<void*>(m_window->winId());
}

bool
QtVulkanSurface::isExposed() const
{
  return m_window && m_window->isExposed();
}

void
QtVulkanSurface::pixelSize(uint32_t& width, uint32_t& height) const
{
  const QSize size = m_window ? m_window->size() : QSize();
  const double scale = m_window ? m_window->devicePixelRatio() : 1.0;
  width = static_cast<uint32_t>(std::max(1.0, std::round(static_cast<double>(size.width()) * scale)));
  height = static_cast<uint32_t>(std::max(1.0, std::round(static_cast<double>(size.height()) * scale)));
}

double
QtVulkanSurface::contentScale() const
{
  return m_window ? m_window->devicePixelRatio() : 1.0;
}

#endif // AGAVE_HAS_VULKAN
