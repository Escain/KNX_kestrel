kestrel_add_library(dpt STD_ONLY
	SOURCES
		src/BitCodecs.cpp
		src/Detail.cpp
		src/Dpt.cpp
		src/NumericCodecs.cpp
		src/TextCodecs.cpp
	PUBLIC_DEPS kestrel::base
)
