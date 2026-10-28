# View-model tests (Qt Test, no Qt Quick needed).
if(NOT TARGET kestrel_viewmodels)
	return()
endif()

foreach(name TreeModels ProjectSession PanelViewModels ProblemsModel)
	kestrel_add_qt_test(viewmodels ${name} SOURCES tst_${name}.cpp LIBS kestrel::viewmodels)
endforeach()

# Bus view models against the in-process simulator (NFR-5).
if(TARGET kestrel_simnet)
	kestrel_add_qt_test(viewmodels BusViewModels SOURCES tst_BusViewModels.cpp LIBS kestrel::viewmodels kestrel::simnet)
	set_tests_properties(qt.viewmodels.BusViewModels PROPERTIES LABELS "qt;viewmodels;bus")
endif()
