kestrel_add_library(simnet
	SOURCES
		include/kestrel/simnet/KnxIpServer.hpp
		include/kestrel/simnet/ScenarioLoader.hpp
		include/kestrel/simnet/Simulation.hpp
		src/KnxIpServer.cpp
		src/Logging.cpp
		src/Logging.hpp
		src/ScenarioLoader.cpp
		src/Simulation.cpp
	PUBLIC_DEPS kestrel::sim Qt6::Core Qt6::Network
)
