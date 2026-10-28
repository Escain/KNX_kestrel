kestrel_add_library(bus
	SOURCES
		include/kestrel/bus/Discovery.hpp
		include/kestrel/bus/Endpoint.hpp
		include/kestrel/bus/Logging.hpp
		include/kestrel/bus/UdpTunnelLink.hpp
		src/Discovery.cpp
		src/Endpoint.cpp
		src/Logging.cpp
		src/UdpTunnelLink.cpp
	PUBLIC_DEPS kestrel::knx Qt6::Core Qt6::Network
)
