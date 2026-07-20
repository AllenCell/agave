#include "QtVulkanSurface.h"

#if AGAVE_HAS_VULKAN

#include <QGuiApplication>
#include <QSize>
#include <QWidget>

#if defined(__linux__)
// For X11 / XCB platforms:
#include <QtGui/qnativeinterface_x11.h>
#endif

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

void*
QtVulkanSurface::nativeDisplay() const
{
#if defined(__linux__)
  // Hand the Vulkan swapchain the same xcb connection Qt is already using for
  // this window. Sharing the connection avoids driver quirks that can arise
  // when presentation happens on a different xcb_connection_t than the one
  // that owns the window (some Mesa drivers keep per-connection present
  // state).
  if (auto* x11App = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
    return x11App->connection(); // returns xcb_connection_t*
  }
  return nullptr;
#else
  return nullptr;
#endif
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
