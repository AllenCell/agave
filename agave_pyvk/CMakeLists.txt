cmake_minimum_required(VERSION 3.24)
project(agave_pyvk VERSION 1.9.0 LANGUAGES CXX)

find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)
add_subdirectory(nanobind)

if(NOT TARGET renderlib)
  message(FATAL_ERROR "agave_pyvk must be configured as part of AGAVE after the renderlib target")
endif()
if(NOT AGAVE_HAS_VULKAN)
  message(FATAL_ERROR "agave_pyvk requires an AGAVE renderlib target with Vulkan enabled")
endif()

nanobind_add_module(agave_py2 NB_STATIC src/bindings.cpp src/VulkanRenderer.cpp)
set_target_properties(agave_py2 PROPERTIES OUTPUT_NAME _native)
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
