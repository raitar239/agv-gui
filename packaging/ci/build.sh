#!/bin/bash
# Configures, builds and tests AGV, then makes a .deb or .rpm (by the distribution) in OUT.
#   packaging/ci/build.sh SOURCE_DIR OUT_DIR
set -euo pipefail
src=$1
out=$2
. /etc/os-release
case "$ID" in
    debian|ubuntu) generator=DEB ;;
    fedora) generator=RPM ;;
    *) generator= ;;
esac

build=$(mktemp -d)
cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DCMAKE_INSTALL_LIBEXECDIR=libexec/agv-gui -DBUILD_TESTING=ON
cmake --build "$build"
# The GUI tests talk to a development agvd on a session bus; without one they skip that part.
if command -v dbus-run-session >/dev/null; then
    dbus-run-session -- ctest --test-dir "$build" --output-on-failure
else
    ctest --test-dir "$build" --output-on-failure
fi
if [ -n "$generator" ]; then
    (cd "$build" && cpack -G "$generator")
    mkdir -p "$out"
    cp "$build"/*.deb "$build"/*.rpm "$out"/ 2>/dev/null || true
fi
