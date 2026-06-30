#pragma once

#if AGAVE_HAS_VULKAN

#include "Backend.h"
#include "Framebuffer.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

class ViewerWindow;

namespace gfxvulkan {

// Provides the platform-native window handle and live surface geometry that the
// Vulkan swapchain needs. Implemented by the application/windowing layer (for
// example a Qt QWindow wrapper in agave_app) so that gfxVulkan stays free of any
// windowing-toolkit dependency.
class ISwapchainSurface
{
public:
  virtual ~ISwapchainSurface() = default;

  // Platform-native window handle used to create the VkSurfaceKHR.
  //   macOS:   NSView*
  //   Windows: HWND
  //   X11:     xcb_window_t (as a pointer-sized value)
  virtual void* nativeHandle() const = 0;

  // True when the surface is visible and can be rendered to.
  virtual bool isExposed() const = 0;

  // Size of the surface in physical pixels (logical size times content scale).
  virtual void pixelSize(uint32_t& width, uint32_t& height) const = 0;

  // Ratio of physical pixels to logical points (e.g. 2.0 on a Retina display).
  virtual double contentScale() const = 0;
};

// Vulkan swapchain bound to a native window surface. All Vulkan and platform
// surface code lives here; the only window-system dependency is the abstract
// ISwapchainSurface supplied by the caller.
class Swapchain
{
public:
  explicit Swapchain(ISwapchainSurface* surface);
  ~Swapchain();

  bool render(ViewerWindow& viewerWindow);
  void requestRecreate() { m_needsRecreate = true; }

private:
  bool createNativeSurface();
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

  ISwapchainSurface* m_surface = nullptr;
  Backend* m_backend = nullptr;

  VkSurfaceKHR m_vkSurface = VK_NULL_HANDLE;
  VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
  VkFence m_acquireFence = VK_NULL_HANDLE;
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
