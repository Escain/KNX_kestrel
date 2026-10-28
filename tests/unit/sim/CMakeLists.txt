kestrel_add_unit_test(sim
	SOURCES
		Integration_test.cpp
		TunnelServer_test.cpp
		VirtualBus_test.cpp
		VirtualDevice_test.cpp
	LIBS kestrel::sim
)
