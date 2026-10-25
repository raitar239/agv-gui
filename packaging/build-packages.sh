#!/bin/bash
# Builds .deb and .rpm packages in containers of each distribution, runs the tests there, then
# installs every package into a fresh container to check its dependencies and binaries:
#
#   sudo packaging/build-packages.sh                    # debian:12 ubuntu:24.04 fedora:latest
#   sudo packaging/build-packages.sh debian:13          # any of those images
#   sudo packaging/build-packages.sh --refresh          # also update images and build tools
#
# Build dependencies are installed once into agv-gui-builder:<distro> images, and downloaded
# packages are kept in agv-gui-cache-* volumes, so later runs download almost nothing.
# Remove them with: docker rmi agv-gui-builder:...; docker volume rm agv-gui-cache-...
#
# Packages land in dist/. Needs Docker (started for the run if it is stopped, not enabled).

set -euo pipefail

src=$(cd "$(dirname "$0")/.." && pwd)
out="$src/dist"
refresh=false
images=()
for arg in "$@"; do
    case "$arg" in
        --refresh) refresh=true ;;
        *) images+=("$arg") ;;
    esac
done
[ ${#images[@]} -gt 0 ] || images=(debian:12 ubuntu:24.04 fedora:latest)

if [ "$(id -u)" -ne 0 ]; then
    echo "Docker needs root: sudo $0 $*" >&2
    exit 1
fi
if ! systemctl is-active --quiet docker; then
    echo "== starting docker for this run"
    systemctl start docker
fi
mkdir -p "$out"

deb_build_deps="ca-certificates cmake ninja-build g++ file dpkg-dev libgl-dev \
    qt6-base-dev qt6-declarative-dev qt6-declarative-dev-tools qt6-tools-dev qt6-tools-dev-tools \
    qt6-l10n-tools qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
    qml6-module-qtquick-shapes qml6-module-qtquick-templates qml6-module-qtquick-window \
    qml6-module-qtqml-workerscript util-linux iproute2 nftables"
rpm_build_deps="cmake ninja-build gcc-c++ rpm-build qt6-qtbase-devel qt6-qtdeclarative-devel \
    qt6-qttools-devel qt6-linguist util-linux iproute nftables"

# Inside the build container: configure, build, test, package.
build_script='
set -e
cmake -S /src -B /tmp/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DCMAKE_INSTALL_LIBEXECDIR=libexec/agv-gui -DBUILD_TESTING=ON
cmake --build /tmp/build
ctest --test-dir /tmp/build --output-on-failure
cd /tmp/build && cpack -G "$GENERATOR"
cp /tmp/build/*."$EXT" /out/
'

# Inside a fresh container: the package must install with its dependencies and start.
smoke_script='
set -e
agvd_path=/usr/libexec/agv-gui/agvd
"$agvd_path" --version
agv-run --help | head -1
test -f /usr/lib/systemd/system/agvd.service
grep -q "ExecStart=$agvd_path" /usr/lib/systemd/system/agvd.service
test -f /usr/share/polkit-1/actions/org.agv.daemon.policy
QT_QPA_PLATFORM=offscreen AGV_SCREENSHOT_DELAY=1500 QT_FORCE_STDERR_LOGGING=1 \
    agv-gui --screenshot /tmp/shot.png --size 700x500 2>&1 | tee /tmp/gui.log
test -s /tmp/shot.png
if grep -E "TypeError|ReferenceError|is not available|cannot load|module .* is not installed" /tmp/gui.log; then
    echo "QML errors in the GUI" >&2
    exit 1
fi
echo "smoke test passed"
'

for image in "${images[@]}"; do
    echo
    echo "================ $image"
    case "$image" in
        debian:*|ubuntu:*)
            generator=DEB ext=deb
            install_deps="set -o pipefail; export DEBIAN_FRONTEND=noninteractive; echo '-- installing build dependencies (a few minutes)'; apt-get update -q && apt-get install -y -q --no-install-recommends $deb_build_deps | { grep -E '^(Get|Setting up)' || true; } | cut -c1-100"
            # Keep downloaded .debs in the cache volume (Docker images delete them by default).
            install_pkg="export DEBIAN_FRONTEND=noninteractive; rm -f /etc/apt/apt.conf.d/docker-clean; apt-get update -qq && apt-get install -y -qq --no-install-recommends /pkg/PKG >/dev/null"
            cache_dir=/var/cache/apt/archives
            ;;
        fedora:*)
            generator=RPM ext=rpm
            install_deps="set -o pipefail; echo '-- installing build dependencies (a few minutes)'; dnf install -y $rpm_build_deps | { grep -E '^ *(Installing|Upgrading)' || true; } | cut -c1-100"
            install_pkg="dnf install -y -q --setopt=keepcache=1 /pkg/PKG >/dev/null"
            cache_dir=/var/cache/dnf
            ;;
        *)
            echo "Unsupported image $image (use debian:*, ubuntu:* or fedora:*)" >&2
            exit 1
            ;;
    esac

    tag=$(echo "$image" | tr ':/' '--')
    stage="$out/.stage-$tag"
    rm -rf "$stage" && mkdir -p "$stage"

    if $refresh || ! docker image inspect "$image" >/dev/null 2>&1; then
        docker pull -q "$image" >/dev/null
    fi
    builder="agv-gui-builder:$tag"
    if $refresh || ! docker image inspect "$builder" >/dev/null 2>&1; then
        echo "== preparing $builder (once; a few minutes)"
        printf 'FROM %s\nSHELL ["/bin/bash", "-c"]\nRUN %s\n' "$image" "$install_deps" | docker build -t "$builder" - 2>&1 | grep -vE "^(Step |Sending build|DEPRECATED|  *Install the buildx|  *https://docs.docker|  *--->|Removing intermediate|Successfully)" || true
        docker image inspect "$builder" >/dev/null 2>&1 || { echo "Preparing $builder failed" >&2; exit 1; }
    fi

    echo "== build and test"
    docker run --rm -v "$src:/src:ro" -v "$stage:/out" -e GENERATOR=$generator -e EXT=$ext "$builder" \
        bash -c "$build_script"

    pkg=$(cd "$stage" && ls *."$ext" | head -1)
    name="${pkg%.$ext}-${tag}.$ext"
    mv "$stage/$pkg" "$out/$name"
    rm -rf "$stage"

    echo "== install $name into a fresh $image"
    docker run --rm -v "$out:/pkg:ro" -v "agv-gui-cache-$tag:$cache_dir" "$image" \
        bash -c "${install_pkg/PKG/$name} && $smoke_script"
    echo "== $image OK: dist/$name"
done

# Hand the results to the user who ran sudo.
[ -n "${SUDO_USER:-}" ] && chown -R "$SUDO_USER": "$out"
echo
ls -la "$out"
