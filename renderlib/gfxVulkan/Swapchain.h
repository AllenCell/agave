#pragma once

#if AGAVE_HAS_VULKAN

#include "Backend.h"
#include "Framebuffer.h"
#include "gfxapi/WindowSurface.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

class ViewerWindow;

namespace gfxvulkan {

// Vulkan swapchain bound to a native window surface. All Vulkan and platform
// surface code lives here; the only window-system dependency is the abstract
// IWindowSurface supplied by the caller.
class Swapchain
{
public:
  explicit Swapchain(gfxApi::IWindowSurface* surface);
  ~Swapchain();

  bool render(ViewerWindow& viewerWindow);
  void requestRecreate() { m_needsRecreate = true; }

private:
  bool createNativeSurface();
  // Keep the platform surface (e.g. the macOS CAMetalLayer) aligned with the
  // window's current geometry. No-op on platforms that don't need it.
  void updateNativeSurfaceLayout();
  bool ensureSurface();
  bool ensureSwapchain();
  bool recreateSwapchain();
  bool acquireNextImage(uint32_t& imageIndex);
  bool present(uint32_t imageIndex);
  void destroySwapchain();
  void destroySurface();

  VkExtent2D requestedExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
  VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
  VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& presentModes) const;
  VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(VkCompositeAlphaFlagsKHR supportedCompositeAlpha) const;

  gfxApi::IWindowSurface* m_surface = nullptr;
  Backend* m_backend = nullptr;

  VkSurfaceKHR m_vkSurface = VK_NULL_HANDLE;
  resources::UniqueSwapchain m_swapchain;
  resources::UniqueFence m_acquireFence;
  VkFormat m_colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
  VkColorSpaceKHR m_colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  VkExtent2D m_extent = {};

  std::vector<VkImage> m_images;
  std::vector<std::unique_ptr<Framebuffer>> m_framebuffers;

  bool m_needsRecreate = true;
  bool m_presentSupported = false;
};

} // namespace gfxvulkan

#endif // AGAVE_HAS_VULKAN
