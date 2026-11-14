target_include_directories(renderlib PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}"
)

target_sources(renderlib PRIVATE
"${CMAKE_CURRENT_SOURCE_DIR}/ConvertChannelData.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/ConvertChannelData.h"
"${CMAKE_CURRENT_SOURCE_DIR}/BlockingFileReader.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/BlockingFileReader.h"
"${CMAKE_CURRENT_SOURCE_DIR}/ApplyTimeStepToScene.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/ApplyTimeStepToScene.h"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReader.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReader.h"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderCCP4.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderCCP4.h"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderCzi.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderCzi.h"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderDicom.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderDicom.h"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderImageSequence.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderImageSequence.h"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderTIFF.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderTIFF.h"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderTestVolume.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderTestVolume.h"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderZarr.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/FileReaderZarr.h"
"${CMAKE_CURRENT_SOURCE_DIR}/LoadRequest.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/LoadRequest.h"
"${CMAKE_CURRENT_SOURCE_DIR}/TimeSeriesLoader.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/TimeSeriesLoader.h"
"${CMAKE_CURRENT_SOURCE_DIR}/TimeSeriesPlayer.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/TimeSeriesPlayer.h"
)

# libCZI dependency for renderlib
# add_compile_definitions(_LIBCZISTATICLIB)
set(LIBCZI_DO_NOT_SET_MSVC_RUNTIME_LIBRARY ON)
set(LIBCZI_BUILD_UNITTESTS OFF)
set(LIBCZI_BUILD_DYNLIB ON)
set(LIBCZI_BUILD_CZICMD OFF)
# EXCLUDE_FROM_ALL also suppresses install rules below this directory, which is
# how the zstd libCZI fetches is kept out of the Agave install tree. The libCZI
# shared library itself is installed explicitly by agave_app and agave_pyvk.
add_subdirectory(libCZI EXCLUDE_FROM_ALL)

# end libczi dependency

# Build only the DCMTK modules needed for grayscale image decoding.  The JPEG
# and JPEG-LS modules also link to dcmimage, even for grayscale images.
set(DCMTK_MODULES "ofstd;oflog;oficonv;dcmdata;dcmimgle;dcmimage;dcmjpeg;dcmjpls" CACHE STRING "DCMTK modules" FORCE)
set(DCMTK_DEFAULT_DICT builtin CACHE STRING "Embed the DCMTK dictionary for standalone Agave builds" FORCE)
set(BUILD_APPS OFF CACHE BOOL "Build DCMTK command line applications" FORCE)
set(DCMTK_WITH_OPENSSL OFF CACHE BOOL "DCMTK TLS support" FORCE)
set(DCMTK_WITH_ICONV OFF CACHE BOOL "DCMTK iconv support" FORCE)
set(DCMTK_WITH_ICU OFF CACHE BOOL "DCMTK ICU support" FORCE)
set(DCMTK_WITH_PNG OFF CACHE BOOL "DCMTK PNG support" FORCE)
set(DCMTK_WITH_TIFF OFF CACHE BOOL "DCMTK TIFF support" FORCE)
set(DCMTK_WITH_XML OFF CACHE BOOL "DCMTK XML support" FORCE)
set(DCMTK_WITH_OPENJPEG OFF CACHE BOOL "DCMTK JPEG 2000 support" FORCE)
set(DCMTK_WITH_DOXYGEN OFF CACHE BOOL "DCMTK API documentation" FORCE)
# DCMTK is statically linked into renderlib, so nothing it installs (headers,
# docs, pkgconfig, export files, static libs) belongs in the Agave install tree.
# EXCLUDE_FROM_ALL suppresses those install rules.
FetchContent_Declare(
  dcmtk
  URL "https://github.com/DCMTK/dcmtk/archive/refs/tags/DCMTK-3.7.0.tar.gz"
  URL_HASH SHA256=5bb3ec8317dc465788bed2ca789e76d03ae5848c9381cce3b14c1a3f8b6aca56
  EXCLUDE_FROM_ALL
)
set(DCMTK_COMPILE_WIN32_MULTITHREADED_DLL ON CACHE BOOL
    "Compile DCMTK using the DLL runtime" FORCE)
FetchContent_MakeAvailable(dcmtk)

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
 dcmdata dcmimgle dcmimage dcmjpeg dcmjpls

tensorstore::tensorstore tensorstore::all_drivers
)
