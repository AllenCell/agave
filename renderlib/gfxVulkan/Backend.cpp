#include "Backend.h"

#include "Framebuffer.h"
#include "GestureRenderer.h"
#include "Logging.h"
#include "RenderVk.h"
#include "RendererVkContext.h"

#include <algorithm>
#include <cstring>
#include <set>

namespace gfxvulkan {

namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
constexpr const char* kPortabilitySubsetExtension = "VK_KHR_portability_subset";

bool
containsName(const std::vector<std::string>& names, const char* name)
{
  return std::find(names.begin(), names.end(), name) != names.end();
}

bool
containsExtension(const std::vector<const char*>& names, const char* name)
{
  return std::any_of(names.begin(), names.end(), [name](const char* current) {
    return std::strcmp(current, name) == 0;
  });
}

void
appendIfAvailable(std::vector<const char*>& enabledExtensions,
                  const std::vector<std::string>& availableExtensions,
                  const char* extensionName)
{
  if (!containsName(availableExtensions, extensionName)) {
    return;
  }
  if (containsExtension(enabledExtensions, extensionName)) {
    return;
  }
  enabledExtensions.push_back(extensionName);
}

std::vector<std::string>
availableInstanceExtensions()
{
  uint32_t extensionCount = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
  std::vector<VkExtensionProperties> properties(extensionCount);
  if (extensionCount > 0) {
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, properties.data());
  }

  std::vector<std::string> names;
  names.reserve(properties.size());
  for (const auto& property : properties) {
    names.emplace_back(property.extensionName);
  }
  return names;
}

std::vector<std::string>
availableInstanceLayers()
{
  uint32_t layerCount = 0;
  vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
  std::vector<VkLayerProperties> properties(layerCount);
  if (layerCount > 0) {
    vkEnumerateInstanceLayerProperties(&layerCount, properties.data());
  }

  std::vector<std::string> names;
  names.reserve(properties.size());
  for (const auto& property : properties) {
    names.emplace_back(property.layerName);
  }
  return names;
}

std::vector<std::string>
availableDeviceExtensions(VkPhysicalDevice physicalDevice)
{
  uint32_t extensionCount = 0;
  vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
  std::vector<VkExtensionProperties> properties(extensionCount);
  if (extensionCount > 0) {
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, properties.data());
  }

  std::vector<std::string> names;
  names.reserve(properties.size());
  for (const auto& property : properties) {
    names.emplace_back(property.extensionName);
  }
  return names;
}

VkBool32 VKAPI_PTR
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
              VkDebugUtilsMessageTypeFlagsEXT,
              const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
              void*)
{
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    LOG_ERROR << "Vulkan validation: " << callbackData->pMessage;
  } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    LOG_WARNING << "Vulkan validation: " << callbackData->pMessage;
  } else {
    LOG_DEBUG << "Vulkan validation: " << callbackData->pMessage;
  }
  return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT
debugMessengerCreateInfo()
{
  VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = debugCallback;
  return createInfo;
}

int
scorePhysicalDevice(VkPhysicalDevice physicalDevice)
{
  VkPhysicalDeviceProperties properties = {};
  vkGetPhysicalDeviceProperties(physicalDevice, &properties);

  int score = 0;
  if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
    score += 1000;
  }
  score += static_cast<int>(properties.limits.maxImageDimension3D);
  return score;
}

} // namespace

Backend::Backend(const gfxApi::InitParams& params)
  : m_params(params)
{
  m_valid = createInstance() && setupDebugMessenger() && pickPhysicalDevice() && createLogicalDevice() &&
            createCommandPool();
  if (m_valid) {
    m_device.initialize(m_deviceHandle);
  }
}

Backend::~Backend()
{
  destroy();
}

std::unique_ptr<gfxApi::IGestureRenderer>
Backend::createGestureRenderer()
{
  return std::make_unique<GestureRenderer>();
}

std::unique_ptr<gfxApi::IGLContext>
Backend::createRendererContext(gfxApi::IGLContext* externalContext)
{
  (void)externalContext;
  return std::make_unique<RendererVkContext>(*this);
}

std::unique_ptr<gfxApi::IRenderWindow>
Backend::createRenderWindow(gfxApi::RenderWindowKind kind, RenderSettings* renderSettings)
{
  (void)kind;
  return std::make_unique<RenderVk>(*this, renderSettings);
}

std::unique_ptr<gfxApi::Framebuffer>
Backend::createFramebuffer(const gfxApi::FramebufferDesc& desc)
{
  return std::make_unique<Framebuffer>(*this, desc);
}

