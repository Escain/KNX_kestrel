kestrel_add_library(sim STD_ONLY
	SOURCES
		src/SimLink.cpp
		src/TunnelServer.cpp
		src/VirtualBus.cpp
		src/VirtualDevice.cpp
	PUBLIC_DEPS kestrel::knx kestrel::dpt
)
