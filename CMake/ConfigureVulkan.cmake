# Unconditionally configure the Vulkan SDK used by renderlib and agave_pyvk.
# This file is included at top-level scope so its targets and variables remain
# available to the renderlib and Python subdirectories.

set(AGAVE_HAS_VULKAN OFF)
set(AGAVE_VULKAN_RUNTIME_LIBRARY "")
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

find_package(Vulkan COMPONENTS glslc shaderc_combined)

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
else()
  message(FATAL_ERROR
    "AGAVE requires Vulkan, glslc, and shaderc_combined. "
    "See the Vulkan diagnostics above for the missing component."
  )
endif()
unset(_agave_vulkan_glslc_candidates)
unset(_agave_vulkan_shaderc_candidates)
unset(_agave_vulkan_header_candidates)
unset(_agave_vulkan_loader_candidates)
unset(_agave_vulkan_shaderc_target_found)
