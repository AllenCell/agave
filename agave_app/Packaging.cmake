# #####################
# CPack
# #####################
find_package(Qt6QTiffPlugin 6.9.3 REQUIRED PATHS ${Qt6Gui_DIR})

set(CPACK_PACKAGE_VENDOR "Allen Institute")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "AGAVE is a viewer for 4D multichannel microscopy images, using physically based lighting and rendering.")

set(CPACK_PACKAGE_NAME "AGAVE")

install(TARGETS agaveapp
	BUNDLE DESTINATION .
	RUNTIME DESTINATION ${INSTALLDIR}
	LIBRARY DESTINATION ${INSTALLDIR}
	DESTINATION ${INSTALLDIR}
)

if(WIN32)
  install(TARGETS agaveapp
    RUNTIME_DEPENDENCIES
    DIRECTORIES "$<TARGET_FILE_DIR:agaveapp>" "$<TARGET_FILE_DIR:Qt6::Core>"
    PRE_EXCLUDE_REGEXES "api-ms-" "ext-ms-"
    POST_EXCLUDE_REGEXES ".*system32/.*\\.dll"
    DESTINATION ${INSTALLDIR}
    CONFIGURATIONS Release
  )
  install(TARGETS agaveapp
    RUNTIME_DEPENDENCIES
    DIRECTORIES "$<TARGET_FILE_DIR:agaveapp>" "$<TARGET_FILE_DIR:Qt6::Core>"
    PRE_EXCLUDE_REGEXES "api-ms-" "ext-ms-"
    POST_EXCLUDE_REGEXES ".*system32/.*\\.dll"
    DESTINATION ${INSTALLDIR}
    CONFIGURATIONS Debug
  )
  install(FILES
    ${PROJECT_SOURCE_DIR}/LICENSE.txt
    ${PROJECT_SOURCE_DIR}/HELP.txt
    $<TARGET_FILE:libCZI>
    $<TARGET_FILE:Qt6::Svg>
    $<TARGET_FILE:Qt6::Xml>
    DESTINATION ${INSTALLDIR}
    CONFIGURATIONS Release
  )
  install(FILES
    ${PROJECT_SOURCE_DIR}/LICENSE.txt
    ${PROJECT_SOURCE_DIR}/HELP.txt
    $<TARGET_FILE:libCZI>
    $<TARGET_FILE:Qt6::Svg>
    $<TARGET_FILE:Qt6::Xml>
    DESTINATION ${INSTALLDIR}
    CONFIGURATIONS Debug
  )
  install(FILES
    ${PROJECT_SOURCE_DIR}/renderlib/assets/fonts/Arial.ttf
    ${PROJECT_SOURCE_DIR}/renderlib/assets/fonts/Courier_New.ttf
    DESTINATION ${INSTALLDIR}/assets/fonts/
  )
  install(FILES
    $<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>
    DESTINATION ${INSTALLDIR}/platforms/
  )
  install(FILES
    $<TARGET_FILE:Qt6::QSvgIconPlugin>
    DESTINATION ${INSTALLDIR}/iconengines/
  )
  install(FILES
    $<TARGET_FILE:Qt6::QTiffPlugin>
    DESTINATION ${INSTALLDIR}/imageformats/
  )
  install(FILES
    $<TARGET_FILE:Qt6::QJpegPlugin>
    DESTINATION ${INSTALLDIR}/imageformats/
  )

  set(CPACK_GENERATOR "NSIS")
  set(CPACK_PRE_BUILD_SCRIPTS "${CMAKE_SOURCE_DIR}/CMake/WindowsPreBuild.cmake")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "AGAVE ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
  set(CPACK_PACKAGE_EXECUTABLES "agave" "AGAVE")
  set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/agave_app/icons/logo.ico")
  set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/agave_app/icons/logo.ico")
  set(CPACK_NSIS_EXECUTABLES_DIRECTORY "agave-install")
  set(CPACK_NSIS_MENU_LINKS "https://allen-cell-animated.github.io/agave" "AGAVE Docs")
  set(CPACK_NSIS_PACKAGE_NAME "AGAVE ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
  set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
    WriteRegStr HKCR 'agave' '' 'URL:agave'
    WriteRegStr HKCR 'agave' 'URL Protocol' ''
    WriteRegStr HKCR 'agave\\\\DefaultIcon' '' '\\\"$INSTDIR\\\\agave-install\\\\agave.exe\\\"'
    WriteRegStr HKCR 'agave\\\\shell' '' ''
    WriteRegStr HKCR 'agave\\\\shell\\\\open' '' ''
    WriteRegStr HKCR 'agave\\\\shell\\\\open\\\\command' '' '\\\"$INSTDIR\\\\agave-install\\\\agave.exe\\\" --load \\\"%1\\\"'
  ")
  set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
    DeleteRegKey HKCR 'agave'
  ")

 include(CPack)

# ###############
elseif(APPLE)
  find_package(Qt6QCocoaIntegrationPlugin 6.9.3 REQUIRED PATHS ${Qt6Gui_DIR})
  find_package(Qt6QMacStylePlugin 6.9.3 REQUIRED PATHS ${Qt6Widgets_DIR})

  # ###############
  set(PACKAGE_OSX_TARGET ${CMAKE_OSX_DEPLOYMENT_TARGET})
  add_definitions(-DPACKAGE_OSX_TARGET)

  # based on code from CMake's QtDialog/CMakeLists.txt
  macro(install_qt6_plugin _qt_plugin_name _qt_plugins_var _prefix)
    get_target_property(_qt_plugin_path "${_qt_plugin_name}" LOCATION)

    if(EXISTS "${_qt_plugin_path}")
      get_filename_component(_qt_plugin_file "${_qt_plugin_path}" NAME)
      get_filename_component(_qt_plugin_type "${_qt_plugin_path}" PATH)
      get_filename_component(_qt_plugin_type "${_qt_plugin_type}" NAME)
      set(_qt_plugin_dest "${_prefix}/Contents/PlugIns/${_qt_plugin_type}")
      install(FILES "${_qt_plugin_path}"
        DESTINATION "${_qt_plugin_dest}")
      set(${_qt_plugins_var}
        "${${_qt_plugins_var}};\$ENV{DEST_DIR}\${CMAKE_INSTALL_PREFIX}/${_qt_plugin_dest}/${_qt_plugin_file}")
    else()
      message(FATAL_ERROR "QT plugin ${_qt_plugin_name} not found")
    endif()
  endmacro()

  install(FILES
    $<TARGET_FILE:Qt6::Widgets>
    $<TARGET_FILE:Qt6::Core>
    $<TARGET_FILE:Qt6::Gui>
    $<TARGET_FILE:Qt6::OpenGL>
    $<TARGET_FILE:Qt6::OpenGLWidgets>
    $<TARGET_FILE:Qt6::Xml>
    $<TARGET_FILE:Qt6::Svg>
    $<TARGET_FILE:Qt6::Network>
    $<TARGET_FILE:Qt6::WebSockets>
    $<TARGET_FILE:TIFF::TIFF>
    DESTINATION agave.app/Contents/Frameworks/
  )

  # calls cmake install command and also adds to the QT_PLUGINS list for use below
  install_qt6_plugin("Qt6::QCocoaIntegrationPlugin" QT_PLUGINS agave.app)
  install_qt6_plugin("Qt6::QTiffPlugin" QT_PLUGINS agave.app)
  install_qt6_plugin("Qt6::QJpegPlugin" QT_PLUGINS agave.app)
  install_qt6_plugin("Qt6::QMacStylePlugin" QT_PLUGINS agave.app)
  install_qt6_plugin("Qt6::QSvgIconPlugin" QT_PLUGINS agave.app)

  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/qt.conf"
    "[Paths]\nPlugins = PlugIns\n")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/qt.conf"
    DESTINATION agave.app/Contents/Resources/)

  install(FILES
    ${PROJECT_SOURCE_DIR}/renderlib/assets/fonts/Arial.ttf
    ${PROJECT_SOURCE_DIR}/renderlib/assets/fonts/Courier_New.ttf
    DESTINATION agave.app/Contents/Resources/assets/fonts/
  )
  install(FILES
    ${PROJECT_SOURCE_DIR}/agave_app/icons/logo.icns
    DESTINATION agave.app/Contents/Resources/
  )

  CONFIGURE_FILE(${CMAKE_SOURCE_DIR}/CMake/Info.plist.cmake ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)
  set_target_properties(agaveapp PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")
	set_target_properties(agaveapp PROPERTIES MACOSX_BUNDLE_ICON_FILE logo.icns)

  # Note Mac specific extension .app
  set(APPS "\${CMAKE_INSTALL_PREFIX}/agave.app")

  # Directories to look for dependencies
  set(DIRS "${CMAKE_BINARY_DIR}")

  # Path used for searching by FIND_XXX(), with appropriate suffixes added
  if(CMAKE_PREFIX_PATH)
    foreach(dir ${CMAKE_PREFIX_PATH})
      # note that this should get the necessary Qt lib directories
      list(APPEND DIRS "${dir}/bin" "${dir}/lib")
    endforeach()
  endif()

  # homebrew lib path
  list(APPEND DIRS "/opt/homebrew/lib" "/usr/local/lib")

  # Append Qt's lib folder
  # list(APPEND DIRS "${QTDIR}/lib")
  # list(APPEND DIRS "${Qt6Widgets_DIR}/../..")

  include(InstallRequiredSystemLibraries)

  message(STATUS "APPS: ${APPS}")
  message(STATUS "PLUGINS: ${QT_PLUGINS}")
  message(STATUS "DIRS: ${DIRS}")

  install(CODE "
    include(InstallRequiredSystemLibraries)
    include(BundleUtilities)
    set(BU_CHMOD_BUNDLE_ITEMS TRUE)
    fixup_bundle(\"\${CMAKE_INSTALL_PREFIX}/agave.app\" \"${QT_PLUGINS}\" \"${DIRS}\" )")

  set(CPACK_GENERATOR "DragNDrop")
  set(CPACK_PRE_BUILD_SCRIPTS "${CMAKE_SOURCE_DIR}/CMake/MacOSPreBuild.cmake")
  set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/agave_app/icons/dmg-icon.icns")

  include(CPack)

else() # Linux
  find_package(Qt6QXcbIntegrationPlugin 6.9.3 REQUIRED PATHS ${Qt6Gui_DIR})

  install(FILES
    ${PROJECT_SOURCE_DIR}/LICENSE.txt
    ${PROJECT_SOURCE_DIR}/HELP.txt
    $<TARGET_FILE:Qt6::Widgets>
    $<TARGET_FILE:Qt6::Core>
    $<TARGET_FILE:Qt6::Gui>
    $<TARGET_FILE:Qt6::OpenGL>
    $<TARGET_FILE:Qt6::OpenGLWidgets>
    $<TARGET_FILE:Qt6::Xml>
    $<TARGET_FILE:Qt6::Svg>
    $<TARGET_FILE:Qt6::Network>
    $<TARGET_FILE:Qt6::WebSockets>
    $<TARGET_FILE:TIFF::TIFF>
    DESTINATION ${INSTALLDIR}
  )
  install(TARGETS libCZI LIBRARY DESTINATION ${INSTALLDIR})
  install(FILES
    $<TARGET_FILE:Qt6::QXcbIntegrationPlugin>
    DESTINATION ${INSTALLDIR}/platforms/
  )
  install(FILES
    $<TARGET_FILE:Qt6::QTiffPlugin>
    DESTINATION ${INSTALLDIR}/imageformats/
  )
  install(FILES
    $<TARGET_FILE:Qt6::QJpegPlugin>
    DESTINATION ${INSTALLDIR}/imageformats/
  )
  install(FILES
    $<TARGET_FILE:Qt6::QSvgIconPlugin>
    DESTINATION ${INSTALLDIR}/iconengines/
  )
  install(FILES
    ${PROJECT_SOURCE_DIR}/renderlib/assets/fonts/Arial.ttf
    ${PROJECT_SOURCE_DIR}/renderlib/assets/fonts/Courier_New.ttf
    DESTINATION ${INSTALLDIR}/assets/fonts/
  )
  set(CPACK_GENERATOR "DEB")
  set(CPACK_DEBIAN_PACKAGE_NAME "agave")
  set(CPACK_PACKAGE_CONTACT "Allen Institute")

  include(CPack)
endif(WIN32)

