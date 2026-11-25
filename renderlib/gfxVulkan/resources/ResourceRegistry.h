#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace gfxvulkan::resources {

// Categories to define the order of destruction of various resource types.
// This is used by releaseAll to determine the order in which resources are destroyed.
// It's designed to ensure that resources are destroyed in a safe order, respecting dependencies between them.
// See DeviceObject.h for the mapping of resource types to destruction phases.
enum class DestructionPhase : uint8_t
{
  Synchronization = 0,
  Framebuffer,
  Pipeline,
  PipelineLayout,
  RenderPass,
  DescriptorPool,
  DescriptorSetLayout,
  ViewAndSampler,
  Allocation,
  Memory,
  Swapchain,
  CommandPool,
};

// This class exists as a safety net for DeviceObjects that are not cleaned up properly by their owned classes.
// If this registry actually destroys anything at Device cleanup time, it should issue warnings into the log.
class ResourceRegistry
{
public:
  // Token is a unique identifier for a tracked resource within the registry.
  using Token = uint64_t;
  using DestroyFunc = std::function<void(VkDevice)>;

  explicit ResourceRegistry(VkDevice device);
  ~ResourceRegistry();

  ResourceRegistry(const ResourceRegistry&) = delete;
  ResourceRegistry& operator=(const ResourceRegistry&) = delete;

  // every DeviceObject is tracked by this registry.
  // We return a unique id for this registration.
  Token track(DestructionPhase phase, DestroyFunc destroy);
  void destroy(Token token);

  // Destroys every still-registered resource, using the DestructionPhase order.
  // After this call, DeviceObjects backed by this registry report invalid handles.
  void releaseAll();

  bool isAlive() const;
  size_t trackedResourceCount() const;

private:
  struct Record
  {
    Token token = 0;
    DestructionPhase phase = DestructionPhase::Allocation;
    DestroyFunc destroy;
  };

  mutable std::mutex m_mutex;
  VkDevice m_device = VK_NULL_HANDLE;
  Token m_nextToken = 1;
  std::unordered_map<Token, Record> m_records;
};

using ResourceRegistryPtr = std::shared_ptr<ResourceRegistry>;

} // namespace gfxvulkan::resources
