target_include_directories(renderlib PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}"
)

add_subdirectory(stb)

target_sources(renderlib PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/IRenderWindow.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/IRenderWindow.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/RenderGL.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/RenderGL.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/RenderGLPT.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/RenderGLPT.h"
)
