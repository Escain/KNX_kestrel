kestrel_add_library(model
	SOURCES
		src/ChangeSet.cpp
		src/Entities.cpp
		src/History.cpp
		src/Id.cpp
		src/Operations.cpp
		src/Project.cpp
	PUBLIC_DEPS kestrel::base kestrel::knx Qt6::Core
)
