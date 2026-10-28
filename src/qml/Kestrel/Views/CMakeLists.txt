qt_add_library(kestrel_qml_views STATIC)
qt_add_qml_module(kestrel_qml_views
	URI Kestrel.Views
	VERSION 1.0
	OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Kestrel/Views
	IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
	DEPENDENCIES QtQuick QtQuick.Controls Kestrel.ViewModels Kestrel.Controls
	QML_FILES
		BuildingsPanel.qml
		BusConnectionPanel.qml
		CatalogPanel.qml
		CommissioningPanel.qml
		DeviceEditor.qml
		GroupAddressPanel.qml
		GroupMonitor.qml
		ProblemsPanel.qml
		PropertiesPanel.qml
		TopologyPanel.qml
		Welcome.qml
)
target_link_libraries(kestrel_qml_views PRIVATE Qt6::Quick Qt6::QuickControls2 kestrel::viewmodels kestrel_qml_controls)
