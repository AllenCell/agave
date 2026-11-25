target_sources(renderlib PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/Device.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Device.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/resources/Buffer.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/resources/DeviceObject.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/resources/Image.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/resources/ResourceRegistry.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/resources/ResourceRegistry.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/resources/SampledImage.h"
)

target_link_libraries(renderlib Vulkan::Vulkan Vulkan::shaderc_combined)
