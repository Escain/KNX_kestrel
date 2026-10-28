qt_add_executable(kestrel main.cpp)
target_link_libraries(kestrel PRIVATE kestrel::shell kestrel::options kestrel::warnings kestrel::sanitizers)
target_compile_definitions(kestrel PRIVATE KESTREL_VERSION="${PROJECT_VERSION}")
set_target_properties(kestrel PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)
