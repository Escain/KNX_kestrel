# Shell tests: the real MainWindow with QQuickWidget hosts, offscreen.
if(NOT TARGET kestrel_shell)
	return()
endif()

kestrel_add_qt_test(shell MainWindow SOURCES tst_MainWindow.cpp LIBS kestrel::shell kestrel::simnet)
target_include_directories(tst_mainwindow PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../viewmodels)

# Helper (not a test): writes the sample project to a folder, for screenshots and manual checks.
add_executable(kestrel_make_sample_project make_sample_project.cpp)
target_include_directories(kestrel_make_sample_project PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../viewmodels)
target_link_libraries(kestrel_make_sample_project PRIVATE kestrel::persist kestrel::options kestrel::warnings)
