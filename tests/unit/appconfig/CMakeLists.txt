kestrel_add_unit_test(appconfig
	SOURCES
		AppConfig_test.cpp
		PrivateCorpus_test.cpp
	LIBS kestrel::appconfig kestrel::catalog
)