void
Backend::clearCurrentFramebuffer(const gfxApi::ClearColor& color)
{
  (void)color;
  // Vulkan has no implicit current framebuffer. Window rendering must clear the
  // active swapchain image inside a command buffer.
}

bool
Backend::createInstance()
{
  const std::vector<std::string> availableExtensions = availableInstanceExtensions();
  const std::vector<std::string> availableLayers = availableInstanceLayers();

  std::vector<const char*> enabledExtensions;
  for (const auto& requested : m_params.vulkanInstanceExtensions) {
    if (!containsName(availableExtensions, requested.c_str())) {
      LOG_ERROR << "Required Vulkan instance extension is not available: " << requested;
      return false;
    }
    enabledExtensions.push_back(requested.c_str());
  }

  if (!m_params.headless) {
    appendIfAvailable(enabledExtensions, availableExtensions, VK_KHR_SURFACE_EXTENSION_NAME);
#if defined(__APPLE__)
    appendIfAvailable(enabledExtensions, availableExtensions, "VK_EXT_metal_surface");
#elif defined(_WIN32)
    appendIfAvailable(enabledExtensions, availableExtensions, "VK_KHR_win32_surface");
#else
    appendIfAvailable(enabledExtensions, availableExtensions, "VK_KHR_xcb_surface");
    appendIfAvailable(enabledExtensions, availableExtensions, "VK_KHR_xlib_surface");
    appendIfAvailable(enabledExtensions, availableExtensions, "VK_KHR_wayland_surface");
#endif
  }

  VkInstanceCreateFlags instanceFlags = 0;
  if (containsName(availableExtensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) &&
      !containsExtension(enabledExtensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
    enabledExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    instanceFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }

  VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};
  if (m_params.enableDebug && containsName(availableExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) &&
      !containsExtension(enabledExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
    enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    debugCreateInfo = debugMessengerCreateInfo();
  }

  std::vector<const char*> enabledLayers;
  if (m_params.enableDebug && containsName(availableLayers, kValidationLayer)) {
    enabledLayers.push_back(kValidationLayer);
  }

  VkApplicationInfo appInfo = {};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "AGAVE";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 9, 0);
  appInfo.pEngineName = "AGAVE renderlib";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_3;

  VkInstanceCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.flags = instanceFlags;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
  createInfo.ppEnabledExtensionNames = enabledExtensions.empty() ? nullptr : enabledExtensions.data();
  createInfo.enabledLayerCount = static_cast<uint32_t>(enabledLayers.size());
  createInfo.ppEnabledLayerNames = enabledLayers.empty() ? nullptr : enabledLayers.data();
  createInfo.pNext = debugCreateInfo.sType == VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT
                       ? &debugCreateInfo
                       : nullptr;

  VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateInstance failed with VkResult " << result;
    return false;
  }
  return true;
}

bool
Backend::setupDebugMessenger()
{
  if (!m_params.enableDebug || m_instance == VK_NULL_HANDLE) {
    return true;
  }

  auto createDebugUtilsMessenger =
    reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance,
                                                                               "vkCreateDebugUtilsMessengerEXT"));
  if (!createDebugUtilsMessenger) {
    return true;
  }

  VkDebugUtilsMessengerCreateInfoEXT createInfo = debugMessengerCreateInfo();
  VkResult result = createDebugUtilsMessenger(m_instance, &createInfo, nullptr, &m_debugMessenger);
  if (result != VK_SUCCESS) {
    LOG_WARNING << "vkCreateDebugUtilsMessengerEXT failed with VkResult " << result;
  }
  return true;
}

bool
Backend::pickPhysicalDevice()
{
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
  if (deviceCount == 0) {
    LOG_ERROR << "No Vulkan physical devices are available";
    return false;
  }

  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

  for (uint32_t i = 0; i < deviceCount; ++i) {
    VkPhysicalDeviceProperties properties = {};
    vkGetPhysicalDeviceProperties(devices[i], &properties);
    LOG_INFO << "Vulkan device " << i << ": " << properties.deviceName;
  }

  if (m_params.selectedGpu >= 0 && static_cast<uint32_t>(m_params.selectedGpu) < deviceCount) {
    m_physicalDevice = devices[static_cast<uint32_t>(m_params.selectedGpu)];
  } else {
    m_physicalDevice = *std::max_element(devices.begin(), devices.end(), [](VkPhysicalDevice a, VkPhysicalDevice b) {
      return scorePhysicalDevice(a) < scorePhysicalDevice(b);
    });
  }

  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

  for (uint32_t i = 0; i < queueFamilyCount; ++i) {
    if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      m_graphicsQueueFamilyIndex = i;
      break;
    }
  }

  if (m_graphicsQueueFamilyIndex == UINT32_MAX) {
    LOG_ERROR << "Selected Vulkan physical device has no graphics queue";
    return false;
  }

  VkPhysicalDeviceProperties properties = {};
  vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
  LOG_INFO << "Selected Vulkan device: " << properties.deviceName;
  return true;
}

