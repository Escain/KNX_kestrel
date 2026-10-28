# knxprod product-database model (specs/07-catalog.md). T1: QtCore only (QXmlStreamReader, not QtXml).
kestrel_add_library(catalog
	SOURCES
		src/Knxprod.cpp
		src/Model.cpp
		src/Parser.cpp
		src/Parser.hpp
	PUBLIC_DEPS kestrel::base Qt6::Core
	PRIVATE_DEPS kestrel::archive
)
