#include "Swapchain.h"

#if AGAVE_HAS_VULKAN

#include "Logging.h"
#include "ViewerWindow.h"
#include "renderlib.h"
#include "gfxapi/Backend.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace gfxvulkan {

namespace {

bool
isResizeResult(VkResult result)
{
  return result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR;
}

} // namespace

Swapchain::Swapchain(ISwapchainSurface* surface)
  : m_surface(surface)
{
  gfxApi::Backend* backend = renderlib::graphicsBackend();
  if (!backend || backend->kind() != gfxApi::BackendKind::Vulkan) {
    LOG_ERROR << "Cannot create Vulkan swapchain without an active Vulkan backend";
    return;
  }

  m_backend = static_cast<Backend*>(backend);
  if (!m_backend->isValid()) {
    LOG_ERROR << "Cannot create Vulkan swapchain with an invalid Vulkan backend";
    m_backend = nullptr;
    return;
  }

  VkFenceCreateInfo fenceInfo = {};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  VkResult result = vkCreateFence(m_backend->logicalDevice(), &fenceInfo, nullptr, &m_acquireFence);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateFence for swapchain image acquire failed with VkResult " << result;
    m_acquireFence = VK_NULL_HANDLE;
  }
}

Swapchain::~Swapchain()
{
  if (m_backend && m_backend->logicalDevice() != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(m_backend->logicalDevice());
  }

  destroySwapchain();

  if (m_backend && m_acquireFence != VK_NULL_HANDLE) {
    vkDestroyFence(m_backend->logicalDevice(), m_acquireFence, nullptr);
    m_acquireFence = VK_NULL_HANDLE;
  }

  destroySurface();
}

bool
Swapchain::render(ViewerWindow& viewerWindow)
{
  if (!m_backend || !m_surface || !m_surface->isExposed()) {
    return false;
  }

  if (!ensureSurface()) {
    return false;
  }
  updateNativeSurfaceLayout();
  if (!ensureSwapchain()) {
    return false;
  }

  uint32_t imageIndex = 0;
  if (!acquireNextImage(imageIndex)) {
    return false;
  }

  if (imageIndex >= m_framebuffers.size() || !m_framebuffers[imageIndex]) {
    LOG_ERROR << "Swapchain acquired an image without a matching framebuffer";
    return false;
  }

  viewerWindow.setSize(static_cast<int>(m_extent.width), static_cast<int>(m_extent.height));
  viewerWindow.redrawTo(m_framebuffers[imageIndex].get());

  VkCommandBuffer commandBuffer = m_backend->beginSingleTimeCommands();
  m_framebuffers[imageIndex]->transitionColorImage(commandBuffer, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  m_backend->endSingleTimeCommands(commandBuffer);

  return present(imageIndex);
}

bool
Swapchain::ensureSurface()
{
  if (m_vkSurface != VK_NULL_HANDLE) {
    return m_presentSupported;
  }

  if (!createNativeSurface()) {
    return false;
  }

  VkBool32 supported = VK_FALSE;
  VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(
    m_backend->physicalDevice(), m_backend->graphicsQueueFamilyIndex(), m_vkSurface, &supported);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkGetPhysicalDeviceSurfaceSupportKHR failed with VkResult " << result;
    destroySurface();
    return false;
  }

  m_presentSupported = supported == VK_TRUE;
  if (!m_presentSupported) {
    LOG_ERROR << "Selected Vulkan graphics queue family cannot present to the window surface";
    destroySurface();
    return false;
  }

  return true;
}

bool
Swapchain::ensureSwapchain()
{
  if (m_vkSurface == VK_NULL_HANDLE || !m_presentSupported) {
    return false;
  }

  VkSurfaceCapabilitiesKHR capabilities = {};
  VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_backend->physicalDevice(), m_vkSurface, &capabilities);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed with VkResult " << result;
    return false;
  }

  const VkExtent2D desiredExtent = requestedExtent(capabilities);
  if (desiredExtent.width == 0 || desiredExtent.height == 0) {
    return false;
  }

  if (!m_needsRecreate && m_swapchain != VK_NULL_HANDLE && desiredExtent.width == m_extent.width &&
      desiredExtent.height == m_extent.height) {
    return true;
  }

  return recreateSwapchain();
}

