qt_add_executable(kestrel main.cpp)
target_link_libraries(kestrel PRIVATE kestrel::shell kestrel::options kestrel::warnings kestrel::sanitizers)
target_compile_definitions(kestrel PRIVATE KESTREL_VERSION="${PROJECT_VERSION}")
set_target_properties(kestrel PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)

install(TARGETS kestrel BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
	# Wayland compositors don't take the window icon from the application: they show the icon of the desktop file
	# named after its app id (QGuiApplication::setDesktopFileName in main.cpp).
	install(FILES kestrel.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
	install(FILES ${PROJECT_SOURCE_DIR}/src/shell/logo/kestrel.svg
		DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
