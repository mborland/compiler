#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Runs INSIDE the cir-env container (cwd = the repo root, e.g. /edg/workspace):
#   cir_src/scripts/box_build.sh [preset] [ninja args...]
# Configures cir_src with a preset (default linux-cir-relwithdebinfo) and
# builds it; the toolchain prefix comes from EDG_LLVM_CIR_PREFIX.
set -e
preset=${1:-linux-cir-relwithdebinfo}
shift || true
case "$preset" in
  linux-cir-relwithdebinfo) dir=cir ;;
  linux-cir-release) dir=cir-release ;;
  linux-cir-asan) dir=cir-asan ;;
  cir-format-only) dir=cir-format ;;
  *) echo "unknown preset $preset" >&2; exit 2 ;;
esac
(cd cir_src && cmake --preset "$preset") 2>&1 | tail -8
ninja -C build/$dir "$@"
