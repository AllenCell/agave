#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace gfxvulkan::resources {

// Device teardown is a safety net, not the normal ownership path. Resources
// ordinarily unregister themselves when their move-only wrapper is destroyed.
// If a wrapper outlives Device, the registry drains its Vulkan object before
// vkDestroyDevice and the later wrapper destructor becomes a no-op.
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

class ResourceRegistry
{
public:
  using Token = uint64_t;
  using Destroy = std::function<void(VkDevice)>;

  explicit ResourceRegistry(VkDevice device);
  ~ResourceRegistry();

  ResourceRegistry(const ResourceRegistry&) = delete;
  ResourceRegistry& operator=(const ResourceRegistry&) = delete;

  Token track(DestructionPhase phase, Destroy destroy);
  void destroy(Token token);
  void forget(Token token);

  // Destroys every still-registered resource in dependency-safe phase order.
  // After this call, wrappers backed by this registry report invalid handles.
  void releaseAll();

  bool isAlive() const;
  size_t trackedResourceCount() const;

private:
  struct Record
  {
    Token token = 0;
    DestructionPhase phase = DestructionPhase::Allocation;
    Destroy destroy;
  };

  mutable std::mutex m_mutex;
  VkDevice m_device = VK_NULL_HANDLE;
  Token m_nextToken = 1;
  std::unordered_map<Token, Record> m_records;
};

using ResourceRegistryPtr = std::shared_ptr<ResourceRegistry>;

} // namespace gfxvulkan::resources
