qt_add_executable(kestrel main.cpp)
target_link_libraries(kestrel PRIVATE kestrel::shell kestrel::options kestrel::warnings kestrel::sanitizers)
target_compile_definitions(kestrel PRIVATE KESTREL_VERSION="${PROJECT_VERSION}")
# A GUI application: no console window on Windows, an app bundle on macOS. The icons are made from
# src/shell/logo/kestrel.svg by tools/icons/make_app_icons.py.
set_target_properties(kestrel PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)
if(WIN32)
	configure_file(kestrel.rc.in kestrel.rc @ONLY)
	target_sources(kestrel PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/kestrel.rc)
elseif(APPLE)
	set_source_files_properties(kestrel.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
	target_sources(kestrel PRIVATE kestrel.icns)
	set_target_properties(kestrel PROPERTIES
		OUTPUT_NAME Kestrel
		MACOSX_BUNDLE_BUNDLE_NAME Kestrel
		MACOSX_BUNDLE_GUI_IDENTIFIER org.kestrel.Kestrel
		MACOSX_BUNDLE_ICON_FILE kestrel.icns
		MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
		MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
		MACOSX_BUNDLE_COPYRIGHT "Kestrel contributors")
endif()

install(TARGETS kestrel BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
	# Wayland compositors don't take the window icon from the application: they show the icon of the desktop file
	# named after its app id (QGuiApplication::setDesktopFileName in main.cpp).
	install(FILES kestrel.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
	install(FILES ${PROJECT_SOURCE_DIR}/src/shell/logo/kestrel.svg
		DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
