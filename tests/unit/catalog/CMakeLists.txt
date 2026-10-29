kestrel_add_unit_test(catalog
	SOURCES
		Catalog_test.cpp
		Knxprod_test.cpp
		ProductIndex_test.cpp
		CatalogFixture.hpp
	LIBS kestrel::catalog kestrel::archive
)
target_compile_definitions(unit_catalog PRIVATE KESTREL_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
