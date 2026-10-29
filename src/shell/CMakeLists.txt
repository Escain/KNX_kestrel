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
	PUBLIC_DEPS kestrel::viewmodels Qt6::Widgets Qt6::Svg Qt6::Quick Qt6::QuickWidgets Qt6::QuickControls2
	PRIVATE_DEPS kestrel_viewmodelsplugin kestrel_qml_controlsplugin kestrel_qml_viewsplugin
)

# The shell's own stroke icons (drawn for Kestrel, recoloured from the theme at run time).
qt_add_resources(kestrel_shell "kestrel_shell_icons"
	PREFIX "/kestrel/icons"
	BASE icons
	FILES
		icons/add-product.svg
		icons/close.svg
		icons/connect.svg
		icons/delete.svg
		icons/download.svg
		icons/kestrel.svg
		icons/monitor.svg
		icons/new.svg
		icons/open.svg
		icons/program.svg
		icons/redo.svg
		icons/save.svg
		icons/undo.svg
)
