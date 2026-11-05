target_include_directories(renderlib PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_sources(renderlib PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/Backend.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Descriptors.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Handles.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/IGraphicsDevice.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/IGestureRenderer.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/IRenderWindow.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/IRenderWindow.h"
)
