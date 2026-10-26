#!/bin/bash
# After pushing tag vVERSION: points packaging/aur/agv-gui at it, fills in the checksum of
# GitHub's tarball and regenerates .SRCINFO for both AUR packages.
#   packaging/aur/update.sh 0.2.0
# Then copy each folder into its AUR git repository (ssh://aur@aur.archlinux.org/<name>.git),
# commit and push. The tarball is only downloadable once the GitHub repository is public.
set -euo pipefail
version=${1:?usage: $0 VERSION}
dir=$(cd "$(dirname "$0")" && pwd)

url="https://github.com/raitar239/agv-gui/archive/refs/tags/v$version.tar.gz"
sum=$(curl -fsSL "$url" | sha256sum | cut -d' ' -f1)
sed -i "s/^pkgver=.*/pkgver=$version/; s/^pkgrel=.*/pkgrel=1/; s/^sha256sums=.*/sha256sums=('$sum')/" "$dir/agv-gui/PKGBUILD"

for name in agv-gui agv-gui-git; do
    (cd "$dir/$name" && makepkg --printsrcinfo > .SRCINFO)
done
echo "agv-gui $version: sha256 $sum"
