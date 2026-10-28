# kestrel_viewmodels (tier T2, ADR-006): item models, panel view models, selection and actions.
# QtCore/Gui/Qml only — never Quick or Widgets — so view-model tests run without a scene graph.

set(_vm_headers
	include/kestrel/viewmodels/ActionRegistry.hpp
	include/kestrel/viewmodels/BuildingTreeModel.hpp
	include/kestrel/viewmodels/BuildingsPanelVM.hpp
	include/kestrel/viewmodels/BusConnectionVM.hpp
	include/kestrel/viewmodels/CatalogComObjectModel.hpp
	include/kestrel/viewmodels/CatalogPanelVM.hpp
	include/kestrel/viewmodels/CatalogService.hpp
	include/kestrel/viewmodels/ComObjectTableModel.hpp
	include/kestrel/viewmodels/CommissioningVM.hpp
	include/kestrel/viewmodels/ConfigurationHooks.hpp
	include/kestrel/viewmodels/DeviceEditorVM.hpp
	include/kestrel/viewmodels/DeviceParameterModel.hpp
	include/kestrel/viewmodels/DeviceStatusService.hpp
	include/kestrel/viewmodels/Format.hpp
	include/kestrel/viewmodels/GroupAddressPanelVM.hpp
	include/kestrel/viewmodels/GroupAddressTreeModel.hpp
	include/kestrel/viewmodels/GroupMonitorVM.hpp
	include/kestrel/viewmodels/KeyedTableModel.hpp
	include/kestrel/viewmodels/PanelVM.hpp
	include/kestrel/viewmodels/ProblemsModel.hpp
	include/kestrel/viewmodels/ProblemsPanelVM.hpp
	include/kestrel/viewmodels/ProjectSession.hpp
	include/kestrel/viewmodels/PropertiesPanelVM.hpp
	include/kestrel/viewmodels/SelectionService.hpp
	include/kestrel/viewmodels/ShadowTreeModel.hpp
	include/kestrel/viewmodels/TelegramModel.hpp
	include/kestrel/viewmodels/TopologyPanelVM.hpp
	include/kestrel/viewmodels/TopologyTreeModel.hpp
	include/kestrel/viewmodels/WelcomeVM.hpp
	include/kestrel/viewmodels/Workspace.hpp
)
set(_vm_sources
	src/ActionRegistry.cpp
	src/BuildingTreeModel.cpp
	src/BuildingsPanelVM.cpp
	src/BusConnectionVM.cpp
	src/CatalogComObjectModel.cpp
	src/CatalogPanelVM.cpp
	src/CatalogService.cpp
	src/ComObjectTableModel.cpp
	src/CommissioningVM.cpp
	src/ConfigurationHooks.cpp
	src/DeviceEditorVM.cpp
	src/DeviceParameterModel.cpp
	src/DeviceStatusService.cpp
	src/Format.cpp
	src/GroupAddressPanelVM.cpp
	src/GroupAddressTreeModel.cpp
	src/GroupMonitorVM.cpp
	src/KeyedTableModel.cpp
	src/PanelVM.cpp
	src/ProblemsModel.cpp
	src/ProblemsPanelVM.cpp
	src/ProjectSession.cpp
	src/PropertiesPanelVM.cpp
	src/SelectionService.cpp
	src/ShadowTreeModel.cpp
	src/TelegramModel.cpp
	src/TopologyPanelVM.cpp
	src/TopologyTreeModel.cpp
	src/WelcomeVM.cpp
	src/Workspace.cpp
)

qt_add_library(kestrel_viewmodels STATIC)
add_library(kestrel::viewmodels ALIAS kestrel_viewmodels)
qt_add_qml_module(kestrel_viewmodels
	URI Kestrel.ViewModels
	VERSION 1.0
	OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Kestrel/ViewModels
	# QAbstractItemModel properties are typed through QtQml.Models
	DEPENDENCIES QtQml.Models
	SOURCES ${_vm_headers} ${_vm_sources}
)
target_include_directories(kestrel_viewmodels
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
	# qmltyperegistrar includes the QML_ELEMENT headers by file name
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/kestrel/viewmodels ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(kestrel_viewmodels
	PUBLIC kestrel::options kestrel::model kestrel::persist kestrel::validate kestrel::dpt kestrel::app kestrel::catalog kestrel::appconfig
		kestrel::download Qt6::Core Qt6::Gui Qt6::Qml
	PRIVATE kestrel::warnings kestrel::sanitizers)
