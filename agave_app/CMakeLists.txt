add_executable(agaveapp MACOSX_BUNDLE "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
set_target_properties(agaveapp PROPERTIES OUTPUT_NAME "agave")
set_property(TARGET agaveapp PROPERTY AUTOMOC ON)
set_property(TARGET agaveapp PROPERTY AUTOUIC ON)
set_property(TARGET agaveapp PROPERTY AUTORCC ON)
target_include_directories(agaveapp PUBLIC
	"${CMAKE_SOURCE_DIR}"
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${glm_SOURCE_DIR}"
)
target_sources(agaveapp PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/aboutDialog.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/aboutDialog.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/agaveGui.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/agaveGui.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/agaveGui.qrc"
	"${CMAKE_CURRENT_SOURCE_DIR}/agaveGui.ui"
	"${CMAKE_CURRENT_SOURCE_DIR}/Aperture.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Aperture.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/AppearanceDockWidget.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/AppearanceDockWidget.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/AppearanceSettingsWidget.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/AppearanceSettingsWidget.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Camera.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Camera.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/CameraDockWidget.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/CameraDockWidget.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/CameraWidget.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/CameraWidget.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/CacheSettings.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/CacheSettings.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/CacheSettingsDockWidget.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/CacheSettingsDockWidget.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/CacheSettingsWidget.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/CacheSettingsWidget.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/citationDialog.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/citationDialog.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/cgiparser.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/cgiparser.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/commandBuffer.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/commandBuffer.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Controls.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Controls.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Film.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Film.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Focus.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Focus.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLView3D.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/GLView3D.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/loadDialog.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/loadDialog.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/mainwindow.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/mainwindow.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/ObjectTransformMode.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/ObjectTransformMode.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Projection.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Projection.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/QRenderSettings.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/QRenderSettings.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/RangeWidget.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/RangeWidget.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/renderer.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/renderer.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/renderDialog.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/renderDialog.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/renderrequest.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/renderrequest.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Section.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Section.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Serialize.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Serialize.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/SerializeV1.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/SerializeV1.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/StatisticsWidget.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/StatisticsWidget.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/StatisticsDockWidget.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/StatisticsDockWidget.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/streamserver.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/streamserver.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/streamtestclient.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/streamtestclient.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/TimelineDockWidget.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/TimelineDockWidget.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/ViewerState.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/ViewerState.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/ViewToolbar.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/ViewToolbar.h"
)
add_subdirectory(python)

add_subdirectory(tfeditor)

if(MSVC)
	target_sources(agaveapp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/version.rc")
	set_property(
		TARGET agaveapp
		PROPERTY VS_DEBUGGER_WORKING_DIRECTORY
		"${CMAKE_INSTALL_PREFIX}/${INSTALLDIR}"
	)
	set_property(
		TARGET agaveapp
		PROPERTY VS_DEBUGGER_COMMAND
		"${CMAKE_INSTALL_PREFIX}/${INSTALLDIR}/$(TargetFileName)"
	)
endif(MSVC)

target_link_libraries(agaveapp PRIVATE
	renderlib
	Qt::Widgets Qt::Core Qt::Gui Qt::Network Qt::OpenGL Qt::OpenGLWidgets Qt::WebSockets Qt::Xml Qt::Svg
)

# copy asset files from renderlib into a directory
# relative to our app executable
if(WIN32)
	add_custom_command(
		TARGET agaveapp POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"$<TARGET_PROPERTY:renderlib,SOURCE_DIR>/assets"
			"$<TARGET_PROPERTY:agaveapp,BINARY_DIR>/assets"
	)
elseif(APPLE)
	# And the following tells CMake where to find and install the file itself.
	set(app_icon_macos "${CMAKE_CURRENT_SOURCE_DIR}/icons/logo.icns")
	set_property(SOURCE ${app_icon_macos} PROPERTY MACOSX_PACKAGE_LOCATION "Resources/${NEW_FILE_PATH}")

	get_target_property(RENDERLIB_SOURCE_DIR renderlib SOURCE_DIR)
	set(RESOURCES
	  ${RENDERLIB_SOURCE_DIR}/assets/fonts/Arial.ttf
	  ${RENDERLIB_SOURCE_DIR}/assets/fonts/Courier_New.ttf
	)

	# Add all of the files as "Source Files".
	target_sources(agaveapp PRIVATE ${RESOURCES})

	# message(STATUS "Copying ${RESOURCES} to macosx bundle Resources")
	# Individually set the file's path properties.
	foreach (FILE ${RESOURCES})
		# Get the relative path from the data-folder to the particular file.
		file(RELATIVE_PATH NEW_FILE "${RENDERLIB_SOURCE_DIR}" ${FILE})

		# Get the relative path to the file.
		get_filename_component(NEW_FILE_PATH ${NEW_FILE} DIRECTORY)

		# Set its location inside the app package (under Resources).

		set_property(SOURCE ${FILE} PROPERTY MACOSX_PACKAGE_LOCATION "Resources/${NEW_FILE_PATH}")
		# Optional: Add the file to the 'Resources' folder group in Xcode.
		#           This also preserves folder structure.
		source_group("Resources/${NEW_FILE_PATH}" FILES "${FILE}")
	endforeach ()
else() # LINUX
	add_custom_command(
		TARGET agaveapp POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"$<TARGET_PROPERTY:renderlib,SOURCE_DIR>/assets"
			"$<TARGET_PROPERTY:agaveapp,BINARY_DIR>/assets"
	)
endif(WIN32)
