#!/bin/bash
# After installing a package: the binaries run, the system files are in place, the GUI loads.
set -euo pipefail
unit=/usr/lib/systemd/system/agvd.service
test -f "$unit"
# libexec on Debian and Fedora, lib on Arch: the unit knows.
agvd_path=$(sed -n 's/^ExecStart=\([^ ]*\).*/\1/p' "$unit")
"$agvd_path" --version
agv-run --help | head -1
agvctl version
test -f /usr/share/polkit-1/actions/org.agv.daemon.policy
test -f /usr/share/icons/hicolor/scalable/apps/agv-gui.svg
QT_QPA_PLATFORM=offscreen AGV_SCREENSHOT_DELAY=1500 QT_FORCE_STDERR_LOGGING=1 \
    agv-gui --screenshot /tmp/shot.png --size 700x500 2>&1 | tee /tmp/gui.log
test -s /tmp/shot.png
if grep -E "TypeError|ReferenceError|is not available|cannot load|module .* is not installed" /tmp/gui.log; then
    echo "QML errors in the GUI" >&2
    exit 1
fi
echo "smoke test passed"
