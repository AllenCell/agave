#include "ResourceRegistry.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace gfxvulkan::resources {

ResourceRegistry::ResourceRegistry(VkDevice device)
  : m_device(device)
{
}

ResourceRegistry::~ResourceRegistry()
{
  releaseAll();
}

ResourceRegistry::Token
ResourceRegistry::track(DestructionPhase phase, Destroy destroy)
{
  if (!destroy) {
    return 0;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_device == VK_NULL_HANDLE) {
    return 0;
  }

  const Token token = m_nextToken++;
  m_records.emplace(token, Record{ token, phase, std::move(destroy) });
  return token;
}

void
ResourceRegistry::destroy(Token token)
{
  Destroy destroy;
  VkDevice device = VK_NULL_HANDLE;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_records.find(token);
    if (it == m_records.end()) {
      return;
    }
    destroy = std::move(it->second.destroy);
    m_records.erase(it);
    device = m_device;
  }

  if (device != VK_NULL_HANDLE && destroy) {
    destroy(device);
  }
}

void
ResourceRegistry::forget(Token token)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_records.erase(token);
}

void
ResourceRegistry::releaseAll()
{
  std::vector<Record> records;
  VkDevice device = VK_NULL_HANDLE;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_device == VK_NULL_HANDLE) {
      m_records.clear();
      return;
    }

    device = m_device;
    records.reserve(m_records.size());
    for (auto& entry : m_records) {
      records.push_back(std::move(entry.second));
    }
    m_records.clear();
    // Invalidate every outstanding wrapper before invoking callbacks.
    m_device = VK_NULL_HANDLE;
  }

  std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) {
    if (a.phase != b.phase) {
      return a.phase < b.phase;
    }
    // Within a phase, destroy the newest object first. This mirrors normal
    // reverse-construction teardown for otherwise-independent objects.
    return a.token > b.token;
  });

  for (auto& record : records) {
    if (record.destroy) {
      record.destroy(device);
    }
  }
}

bool
ResourceRegistry::isAlive() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_device != VK_NULL_HANDLE;
}

size_t
ResourceRegistry::trackedResourceCount() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_records.size();
}

} // namespace gfxvulkan::resources
