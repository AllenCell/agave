# KosmicKrisp is an ICD loaded by libvulkan, never a link dependency.
function(agave_configure_macos_vulkan)
  get_filename_component(_sdk_lib_dir "${Vulkan_LIBRARY}" DIRECTORY)
  set(_driver "${_sdk_lib_dir}/libvulkan_kosmickrisp.dylib")
  set(_manifest "${AGAVE_VULKAN_SDK_ROOT}/share/vulkan/icd.d/libkosmickrisp_icd.json")
  if(NOT EXISTS "${_driver}" OR NOT EXISTS "${_manifest}")
    message(FATAL_ERROR
      "AGAVE requires KosmicKrisp on macOS. Install Vulkan SDK 1.4.363.0 or later "
      "with libvulkan_kosmickrisp.dylib and share/vulkan/icd.d/libkosmickrisp_icd.json. "
      "Set AGAVE_VULKAN_SDK_ROOT to its macOS directory.")
  endif()
  set(AGAVE_VULKAN_KOSMICKRISP_LIBRARY "${_driver}" PARENT_SCOPE)
  set(AGAVE_VULKAN_KOSMICKRISP_MANIFEST "${_manifest}" PARENT_SCOPE)
  # Keep the loader's versioned names and symlink chain, but exclude other ICDs.
  file(GLOB _loader_files "${_sdk_lib_dir}/libvulkan.[0-9]*.dylib")
  list(APPEND _loader_files "${Vulkan_LIBRARY}")
  list(REMOVE_DUPLICATES _loader_files)
  set(AGAVE_VULKAN_LOADER_FILES "${_loader_files}" PARENT_SCOPE)
endfunction()

function(agave_install_macos_vulkan library_destination manifest_destination)
  cmake_parse_arguments(ARG "" "COMPONENT" "" ${ARGN})
  set(_component_args)
  if(ARG_COMPONENT)
    list(APPEND _component_args COMPONENT "${ARG_COMPONENT}")
  endif()
  install(FILES ${AGAVE_VULKAN_LOADER_FILES} "${AGAVE_VULKAN_KOSMICKRISP_LIBRARY}"
          DESTINATION "${library_destination}" ${_component_args})

  # Preserve SDK metadata, rewriting only the driver path for this layout.
  file(READ "${AGAVE_VULKAN_KOSMICKRISP_MANIFEST}" _manifest)
  file(RELATIVE_PATH _driver_path "/${manifest_destination}"
       "/${library_destination}/libvulkan_kosmickrisp.dylib")
  string(JSON _manifest SET "${_manifest}" ICD library_path "\"${_driver_path}\"")
  set(_output "${CMAKE_CURRENT_BINARY_DIR}/vulkan/libkosmickrisp_icd.json")
  file(WRITE "${_output}" "${_manifest}\n")
  install(FILES "${_output}" DESTINATION "${manifest_destination}" ${_component_args})
endfunction()
