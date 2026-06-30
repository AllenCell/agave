target_include_directories(renderlib PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/.."
	"${Vulkan_INCLUDE_DIRS}"
)

target_sources(renderlib PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/Backend.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Backend.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Device.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Device.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Framebuffer.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Framebuffer.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/GestureRenderer.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/GestureRenderer.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/RenderVk.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/RenderVk.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/RenderVkPT.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/RenderVkPT.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/RendererVkContext.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/RendererVkContext.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Swapchain.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Swapchain.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/VolumeTextureVk.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/VolumeTextureVk.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/VulkanUtil.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/VulkanUtil.h"
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
	"${CMAKE_CURRENT_SOURCE_DIR}/shadersrc/make_spirv.py"
)

if(APPLE)
	target_sources(renderlib PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/Swapchain_mac.mm"
	)
endif()

target_compile_definitions(renderlib PUBLIC AGAVE_HAS_VULKAN=1)
target_link_libraries(renderlib
	Vulkan::Vulkan
	${AGAVE_VULKAN_RUNTIME_LIBRARY}
	Vulkan::shaderc_combined
)

if(APPLE)
	target_link_libraries(renderlib
		"-framework AppKit"
		"-framework QuartzCore"
	)
endif()
