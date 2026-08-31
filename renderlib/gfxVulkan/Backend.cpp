#include "Backend.h"

#include "Framebuffer.h"
#include "GestureRenderer.h"
#include "Logging.h"
#include "NativeSurface.h"
#include "RenderVk.h"
#include "RenderVkPT.h"
#include "RendererVkContext.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <sstream>
#include <utility>

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
  return std::any_of(
    names.begin(), names.end(), [name](const char* current) { return std::strcmp(current, name) == 0; });
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
  createInfo.messageSeverity =
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
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

std::string
physicalDeviceName(VkPhysicalDevice physicalDevice)
{
  VkPhysicalDeviceProperties properties = {};
  vkGetPhysicalDeviceProperties(physicalDevice, &properties);
  return properties.deviceName;
}

std::string
apiVersionToString(uint32_t version)
{
  std::ostringstream ss;
  ss << VK_API_VERSION_MAJOR(version) << "." << VK_API_VERSION_MINOR(version) << "." << VK_API_VERSION_PATCH(version);
  return ss.str();
}

// Fallback for devices that cannot report VkPhysicalDeviceDriverProperties.
// The spec mandates no encoding for driverVersion: each vendor packs it
// differently, so decoding it like an API version prints nonsense for the two
// exceptions below. Everyone else does follow the API version layout.
std::string
driverVersionToString(uint32_t driverVersion, uint32_t vendorID)
{
  std::ostringstream ss;

  if (vendorID == 0x10de) { // NVIDIA: 10 | 8 | 8 | 6 bits
    ss << ((driverVersion >> 22) & 0x3ff) << "." << ((driverVersion >> 14) & 0x0ff) << "."
       << ((driverVersion >> 6) & 0x0ff) << "." << (driverVersion & 0x03f);
    return ss.str();
  }
#if defined(_WIN32)
  if (vendorID == 0x8086) { // Intel, Windows driver only: 18 | 14 bits
    ss << (driverVersion >> 14) << "." << (driverVersion & 0x3fff);
    return ss.str();
  }
#endif

  return apiVersionToString(driverVersion);
}

// VkPhysicalDeviceDriverProperties carries the driver's own name and version
// string -- what the vendor actually publishes -- instead of the packed
// driverVersion integer whose layout the spec leaves undefined. Core since
// Vulkan 1.2, and reachable on 1.1 devices via VK_KHR_driver_properties.
bool
queryDriverProperties(VkPhysicalDevice physicalDevice,
                      const VkPhysicalDeviceProperties& properties,
                      VkPhysicalDeviceDriverProperties& driverProperties)
{
  // vkGetPhysicalDeviceProperties2 is only core from Vulkan 1.1.
  if (properties.apiVersion < VK_API_VERSION_1_1) {
    return false;
  }
  if (properties.apiVersion < VK_API_VERSION_1_2 &&
      !containsName(availableDeviceExtensions(physicalDevice), VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME)) {
    return false;
  }

  driverProperties = {};
  driverProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

  VkPhysicalDeviceProperties2 properties2 = {};
  properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  properties2.pNext = &driverProperties;
  vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);
  return true;
}

// Prefer the driver-reported version string; fall back to decoding the packed
// driverVersion when the device cannot report driver properties.
std::string
driverDescription(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceProperties& properties)
{
  VkPhysicalDeviceDriverProperties driverProperties = {};
  if (queryDriverProperties(physicalDevice, properties, driverProperties)) {
    const std::string driverInfo = driverProperties.driverInfo;
    const std::string driverName = driverProperties.driverName;
    if (!driverInfo.empty()) {
      return driverName.empty() ? driverInfo : driverInfo + " (" + driverName + ")";
    }
    if (!driverName.empty()) {
      return driverVersionToString(properties.driverVersion, properties.vendorID) + " (" + driverName + ")";
    }
  }

  return driverVersionToString(properties.driverVersion, properties.vendorID);
}

const char*
deviceTypeToString(VkPhysicalDeviceType deviceType)
{
  switch (deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
      return "integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
      return "discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
      return "virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
      return "CPU";
    case VK_PHYSICAL_DEVICE_TYPE_OTHER:
    default:
      return "other";
  }
}

// What a physical device can do, evaluated once per device and then reused for
// both the startup log and the selection decision.
struct DeviceCapabilities
{
  // First queue family with VK_QUEUE_GRAPHICS_BIT.
  uint32_t graphicsQueueFamilyIndex = UINT32_MAX;
  // First queue family with both VK_QUEUE_GRAPHICS_BIT and presentation
  // support for the surface that was queried. UINT32_MAX when no surface was
  // supplied (headless) or when no single family can do both.
  uint32_t graphicsPresentQueueFamilyIndex = UINT32_MAX;
  bool hasSwapchainExtension = false;
};

