#pragma once

#include "Image.h"

namespace gfxvulkan::resources {

// Convenience ownership bundle for the common case where one image, one view,
// and one sampler intentionally share a lifetime. Sampler remains a standalone
// primitive so callers can instead reuse a sampler across multiple views.
class SampledImage
{
public:
  SampledImage() = default;
  SampledImage(Image image, UniqueImageView view, UniqueSampler sampler)
    : m_image(std::move(image))
    , m_view(std::move(view))
    , m_sampler(std::move(sampler))
  {
  }

  SampledImage(SampledImage&&) noexcept = default;
  SampledImage& operator=(SampledImage&&) noexcept = default;
  SampledImage(const SampledImage&) = delete;
  SampledImage& operator=(const SampledImage&) = delete;

  VkImage image() const { return m_image.get(); }
  VkDeviceMemory memory() const { return m_image.memory(); }
  VkImageView view() const { return m_view.get(); }
  VkSampler sampler() const { return m_sampler.get(); }
  bool valid() const { return m_image && m_view && m_sampler; }
  explicit operator bool() const { return valid(); }

  void replaceSampler(UniqueSampler sampler) { m_sampler = std::move(sampler); }

  void reset()
  {
    m_sampler.reset();
    m_view.reset();
    m_image.reset();
  }

private:
  Image m_image;
  UniqueImageView m_view;
  UniqueSampler m_sampler;
};

} // namespace gfxvulkan::resources
