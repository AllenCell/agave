#pragma once

#include "Device.h"
#include "gfxapi/Backend.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class RenderSettings;

namespace gfxvulkan {

class Backend : public gfxApi::Backend
{
public:
  explicit Backend(const gfxApi::InitParams& params);
  ~Backend() override;

  Device& device() override { return m_device; }
  std::unique_ptr<gfxApi::IGestureRenderer> createGestureRenderer() override;
  std::unique_ptr<gfxApi::IGLContext> createRendererContext(gfxApi::IGLContext* externalContext = nullptr) override;
  std::unique_ptr<gfxApi::IRenderWindow> createRenderWindow(gfxApi::RenderWindowKind kind,
                                                            RenderSettings* renderSettings) override;
  std::unique_ptr<gfxApi::Framebuffer> createFramebuffer(const gfxApi::FramebufferDesc& desc) override;
  void clearCurrentFramebuffer(const gfxApi::ClearColor& color) override;
  bool isHeadless() const override { return m_params.headless; }
  gfxApi::BackendKind kind() const override { return gfxApi::BackendKind::Vulkan; }
  bool initDeviceForWindow(gfxApi::IWindowSurface* surface = nullptr) override;
  bool isDeviceReady() const override { return m_deviceReady; }

  // Headless counterpart to initDeviceForWindow: there is no surface to wait
  // for, so the owner brings the device up right after construction.
  bool initDeviceHeadless();

  // True once the VkInstance exists. A valid backend still has no device until
  // initDeviceHeadless() or initDeviceForWindow() succeeds.
  bool isValid() const { return m_valid; }

  VkInstance instance() const { return m_instance; }
  VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
  VkDevice logicalDevice() const { return m_deviceHandle; }
  VkQueue graphicsQueue() const { return m_graphicsQueue; }
  uint32_t graphicsQueueFamilyIndex() const { return m_graphicsQueueFamilyIndex; }
  VkCommandPool commandPool() const { return m_commandPool.get(); }

  uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
  VkCommandBuffer beginSingleTimeCommands() const;
  void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;

  static void listDevices(int selectedGpu);

private:
  bool createInstance();
  bool setupDebugMessenger();
  // Select a physical device and bring up the logical device, queues, and
  // command pool. presentationSurface is VK_NULL_HANDLE for headless and must
  // be a real surface when windowed.
  bool initializeDevice(VkSurfaceKHR presentationSurface);
  bool pickPhysicalDevice(VkSurfaceKHR presentationSurface);
  bool createLogicalDevice();
  bool createCommandPool();
  // Tear down everything created by initializeDevice, leaving the instance.
  void destroyDevice();
  void destroy();

  std::vector<const char*> enabledInstanceExtensions() const;
  bool enabledDeviceExtensions(VkPhysicalDevice physicalDevice, std::vector<const char*>& extensions) const;

  gfxApi::InitParams m_params;
  Device m_device;

  VkInstance m_instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
  VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
  VkDevice m_deviceHandle = VK_NULL_HANDLE;
  VkQueue m_graphicsQueue = VK_NULL_HANDLE;
  resources::UniqueCommandPool m_commandPool;
  uint32_t m_graphicsQueueFamilyIndex = UINT32_MAX;
  bool m_valid = false;
  bool m_deviceReady = false;
};

} // namespace gfxvulkan
