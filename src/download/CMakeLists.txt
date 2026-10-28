kestrel_add_library(download STD_ONLY
	SOURCES
		src/Executor.cpp
		src/Plan.cpp
		src/Resolver.cpp
	PUBLIC_DEPS kestrel::base kestrel::knx
)
