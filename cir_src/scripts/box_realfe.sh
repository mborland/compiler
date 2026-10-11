#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env (cwd = the tree root): build cpfe-cir (debug), then compare the
# real front end + library against g++ for tests/tests/cir/cirgen/<name>.sft.cpp
# files.  usage: box_realfe.sh [--nobuild] [--c++20] name...
# (an option of the form --c++NN is passed to the front end and g++ as -std=c++NN)
opts=""
if [ "$1" = --nobuild ]; then shift; else
  (cd cir_src && cmake --preset linux-gcc-debug-cir > /dev/null 2>&1)
  ninja -C build/gcc-cir cpfe-cir 2>&1 | grep -E "error|FAILED" | head -20
fi
case "$1" in --c++*) RF_OPTIONS=$1; RF_GCC=-std=${1#--}; shift;; esac
rm -rf .tmp-cmp && mkdir -p .tmp-cmp/files
args=""
for n in "$@"; do
  cp tests/tests/cir/cirgen/$n.sft.cpp .tmp-cmp/files/$n.cpp
  args="$args $n.cpp"
done
echo "set -- $args" > .tmp-cmp/args.sh
[ -n "$RF_OPTIONS" ] && echo "export CMP_OPTIONS='$RF_OPTIONS' CMP_GCC_OPTIONS='$RF_GCC'" >> .tmp-cmp/args.sh
bash cir_src/test/writer/tools/cmp_cxx_box.sh
