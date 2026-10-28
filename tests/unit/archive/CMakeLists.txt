kestrel_add_unit_test(archive
	SOURCES
		ZipReader_test.cpp
	LIBS kestrel::archive
)
target_compile_definitions(unit_archive PRIVATE KESTREL_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
