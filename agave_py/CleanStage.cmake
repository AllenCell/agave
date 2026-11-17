if(NOT AGAVE_PY_PROJECT_DIR OR NOT AGAVE_PY_PACKAGE_DIR)
  message(FATAL_ERROR "agave_py stage directories were not provided")
endif()

file(GLOB _agave_py_native_modules
  "${AGAVE_PY_PACKAGE_DIR}/_native*.pyd"
  "${AGAVE_PY_PACKAGE_DIR}/_native*.so"
)
file(GLOB _agave_py_vulkan_loaders
  "${AGAVE_PY_PACKAGE_DIR}/libvulkan*.dylib"
)
set(_agave_py_stale_files ${_agave_py_native_modules} ${_agave_py_vulkan_loaders})
if(_agave_py_stale_files)
  file(REMOVE ${_agave_py_stale_files})
endif()
file(REMOVE_RECURSE "${AGAVE_PY_PACKAGE_DIR}/share/vulkan/icd.d")
file(REMOVE_RECURSE "${AGAVE_PY_PROJECT_DIR}/build")
