target_include_directories(renderlib PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}"
)
target_sources(renderlib PRIVATE
"${CMAKE_CURRENT_SOURCE_DIR}/stb_image.h"
"${CMAKE_CURRENT_SOURCE_DIR}/stb_image_write.h"
"${CMAKE_CURRENT_SOURCE_DIR}/stb_truetype.h"
)
