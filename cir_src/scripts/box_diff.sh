#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env (cwd = the tree root): the clang CIR shape differential on real
# front-end output.  For each C++ source: cpfe-cir -> edg-cir --emit=cir (no
# CIR passes) against clang -fclangir -emit-cir (passes disabled), normalized
# and diffed per function by cir-compare.py --cxx.  Output in build/scratch/diff.
# usage: box_diff.sh [--nobuild] [--loose] [--passes] file.cpp...
if [ "$1" = --nobuild ]; then shift; else
  (cd cir_src && cmake --preset linux-gcc-debug-cir > /dev/null 2>&1)
  ninja -C build/gcc-cir cpfe-cir 2>&1 | grep -E "error|FAILED" | head
  (cd cir_src && cmake --preset linux-cir-relwithdebinfo > /dev/null 2>&1); ninja -C build/cir 2>&1 | grep -E "error|FAILED" | head
fi
extra=""; out=build/scratch/diff
[ "$1" = --loose ] && { extra=--loose; shift; }
# --passes: both sides run their CIR passes (output in build/scratch/diffp)
[ "$1" = --passes ] && { extra="$extra --passes"; out=build/scratch/diffp; shift; }
W=$PWD
export EDG_CIR_RT_LIBDIR=$W/build/cir/lib LD_LIBRARY_PATH=$W/build/cir/lib
FE=$W/build/gcc-cir/bin/cpfe-cir
# the C++ library search path of the container's g++ (for the spike sources)
CINCS=$(echo | g++ -x c++ -E -v - 2>&1 | sed -n '/search starts here/,/End of search/p' | grep '^ /' | sed 's|^ | --clang-arg=-internal-isystem --clang-arg=|' | tr '\n' ' ')
INCS=$(echo | g++ -x c++ -E -v - 2>&1 | sed -n '/search starts here/,/End of search/p' | grep "^ /" | sed 's|^ | --sys_include |' | tr '\n' ' ')
mkdir -p $out
for f in "$@"; do
  n=$(echo ${f#*/} | tr '/' '_'); n=${n%.cpp}; n=${n%.sft}
  std=$(sed -n 's|^//options:.*--c++\([0-9]*\).*|\1|p' $f | head -1); std=${std:-17}
  rm -f $out/$n.ecil
  # Sources that include system headers need the GNU mode (the container's
  # libstdc++); the others run in the front end's default mode.
  mode=""; fincs=""
  if grep -q '#include *<' $f; then mode="--g++ --gnu_version=150200"; fincs="$INCS"; fi
  $FE --edg_base $W/bases/docker/cir-env/gcc --c++$std $mode $fincs --ecil_output=$out/$n.ecil $f > $out/$n.fe 2>&1
  if [ ! -s $out/$n.ecil ]; then echo "$f: FE-FAIL"; continue; fi
  python3 cir_src/scripts/cir-compare.py --cxx --fe --keep-going $CINCS --cxx-std c++$std $extra --global-order --ecil $out/$n.ecil --c $f > $out/$n.cmp 2>&1
  echo "$f: rc=$? same=$(grep -c '^same ' $out/$n.cmp) diff=$(grep -c '^DIFF ' $out/$n.cmp) only-edg=$(grep -c '^only in edg-cir' $out/$n.cmp) only-clang=$(grep -c '^only in clang' $out/$n.cmp)"
done
