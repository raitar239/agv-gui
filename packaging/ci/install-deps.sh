#!/bin/bash
# Installs what building and testing AGV needs on this distribution (run as root).
# Used by packaging/build-packages.sh (Docker) and .github/workflows/ci.yml.
set -euo pipefail
. /etc/os-release

case "$ID" in
    debian|ubuntu)
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -q
        apt-get install -y -q --no-install-recommends \
            ca-certificates cmake ninja-build g++ file dpkg-dev libgl-dev git \
            qt6-base-dev qt6-declarative-dev qt6-declarative-dev-tools qt6-tools-dev qt6-tools-dev-tools \
            qt6-l10n-tools qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
            qml6-module-qtquick-shapes qml6-module-qtquick-templates qml6-module-qtquick-window \
            qml6-module-qtqml-workerscript util-linux iproute2 nftables dbus
        ;;
    fedora)
        dnf install -y cmake ninja-build gcc-c++ rpm-build git qt6-qtbase-devel qt6-qtdeclarative-devel \
            qt6-qttools-devel qt6-linguist util-linux iproute nftables dbus-daemon
        ;;
    arch)
        pacman -Syu --noconfirm --needed base-devel cmake ninja git qt6-base qt6-declarative qt6-tools \
            nftables iproute2 polkit dbus
        ;;
    *)
        echo "No dependency list for $ID" >&2
        exit 1
        ;;
esac
