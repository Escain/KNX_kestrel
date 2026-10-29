# knxprod product-database model (specs/07-catalog.md). T1: QtCore only (QXmlStreamReader, not QtXml).
kestrel_add_library(catalog
	SOURCES
		src/Knxprod.cpp
		src/Model.cpp
		src/Parser.cpp
		src/Parser.hpp
		src/ProductIndex.cpp
		include/kestrel/catalog/ProductIndex.hpp
	PUBLIC_DEPS kestrel::base Qt6::Core
	PRIVATE_DEPS kestrel::archive
)

# The product index: manufacturers' products and the official download of their product files (no files).
qt_add_resources(kestrel_catalog "kestrel_catalog_index"
	PREFIX "/kestrel/catalog"
	BASE data
	FILES data/product-index.json
)
