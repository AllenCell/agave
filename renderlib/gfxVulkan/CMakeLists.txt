# This PR compiles the device before the Vulkan backend is wired into renderlib.
# Keep it out of the OpenGL application's link and packaging dependencies.
add_library(agave_vulkan_device_check OBJECT
	"${CMAKE_CURRENT_SOURCE_DIR}/Backend.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Backend.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Device.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Device.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Framebuffer.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Framebuffer.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/RendererVkContext.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/RendererVkContext.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Swapchain.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Swapchain.h"
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
