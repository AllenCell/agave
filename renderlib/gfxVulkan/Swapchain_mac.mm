#define VK_USE_PLATFORM_METAL_EXT

#include "NativeSurface.h"
#include "Swapchain.h"

#if AGAVE_HAS_VULKAN && defined(__APPLE__)

#include "Logging.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

namespace gfxvulkan {

bool
Swapchain::createNativeSurface()
{
  if (!m_backend) {
    return false;
  }

  m_vkSurface = createNativeWindowSurface(m_backend->instance(), m_surface);
  return m_vkSurface != VK_NULL_HANDLE;
}

VkSurfaceKHR
createNativeWindowSurface(VkInstance instance, gfxApi::IWindowSurface* surface)
{
  if (instance == VK_NULL_HANDLE || !surface) {
    return VK_NULL_HANDLE;
  }

  NSView* view = reinterpret_cast<NSView*>(surface->nativeHandle());
  if (!view) {
    LOG_ERROR << "Unable to get an NSView for the Vulkan window";
    return VK_NULL_HANDLE;
  }

  // Note on Qt integration: standalone Vulkan apps (e.g. GLFW) usually own the
  // whole NSWindow and install the CAMetalLayer as the view's backing layer
  // via [view setLayer:] + [view setWantsLayer:YES]. That switches the view
  // into "layer-hosting" mode, where AppKit no longer manages the layer's
  // position or size.
  //
  // In AGAVE the Vulkan surface is hosted by a QWidget that is one node in a
  // Qt-managed view hierarchy. Making the view layer-hosting breaks Qt's
  // layout: AppKit stops repositioning the layer to match the widget's frame,
  // so the metal surface anchors at the parent view's origin instead of the
  // widget's actual (x, y) position. To keep Qt's layout intact we leave the
  // view in the default layer-backed mode (AppKit keeps its auto-managed
  // backing layer positioned correctly) and attach the CAMetalLayer as a
  // sublayer with a resizing mask so it tracks the backing layer's bounds.
  [view setWantsLayer:YES];

  CAMetalLayer* metalLayer = nil;
  for (CALayer* sub in [[view layer] sublayers]) {
    if ([sub isKindOfClass:[CAMetalLayer class]]) {
      metalLayer = static_cast<CAMetalLayer*>(sub);
      break;
    }
  }
  if (!metalLayer) {
    metalLayer = [CAMetalLayer layer];
    metalLayer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
    [[view layer] addSublayer:metalLayer];
  }

  metalLayer.contentsScale = surface->contentScale();
  metalLayer.frame = [view bounds];

  auto createMetalSurface = reinterpret_cast<PFN_vkCreateMetalSurfaceEXT>(
    vkGetInstanceProcAddr(instance, "vkCreateMetalSurfaceEXT"));
  if (!createMetalSurface) {
    LOG_ERROR << "vkCreateMetalSurfaceEXT is not available on the current "
                 "Vulkan instance";
    return VK_NULL_HANDLE;
  }

  VkMetalSurfaceCreateInfoEXT createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
  createInfo.pLayer = metalLayer;

  VkSurfaceKHR vkSurface = VK_NULL_HANDLE;
  VkResult result = createMetalSurface(instance, &createInfo, nullptr, &vkSurface);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateMetalSurfaceEXT failed with VkResult " << result;
    return VK_NULL_HANDLE;
  }

  return vkSurface;
}

void
Swapchain::updateNativeSurfaceLayout()
{
  if (!m_surface) {
    return;
  }
  NSView* view = reinterpret_cast<NSView*>(m_surface->nativeHandle());
  if (!view) {
    return;
  }
  CAMetalLayer* metalLayer = nil;
  for (CALayer* sub in [[view layer] sublayers]) {
    if ([sub isKindOfClass:[CAMetalLayer class]]) {
      metalLayer = static_cast<CAMetalLayer*>(sub);
      break;
    }
  }
  if (!metalLayer) {
    return;
  }
  metalLayer.contentsScale = m_surface->contentScale();
  metalLayer.frame = [view bounds];
}

} // namespace gfxvulkan

#endif // AGAVE_HAS_VULKAN && defined(__APPLE__)
