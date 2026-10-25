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
