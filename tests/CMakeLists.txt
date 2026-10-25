set(fixtures_dir "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cli")

add_executable(tst_cliparser tst_cliparser.cpp)
target_link_libraries(tst_cliparser PRIVATE agv-common Qt6::Test)
target_compile_definitions(tst_cliparser PRIVATE AGV_FIXTURES_DIR="${fixtures_dir}")
add_test(NAME cliparser COMMAND tst_cliparser)

add_executable(tst_vpnservice tst_vpnservice.cpp)
target_link_libraries(tst_vpnservice PRIVATE agvd-core Qt6::Test)
target_compile_definitions(tst_vpnservice PRIVATE
    AGV_FIXTURES_DIR="${fixtures_dir}"
    AGV_FAKE_CLI="${CMAKE_CURRENT_SOURCE_DIR}/fake-adguardvpn-cli")
add_test(NAME vpnservice COMMAND tst_vpnservice)

add_executable(tst_hostsystem tst_hostsystem.cpp)
target_link_libraries(tst_hostsystem PRIVATE agvd-core Qt6::Test)
target_compile_definitions(tst_hostsystem PRIVATE AGV_FAKE_CLI="${CMAKE_CURRENT_SOURCE_DIR}/fake-adguardvpn-cli")
add_test(NAME hostsystem COMMAND tst_hostsystem)

add_executable(tst_polkit tst_polkit.cpp)
target_link_libraries(tst_polkit PRIVATE agvd-core Qt6::Test)
add_test(NAME polkit COMMAND tst_polkit)

add_executable(tst_routing tst_routing.cpp)
target_link_libraries(tst_routing PRIVATE agvd-core Qt6::Test)
add_test(NAME routing COMMAND tst_routing)

add_executable(tst_agvrun tst_agvrun.cpp)
target_link_libraries(tst_agvrun PRIVATE agvd-core Qt6::Test)
target_compile_definitions(tst_agvrun PRIVATE AGV_RUN_BINARY="$<TARGET_FILE:agv-run>")
add_dependencies(tst_agvrun agv-run)
add_test(NAME agvrun COMMAND tst_agvrun)
