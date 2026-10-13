#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env: the M4 clang CIR shape differential.  For each probe
# (cir_src/test/writer/probes/m4/*.cpp, or the files given) the front end
# (cpfe-cir) makes the ECIL stream, edg-cir --emit=cir (no CIR passes) and
# clang -fclangir -emit-cir are normalized by cir-compare.py --cxx --fe with
# the structor variants kept apart, and cir-op-compare.py compares the
# material op multisets per function.
#   usage: box_diff_m4.sh [files.cpp...]
# Environment: FE_TREE (a tree with build/gcc-cir/bin/cpfe-cir and
# bases/docker/cir-env/gcc, default the cwd), CIR_BUILD (the edg-cir build,
# default build/cir), OUT (default build/scratch/m4diff).
FE_TREE=${FE_TREE:-$PWD}
CIR_BUILD=${CIR_BUILD:-$PWD/build/cir}
OUT=${OUT:-$PWD/build/scratch/m4diff}
S=$(dirname "$0")
export LD_LIBRARY_PATH=$CIR_BUILD/lib EDG_CIR_RT_LIBDIR=$CIR_BUILD/lib
CINCS=$(echo | g++ -x c++ -E -v - 2>&1 | sed -n '/search starts here/,/End of search/p' | grep '^ /' | sed 's|^ | --clang-arg=-internal-isystem --clang-arg=|' | tr '\n' ' ')
INCS=$(echo | g++ -x c++ -E -v - 2>&1 | sed -n '/search starts here/,/End of search/p' | grep "^ /" | sed 's|^ | --sys_include |' | tr '\n' ' ')
rm -rf $OUT; mkdir -p $OUT
files=("$@"); [ ${#files[@]} = 0 ] && files=($FE_TREE/cir_src/test/writer/probes/m4/*.cpp)
for f in "${files[@]}"; do
  n=$(basename $f .cpp)
  opts=$(sed -n 's|^//options:||p' $f | head -1)
  std=$(echo "$opts" | sed -n 's|.*--c++\([0-9]*\).*|\1|p'); std=${std:-17}
  mode=""; fincs=""
  if grep -q '#include *<' $f; then mode="--g++ --gnu_version=150200"; fincs="$INCS"; fi
  exc="--clang-arg=-fcxx-exceptions --clang-arg=-fexceptions"
  case "$opts" in *no_exceptions*) exc="";; esac
  timeout 120 $FE_TREE/build/gcc-cir/bin/cpfe-cir --edg_base $FE_TREE/bases/docker/cir-env/gcc $opts --c++$std $mode $fincs --ecil_output=$OUT/$n.ecil $f > $OUT/$n.fe 2>&1
  if [ ! -s $OUT/$n.ecil ]; then echo "$n: FE-FAIL"; continue; fi
  timeout 300 python3 $S/cir-compare.py --cxx --no-structor-fold --dump $OUT/$n.dump --fe --keep-going $CINCS $exc --cxx-std c++$std --global-order --edg-cir $CIR_BUILD/bin/edg-cir --ecil $OUT/$n.ecil --c $f > $OUT/$n.cmp 2>&1
  echo "$n: rc=$? $(head -1 $OUT/$n.cmp | grep -q failed && head -1 $OUT/$n.cmp)"
done
python3 $S/cir-op-compare.py --top 40 --categorize --groups 25 $OUT/*.dump > $OUT/summary.txt 2>&1
head -12 $OUT/summary.txt