bool
Swapchain::recreateSwapchain()
{
  VkDevice device = m_backend->logicalDevice();
  vkDeviceWaitIdle(device);
  destroySwapchain();

  VkSurfaceCapabilitiesKHR capabilities = {};
  VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_backend->physicalDevice(), m_vkSurface, &capabilities);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed with VkResult " << result;
    return false;
  }

  uint32_t formatCount = 0;
  result = vkGetPhysicalDeviceSurfaceFormatsKHR(m_backend->physicalDevice(), m_vkSurface, &formatCount, nullptr);
  if (result != VK_SUCCESS || formatCount == 0) {
    LOG_ERROR << "No Vulkan surface formats are available for the window";
    return false;
  }
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(m_backend->physicalDevice(), m_vkSurface, &formatCount, formats.data());

  uint32_t presentModeCount = 0;
  result =
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_backend->physicalDevice(), m_vkSurface, &presentModeCount, nullptr);
  if (result != VK_SUCCESS || presentModeCount == 0) {
    LOG_ERROR << "No Vulkan present modes are available for the window";
    return false;
  }
  std::vector<VkPresentModeKHR> presentModes(presentModeCount);
  vkGetPhysicalDeviceSurfacePresentModesKHR(
    m_backend->physicalDevice(), m_vkSurface, &presentModeCount, presentModes.data());

  const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats);
  const VkPresentModeKHR presentMode = choosePresentMode(presentModes);
  const VkExtent2D extent = requestedExtent(capabilities);
  if (extent.width == 0 || extent.height == 0) {
    return false;
  }

  if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
    LOG_ERROR << "Window swapchain images cannot be used as Vulkan color attachments";
    return false;
  }

  VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  if (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
    imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }

  uint32_t imageCount = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0) {
    imageCount = std::min(imageCount, capabilities.maxImageCount);
  }

  VkSwapchainCreateInfoKHR createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = m_vkSurface;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = surfaceFormat.format;
  createInfo.imageColorSpace = surfaceFormat.colorSpace;
  createInfo.imageExtent = extent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = imageUsage;
  createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  createInfo.preTransform = capabilities.currentTransform;
  createInfo.compositeAlpha = chooseCompositeAlpha(capabilities.supportedCompositeAlpha);
  createInfo.presentMode = presentMode;
  createInfo.clipped = VK_TRUE;
  createInfo.oldSwapchain = VK_NULL_HANDLE;

  result = vkCreateSwapchainKHR(device, &createInfo, nullptr, &m_swapchain);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateSwapchainKHR failed with VkResult " << result;
    m_swapchain = VK_NULL_HANDLE;
    return false;
  }

  m_colorFormat = surfaceFormat.format;
  m_colorSpace = surfaceFormat.colorSpace;
  m_extent = extent;

  uint32_t actualImageCount = 0;
  vkGetSwapchainImagesKHR(device, m_swapchain, &actualImageCount, nullptr);
  m_images.resize(actualImageCount);
  vkGetSwapchainImagesKHR(device, m_swapchain, &actualImageCount, m_images.data());

  m_framebuffers.clear();
  m_framebuffers.reserve(m_images.size());
  for (VkImage image : m_images) {
    m_framebuffers.push_back(std::make_unique<Framebuffer>(
      *m_backend, m_extent.width, m_extent.height, m_colorFormat, image, VK_IMAGE_LAYOUT_UNDEFINED));
  }

  m_needsRecreate = false;
  return !m_framebuffers.empty();
}

bool
Swapchain::acquireNextImage(uint32_t& imageIndex)
{
  if (m_acquireFence == VK_NULL_HANDLE) {
    LOG_ERROR << "Cannot acquire a swapchain image without an acquire fence";
    return false;
  }

  VkDevice device = m_backend->logicalDevice();
  vkResetFences(device, 1, &m_acquireFence);
  VkResult result = vkAcquireNextImageKHR(device, m_swapchain, UINT64_MAX, VK_NULL_HANDLE, m_acquireFence, &imageIndex);
  if (result == VK_ERROR_SURFACE_LOST_KHR) {
    destroySwapchain();
    destroySurface();
    return false;
  }
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    m_needsRecreate = true;
    return false;
  }
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    LOG_ERROR << "vkAcquireNextImageKHR failed with VkResult " << result;
    return false;
  }

  vkWaitForFences(device, 1, &m_acquireFence, VK_TRUE, UINT64_MAX);
  if (result == VK_SUBOPTIMAL_KHR) {
    m_needsRecreate = true;
  }
  return true;
}

