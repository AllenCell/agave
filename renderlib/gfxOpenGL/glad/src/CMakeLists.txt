target_include_directories(renderlib PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}"
)
target_sources(renderlib PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/glad.c"
)
