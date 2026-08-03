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
cmake_policy(SET CMP0207 NEW) # file(GET_RUNTIME_DEPENDENCIES) normalizes paths before matching

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

# this is my find_package(Vulkan) with some extra checks
include("${CMAKE_CURRENT_SOURCE_DIR}/CMake/ConfigureVulkan.cmake")

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

add_subdirectory(agave_pyvk)

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


