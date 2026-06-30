#pragma once

#if AGAVE_HAS_VULKAN

#include "renderlib/gfxVulkan/Swapchain.h"

#include <cstdint>

class QWidget;

// Qt-backed implementation of the windowing-layer contract that the Vulkan
// swapchain depends on. This is the single place where the swapchain's window
// dependencies meet Qt; gfxVulkan itself never includes any Qt headers.
//
// The surface wraps a native QWidget (one with Qt::WA_NativeWindow set), so the
// Vulkan surface is created from the widget's own NSView/HWND. Letting the
// widget own the native surface keeps it laid out by Qt exactly like any other
// widget, avoiding the geometry offsets that come with embedding a separate
// QWindow via QWidget::createWindowContainer.
class QtVulkanSurface : public gfxvulkan::ISwapchainSurface
{
public:
  explicit QtVulkanSurface(QWidget* widget);

  void* nativeHandle() const override;
  bool isExposed() const override;
  void pixelSize(uint32_t& width, uint32_t& height) const override;
  double contentScale() const override;

private:
  QWidget* m_widget = nullptr;
};

#endif // AGAVE_HAS_VULKAN