bool
Swapchain::present(uint32_t imageIndex)
{
  VkPresentInfoKHR presentInfo = {};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &m_swapchain;
  presentInfo.pImageIndices = &imageIndex;

  VkResult result = vkQueuePresentKHR(m_backend->graphicsQueue(), &presentInfo);
  if (result == VK_ERROR_SURFACE_LOST_KHR) {
    destroySwapchain();
    destroySurface();
    return false;
  }
  if (isResizeResult(result)) {
    m_needsRecreate = true;
    return result == VK_SUBOPTIMAL_KHR;
  }
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkQueuePresentKHR failed with VkResult " << result;
    return false;
  }
  return true;
}

void
Swapchain::destroySwapchain()
{
  if (!m_backend) {
    return;
  }

  VkDevice device = m_backend->logicalDevice();
  m_framebuffers.clear();
  m_images.clear();

  if (m_swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device, m_swapchain, nullptr);
    m_swapchain = VK_NULL_HANDLE;
  }

  m_extent = {};
}

void
Swapchain::destroySurface()
{
  if (!m_backend || m_vkSurface == VK_NULL_HANDLE) {
    return;
  }

  vkDestroySurfaceKHR(m_backend->instance(), m_vkSurface, nullptr);
  m_vkSurface = VK_NULL_HANDLE;
  m_presentSupported = false;
  m_needsRecreate = true;
}

VkExtent2D
Swapchain::requestedExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
{
  if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
    return capabilities.currentExtent;
  }

  uint32_t width = 1;
  uint32_t height = 1;
  if (m_surface) {
    m_surface->pixelSize(width, height);
  }
  width = std::max(width, 1u);
  height = std::max(height, 1u);

  width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
  height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
  return { width, height };
}

VkSurfaceFormatKHR
Swapchain::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const
{
  if (formats.size() == 1 && formats.front().format == VK_FORMAT_UNDEFINED) {
    return { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
  }

  const VkFormat preferredFormats[] = {
    VK_FORMAT_B8G8R8A8_UNORM,
    VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_B8G8R8A8_SRGB,
    VK_FORMAT_R8G8B8A8_SRGB,
  };
  for (VkFormat preferredFormat : preferredFormats) {
    auto it = std::find_if(formats.begin(), formats.end(), [preferredFormat](const VkSurfaceFormatKHR& format) {
      return format.format == preferredFormat && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    if (it != formats.end()) {
      return *it;
    }
  }

  return formats.front();
}

VkPresentModeKHR
Swapchain::choosePresentMode(const std::vector<VkPresentModeKHR>& presentModes) const
{
  if (std::find(presentModes.begin(), presentModes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != presentModes.end()) {
    return VK_PRESENT_MODE_MAILBOX_KHR;
  }
  if (std::find(presentModes.begin(), presentModes.end(), VK_PRESENT_MODE_FIFO_RELAXED_KHR) != presentModes.end()) {
    return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}

VkCompositeAlphaFlagBitsKHR
Swapchain::chooseCompositeAlpha(VkCompositeAlphaFlagsKHR supportedCompositeAlpha) const
{
  const VkCompositeAlphaFlagBitsKHR preferredFlags[] = {
    VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
    VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
    VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
  };
  for (VkCompositeAlphaFlagBitsKHR flag : preferredFlags) {
    if (supportedCompositeAlpha & flag) {
      return flag;
    }
  }
  return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

#if !defined(__APPLE__) && !defined(_WIN32)
bool
Swapchain::createNativeSurface()
{
  LOG_ERROR << "Vulkan swapchain native surface creation is not implemented for this platform";
  return false;
}

void
Swapchain::updateNativeSurfaceLayout()
{
}
#endif

} // namespace gfxvulkan

#endif // AGAVE_HAS_VULKAN
