# kestrel_shell (tier T2, ADR-006): QMainWindow, docks, menus and QQuickWidget hosts for the QML views.
kestrel_add_library(shell
	SOURCES
		include/kestrel/shell/DocumentArea.hpp
		include/kestrel/shell/MainWindow.hpp
		include/kestrel/shell/QmlDock.hpp
		include/kestrel/shell/QmlHost.hpp
		include/kestrel/shell/Style.hpp
		src/DocumentArea.cpp
		src/MainWindow.cpp
		src/QmlDock.cpp
		src/QmlHost.cpp
		src/Style.cpp
	PUBLIC_DEPS kestrel::viewmodels Qt6::Widgets Qt6::Quick Qt6::QuickWidgets Qt6::QuickControls2
	PRIVATE_DEPS kestrel_viewmodelsplugin kestrel_qml_controlsplugin kestrel_qml_viewsplugin
)
