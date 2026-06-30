#pragma once

#if AGAVE_HAS_VULKAN

#include "renderlib/gfxVulkan/Swapchain.h"

#include <cstdint>

class QWindow;

// Qt-backed implementation of the windowing-layer contract that the Vulkan
// swapchain depends on. This is the single place where the swapchain's window
// dependencies meet Qt; gfxVulkan itself never includes any Qt headers.
class QtVulkanSurface : public gfxvulkan::ISwapchainSurface
{
public:
  explicit QtVulkanSurface(QWindow* window);

  void* nativeHandle() const override;
  bool isExposed() const override;
  void pixelSize(uint32_t& width, uint32_t& height) const override;
  double contentScale() const override;

private:
  QWindow* m_window = nullptr;
};

#endif // AGAVE_HAS_VULKAN
