kestrel_add_qt_test(bus UdpTunnelLink
	SOURCES tst_UdpTunnelLink.cpp
	LIBS kestrel::bus kestrel::simnet Qt6::Network)
kestrel_add_qt_test(bus Discovery
	SOURCES tst_Discovery.cpp
	LIBS kestrel::bus kestrel::simnet Qt6::Network)
