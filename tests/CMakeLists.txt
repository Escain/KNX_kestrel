# Single main() for every UnitTests.hpp binary.
add_library(kestrel_unit_main OBJECT support/unit_main.cpp)
target_link_libraries(kestrel_unit_main PRIVATE escain::unittests kestrel::options)
set_target_properties(kestrel_unit_main PROPERTIES AUTOMOC OFF)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	# MUST_DIE uses setjmp inside test bodies (see UnitTests.hpp)
	target_compile_options(kestrel_unit_main PRIVATE -Wno-clobbered)
endif()

file(GLOB _unit_dirs LIST_DIRECTORIES true ${CMAKE_CURRENT_SOURCE_DIR}/unit/*)
foreach(dir IN LISTS _unit_dirs)
	if(EXISTS ${dir}/CMakeLists.txt)
		add_subdirectory(${dir})
	endif()
endforeach()

# Qt Test suites: one directory per library (tests/qt/<lib>/CMakeLists.txt).
find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
file(GLOB _qt_dirs LIST_DIRECTORIES true ${CMAKE_CURRENT_SOURCE_DIR}/qt/*)
foreach(dir IN LISTS _qt_dirs)
	if(EXISTS ${dir}/CMakeLists.txt)
		add_subdirectory(${dir})
	endif()
endforeach()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/robot/CMakeLists.txt)
	add_subdirectory(robot)
endif()
