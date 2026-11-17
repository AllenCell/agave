cmake_minimum_required(VERSION 3.28)
project(agave_py VERSION 1.10.0 LANGUAGES CXX)

if(POLICY CMP0207)
  cmake_policy(SET CMP0207 NEW)
endif()

find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)
# nanobind caches the ABI extension suffix. Recompute it when an existing CMake
# build tree is deliberately reconfigured for another Python interpreter.
unset(NB_SUFFIX CACHE)
unset(NB_SUFFIX_S CACHE)
add_subdirectory(nanobind)

if(NOT TARGET renderlib)
  message(FATAL_ERROR "agave_py must be configured as part of AGAVE after the renderlib target")
endif()
if(NOT AGAVE_HAS_VULKAN)
  message(FATAL_ERROR "agave_py requires an AGAVE renderlib target with Vulkan enabled")
endif()

nanobind_add_module(agave_py_native NB_STATIC src/bindings.cpp)
set_target_properties(agave_py_native PROPERTIES
  OUTPUT_NAME _native
)
if(APPLE)
  # Keep bundled dylibs discoverable after the extension is staged into the
  # Python package. The build-tree rpaths point at the Vulkan SDK and are
  # intentionally removed by CMake during installation.
  set_target_properties(agave_py_native PROPERTIES
    INSTALL_RPATH "@loader_path"
  )
endif()
target_include_directories(agave_py_native PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(agave_py_native PRIVATE renderlib)

if(WIN32)
  install(TARGETS agave_py_native
          RUNTIME_DEPENDENCIES
          DIRECTORIES "$<TARGET_FILE_DIR:agave_py_native>"
          PRE_EXCLUDE_REGEXES "api-ms-" "ext-ms-" "python[0-9]+\\.dll"
          POST_EXCLUDE_REGEXES ".*system32/.*\\.dll"
          LIBRARY DESTINATION agave_py
          COMPONENT agave_py
          RUNTIME DESTINATION agave_py
          COMPONENT agave_py)
else()
  install(TARGETS agave_py_native
          LIBRARY DESTINATION agave_py
          COMPONENT agave_py
          RUNTIME DESTINATION agave_py
          COMPONENT agave_py)
endif()
if(APPLE)
  agave_install_macos_vulkan(agave_py agave_py/share/vulkan/icd.d
                            COMPONENT agave_py)
endif()
if(TARGET libCZI)
  install(TARGETS libCZI
          LIBRARY DESTINATION agave_py
          COMPONENT agave_py
          RUNTIME DESTINATION agave_py
          COMPONENT agave_py)
endif()
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../renderlib/assets/"
        DESTINATION agave_py/assets
        COMPONENT agave_py)

set(AGAVE_PY_STAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
add_custom_target(stage_agave_py ALL
  COMMAND "${CMAKE_COMMAND}"
          "-DAGAVE_PY_PROJECT_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
          "-DAGAVE_PY_PACKAGE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/agave_py"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/CleanStage.cmake"
  COMMAND "${CMAKE_COMMAND}"
          --install "${CMAKE_BINARY_DIR}"
          --config "$<CONFIG>"
          --component agave_py
          --prefix "${AGAVE_PY_STAGE_DIR}"
  DEPENDS agave_py_native
  COMMENT "Staging agave_py native package files"
  VERBATIM
)
