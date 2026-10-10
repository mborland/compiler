#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env, from the repo root: configure (with lit), build and test.
#   cir_src/scripts/box_all.sh [preset]
set -e
preset=${1:-linux-cir-relwithdebinfo}
case "$preset" in
  linux-cir-relwithdebinfo) dir=cir ;;
  linux-cir-release) dir=cir-release ;;
  linux-cir-asan) dir=cir-asan ;;
  *) echo "unknown preset $preset" >&2; exit 2 ;;
esac
lit_arg=
[ -f /lit/lit.py ] && lit_arg=-DEDG_CIR_LIT=/lit/lit.py
(cd cir_src && cmake --preset "$preset" $lit_arg > /dev/null)
ninja -C build/$dir 2>&1 | grep -E "error|warning|FAILED" || true
(cd build/$dir && ctest 2>&1 | tail -14)
