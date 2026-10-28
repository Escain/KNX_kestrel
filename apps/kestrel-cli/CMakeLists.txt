add_executable(kestrel-cli
	main.cpp
	BusCommands.cpp
	CatalogCommands.cpp
	Cli.cpp
	Cli.hpp
	ProjectCommands.cpp
)
target_link_libraries(kestrel-cli PRIVATE
	kestrel::app kestrel::catalog kestrel::persist kestrel::validate kestrel::model kestrel::dpt
	kestrel::options kestrel::warnings kestrel::sanitizers Qt6::Core Qt6::Network)
