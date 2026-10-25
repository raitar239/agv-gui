set(AGVD_PATH ${CMAKE_INSTALL_FULL_LIBEXECDIR}/agvd)

configure_file(systemd/agvd.service.in ${CMAKE_CURRENT_BINARY_DIR}/agvd.service @ONLY)

set(SYSTEMD_SYSTEM_UNIT_DIR ${CMAKE_INSTALL_PREFIX}/lib/systemd/system
    CACHE PATH "Directory for systemd system units")

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/agvd.service DESTINATION ${SYSTEMD_SYSTEM_UNIT_DIR})
install(FILES dbus/org.agv.Daemon1.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system.d)
install(FILES dbus/org.agv.Daemon1.service DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system-services)
install(FILES polkit/org.agv.daemon.policy DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
