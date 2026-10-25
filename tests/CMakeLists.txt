add_executable(tst_cliparser tst_cliparser.cpp)
target_link_libraries(tst_cliparser PRIVATE agv-common Qt6::Test)
target_compile_definitions(tst_cliparser PRIVATE
    AGV_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cli")
add_test(NAME cliparser COMMAND tst_cliparser)
