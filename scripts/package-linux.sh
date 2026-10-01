#!/usr/bin/env bash
# Builds a release on Linux and packages dist/OpenPractice-<version>-linux-x64.tar.gz.
#
#   scripts/package-linux.sh            (on Linux, or in WSL from the repository root)
#
# The build happens in /tmp so a checkout on a Windows drive (/mnt/c under WSL) doesn't
# confuse make with skewed file times. Needs g++ 9+, CMake 3.16+ and libglfw3-dev.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="$(sed -n 's/^project(OpenPractice VERSION \([0-9.]*\).*/\1/p' "$repo/CMakeLists.txt")"
name="OpenPractice-$version-linux-x64"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

mkdir -p "$work/src" "$work/pkg/$name"
cp -r "$repo"/{CMakeLists.txt,LICENSE,README.md,include,src,gui,tests,third_party} "$work/src/"

cmake -S "$work/src" -B "$work/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$work/build" -j"$(nproc)"
ctest --test-dir "$work/build" --output-on-failure

strip "$work/build/openpractice" "$work/build/openpractice-gui"
cp "$work/build/openpractice" "$work/build/openpractice-gui" "$repo/README.md" "$repo/LICENSE" "$work/pkg/$name/"
chmod 755 "$work/pkg/$name" "$work/pkg/$name"/openpractice*
chmod 644 "$work/pkg/$name"/{README.md,LICENSE}  # a Windows drive marks every file executable
mkdir -p "$repo/dist"
tar -czf "$repo/dist/$name.tar.gz" --owner=0 --group=0 -C "$work/pkg" "$name"
echo "Wrote dist/$name.tar.gz"
