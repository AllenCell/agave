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

  // Use AppKit's managed backing layer (which is positioned correctly within
  // the parent for an embedded, laid-out subview) and add our CAMetalLayer as a
  // sublayer. Replacing the backing layer via setLayer: (layer-hosting) and
  // setting its frame to the view bounds positioned the surface at the parent
  // origin instead of the view's real frame, offsetting the whole surface by the
  // view's y position.
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

  metalLayer.contentsScale = m_surface->contentScale();
  metalLayer.frame = [view bounds];

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
