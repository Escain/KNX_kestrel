# Acceptance suites: kestrel-cli against a kestrel-sim process (specs/05-test-strategy.md).
if(NOT TARGET kestrel-cli OR NOT TARGET kestrel-sim)
	return()
endif()

kestrel_add_robot_suite(bus_simulator)
kestrel_add_robot_suite(project_cli)
