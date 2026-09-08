#!/usr/bin/env bash
set -eo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$project_root"
source /opt/ros/humble/setup.bash
# rosidl's CMake file(STRINGS) handling truncates non-ASCII build paths.
# Keep the ASCII build directory across reboots; installed packages remain in ./install.
cupcup_build_dir="${CUPCUP_BUILD_DIR:-$HOME/.cache/cupcup-build}"
colcon build --build-base "$cupcup_build_dir" --executor sequential "$@" --cmake-args -DBUILD_TESTING=OFF
