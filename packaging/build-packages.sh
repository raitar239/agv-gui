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

# The steps themselves live in packaging/ci, shared with GitHub Actions.
ci="$src/packaging/ci"

for image in "${images[@]}"; do
    echo
    echo "================ $image"
    case "$image" in
        debian:*|ubuntu:*)
            ext=deb
            # Keep downloaded .debs in the cache volume (Docker images delete them by default).
            install_pkg="export DEBIAN_FRONTEND=noninteractive; rm -f /etc/apt/apt.conf.d/docker-clean; apt-get update -qq && apt-get install -y -qq --no-install-recommends /pkg/PKG >/dev/null"
            cache_dir=/var/cache/apt/archives
            ;;
        fedora:*)
            ext=rpm
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
    # The suffix changes with packaging/ci/install-deps.sh, so older builders are not reused.
    builder="agv-gui-builder:$tag-2"
    if $refresh || ! docker image inspect "$builder" >/dev/null 2>&1; then
        echo "== preparing $builder (once; a few minutes)"
        printf 'FROM %s\nCOPY install-deps.sh /tmp/\nRUN bash /tmp/install-deps.sh > /tmp/deps.log 2>&1 || { tail -30 /tmp/deps.log; exit 1; }\n' "$image" \
            | docker build -t "$builder" -f - "$ci" 2>&1 | grep -vE "^(Step |Sending build|DEPRECATED|  *Install the buildx|  *https://docs.docker|  *--->|Removing intermediate|Successfully)" || true
        docker image inspect "$builder" >/dev/null 2>&1 || { echo "Preparing $builder failed" >&2; exit 1; }
    fi

    echo "== build and test"
    docker run --rm -v "$src:/src:ro" -v "$stage:/out" "$builder" bash /src/packaging/ci/build.sh /src /out

    pkg=$(cd "$stage" && ls *."$ext" | head -1)
    name="${pkg%.$ext}-${tag}.$ext"
    mv "$stage/$pkg" "$out/$name"
    rm -rf "$stage"

    echo "== install $name into a fresh $image"
    docker run --rm -v "$out:/pkg:ro" -v "$ci:/ci:ro" -v "agv-gui-cache-$tag:$cache_dir" "$image" \
        bash -c "${install_pkg/PKG/$name} && bash /ci/smoke.sh"
    echo "== $image OK: dist/$name"
done

# Hand the results to the user who ran sudo.
[ -n "${SUDO_USER:-}" ] && chown -R "$SUDO_USER": "$out"
echo
ls -la "$out"
