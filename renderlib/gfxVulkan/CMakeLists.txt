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
	"${CMAKE_CURRENT_SOURCE_DIR}/RendererVkContext.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/RendererVkContext.h"
)

target_compile_definitions(renderlib PUBLIC AGAVE_HAS_VULKAN=1)
target_link_libraries(renderlib
	Vulkan::Vulkan
	${AGAVE_VULKAN_RUNTIME_LIBRARY}
	Vulkan::shaderc_combined
)
