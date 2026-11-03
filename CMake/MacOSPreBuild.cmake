# force macOS code signing

if(NOT DEFINED APP_DIR)
  set(APP_DIR "${CPACK_TEMPORARY_INSTALL_DIRECTORY}")
endif()
if(NOT EXISTS "${APP_DIR}/agave.app" AND EXISTS "${APP_DIR}/ALL_IN_ONE/agave.app")
  set(APP_DIR "${APP_DIR}/ALL_IN_ONE")
endif()

file(GLOB LIBSTOSIGN
  "${APP_DIR}/agave.app/Contents/Frameworks/*"
  "${APP_DIR}/agave.app/Contents/PlugIns/*/*.dylib")
foreach(LIBTOSIGN ${LIBSTOSIGN})
  # BundleUtilities can leave SDK/Homebrew symlinks whose versioned target
  # was copied under its install name instead. They are not runtime libraries.
  if(IS_SYMLINK "${LIBTOSIGN}" AND NOT EXISTS "${LIBTOSIGN}")
    file(REMOVE "${LIBTOSIGN}")
    continue()
  endif()
  execute_process(COMMAND codesign --force --sign - "${LIBTOSIGN}" COMMAND_ERROR_IS_FATAL ANY)
endforeach()
execute_process(COMMAND codesign --force --sign - "${APP_DIR}/agave.app" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND codesign --verify --deep --strict "${APP_DIR}/agave.app" COMMAND_ERROR_IS_FATAL ANY)

# remove unneeded dirs from installer 
# these directories are a result of the install step for libCZI (in renderlib/io)
execute_process(COMMAND rm -rf "${APP_DIR}/include")
execute_process(COMMAND rm -rf "${APP_DIR}/lib")
