set(GLAD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/graphics/glad/include")

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/version.h.in"
	"${CMAKE_CURRENT_SOURCE_DIR}/version.h"
)

add_library(renderlib "${CMAKE_CURRENT_SOURCE_DIR}/renderlib.cpp")
set_property(TARGET renderlib PROPERTY AUTOMOC ON)

target_include_directories(renderlib PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${glm_SOURCE_DIR}"
	${GLAD_DIR}
)
target_sources(renderlib PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/AppScene.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/AppScene.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/AreaLightTool.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/AreaLightTool.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/AxisHelperTool.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/AxisHelperTool.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/BoundingBox.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/BoundingBox.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/BoundingBoxTool.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/BoundingBoxTool.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/CacheConfig.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/CacheManager.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/CacheManager.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/CCamera.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/CCamera.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/ClipPlaneTool.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/ClipPlaneTool.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Colormap.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Colormap.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/command.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/command.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Flags.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Flags.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Font.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Font.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Fuse.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Fuse.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/GradientData.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/GradientData.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Histogram.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Histogram.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/IFileReader.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/ImageXYZC.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/ImageXYZC.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Light.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Light.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Lighting.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Lighting.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Logging.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Logging.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Manipulator.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Manipulator.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/MathUtil.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/MathUtil.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/MoveTool.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/MoveTool.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Object3d.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Object3d.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Origins.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Origins.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/RenderSettings.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/RenderSettings.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/RotateTool.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/RotateTool.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/ScaleBarTool.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/ScaleBarTool.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/SceneLight.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/SceneLight.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/ScenePlane.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/ScenePlane.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/SceneView.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/SceneView.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/SphereLightTool.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/SphereLightTool.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Status.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Status.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/StringUtil.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/StringUtil.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/SystemInfo.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/SystemInfo.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/threading.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/threading.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Timeline.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Timeline.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Timing.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Timing.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/TimeStampTool.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/TimeStampTool.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/tiny_obj_loader.cc"
	"${CMAKE_CURRENT_SOURCE_DIR}/tiny_obj_loader.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/version.hpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/ViewerWindow.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/ViewerWindow.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/VolumeDimensions.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/VolumeDimensions.h"
)
add_subdirectory(gesture)
add_subdirectory(gfxapi)
add_subdirectory(graphics)
add_subdirectory(gfxOpenGL)
add_subdirectory(io)
add_subdirectory(pugixml)

target_link_libraries(renderlib
	${CMAKE_DL_LIBS}
	${TIFF_LIBRARIES}
	spdlog::spdlog_header_only
)

IF(WIN32)
	target_link_libraries(renderlib glm::glm)
ENDIF(WIN32)
