qt_add_library(kestrel_qml_controls STATIC)
qt_add_qml_module(kestrel_qml_controls
	URI Kestrel.Controls
	VERSION 1.0
	OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Kestrel/Controls
	IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
	DEPENDENCIES QtQuick QtQuick.Controls Kestrel.ViewModels
	QML_FILES
		ActionButton.qml
		EntityTreeView.qml
		ErrorBanner.qml
		TreePanel.qml
)
target_link_libraries(kestrel_qml_controls PRIVATE Qt6::Quick Qt6::QuickControls2 kestrel::viewmodels)
