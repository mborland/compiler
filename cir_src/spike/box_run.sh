#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Runs inside the cir-env container, cwd = /edg/workspace.
# SPIKE_PREFIX=/full selects the unpruned install (the pruned prefix lacks
# files referenced by LLVMExports.cmake, so find_package(LLVM) fails there).
cmake -S cir_src/spike -B build/cir-spike -G Ninja \
  -DEDG_LLVM_CIR_PREFIX=${SPIKE_PREFIX:-$EDG_LLVM_CIR_PREFIX} 2>&1 | tail -5
ninja -C build/cir-spike spike 2>&1 | tail -20
