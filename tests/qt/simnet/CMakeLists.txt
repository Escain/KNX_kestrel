kestrel_add_qt_test(simnet ScenarioLoader
	SOURCES tst_ScenarioLoader.cpp
	LIBS kestrel::simnet)
kestrel_add_qt_test(simnet KnxIpServer
	SOURCES tst_KnxIpServer.cpp
	LIBS kestrel::simnet Qt6::Network)
