# QML modules (presentation only, ADR-006). Static libraries; the shell links their plugins.
# All modules are emitted below ${CMAKE_BINARY_DIR}/qml so that qmlcachegen/qmllint resolve the imports.
add_subdirectory(Kestrel/Controls)
add_subdirectory(Kestrel/Views)
