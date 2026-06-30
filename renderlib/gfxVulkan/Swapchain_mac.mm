#define VK_USE_PLATFORM_METAL_EXT

#include "Swapchain.h"

#if AGAVE_HAS_VULKAN && defined(__APPLE__)

#include "Logging.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

namespace gfxvulkan {

bool
Swapchain::createNativeSurface()
{
  if (!m_backend || !m_surface) {
    return false;
  }

  NSView* view = reinterpret_cast<NSView*>(m_surface->nativeHandle());
  if (!view) {
    LOG_ERROR << "Unable to get an NSView for the Vulkan window";
    return false;
  }

  [view setWantsLayer:YES];

  CAMetalLayer* metalLayer = nil;
  CALayer* existingLayer = [view layer];
  if ([existingLayer isKindOfClass:[CAMetalLayer class]]) {
    metalLayer = static_cast<CAMetalLayer*>(existingLayer);
  } else {
    metalLayer = [CAMetalLayer layer];
    [view setLayer:metalLayer];
  }

  metalLayer.contentsScale = m_surface->contentScale();
  metalLayer.frame = [view bounds];
  metalLayer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;

  auto createMetalSurface =
    reinterpret_cast<PFN_vkCreateMetalSurfaceEXT>(vkGetInstanceProcAddr(m_backend->instance(),
                                                                        "vkCreateMetalSurfaceEXT"));
  if (!createMetalSurface) {
    LOG_ERROR << "vkCreateMetalSurfaceEXT is not available on the current Vulkan instance";
    return false;
  }

  VkMetalSurfaceCreateInfoEXT createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
  createInfo.pLayer = metalLayer;

  VkResult result = createMetalSurface(m_backend->instance(), &createInfo, nullptr, &m_vkSurface);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateMetalSurfaceEXT failed with VkResult " << result;
    m_vkSurface = VK_NULL_HANDLE;
    return false;
  }

  return true;
}

} // namespace gfxvulkan

#endif // AGAVE_HAS_VULKAN && defined(__APPLE__)
