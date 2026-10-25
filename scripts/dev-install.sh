#!/bin/sh
# Installs a development build of agvd system-wide, or removes it again.
#
#   sudo scripts/dev-install.sh [build-dir]        install and start agvd (not enabled at boot)
#   sudo scripts/dev-install.sh --uninstall        stop and remove it; keeps /var/lib/agvd
#   sudo scripts/dev-install.sh --uninstall --purge  also delete /var/lib/agvd (its login)
#
# Packages install to /usr; this script uses /usr/local and /etc so it never touches files
# owned by the package manager. polkit only reads actions from /usr/share, so that one file is
# the exception.

set -eu

LIBEXEC=/usr/local/libexec/agvd
AGV_RUN=/usr/local/bin/agv-run
AGV_GUI=/usr/local/bin/agv-gui
DESKTOP=/usr/local/share/applications/agv-gui.desktop
UNIT=/etc/systemd/system/agvd.service
DBUS_POLICY=/etc/dbus-1/system.d/org.agv.Daemon1.conf
POLKIT_ACTIONS=/usr/share/polkit-1/actions/org.agv.daemon.policy

if [ "$(id -u)" -ne 0 ]; then
    echo "Run as root: sudo $0 $*" >&2
    exit 1
fi

reload_dbus() {
    systemctl reload dbus.service 2>/dev/null || systemctl reload dbus-broker.service 2>/dev/null || true
}

if [ "${1:-}" = "--uninstall" ]; then
    systemctl stop agvd.service 2>/dev/null || true
    # Routing state outlives the daemon on purpose (excluded apps keep working across restarts),
    # so remove it explicitly.
    [ -x "$LIBEXEC" ] && "$LIBEXEC" --cleanup --data-dir /var/lib/agvd || true
    rm -f "$LIBEXEC" "$AGV_RUN" "$AGV_GUI" "$DESKTOP" "$UNIT" "$DBUS_POLICY" "$POLKIT_ACTIONS"
    systemctl daemon-reload
    reload_dbus
    if [ "${2:-}" = "--purge" ]; then
        rm -rf /var/lib/agvd
        echo "Removed agvd and /var/lib/agvd"
    else
        echo "Removed agvd (kept /var/lib/agvd)"
    fi
    exit 0
fi

src=$(cd "$(dirname "$0")/.." && pwd)
build=${1:-$src/build}

if [ ! -x "$build/src/daemon/agvd" ]; then
    echo "No agvd in $build; build first: cmake --build $build" >&2
    exit 1
fi

install -Dm755 "$build/src/daemon/agvd" "$LIBEXEC"
install -Dm755 "$build/src/run/agv-run" "$AGV_RUN"
if [ -x "$build/src/gui/agv-gui" ]; then
    install -Dm755 "$build/src/gui/agv-gui" "$AGV_GUI"
    install -Dm644 "$src/data/agv-gui.desktop" "$DESKTOP"
fi
sed "s|@AGVD_PATH@|$LIBEXEC|" "$src/data/systemd/agvd.service.in" | install -Dm644 /dev/stdin "$UNIT"
install -Dm644 "$src/data/dbus/org.agv.Daemon1.conf" "$DBUS_POLICY"
install -Dm644 "$src/data/polkit/org.agv.daemon.policy" "$POLKIT_ACTIONS"

systemctl daemon-reload
reload_dbus
systemctl restart agvd.service
systemctl --no-pager --lines=5 status agvd.service || true
