#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env: stream + compile one corpus case, keeping the ecil.
# usage: box_crash.sh path-under-tests/tests [front end options...]
f=tests/tests/$1; shift
W=$PWD; export EDG_CIR_RT_LIBDIR=$W/build/cir/lib LD_LIBRARY_PATH=$W/build/cir/lib
n=$(basename $f .sft.cpp); mkdir -p build/scratch/crash
INCS=$(echo | g++ -x c++ -E -v - 2>&1 | sed -n '/search starts here/,/End of search/p' | grep "^ /" | sed 's|^ | --sys_include |' | tr '\n' ' ')
$W/build/gcc-cir/bin/cpfe-cir --edg_base $W/bases/docker/cir-env/gcc --g++ --gnu_version=999999 -tused -e 200 --no_wrap --no_wrap_diagnostics -DTEST_NUMBER=1 "$@" --ecil_output=build/scratch/crash/$n.ecil $f > build/scratch/crash/$n.fe 2>&1; echo "fe rc=$?"; head -5 build/scratch/crash/$n.fe
build/cir/bin/edg-cir --emit=asm -o build/scratch/crash/$n.s build/scratch/crash/$n.ecil 2>&1 | head -12; echo "lib rc=${PIPESTATUS[0]}"
