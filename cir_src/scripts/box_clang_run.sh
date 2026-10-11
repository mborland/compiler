#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env: compile a C++ file with the pin's clang -fclangir, run it.
# usage: box_clang_run.sh file.cpp
f=$1
mkdir -p build/scratch
/edg/llvm-cir/bin/clang -fclangir -O0 $f -o build/scratch/ref.exe -lstdc++ 2>&1 | head -20
build/scratch/ref.exe; echo " rc=$?"
