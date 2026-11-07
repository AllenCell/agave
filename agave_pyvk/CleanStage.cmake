if(NOT AGAVE_PYVK_PROJECT_DIR OR NOT AGAVE_PYVK_PACKAGE_DIR)
  message(FATAL_ERROR "agave_pyvk stage directories were not provided")
endif()

file(GLOB _agave_pyvk_native_modules
  "${AGAVE_PYVK_PACKAGE_DIR}/_native*.pyd"
  "${AGAVE_PYVK_PACKAGE_DIR}/_native*.so"
)
file(GLOB _agave_pyvk_vulkan_loaders
  "${AGAVE_PYVK_PACKAGE_DIR}/libvulkan*.dylib"
)
set(_agave_pyvk_stale_files ${_agave_pyvk_native_modules} ${_agave_pyvk_vulkan_loaders})
if(_agave_pyvk_stale_files)
  file(REMOVE ${_agave_pyvk_stale_files})
endif()
file(REMOVE_RECURSE "${AGAVE_PYVK_PACKAGE_DIR}/share/vulkan/icd.d")
file(REMOVE_RECURSE "${AGAVE_PYVK_PROJECT_DIR}/build")
