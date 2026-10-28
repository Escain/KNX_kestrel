# View-model tests (Qt Test, no Qt Quick needed).
if(NOT TARGET kestrel_viewmodels)
	return()
endif()

foreach(name TreeModels ProjectSession PanelViewModels ProblemsModel)
	kestrel_add_qt_test(viewmodels ${name} SOURCES tst_${name}.cpp LIBS kestrel::viewmodels)
endforeach()

# Catalog dock and catalog device editor against the synthetic M-FFFF product file (ADR-009).
kestrel_add_qt_test(viewmodels CatalogViewModels SOURCES tst_CatalogViewModels.cpp LIBS kestrel::viewmodels)
target_compile_definitions(tst_catalogviewmodels PRIVATE KESTREL_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")

# Bus view models against the in-process simulator (NFR-5).
if(TARGET kestrel_simnet)
	kestrel_add_qt_test(viewmodels BusViewModels SOURCES tst_BusViewModels.cpp LIBS kestrel::viewmodels kestrel::simnet)
	target_compile_definitions(tst_busviewmodels PRIVATE KESTREL_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
	set_tests_properties(qt.viewmodels.BusViewModels PROPERTIES LABELS "qt;viewmodels;bus")
endif()
