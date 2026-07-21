if(NOT AGAVE_PYVK_PROJECT_DIR OR NOT AGAVE_PYVK_PACKAGE_DIR)
  message(FATAL_ERROR "agave_pyvk stage directories were not provided")
endif()

file(GLOB _agave_pyvk_native_modules
  "${AGAVE_PYVK_PACKAGE_DIR}/_native*.pyd"
  "${AGAVE_PYVK_PACKAGE_DIR}/_native*.so"
)
file(REMOVE ${_agave_pyvk_native_modules})
file(REMOVE_RECURSE "${AGAVE_PYVK_PROJECT_DIR}/build")
