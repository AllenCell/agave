#pragma once

#include <cstdint>

namespace gfxApi {

// Opaque, backend-agnostic resource handles. A backend's IGraphicsDevice
// is the only thing that knows how to translate these into concrete GPU
// objects. Value 0 is reserved as "invalid handle".
//
// Distinct strong types are used (instead of a single uint64_t alias) so the
// compiler catches passing a TextureHandle where a BufferHandle is expected.

namespace detail {
template<typename Tag>
struct Handle
{
  uint64_t id = 0;

  constexpr bool isValid() const { return id != 0; }
  constexpr bool operator==(const Handle& other) const { return id == other.id; }
  constexpr bool operator!=(const Handle& other) const { return id != other.id; }
};
} // namespace detail

struct ShaderTag;
struct ShaderProgramTag;
struct TextureTag;
struct BufferTag;
struct FramebufferTag;
struct SamplerTag;

using ShaderHandle = detail::Handle<ShaderTag>;
using ShaderProgramHandle = detail::Handle<ShaderProgramTag>;
using TextureHandle = detail::Handle<TextureTag>;
using BufferHandle = detail::Handle<BufferTag>;
using FramebufferHandle = detail::Handle<FramebufferTag>;
using SamplerHandle = detail::Handle<SamplerTag>;

} // namespace gfxApi
