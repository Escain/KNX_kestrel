kestrel_add_library(knx STD_ONLY
	SOURCES
		src/Address.cpp
		src/Apdu.cpp
		src/BusLink.cpp
		src/Cemi.cpp
		src/IaProgrammer.cpp
		src/KnxIp.cpp
		src/ManagementSession.cpp
		src/SerialNumber.cpp
		src/TunnelClient.cpp
	PUBLIC_DEPS kestrel::base
)
