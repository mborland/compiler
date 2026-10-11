#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env: assemble test/cirgen/<name>.test (header08_cxx) and diff the
# CIR against clang's for a C++ source with cir-compare.py --cxx.
# usage: box_compare_cxx.sh <name> <source.cpp> [cir-compare args]
n=$1; src=$2; shift 2
mkdir -p build/scratch
cat cir_src/test/cirgen/Inputs/header08_cxx.txt cir_src/test/cirgen/$n.test > build/scratch/$n.txt || exit 1
build/cir/tools/ecil-asm/ecil-asm build/scratch/$n.txt -o build/scratch/$n.ecil || exit 1
python3 cir_src/scripts/cir-compare.py --cxx --ecil build/scratch/$n.ecil --c "$src" "$@"
