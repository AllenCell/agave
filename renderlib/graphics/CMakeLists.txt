target_include_directories(renderlib PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}"
)

add_subdirectory(glsl)
add_subdirectory(stb)

target_sources(renderlib PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/Framebuffer.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Framebuffer.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/GestureGraphicsGL.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/GestureGraphicsGL.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/ImageXyzcGpu.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/ImageXyzcGpu.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/IRenderWindow.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/IRenderWindow.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/RenderGL.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/RenderGL.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/RenderGLPT.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/RenderGLPT.h"
)
