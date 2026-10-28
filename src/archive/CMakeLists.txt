# Own ZIP reader (ADR-004): C++ standard library plus system zlib, no Qt.
find_package(ZLIB REQUIRED)

kestrel_add_library(archive STD_ONLY
	SOURCES
		src/ZipReader.cpp
	PUBLIC_DEPS kestrel::base
	PRIVATE_DEPS ZLIB::ZLIB
)
