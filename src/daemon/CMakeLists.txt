# Everything except main() lives in a library so tests can drive VpnService with a fake CLI.
add_library(agvd-core STATIC
    appexclusionservice.h
    appexclusionservice.cpp
    appmatcher.h
    appmatcher.cpp
    appstore.h
    appstore.cpp
    clirunner.h
    clirunner.cpp
    daemonerror.h
    execwatcher.h
    execwatcher.cpp
    vpnservice.h
    vpnservice.cpp
    hostsystem.h
    hostsystem.cpp
    polkitauthorizer.h
    polkitauthorizer.cpp
    routingengine.h
    routingengine.cpp
    runsocketserver.h
    runsocketserver.cpp
    daemonservice.h
    daemonservice.cpp
)
target_include_directories(agvd-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(agvd-core PUBLIC agv-common Qt6::Core Qt6::DBus Qt6::Network)
target_compile_options(agvd-core PRIVATE -Wall -Wextra -Wpedantic)

set(adaptor_sources)
qt_add_dbus_adaptor(adaptor_sources
    ${PROJECT_SOURCE_DIR}/dbus/org.agv.Daemon1.xml
    daemonservice.h
    agv::DaemonService
    daemon1adaptor
    Daemon1Adaptor
)

add_executable(agvd main.cpp ${adaptor_sources})
target_link_libraries(agvd PRIVATE agvd-core)
target_include_directories(agvd PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(agvd PRIVATE AGV_VERSION="${PROJECT_VERSION}")
target_compile_options(agvd PRIVATE -Wall -Wextra)

install(TARGETS agvd DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
