target_include_directories(renderlib PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}"
)
target_sources(renderlib PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/GLBasicVolumeShader.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLBasicVolumeShader.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLCopyShader.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLCopyShader.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLGuiShader.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLGuiShader.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLPTVolumeShader.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLPTVolumeShader.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLThickLines.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLThickLines.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLToneMapShader.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLToneMapShader.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLFlatShader2D.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLFlatShader2D.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLImageShader2DnoLut.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLImageShader2DnoLut.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/shaders.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/shaders.h"
)
add_subdirectory(shadersrc)
