#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env: run a .cir file through the pass list of our pipeline
# (lib/driver/pipeline.cpp: runCIRToCIRPasses = canonicalize, target-lowering,
# cxxabi-lowering, lowering-prepare, call-conv-lowering; then the direct
# lowering, which runs hoist-allocas, flatten-cfg, eh-abi-lowering and
# goto-solver) with the pin's cir-opt / cir-translate, and compile the LLVM IR
# to an object file.
#   usage: cir2obj.sh in.cir out.o [-- extra cir-opt args]
# Writes out.o and out.ll (and out.lowered.cir, the CIR after the CIR passes).
L=${EDG_LLVM_CIR:-/edg/llvm-cir}
in=$1; out=$2; shift 2
$L/bin/cir-opt --cir-canonicalize --cir-target-lowering --cir-cxxabi-lowering \
  --cir-lowering-prepare --cir-call-conv-lowering=target=x86_64 "$@" $in \
  -o ${out%.o}.lowered.cir || exit 1
$L/bin/cir-translate --cir-to-llvmir ${out%.o}.lowered.cir -o ${out%.o}.ll || exit 1
# cir-translate leaves its own datalayout; the target default is the right one.
sed -i '/^target datalayout/d' ${out%.o}.ll
$L/bin/clang -c -O0 -Wno-override-module ${out%.o}.ll -o $out
