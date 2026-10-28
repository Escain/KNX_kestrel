kestrel_add_qt_test(app BusSession
	SOURCES tst_BusSession.cpp
	LIBS kestrel::app kestrel::simnet Qt6::Network)
