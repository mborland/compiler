#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env: print the function NAME of source F from both sides (clang and edg-cir, no passes).
# usage: box_side.sh src.cpp mangled-name [std]
f=$1; fn=$2; std=${3:-17}
W=$PWD; export LD_LIBRARY_PATH=$W/build/cir/lib
mkdir -p build/scratch/side
$W/build/gcc-cir/bin/cpfe-cir --edg_base $W/bases/docker/cir-env/gcc --c++$std --ecil_output=build/scratch/side/x.ecil $f > /dev/null 2>&1
build/cir/bin/edg-cir --emit=cir --disable-cir-passes build/scratch/side/x.ecil > build/scratch/side/ours.cir 2>&1
/edg/llvm-cir/bin/clang -cc1 -triple x86_64-unknown-linux-gnu -x c++ -std=c++$std -fclangir -emit-cir -clangir-disable-passes $f -o build/scratch/side/clang.cir 2>&1 | head -3
for s in clang ours; do echo "######## $s"; sed -e 's/ loc(.*//' -e 's/{llvm[^}]*}//g' build/scratch/side/$s.cir | awk -v n="@$fn(" 'index($0,n){p=1} p{print} p&&/^  }/{exit}'; done
