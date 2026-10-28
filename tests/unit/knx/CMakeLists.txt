kestrel_add_unit_test(knx
	SOURCES
		Address_test.cpp
		Apdu_test.cpp
		Cemi_test.cpp
		IaProgrammer_test.cpp
		KnxIp_test.cpp
		ManagementSession_test.cpp
		TunnelClient_test.cpp
	LIBS kestrel::knx
)
