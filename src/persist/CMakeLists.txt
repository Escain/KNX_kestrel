kestrel_add_library(persist
	SOURCES
		src/Persist.cpp
	PUBLIC_DEPS kestrel::model Qt6::Core
)
target_compile_definitions(kestrel_persist PRIVATE KESTREL_VERSION_STRING="${PROJECT_VERSION}")