// Presentation support is a property of a (device, queue family, surface)
// triple, so this must be given the surface the window will actually present
// to. Pass VK_NULL_HANDLE to skip the presentation queries entirely.
DeviceCapabilities
inspectPhysicalDevice(VkPhysicalDevice physicalDevice, VkSurfaceKHR presentationSurface)
{
  DeviceCapabilities capabilities;
  capabilities.hasSwapchainExtension =
    containsName(availableDeviceExtensions(physicalDevice), VK_KHR_SWAPCHAIN_EXTENSION_NAME);

  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

  for (uint32_t i = 0; i < queueFamilyCount; ++i) {
    if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
      continue;
    }
    if (capabilities.graphicsQueueFamilyIndex == UINT32_MAX) {
      capabilities.graphicsQueueFamilyIndex = i;
    }

    if (presentationSurface == VK_NULL_HANDLE || capabilities.graphicsPresentQueueFamilyIndex != UINT32_MAX) {
      continue;
    }

    VkBool32 supported = VK_FALSE;
    VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, presentationSurface, &supported);
    if (result != VK_SUCCESS) {
      LOG_WARNING << "vkGetPhysicalDeviceSurfaceSupportKHR failed for " << physicalDeviceName(physicalDevice)
                  << " queue family " << i << " with VkResult " << result;
      continue;
    }
    if (supported == VK_TRUE) {
      capabilities.graphicsPresentQueueFamilyIndex = i;
    }
  }

  return capabilities;
}

// Requirements differ by mode: headless only needs a graphics queue, while
// windowed additionally needs to be able to present to the window's surface
// and to create a swapchain. On failure, reason describes what is missing.
bool
isDeviceCompatible(const DeviceCapabilities& capabilities, bool requiresPresent, std::string& reason)
{
  if (capabilities.graphicsQueueFamilyIndex == UINT32_MAX) {
    reason = "has no graphics-capable queue family";
    return false;
  }
  if (!requiresPresent) {
    return true;
  }
  if (!capabilities.hasSwapchainExtension) {
    reason = std::string("does not support ") + VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    return false;
  }
  if (capabilities.graphicsPresentQueueFamilyIndex == UINT32_MAX) {
    // AGAVE renders and presents on one queue, so a device that can present
    // only from a compute/transfer-style family is rejected here rather than
    // handled with a separate present queue and image ownership transfers.
    reason = "has no queue family that supports both graphics and presentation to this window surface";
    return false;
  }
  return true;
}

// Human-readable capability summary for the device list logged at startup.
std::string
describeCapabilities(const DeviceCapabilities& capabilities, bool requiresPresent)
{
  std::string description = capabilities.graphicsQueueFamilyIndex == UINT32_MAX ? "no graphics queue" : "graphics";
  if (requiresPresent) {
    description += capabilities.graphicsPresentQueueFamilyIndex == UINT32_MAX ? ", cannot present to this window"
                                                                              : ", can present to this window";
    if (!capabilities.hasSwapchainExtension) {
      description += ", no swapchain extension";
    }
  }
  return description;
}

// Extra guidance for the windowed case, where "no device can present" usually
// means a platform/driver mismatch rather than missing hardware.
void
logWindowedSelectionHint()
{
#if defined(_WIN32) || defined(__APPLE__)
  LOG_ERROR << "Try a different --gpu index (see --list_devices) or --graphics_backend opengl.";
#else
  LOG_ERROR << "Try a different --gpu index (see --list_devices), --graphics_backend opengl, or "
               "QT_QPA_PLATFORM=xcb.";
#endif
}

} // namespace

// Construction only brings up the VkInstance. Device selection is a separate,
// explicit step because it depends on something the constructor cannot know:
// which physical device and queue family are usable is a property of the
// surface that has to be presentable. Windowed callers therefore wait for
// their window and call initDeviceForWindow(); headless callers have no
// surface to wait for and call initDeviceHeadless() immediately; device
// enumeration (--list_devices) needs the instance and nothing more.
Backend::Backend(const gfxApi::InitParams& params)
  : m_params(params)
{
  m_valid = createInstance() && setupDebugMessenger();
}

Backend::~Backend()
{
  destroy();
}

std::unique_ptr<gfxApi::IGestureRenderer>
Backend::createGestureRenderer()
{
  if (!m_deviceReady) {
    LOG_ERROR << "Cannot create a Vulkan gesture renderer before device initialization";
    return nullptr;
  }
  return std::make_unique<GestureRenderer>();
}

