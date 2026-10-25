# .deb and .rpm through CPack:  cpack -G DEB  /  cpack -G RPM  (from the build directory).
# Configure with -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBEXECDIR=libexec/agv-gui.
# packaging/build-packages.sh does all of it inside containers of each distribution.

set(CPACK_PACKAGE_NAME agv-gui)
set(CPACK_PACKAGE_VENDOR "agv-gui")
set(CPACK_PACKAGE_CONTACT "Raitar <233604013+raitar239@users.noreply.github.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/raitar239/agv-gui")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "VPN client for AdGuard VPN CLI with per-app exclusions")
set(CPACK_PACKAGE_DESCRIPTION
    "AGV controls the AdGuard VPN command-line client from a Qt window and the tray, and excludes
chosen applications from the VPN (or sends only them through it) with cgroups and nftables.
Not affiliated with AdGuard. The AdGuard VPN CLI itself is installed separately.")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
set(CPACK_STRIP_FILES ON)

set(_scripts "${PROJECT_SOURCE_DIR}/packaging/scripts")

# Debian / Ubuntu
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION net)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)  # Qt libraries from the binaries
# QML modules are loaded at run time, so shlibdeps cannot see them.
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-shapes, qml6-module-qtquick-templates, qml6-module-qtquick-window, qml6-module-qtqml-workerscript, nftables, iproute2, polkitd | policykit-1, systemd")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "xdg-desktop-portal")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${_scripts}/postinst;${_scripts}/prerm;${_scripts}/postrm")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)

# Fedora / openSUSE
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Internet")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtdeclarative, nftables, iproute, polkit, systemd")
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${_scripts}/postinst")
set(CPACK_RPM_PRE_UNINSTALL_SCRIPT_FILE "${_scripts}/prerm")
set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${_scripts}/postrm")
# Directories owned by the filesystem/systemd/dbus/polkit packages.
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/lib/systemd /usr/lib/systemd/system /usr/libexec /usr/share/applications
    /usr/share/dbus-1 /usr/share/dbus-1/system.d /usr/share/dbus-1/system-services
    /usr/share/polkit-1 /usr/share/polkit-1/actions)

include(CPack)
