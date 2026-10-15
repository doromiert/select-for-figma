#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_dir"
# The CEF subprocess must find the same SDK libraries as the primary process.
app_build_dir="${FIGMA_GTK_BUILD_DIR:-$project_dir/build-flake}"
export LD_LIBRARY_PATH="$app_build_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$app_build_dir/figma-gtk" "$@"
