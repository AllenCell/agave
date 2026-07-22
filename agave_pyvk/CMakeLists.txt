cmake_minimum_required(VERSION 3.24)
project(agave_pyvk VERSION 1.9.0 LANGUAGES CXX)

find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)
# nanobind caches the ABI extension suffix. Recompute it when an existing CMake
# build tree is deliberately reconfigured for another Python interpreter.
unset(NB_SUFFIX CACHE)
unset(NB_SUFFIX_S CACHE)
add_subdirectory(nanobind)

if(NOT TARGET renderlib)
  message(FATAL_ERROR "agave_pyvk must be configured as part of AGAVE after the renderlib target")
endif()
if(NOT AGAVE_HAS_VULKAN)
  message(FATAL_ERROR "agave_pyvk requires an AGAVE renderlib target with Vulkan enabled")
endif()

nanobind_add_module(agave_py2 NB_STATIC src/bindings.cpp src/VulkanRenderer.cpp)
set_target_properties(agave_py2 PROPERTIES
  OUTPUT_NAME _native
)
if(APPLE)
  # Keep bundled dylibs discoverable after the extension is staged into the
  # Python package. The build-tree rpaths point at the Vulkan SDK and are
  # intentionally removed by CMake during installation.
  set_target_properties(agave_py2 PROPERTIES
    INSTALL_RPATH "@loader_path"
  )
endif()
target_include_directories(agave_py2 PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(agave_py2 PRIVATE renderlib)

if(WIN32)
  install(TARGETS agave_py2
          RUNTIME_DEPENDENCIES
          DIRECTORIES "$<TARGET_FILE_DIR:agave_py2>"
          PRE_EXCLUDE_REGEXES "api-ms-" "ext-ms-" "python[0-9]+\\.dll"
          POST_EXCLUDE_REGEXES ".*system32/.*\\.dll"
          LIBRARY DESTINATION agave_pyvk
          COMPONENT agave_pyvk
          RUNTIME DESTINATION agave_pyvk
          COMPONENT agave_pyvk)
else()
  install(TARGETS agave_py2
          LIBRARY DESTINATION agave_pyvk
          COMPONENT agave_pyvk
          RUNTIME DESTINATION agave_pyvk
          COMPONENT agave_pyvk)
endif()
if(APPLE)
  # Vulkan::Vulkan normally resolves to the SDK's unversioned loader symlink,
  # while its Mach-O install name is @rpath/libvulkan.1.dylib. Install the
  # complete loader symlink chain so both names and the real file are present.
  get_filename_component(_agave_pyvk_vulkan_library_dir "${Vulkan_LIBRARY}" DIRECTORY)
  file(GLOB _agave_pyvk_vulkan_loader_files
       "${_agave_pyvk_vulkan_library_dir}/libvulkan*.dylib")
  install(FILES ${_agave_pyvk_vulkan_loader_files}
          DESTINATION agave_pyvk
          COMPONENT agave_pyvk)
endif()
if(TARGET libCZI)
  install(TARGETS libCZI
          LIBRARY DESTINATION agave_pyvk
          COMPONENT agave_pyvk
          RUNTIME DESTINATION agave_pyvk
          COMPONENT agave_pyvk)
endif()
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../renderlib/assets/"
        DESTINATION agave_pyvk/assets
        COMPONENT agave_pyvk)

set(AGAVE_PYVK_STAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
add_custom_target(stage_agave_pyvk ALL
  COMMAND "${CMAKE_COMMAND}"
          "-DAGAVE_PYVK_PROJECT_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
          "-DAGAVE_PYVK_PACKAGE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/agave_pyvk"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/CleanStage.cmake"
  COMMAND "${CMAKE_COMMAND}"
          --install "${CMAKE_BINARY_DIR}"
          --config "$<CONFIG>"
          --component agave_pyvk
          --prefix "${AGAVE_PYVK_STAGE_DIR}"
  DEPENDS agave_py2
  COMMENT "Staging agave_pyvk native package files"
  VERBATIM
)
