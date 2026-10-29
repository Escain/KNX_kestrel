kestrel_add_library(app
	SOURCES
		include/kestrel/app/BusSession.hpp
		include/kestrel/app/ProductFetcher.hpp
		include/kestrel/app/TelegramRecord.hpp
		src/BusSession.cpp
		src/BusWorker.cpp
		src/BusWorker.hpp
		src/ProductFetcher.cpp
		src/TelegramRecord.cpp
	PUBLIC_DEPS kestrel::bus kestrel::dpt kestrel::download kestrel::catalog Qt6::Core Qt6::Network
)
