#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env: clang's plain CIRGen output for a C++ file (no CIR passes).
# usage: box_clang_cir.sh file.cpp [extra clang -cc1 args]
f=$1; shift
/edg/llvm-cir/bin/clang -cc1 -triple x86_64-unknown-linux-gnu -x c++ -std=c++17 \
  -fclangir -emit-cir -clangir-disable-passes "$@" $f -o -
