# Project-wide options and INTERFACE targets carrying compile flags.
# Never use global add_compile_options(): every target links kestrel::options
# and kestrel::warnings explicitly.

option(KESTREL_BUILD_GUI "Build the Qt Widgets/Quick desktop application" ON)
option(KESTREL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(KESTREL_SANITIZE "" CACHE STRING "Semicolon list of sanitizers (address;undefined)")
option(KESTREL_ENABLE_AUTOMATION "Compile the GUI automation server used by Robot GUI suites" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(kestrel_options INTERFACE)
add_library(kestrel::options ALIAS kestrel_options)
target_compile_features(kestrel_options INTERFACE cxx_std_20)
target_compile_definitions(kestrel_options INTERFACE
	QT_NO_KEYWORDS
	QT_NO_CAST_FROM_ASCII
	QT_NO_CAST_TO_ASCII
	QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
	QT_DISABLE_DEPRECATED_UP_TO=0x060800)

add_library(kestrel_warnings INTERFACE)
add_library(kestrel::warnings ALIAS kestrel_warnings)
if(MSVC)
	target_compile_options(kestrel_warnings INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor)
	if(KESTREL_WARNINGS_AS_ERRORS)
		target_compile_options(kestrel_warnings INTERFACE /WX)
	endif()
else()
	target_compile_options(kestrel_warnings INTERFACE
		-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
		-Wnon-virtual-dtor -Woverloaded-virtual -Wcast-align -Wformat=2
		-Wimplicit-fallthrough -Wdouble-promotion)
	if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		target_compile_options(kestrel_warnings INTERFACE -Wduplicated-cond -Wlogical-op)
	endif()
	if(KESTREL_WARNINGS_AS_ERRORS)
		target_compile_options(kestrel_warnings INTERFACE -Werror)
	endif()
endif()

add_library(kestrel_sanitizers INTERFACE)
add_library(kestrel::sanitizers ALIAS kestrel_sanitizers)
if(KESTREL_SANITIZE)
	list(JOIN KESTREL_SANITIZE "," _kestrel_san)
	target_compile_options(kestrel_sanitizers INTERFACE
		-fsanitize=${_kestrel_san} -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
	target_link_options(kestrel_sanitizers INTERFACE -fsanitize=${_kestrel_san})
endif()

# kestrel_add_library(<name> [STD_ONLY] SOURCES ... [PUBLIC_DEPS ...] [PRIVATE_DEPS ...])
#   Creates STATIC library kestrel_<name> with alias kestrel::<name>.
#   Public headers live in <dir>/include/kestrel/<name>/.
function(kestrel_add_library name)
	cmake_parse_arguments(ARG "STD_ONLY" "" "SOURCES;PUBLIC_DEPS;PRIVATE_DEPS" ${ARGN})
	add_library(kestrel_${name} STATIC ${ARG_SOURCES})
	add_library(kestrel::${name} ALIAS kestrel_${name})
	target_include_directories(kestrel_${name}
		PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
	target_link_libraries(kestrel_${name}
		PUBLIC  kestrel::options ${ARG_PUBLIC_DEPS}
		PRIVATE kestrel::warnings kestrel::sanitizers ${ARG_PRIVATE_DEPS})
	set_target_properties(kestrel_${name} PROPERTIES KESTREL_STD_ONLY "${ARG_STD_ONLY}")
	if(ARG_STD_ONLY)
		set_target_properties(kestrel_${name} PROPERTIES AUTOMOC OFF AUTORCC OFF AUTOUIC OFF)
	endif()
endfunction()
