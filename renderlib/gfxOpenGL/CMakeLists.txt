target_sources(renderlib PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/Backend.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Backend.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Device.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Device.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLContext.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLContext.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/HeadlessGLContext.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/HeadlessGLContext.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/RendererGLContext.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/RendererGLContext.cpp"
)
