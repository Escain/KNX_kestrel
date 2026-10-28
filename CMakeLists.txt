cmake_minimum_required(VERSION 3.24)

project(Kestrel
	VERSION 0.1.0
	DESCRIPTION "Open configuration tool for KNX installations"
	LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(KestrelOptions)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network)
if(KESTREL_BUILD_GUI)
	find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Widgets Qml Quick QuickControls2 QuickWidgets)
endif()
qt_standard_project_setup(REQUIRES 6.8)
if(QT_KNOWN_POLICY_QTP0001)
	qt_policy(SET QTP0001 NEW)
endif()

include(CTest)
include(KestrelTesting)

add_subdirectory(third_party)
add_subdirectory(src)
add_subdirectory(apps)

if(BUILD_TESTING)
	add_subdirectory(tests)
endif()

include(KestrelLayering)
