# Test registration helpers. Every suite is a ctest entry with labels so that
# `ctest -L unit`, `-L qt`, `-L robot` select a level of the test pyramid.

# kestrel_add_unit_test(<lib> SOURCES ... LIBS ...)
#   Builds unit_<lib> using UnitTests.hpp (one binary per library).
function(kestrel_add_unit_test lib)
	cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
	set(target unit_${lib})
	add_executable(${target} ${ARG_SOURCES})
	set_target_properties(${target} PROPERTIES AUTOMOC OFF)
	target_link_libraries(${target} PRIVATE
		kestrel_unit_main escain::unittests kestrel::options kestrel::warnings kestrel::sanitizers ${ARG_LIBS})
	target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
	add_test(NAME unit.${lib} COMMAND ${target})
	set_tests_properties(unit.${lib} PROPERTIES LABELS "unit;${lib}" TIMEOUT 60)
endfunction()

# kestrel_add_qt_test(<lib> <Name> SOURCES ... LIBS ...)
#   Builds tst_<name> using Qt Test. Use for code that needs an event loop.
function(kestrel_add_qt_test lib name)
	cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
	string(TOLOWER "tst_${name}" target)
	add_executable(${target} ${ARG_SOURCES})
	target_link_libraries(${target} PRIVATE
		Qt6::Test kestrel::options kestrel::warnings kestrel::sanitizers ${ARG_LIBS})
	target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
	add_test(NAME qt.${lib}.${name} COMMAND ${target})
	set_tests_properties(qt.${lib}.${name} PROPERTIES
		LABELS "qt;${lib}" TIMEOUT 120
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endfunction()

# kestrel_find_robot_python(<outVar>)
#   The first python3 on PATH is not necessarily the one with Robot Framework installed
#   (e.g. /usr/local/bin/python3.10 next to the distribution's /usr/bin/python3), so pick the
#   first candidate that can `import robot`. KESTREL_ROBOT_PYTHON overrides the search.
function(kestrel_find_robot_python outVar)
	if(KESTREL_ROBOT_PYTHON_RESOLVED)
		set(${outVar} ${KESTREL_ROBOT_PYTHON_RESOLVED} PARENT_SCOPE)
		return()
	endif()
	find_program(KESTREL_PATH_PYTHON3 NAMES python3)
	find_program(KESTREL_SYSTEM_PYTHON3 NAMES python3 PATHS /usr/bin NO_DEFAULT_PATH)
	foreach(candidate IN ITEMS ${KESTREL_ROBOT_PYTHON} ${KESTREL_PATH_PYTHON3} ${KESTREL_SYSTEM_PYTHON3})
		if(candidate)
			execute_process(COMMAND ${candidate} -c "import robot"
				RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
			if(rc EQUAL 0)
				set(KESTREL_ROBOT_PYTHON_RESOLVED ${candidate} CACHE INTERNAL "Python with Robot Framework")
				set(${outVar} ${candidate} PARENT_SCOPE)
				return()
			endif()
		endif()
	endforeach()
	set(${outVar} "" PARENT_SCOPE)
endfunction()

# kestrel_add_robot_suite(<name>)
#   Runs tests/robot/suites/<name>.robot with the CLI and simulator paths.
function(kestrel_add_robot_suite name)
	kestrel_find_robot_python(python)
	if(NOT python)
		message(STATUS "Robot Framework not found (set KESTREL_ROBOT_PYTHON): robot suite ${name} skipped")
		return()
	endif()
	set(outdir ${CMAKE_BINARY_DIR}/robot/${name})
	add_test(NAME robot.${name}
		COMMAND ${python} -m robot
			--outputdir ${outdir}
			--variable CLI:$<TARGET_FILE:kestrel-cli>
			--variable SIM:$<TARGET_FILE:kestrel-sim>
			--variable DATA:${PROJECT_SOURCE_DIR}/tests/data
			--pythonpath ${PROJECT_SOURCE_DIR}/tests/robot/libraries
			${PROJECT_SOURCE_DIR}/tests/robot/suites/${name}.robot
		WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
	set_tests_properties(robot.${name} PROPERTIES LABELS "robot;acceptance" TIMEOUT 300)
endfunction()
