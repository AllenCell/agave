add_subdirectory(glad/src)
add_subdirectory(glsl)

target_include_directories(renderlib PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/glad/include"
)

target_sources(renderlib PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/Backend.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Backend.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Device.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Device.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Framebuffer.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Framebuffer.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/FontGL.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/FontGL.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/FSQ.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/FSQ.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/GestureGraphicsGL.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/GestureGraphicsGL.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/GestureRenderer.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/GestureRenderer.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLContext.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLContext.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/HeadlessGLContext.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/HeadlessGLContext.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Image2D.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Image2D.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Image3D.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Image3D.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/RendererGLContext.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/RendererGLContext.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Util.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Util.h"
)

target_link_libraries(renderlib
	Qt::Core Qt::OpenGL
	${OPENGL_egl_LIBRARY}
)
