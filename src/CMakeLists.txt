# Libraries, listed bottom-up (see specs/02-architecture.md for the dependency rules).
# A directory is only added once it has a CMakeLists.txt, so layers can land incrementally.
set(KESTREL_CORE_LIBS base knx dpt sim model persist validate bus simnet app)
set(KESTREL_GUI_LIBS viewmodels qml shell)

foreach(lib IN LISTS KESTREL_CORE_LIBS)
	if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${lib}/CMakeLists.txt)
		add_subdirectory(${lib})
	endif()
endforeach()

if(KESTREL_BUILD_GUI)
	foreach(lib IN LISTS KESTREL_GUI_LIBS)
		if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${lib}/CMakeLists.txt)
			add_subdirectory(${lib})
		endif()
	endforeach()
endif()
