target_include_directories(renderlib PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}"
)

target_sources(renderlib PRIVATE
"${CMAKE_CURRENT_SOURCE_DIR}/ConvertChannelData.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/ConvertChannelData.h"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReader.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReader.h"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderCCP4.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderCCP4.h"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderCzi.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderCzi.h"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderImageSequence.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderImageSequence.h"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderTIFF.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderTIFF.h"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderZarr.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderZarr.h"
)

# libCZI dependency for renderlib
# add_compile_definitions(_LIBCZISTATICLIB)
set(LIBCZI_DO_NOT_SET_MSVC_RUNTIME_LIBRARY ON)
set(LIBCZI_BUILD_UNITTESTS OFF)
set(LIBCZI_BUILD_DYNLIB ON)
set(LIBCZI_BUILD_CZICMD OFF)
add_subdirectory(libCZI)

# end libczi dependency

# Use these system/vcpkg-provided dependencies instead of tensorstore's bundled ones.
set(TENSORSTORE_USE_SYSTEM_TIFF ON)
set(TENSORSTORE_USE_SYSTEM_ZLIB ON)
set(TENSORSTORE_USE_SYSTEM_CURL ON)
FetchContent_Declare(
	tensorstore
        URL "https://github.com/google/tensorstore/archive/refs/tags/v0.1.84.tar.gz"
        URL_HASH SHA256=D86FE9DCA4B69E5C8B488562351DC654BCE1400F5981F922E4BE9BA4FAFC95CE
)

FetchContent_MakeAvailable(tensorstore)

# dav1d 1.5.x (pulled in by tensorstore via libavif) fails on MSVC 14.52+ with
# C7780 because its compat stdatomic.h uses __has_include_next without defining
# it. Apply upstream dav1d commit a34e0685b9 until a release includes it.
FetchContent_GetProperties(dav1d)
if(MSVC AND dav1d_POPULATED)
  set(_dav1d_stdatomic "${dav1d_SOURCE_DIR}/include/compat/msvc/stdatomic.h")
  if(EXISTS "${_dav1d_stdatomic}")
    file(READ "${_dav1d_stdatomic}" _dav1d_stdatomic_contents)
    string(FIND "${_dav1d_stdatomic_contents}" "#ifndef __has_include_next" _dav1d_already_patched)
    if(_dav1d_already_patched EQUAL -1)
      set(_dav1d_has_include_next_line "#if defined(__clang__) && __has_include_next(<stdatomic.h>)")
      string(REPLACE "${_dav1d_has_include_next_line}"
        "#ifndef __has_include_next\n#define __has_include_next(x) 0\n#endif\n\n${_dav1d_has_include_next_line}"
        _dav1d_stdatomic_contents "${_dav1d_stdatomic_contents}")
      file(WRITE "${_dav1d_stdatomic}" "${_dav1d_stdatomic_contents}")
      message(STATUS "Patched dav1d compat stdatomic.h for newer MSVC")
    endif()
  endif()
endif()
# end tensorstore dependency

target_link_libraries(renderlib
libCZI

tensorstore::tensorstore
# FileReaderZarr uses DimExpression (Dims(...).HalfOpenInterval/Transpose),
# whose implementation libraries are not propagated by the driver targets.
tensorstore::index_space_dim_expression
# FileReaderZarr uses JSON, Zarr v2/v3, and file/HTTP/GCS/S3 kvstores.
# CacheManager uses Zarr v3 with the file kvstore. The Zarr targets include
# their compression codecs and Zarr v3 sharding support transitively.
tensorstore::driver_json
tensorstore::driver_zarr
tensorstore::driver_zarr3
tensorstore::kvstore_file
tensorstore::kvstore_http
tensorstore::kvstore_gcs
tensorstore::kvstore_s3
)