std::vector<const char*>
Backend::enabledDeviceExtensions(VkPhysicalDevice physicalDevice) const
{
  const std::vector<std::string> availableExtensions = availableDeviceExtensions(physicalDevice);
  std::vector<const char*> enabledExtensions;

  if (containsName(availableExtensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
    enabledExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  }

  if (containsName(availableExtensions, kPortabilitySubsetExtension)) {
    enabledExtensions.push_back(kPortabilitySubsetExtension);
  }

  return enabledExtensions;
}

bool
Backend::createLogicalDevice()
{
  const float queuePriority = 1.0f;
  VkDeviceQueueCreateInfo queueCreateInfo = {};
  queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueCreateInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;
  queueCreateInfo.queueCount = 1;
  queueCreateInfo.pQueuePriorities = &queuePriority;

  VkPhysicalDeviceFeatures deviceFeatures = {};
  const std::vector<const char*> deviceExtensions = enabledDeviceExtensions(m_physicalDevice);

  VkDeviceCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.queueCreateInfoCount = 1;
  createInfo.pQueueCreateInfos = &queueCreateInfo;
  createInfo.pEnabledFeatures = &deviceFeatures;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
  createInfo.ppEnabledExtensionNames = deviceExtensions.empty() ? nullptr : deviceExtensions.data();

  VkResult result = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_deviceHandle);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateDevice failed with VkResult " << result;
    return false;
  }

  vkGetDeviceQueue(m_deviceHandle, m_graphicsQueueFamilyIndex, 0, &m_graphicsQueue);
  return true;
}

bool
Backend::createCommandPool()
{
  VkCommandPoolCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  createInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;

  VkResult result = vkCreateCommandPool(m_deviceHandle, &createInfo, nullptr, &m_commandPool);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateCommandPool failed with VkResult " << result;
    return false;
  }
  return true;
}

void
Backend::destroy()
{
  if (m_deviceHandle != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(m_deviceHandle);
  }

  m_device.release();

  if (m_commandPool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(m_deviceHandle, m_commandPool, nullptr);
    m_commandPool = VK_NULL_HANDLE;
  }

  if (m_deviceHandle != VK_NULL_HANDLE) {
    vkDestroyDevice(m_deviceHandle, nullptr);
    m_deviceHandle = VK_NULL_HANDLE;
  }

  if (m_debugMessenger != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
    auto destroyDebugUtilsMessenger =
      reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance,
                                                                                  "vkDestroyDebugUtilsMessengerEXT"));
    if (destroyDebugUtilsMessenger) {
      destroyDebugUtilsMessenger(m_instance, m_debugMessenger, nullptr);
    }
    m_debugMessenger = VK_NULL_HANDLE;
  }

  if (m_instance != VK_NULL_HANDLE) {
    vkDestroyInstance(m_instance, nullptr);
    m_instance = VK_NULL_HANDLE;
  }

  m_physicalDevice = VK_NULL_HANDLE;
  m_graphicsQueue = VK_NULL_HANDLE;
  m_graphicsQueueFamilyIndex = UINT32_MAX;
  m_valid = false;
}

uint32_t
Backend::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
{
  VkPhysicalDeviceMemoryProperties memoryProperties = {};
  vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memoryProperties);

  for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
    const bool typeMatches = (typeFilter & (1u << i)) != 0;
    const bool propertiesMatch = (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties;
    if (typeMatches && propertiesMatch) {
      return i;
    }
  }

  LOG_ERROR << "Failed to find a compatible Vulkan memory type";
  return UINT32_MAX;
}

VkCommandBuffer
Backend::beginSingleTimeCommands() const
{
  VkCommandBufferAllocateInfo allocateInfo = {};
  allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocateInfo.commandPool = m_commandPool;
  allocateInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  vkAllocateCommandBuffers(m_deviceHandle, &allocateInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  return commandBuffer;
}

void
Backend::endSingleTimeCommands(VkCommandBuffer commandBuffer) const
{
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(m_graphicsQueue);
  vkFreeCommandBuffers(m_deviceHandle, m_commandPool, 1, &commandBuffer);
}

void
Backend::listDevices(int selectedGpu)
{
  gfxApi::InitParams params;
  params.selectedGpu = selectedGpu;
  Backend backend(params);
  (void)backend;
}

} // namespace gfxvulkan