std::unique_ptr<gfxApi::IGLContext>
Backend::createRendererContext(gfxApi::IGLContext* externalContext)
{
  (void)externalContext;
  if (!m_deviceReady) {
    LOG_ERROR << "Cannot create a Vulkan renderer context before device initialization";
    return nullptr;
  }
  return std::make_unique<RendererVkContext>(*this);
}

std::unique_ptr<gfxApi::IRenderWindow>
Backend::createRenderWindow(gfxApi::RenderWindowKind kind, RenderSettings* renderSettings)
{
  if (!m_deviceReady) {
    LOG_ERROR << "Cannot create a Vulkan render window before device initialization";
    return nullptr;
  }
  switch (kind) {
    case gfxApi::RenderWindowKind::RaymarchBlended:
      return std::make_unique<RenderVk>(*this, renderSettings);
    case gfxApi::RenderWindowKind::PathTrace:
    default:
      return std::make_unique<RenderVkPT>(*this, renderSettings);
  }
}

std::unique_ptr<gfxApi::Framebuffer>
Backend::createFramebuffer(const gfxApi::FramebufferDesc& desc)
{
  if (!m_deviceReady) {
    LOG_ERROR << "Cannot create a Vulkan framebuffer before device initialization";
    return nullptr;
  }
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
Backend::initDeviceHeadless()
{
  if (m_deviceReady) {
    return true;
  }
  if (!m_valid) {
    LOG_ERROR << "Cannot initialize a Vulkan device from an invalid backend";
    return false;
  }
  if (!m_params.headless) {
    LOG_ERROR << "initDeviceHeadless called on a windowed Vulkan backend; use initDeviceForWindow so the device is "
                 "chosen against the window surface";
    return false;
  }

  return initializeDevice(VK_NULL_HANDLE);
}

bool
Backend::initDeviceForWindow(gfxApi::IWindowSurface* surface)
{
  if (m_deviceReady) {
    return true;
  }
  if (!m_valid) {
    LOG_ERROR << "Cannot initialize a Vulkan window device from an invalid backend";
    return false;
  }
  // A headless backend has no window to select against; the surface, if any,
  // is irrelevant. This is the only case where a missing surface is benign.
  if (m_params.headless) {
    return initDeviceHeadless();
  }
  // A null surface must never be read as "headless". In windowed mode it means
  // the caller created renderers before the window existed, which is exactly
  // the ordering bug this hook is here to catch.
  if (!surface) {
    LOG_ERROR << "Windowed Vulkan initialization requires a native window surface";
    return false;
  }

  // This surface exists only to answer "which device can present here?". The
  // Swapchain creates and owns its own VkSurfaceKHR for the same window, since
  // it has to be able to drop and rebuild it across resizes.
  VkSurfaceKHR presentationSurface = createNativeWindowSurface(m_instance, surface);
  if (presentationSurface == VK_NULL_HANDLE) {
    LOG_ERROR << "Unable to create a Vulkan surface for device selection";
    return false;
  }

  const bool initialized = initializeDevice(presentationSurface);
  vkDestroySurfaceKHR(m_instance, presentationSurface, nullptr);
  return initialized;
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
  createInfo.pNext =
    debugCreateInfo.sType == VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT ? &debugCreateInfo : nullptr;

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

  auto createDebugUtilsMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
    vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
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
Backend::initializeDevice(VkSurfaceKHR presentationSurface)
{
  if (m_deviceReady) {
    return true;
  }
  if (!m_params.headless && presentationSurface == VK_NULL_HANDLE) {
    LOG_ERROR << "Windowed Vulkan device selection requires a presentation surface";
    return false;
  }

  if (!pickPhysicalDevice(presentationSurface)) {
    return false;
  }
  if (!createLogicalDevice()) {
    destroyDevice();
    return false;
  }
  m_device.initialize(m_physicalDevice, m_deviceHandle);
  if (!createCommandPool()) {
    destroyDevice();
    return false;
  }

  m_deviceReady = true;
  return true;
}

bool
Backend::pickPhysicalDevice(VkSurfaceKHR presentationSurface)
{
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
  if (deviceCount == 0) {
    LOG_ERROR << "No Vulkan physical devices are available";
    return false;
  }

  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

  // Presentation is only required when this backend is going to drive a window,
  // regardless of whether the device was chosen explicitly or automatically.
  const bool requiresPresent = !m_params.headless;
  const int requestedGpu = m_params.selectedGpu;

  std::vector<DeviceCapabilities> capabilities;
  capabilities.reserve(deviceCount);
  for (uint32_t i = 0; i < deviceCount; ++i) {
    capabilities.push_back(inspectPhysicalDevice(devices[i], requiresPresent ? presentationSurface : VK_NULL_HANDLE));
  }

  // These indices are what --gpu N refers to, so log the raw enumeration order.
  for (uint32_t i = 0; i < deviceCount; ++i) {
    LOG_INFO << "Vulkan device " << i << ": " << physicalDeviceName(devices[i]) << " ("
             << describeCapabilities(capabilities[i], requiresPresent) << ")";
  }

  uint32_t chosenIndex = UINT32_MAX;

  if (requestedGpu >= 0) {
    // Explicit selection: use exactly this device, but validate it against the
    // mode's requirements and fail loudly instead of falling back to another.
    if (static_cast<uint32_t>(requestedGpu) >= deviceCount) {
      LOG_ERROR << "Requested Vulkan device " << requestedGpu << " does not exist; " << deviceCount
                << " device(s) are available. Run with --list_devices to see the valid indices.";
      return false;
    }

    std::string reason;
    if (!isDeviceCompatible(capabilities[requestedGpu], requiresPresent, reason)) {
      LOG_ERROR << "Requested Vulkan device " << requestedGpu << " (" << physicalDeviceName(devices[requestedGpu])
                << ") " << reason << ".";
      if (requiresPresent) {
        logWindowedSelectionHint();
      }
      return false;
    }
    chosenIndex = static_cast<uint32_t>(requestedGpu);
  } else {
    // Auto-selection: skip incompatible devices, take the highest scoring one
    // that remains.
    int bestScore = 0;
    for (uint32_t i = 0; i < deviceCount; ++i) {
      std::string reason;
      if (!isDeviceCompatible(capabilities[i], requiresPresent, reason)) {
        LOG_INFO << "Skipping Vulkan device " << i << " (" << physicalDeviceName(devices[i]) << "): " << reason;
        continue;
      }
      const int score = scorePhysicalDevice(devices[i]);
      if (chosenIndex == UINT32_MAX || score > bestScore) {
        chosenIndex = i;
        bestScore = score;
      }
    }

    if (chosenIndex == UINT32_MAX) {
      LOG_ERROR << "No Vulkan device is compatible with " << (requiresPresent ? "windowed" : "headless")
                << " rendering.";
      if (requiresPresent) {
        logWindowedSelectionHint();
      }
      return false;
    }
  }

  m_physicalDevice = devices[chosenIndex];
  m_graphicsQueueFamilyIndex = requiresPresent ? capabilities[chosenIndex].graphicsPresentQueueFamilyIndex
                                               : capabilities[chosenIndex].graphicsQueueFamilyIndex;

  LOG_INFO << "Selected Vulkan device " << chosenIndex << ": " << physicalDeviceName(m_physicalDevice)
           << " (queue family " << m_graphicsQueueFamilyIndex
           << (requiresPresent ? ", graphics+present)" : ", graphics)");
  return true;
}

bool
Backend::enabledDeviceExtensions(VkPhysicalDevice physicalDevice, std::vector<const char*>& extensions) const
{
  const std::vector<std::string> availableExtensions = availableDeviceExtensions(physicalDevice);
  extensions.clear();

  if (!m_params.headless) {
    if (!containsName(availableExtensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
      LOG_ERROR << "Windowed Vulkan device " << physicalDeviceName(physicalDevice) << " is missing "
                << VK_KHR_SWAPCHAIN_EXTENSION_NAME;
      return false;
    }
    extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  }

  if (containsName(availableExtensions, kPortabilitySubsetExtension)) {
    extensions.push_back(kPortabilitySubsetExtension);
  }

  return true;
}

bool
Backend::createLogicalDevice()
{
  if (m_graphicsQueueFamilyIndex == UINT32_MAX) {
    LOG_ERROR << "Cannot create a Vulkan logical device without a selected queue family";
    return false;
  }

  const float queuePriority = 1.0f;
  VkDeviceQueueCreateInfo queueCreateInfo = {};
  queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueCreateInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;
  queueCreateInfo.queueCount = 1;
  queueCreateInfo.pQueuePriorities = &queuePriority;

  VkPhysicalDeviceFeatures deviceFeatures = {};
  std::vector<const char*> deviceExtensions;
  if (!enabledDeviceExtensions(m_physicalDevice, deviceExtensions)) {
    return false;
  }

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

  auto commandPool = m_device.createCommandPool(createInfo);
  if (!commandPool) {
    return false;
  }
  m_commandPool = std::move(*commandPool);
  return true;
}

void
Backend::destroyDevice()
{
  if (m_deviceHandle != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(m_deviceHandle);
  }

  // The command pool is a Backend-owned resource. Release it through its
  // normal owner before Device checks for objects that outlived their owners.
  m_commandPool.reset();
  m_device.release();

  if (m_deviceHandle != VK_NULL_HANDLE) {
    vkDestroyDevice(m_deviceHandle, nullptr);
    m_deviceHandle = VK_NULL_HANDLE;
  }

  m_physicalDevice = VK_NULL_HANDLE;
  m_graphicsQueue = VK_NULL_HANDLE;
  m_graphicsQueueFamilyIndex = UINT32_MAX;
  m_deviceReady = false;
}

void
Backend::destroy()
{
  destroyDevice();

  if (m_debugMessenger != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
    auto destroyDebugUtilsMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
      vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
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
  return m_device.findMemoryType(typeFilter, properties);
}

VkCommandBuffer
Backend::beginSingleTimeCommands() const
{
  VkCommandBufferAllocateInfo allocateInfo = {};
  allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocateInfo.commandPool = m_commandPool.get();
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
  VkResult result = vkEndCommandBuffer(commandBuffer);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkEndCommandBuffer failed with VkResult " << result;
    vkFreeCommandBuffers(m_deviceHandle, m_commandPool.get(), 1, &commandBuffer);
    return;
  }

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  VkFenceCreateInfo fenceInfo = {};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  VkFence submittedFence = VK_NULL_HANDLE;
  result = vkCreateFence(m_deviceHandle, &fenceInfo, nullptr, &submittedFence);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateFence failed with VkResult " << result;
    vkFreeCommandBuffers(m_deviceHandle, m_commandPool.get(), 1, &commandBuffer);
    return;
  }

  // Wait only for this submission. Queue-idle also waited for unrelated work
  // submitted after this helper, which made large time-step uploads block the
  // GUI longer than the upload itself required.
  result = vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, submittedFence);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkQueueSubmit failed with VkResult " << result;
    vkDestroyFence(m_deviceHandle, submittedFence, nullptr);
    vkFreeCommandBuffers(m_deviceHandle, m_commandPool.get(), 1, &commandBuffer);
    return;
  }

  result = vkWaitForFences(m_deviceHandle, 1, &submittedFence, VK_TRUE, UINT64_MAX);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkWaitForFences failed with VkResult " << result;
  }

  VkCommandPool commandPool = m_commandPool.get();
  vkFreeCommandBuffers(m_deviceHandle, commandPool, 1, &commandBuffer);
  vkDestroyFence(m_deviceHandle, submittedFence, nullptr);
}

