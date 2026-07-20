cmake_minimum_required(VERSION 3.24)
include(FetchContent)  # Needed to recognize FetchContent_Declare in renderlib

if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Supported macOS architecture" FORCE)
  if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    if(DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
      set(CMAKE_OSX_DEPLOYMENT_TARGET "$ENV{MACOSX_DEPLOYMENT_TARGET}" CACHE STRING "Minimum OS X deployment version")
    else()
      set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum OS X deployment version")
    endif()
  endif()
  # Ensure compile_commands.json has correct sysroot for clang-tidy
  if(NOT CMAKE_OSX_SYSROOT)
    execute_process(
      COMMAND xcrun --show-sdk-path
      OUTPUT_VARIABLE _osx_sysroot
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE _xcrun_result
    )
    if(_xcrun_result EQUAL 0 AND _osx_sysroot)
      set(CMAKE_OSX_SYSROOT "${_osx_sysroot}" CACHE PATH "macOS SDK path")
    endif()
  endif()
endif(APPLE)

if(POLICY CMP0048)
  cmake_policy(SET CMP0048 NEW)
endif(POLICY CMP0048)

cmake_policy(SET CMP0091 NEW) # enable new "MSVC runtime library selection" (https://cmake.org/cmake/help/latest/variable/CMAKE_MSVC_RUNTIME_LIBRARY.html)
cmake_policy(SET CMP0135 NEW) # set timestamps to time of extraction for ExternalProject_Add
if(POLICY CMP0207)
  cmake_policy(SET CMP0207 NEW) # file(GET_RUNTIME_DEPENDENCIES) normalizes paths before matching
endif()

project(
  agave
  VERSION 1.10.0
  LANGUAGES C CXX
)

option(AGAVE_BUILD_APP "Build the Qt desktop application and installers" ON)
option(AGAVE_BUILD_TESTS "Build the C++ test suite" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

IF(WIN32)
ELSE()
  # most of this is for libCZI (?):
  # SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -std=c++17 -fPIC -O0 -g -D_FILE_OFFSET_BITS=64 -fvisibility=hidden")
  # SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D__ANSI__ -fPIC -D_FILE_OFFSET_BITS=64")
  add_compile_definitions(LINUXENV)
ENDIF(WIN32)

# Add paths to our CMake code to the module path, so they can be found automatically by
# CMake.
set(CMAKE_MODULE_PATH
  "${CMAKE_SOURCE_DIR}/CMake"
  ${CMAKE_MODULE_PATH}
)

# Find includes in corresponding build directories
# set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(UNIX AND NOT APPLE)
  set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
  set(CMAKE_INSTALL_RPATH "\$ORIGIN")
endif()

find_package(spdlog REQUIRED)

# if(MSVC)
# Debug library suffix.
# set(CMAKE_DEBUG_POSTFIX "d")
# To prevent a lot of spam warnings about standard POSIX functions
# and unsafe use of the standard library.
add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_SCL_SECURE_NO_WARNINGS)

# endif()

FetchContent_Declare(
            glm
            GIT_REPOSITORY https://github.com/g-truc/glm.git
            GIT_TAG 1.0.1
    )
FetchContent_MakeAvailable(glm)

find_package(TIFF REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)

find_package(OpenGL)

option(AGAVE_ENABLE_VULKAN "Build the Vulkan graphics backend when the Vulkan SDK is available." ON)
option(AGAVE_BUILD_PYVK "Build the headless Vulkan Python package" ON)
set(AGAVE_HAS_VULKAN OFF)
set(AGAVE_VULKAN_RUNTIME_LIBRARY "")
if(AGAVE_ENABLE_VULKAN)
  set(AGAVE_VULKAN_SDK_ROOT "$ENV{VULKAN_SDK}" CACHE PATH "Path to the Vulkan SDK platform directory")
  if(NOT AGAVE_VULKAN_SDK_ROOT)
    # No VULKAN_SDK env var and no cached value: look for a locally-installed
    # SDK in the platform's default install root and pick the highest version.
    if(WIN32)
      set(_agave_vulkan_search_glob "C:/VulkanSDK/*")
      set(_agave_vulkan_platform_subdir "")
    elseif(APPLE)
      set(_agave_vulkan_search_glob "$ENV{HOME}/VulkanSDK/*")
      set(_agave_vulkan_platform_subdir "macOS")
    else()
      set(_agave_vulkan_search_glob "$ENV{HOME}/VulkanSDK/*")
      set(_agave_vulkan_platform_subdir "x86_64")
    endif()
    file(GLOB _agave_vulkan_candidates LIST_DIRECTORIES true "${_agave_vulkan_search_glob}")
    list(SORT _agave_vulkan_candidates)
    list(REVERSE _agave_vulkan_candidates)
    foreach(_agave_vulkan_candidate IN LISTS _agave_vulkan_candidates)
      if(_agave_vulkan_platform_subdir)
        set(_agave_vulkan_candidate "${_agave_vulkan_candidate}/${_agave_vulkan_platform_subdir}")
      endif()
      if(IS_DIRECTORY "${_agave_vulkan_candidate}" AND EXISTS "${_agave_vulkan_candidate}/include/vulkan/vulkan.h")
        set(AGAVE_VULKAN_SDK_ROOT "${_agave_vulkan_candidate}" CACHE PATH "Path to the Vulkan SDK platform directory" FORCE)
        break()
      endif()
    endforeach()
    unset(_agave_vulkan_search_glob)
    unset(_agave_vulkan_platform_subdir)
    unset(_agave_vulkan_candidates)
    unset(_agave_vulkan_candidate)
  endif()
  if(AGAVE_VULKAN_SDK_ROOT)
    set(ENV{VULKAN_SDK} "${AGAVE_VULKAN_SDK_ROOT}")
    list(INSERT CMAKE_PREFIX_PATH 0 "${AGAVE_VULKAN_SDK_ROOT}")
    if(EXISTS "${AGAVE_VULKAN_SDK_ROOT}/include/vulkan/vulkan.h")
      set(Vulkan_INCLUDE_DIR "${AGAVE_VULKAN_SDK_ROOT}/include" CACHE PATH "Vulkan SDK include directory" FORCE)
    endif()
    if(EXISTS "${AGAVE_VULKAN_SDK_ROOT}/bin/glslc")
      set(Vulkan_GLSLC_EXECUTABLE "${AGAVE_VULKAN_SDK_ROOT}/bin/glslc" CACHE FILEPATH "Vulkan glslc executable" FORCE)
    endif()
    if(EXISTS "${AGAVE_VULKAN_SDK_ROOT}/bin/glslangValidator")
      set(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE
          "${AGAVE_VULKAN_SDK_ROOT}/bin/glslangValidator"
          CACHE FILEPATH
          "Vulkan glslangValidator executable"
          FORCE)
    endif()
    if(WIN32 AND EXISTS "${AGAVE_VULKAN_SDK_ROOT}/Lib/vulkan-1.lib")
      set(Vulkan_LIBRARY "${AGAVE_VULKAN_SDK_ROOT}/Lib/vulkan-1.lib" CACHE FILEPATH "Vulkan loader library" FORCE)
    elseif(APPLE AND EXISTS "${AGAVE_VULKAN_SDK_ROOT}/lib/libvulkan.dylib")
      set(Vulkan_LIBRARY "${AGAVE_VULKAN_SDK_ROOT}/lib/libvulkan.dylib" CACHE FILEPATH "Vulkan loader library" FORCE)
    elseif(EXISTS "${AGAVE_VULKAN_SDK_ROOT}/lib/libvulkan.so")
      set(Vulkan_LIBRARY "${AGAVE_VULKAN_SDK_ROOT}/lib/libvulkan.so" CACHE FILEPATH "Vulkan loader library" FORCE)
    endif()
  endif()

  file(GLOB _agave_vulkan_glslc_candidates
    "${AGAVE_VULKAN_SDK_ROOT}/bin/glslc*"
    "${AGAVE_VULKAN_SDK_ROOT}/Bin/glslc*"
  )
  file(GLOB _agave_vulkan_shaderc_candidates
    "${AGAVE_VULKAN_SDK_ROOT}/lib/*shaderc*"
    "${AGAVE_VULKAN_SDK_ROOT}/Lib/*shaderc*"
  )
  file(GLOB _agave_vulkan_header_candidates
    "${AGAVE_VULKAN_SDK_ROOT}/include/vulkan/vulkan.h"
    "${AGAVE_VULKAN_SDK_ROOT}/Include/vulkan/vulkan.h"
  )
  file(GLOB _agave_vulkan_loader_candidates
    "${AGAVE_VULKAN_SDK_ROOT}/lib/libvulkan*"
    "${AGAVE_VULKAN_SDK_ROOT}/Lib/vulkan*"
  )
  message(STATUS "Vulkan diagnostics before find_package:")
  message(STATUS "  VULKAN_SDK environment: $ENV{VULKAN_SDK}")
  message(STATUS "  AGAVE_VULKAN_SDK_ROOT: ${AGAVE_VULKAN_SDK_ROOT}")
  message(STATUS "  Vulkan header files in SDK: ${_agave_vulkan_header_candidates}")
  message(STATUS "  Vulkan loader files in SDK: ${_agave_vulkan_loader_candidates}")
  message(STATUS "  glslc files in SDK: ${_agave_vulkan_glslc_candidates}")
  message(STATUS "  shaderc files in SDK: ${_agave_vulkan_shaderc_candidates}")

  find_package(Vulkan COMPONENTS glslc shaderc_combined QUIET)

  if(TARGET Vulkan::shaderc_combined)
    set(_agave_vulkan_shaderc_target_found TRUE)
  else()
    set(_agave_vulkan_shaderc_target_found FALSE)
  endif()
  message(STATUS "Vulkan diagnostics after find_package:")
  message(STATUS "  Vulkan_FOUND: ${Vulkan_FOUND}")
  message(STATUS "  Vulkan_VERSION: ${Vulkan_VERSION}")
  message(STATUS "  Vulkan_INCLUDE_DIR: ${Vulkan_INCLUDE_DIR}")
  message(STATUS "  Vulkan_INCLUDE_DIRS: ${Vulkan_INCLUDE_DIRS}")
  message(STATUS "  Vulkan_LIBRARY: ${Vulkan_LIBRARY}")
  message(STATUS "  Vulkan_LIBRARIES: ${Vulkan_LIBRARIES}")
  message(STATUS "  Vulkan_glslc_FOUND: ${Vulkan_glslc_FOUND}")
  message(STATUS "  Vulkan_GLSLC_EXECUTABLE: ${Vulkan_GLSLC_EXECUTABLE}")
  message(STATUS "  Vulkan_shaderc_combined_FOUND: ${Vulkan_shaderc_combined_FOUND}")
  message(STATUS "  Vulkan_shaderc_combined_LIBRARY: ${Vulkan_shaderc_combined_LIBRARY}")
  message(STATUS "  Vulkan::shaderc_combined target exists: ${_agave_vulkan_shaderc_target_found}")

  if(Vulkan_FOUND AND TARGET Vulkan::shaderc_combined)
    set(AGAVE_HAS_VULKAN ON)
    set(AGAVE_VULKAN_RUNTIME_LIBRARY "${Vulkan_LIBRARY}")
    if(APPLE)
      find_library(AGAVE_VULKAN_KOSMICKRISP_LIBRARY
        NAMES vulkan_kosmickrisp
        PATHS
          "${AGAVE_VULKAN_SDK_ROOT}/lib"
          "$ENV{VULKAN_SDK}/lib"
          "${Vulkan_LIBRARY_DIR}"
        NO_DEFAULT_PATH
      )
      if(AGAVE_VULKAN_KOSMICKRISP_LIBRARY)
        set(AGAVE_VULKAN_RUNTIME_LIBRARY "${AGAVE_VULKAN_KOSMICKRISP_LIBRARY}")
        message(STATUS "Using macOS Vulkan runtime: ${AGAVE_VULKAN_RUNTIME_LIBRARY}")
      else()
        message(WARNING "libvulkan_kosmickrisp.dylib was not found; falling back to ${Vulkan_LIBRARY}")
      endif()
    endif()
    message(STATUS "Vulkan backend enabled: SDK ${Vulkan_VERSION}")
  elseif(AGAVE_BUILD_PYVK)
    message(FATAL_ERROR
      "AGAVE_BUILD_PYVK requires Vulkan, glslc, and shaderc_combined. "
      "See the Vulkan diagnostics above for the missing component."
    )
  else()
    message(WARNING "AGAVE_ENABLE_VULKAN is ON, but a required Vulkan component was not found. Vulkan backend disabled.")
  endif()
  unset(_agave_vulkan_glslc_candidates)
  unset(_agave_vulkan_shaderc_candidates)
  unset(_agave_vulkan_header_candidates)
  unset(_agave_vulkan_loader_candidates)
  unset(_agave_vulkan_shaderc_target_found)
endif()

# normally CMAKE_INSTALL_PREFIX is meant to be outside of the build tree
if("${CMAKE_BUILD_TYPE}" STREQUAL "")
  set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/install)
else()
  set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/install/${CMAKE_BUILD_TYPE})
endif()
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# #####################
# THE COMMON CORE LIBRARIES
# #####################
add_subdirectory(renderlib)

if(AGAVE_BUILD_PYVK)
  add_subdirectory(agave_pyvk)
endif()

set(INSTALLDIR "${CMAKE_PROJECT_NAME}-install")

message(STATUS "CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "INSTALLDIR: ${INSTALLDIR}")
message(STATUS "CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")
message(STATUS "CMAKE_LIBRARY_OUTPUT_DIRECTORY: ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message(STATUS "CMAKE_RUNTIME_OUTPUT_DIRECTORY: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# #####################
# UNIT TESTING
# #####################
if(AGAVE_BUILD_TESTS)
add_subdirectory(test)
endif()

# #####################
# THE FRONT END QT EXE
# #####################
if(AGAVE_BUILD_APP)
  add_subdirectory(agave_app)

  # #####################
  # Documentation
  # #####################
  CONFIGURE_FILE(${CMAKE_SOURCE_DIR}/CMake/conf.py.cmake ${CMAKE_SOURCE_DIR}/docs/conf.py @ONLY)
endif()


