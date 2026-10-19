#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env, from the tree root: configure, build and run ctest for the
# three LLVM presets (relwithdebinfo, release, asan).  usage: box_allpresets.sh
for pd in "linux-cir-relwithdebinfo cir" "linux-cir-release cir-release" \
          "linux-cir-asan cir-asan"; do
  set -- $pd
  echo "=== $1"
  (cd cir_src && cmake --preset $1 -DEDG_CIR_LIT=/lit/lit.py > /dev/null) ||
    echo "CONFIGURE FAILED"
  ninja -C build/$2 2>&1 | grep -E "error|FAILED|warning" | head -20
  (cd build/$2 && ctest 2>&1 | tail -14)
done
echo ALLDONE