void
Backend::listDevices(int selectedGpu)
{
  // No window exists yet, so build an instance without the windowing-system
  // surface extensions. Construction stops at the instance, which is all
  // enumeration needs.
  gfxApi::InitParams params;
  params.headless = true;
  params.selectedGpu = selectedGpu;
  Backend backend(params);
  if (!backend.isValid()) {
    LOG_ERROR << "Unable to create a Vulkan instance to enumerate devices";
    return;
  }

  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(backend.m_instance, &deviceCount, nullptr);
  if (deviceCount == 0) {
    LOG_INFO << "No Vulkan physical devices are available";
    return;
  }

  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(backend.m_instance, &deviceCount, devices.data());

  LOG_INFO << deviceCount << " Vulkan device(s) found. These indices are what --gpu N selects.";
  for (uint32_t i = 0; i < deviceCount; ++i) {
    VkPhysicalDeviceProperties properties = {};
    vkGetPhysicalDeviceProperties(devices[i], &properties);
    // Query without a surface: presentation support depends on the actual
    // window surface, which does not exist during device listing.
    const DeviceCapabilities capabilities = inspectPhysicalDevice(devices[i], VK_NULL_HANDLE);

    LOG_INFO << "Vulkan device " << i << ": " << properties.deviceName;
    LOG_INFO << "  API version: " << apiVersionToString(properties.apiVersion);
    LOG_INFO << "  Driver version: " << driverDescription(devices[i], properties);
    LOG_INFO << "  Device type: " << deviceTypeToString(properties.deviceType);
    LOG_INFO << "  Capabilities: "
             << (capabilities.graphicsQueueFamilyIndex == UINT32_MAX ? "no graphics queue" : "graphics")
             << (capabilities.hasSwapchainExtension ? ", swapchain" : ", no swapchain");
  }

  if (selectedGpu >= 0 && static_cast<uint32_t>(selectedGpu) >= deviceCount) {
    LOG_WARNING << "--gpu " << selectedGpu << " is out of range for the " << deviceCount << " device(s) listed above";
  }
}

} // namespace gfxvulkan
