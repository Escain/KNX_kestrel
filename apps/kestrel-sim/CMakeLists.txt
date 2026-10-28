add_executable(kestrel-sim
	main.cpp
	SimController.cpp
	SimController.hpp
	StdinReader.cpp
	StdinReader.hpp
)
target_link_libraries(kestrel-sim PRIVATE
	kestrel::simnet kestrel::options kestrel::warnings kestrel::sanitizers Qt6::Core Qt6::Network)
