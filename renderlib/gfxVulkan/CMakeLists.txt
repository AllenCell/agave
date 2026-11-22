# This PR compiles the device before the Vulkan backend is wired into renderlib.
# Keep it out of the OpenGL application's link and packaging dependencies.
add_library(agave_vulkan_device_check OBJECT
	"${CMAKE_CURRENT_SOURCE_DIR}/Backend.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Backend.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Device.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Device.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Framebuffer.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Framebuffer.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/GestureRenderer.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/GestureRenderer.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/RendererVkContext.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/RendererVkContext.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/RenderVk.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/RenderVk.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/RenderVkPT.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/RenderVkPT.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Swapchain.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Swapchain.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/VulkanUtil.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/VulkanUtil.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/VolumeTextureVk.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/VolumeTextureVk.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/basicVolume.frag"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/basicVolume.vert"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/basicVolume_frag_spv.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/basicVolume_vert_spv.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/copy.frag"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/copy.vert"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/copy_frag_spv.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/copy_vert_spv.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/flat.frag"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/flat.vert"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/flat_frag_spv.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/flat_vert_spv.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/gui.frag"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/gui.vert"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/gui_frag_spv.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/gui_vert_spv.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/imageNoLut.frag"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/imageNoLut.vert"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/imageNoLut_frag_spv.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/imageNoLut_vert_spv.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/make_spirv.py"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/pathTraceVolume.frag"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/pathTraceVolume.vert"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/pathTraceVolume_frag_spv.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/pathTraceVolume_vert_spv.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/ptAccum.frag"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/ptAccum.vert"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/ptAccum_frag_spv.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/ptAccum_vert_spv.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/thickLines.frag"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/thickLines.vert"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/thickLines_frag_spv.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/thickLines_vert_spv.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/toneMap.frag"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/toneMap.vert"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/toneMap_frag_spv.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/toneMap_vert_spv.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/volume.frag"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/volume.vert"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/volume_frag_spv.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/volume_vert_spv.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/resources/Buffer.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/resources/DeviceObject.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/resources/Image.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/resources/ResourceRegistry.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/resources/ResourceRegistry.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/resources/SampledImage.h"
)

target_include_directories(agave_vulkan_device_check PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${glm_SOURCE_DIR}"
)

target_link_libraries(agave_vulkan_device_check PRIVATE
    Vulkan::Vulkan
    Vulkan::shaderc_combined
    spdlog::spdlog_header_only
)

target_compile_definitions(agave_vulkan_device_check PRIVATE AGAVE_HAS_VULKAN=1)

if(WIN32)
	target_sources(agave_vulkan_device_check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Swapchain_win.cpp")
elseif(APPLE)
	enable_language(OBJCXX)
	target_sources(agave_vulkan_device_check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Swapchain_mac.mm")
else()
	target_sources(agave_vulkan_device_check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Swapchain_linux.cpp")
endif()
