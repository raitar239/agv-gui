set(fixtures_dir "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cli")

add_executable(tst_cliparser tst_cliparser.cpp)
target_link_libraries(tst_cliparser PRIVATE agv-common Qt6::Test)
target_compile_definitions(tst_cliparser PRIVATE AGV_FIXTURES_DIR="${fixtures_dir}")
add_test(NAME cliparser COMMAND tst_cliparser)

add_executable(tst_siteexclusion tst_siteexclusion.cpp)
target_link_libraries(tst_siteexclusion PRIVATE agv-common Qt6::Test)
add_test(NAME siteexclusion COMMAND tst_siteexclusion)

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

add_executable(tst_appmatcher tst_appmatcher.cpp)
target_link_libraries(tst_appmatcher PRIVATE agvd-core Qt6::Test)
add_test(NAME appmatcher COMMAND tst_appmatcher)

if(AGV_BUILD_GUI)
    add_executable(tst_installedapps tst_installedapps.cpp ${PROJECT_SOURCE_DIR}/src/gui/installedapps.cpp)
    target_include_directories(tst_installedapps PRIVATE ${PROJECT_SOURCE_DIR}/src/gui)
    target_link_libraries(tst_installedapps PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME installedapps COMMAND tst_installedapps)

    add_executable(tst_keyedlistmodel tst_keyedlistmodel.cpp ${PROJECT_SOURCE_DIR}/src/gui/keyedlistmodel.cpp)
    target_include_directories(tst_keyedlistmodel PRIVATE ${PROJECT_SOURCE_DIR}/src/gui)
    # Gui: QAbstractItemModelTester checks the decoration role for QPixmap/QIcon values.
    target_link_libraries(tst_keyedlistmodel PRIVATE Qt6::Gui Qt6::Test)
    add_test(NAME keyedlistmodel COMMAND tst_keyedlistmodel)

    # The real GUI, offscreen: a window is opened, closed and opened again in one process (as the
    # tray does), then rendered. Any QML error on the way fails the test.
    add_test(NAME gui_reopen
        COMMAND agv-gui --screenshot ${CMAKE_CURRENT_BINARY_DIR}/gui_reopen.png --size 700x500)
    set_tests_properties(gui_reopen PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1;AGV_SCREENSHOT_REOPEN=1;AGV_SCREENSHOT_DELAY=1500;AGV_SESSION_BUS=1"
        FAIL_REGULAR_EXPRESSION "TypeError|ReferenceError|Unable to assign|is not available|registerSingletonInstance|cannot load|did not free"
        TIMEOUT 30)

    # Same at a fractional scale, where the software renderer behaves differently.
    add_test(NAME gui_fractional_scale
        COMMAND agv-gui --screenshot ${CMAKE_CURRENT_BINARY_DIR}/gui_fractional_scale.png --size 700x500)
    set_tests_properties(gui_fractional_scale PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=1.3333334;QT_FORCE_STDERR_LOGGING=1;AGV_SCREENSHOT_DELAY=1500;AGV_SESSION_BUS=1"
        FAIL_REGULAR_EXPRESSION "TypeError|ReferenceError|Unable to assign|is not available|registerSingletonInstance|cannot load|did not free"
        TIMEOUT 30)
endif()
