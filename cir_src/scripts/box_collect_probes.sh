#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env, from the tree root: the ECIL stream of every writer probe
# (cir_src/test/writer/probes/**, .c and .cpp) through the box front end, into
# OUTDIR as <path with / as __>.ecil.   usage: box_collect_probes.sh OUTDIR
out=${1:?output directory}
W=$PWD
export EDG_CIR_RT_LIBDIR=$W/build/cir/lib LD_LIBRARY_PATH=$W/build/cir/lib
FE=$W/build/gcc-cir/bin/cpfe-cir
export PATH=$W/dev_tools/bin:$PATH PYTHONPATH=$W/dev_tools/pylibs
VER=$(edg-scrape-compiler gcc version)
sys_includes() {  # the system include directories of gcc for a language
  local d out=
  IFS=:; for d in $(edg-scrape-compiler gcc --lang "$1" includes); do
    out="$out --sys_include=$d"; done
  echo "$out"
}
CXX_SYS=$(sys_includes c++); C_SYS=$(sys_includes c)
mkdir -p "$out"
n=0; ok=0
for f in $(cd cir_src/test/writer/probes && find . -name '*.c' -o -name '*.cpp' | sort); do
  f=${f#./}; name=$(echo "$f" | tr / _)
  n=$((n+1))
  # C probes as C; C++ probes with the first standard that compiles them.
  case $f in
    *.c) stds="--c --gnu_version=$VER $C_SYS";;
    *) stds="--g++ --gnu_version=$VER --c++17 $CXX_SYS|--g++ --gnu_version=$VER --c++20 $CXX_SYS|--g++ --gnu_version=$VER --c++14 $CXX_SYS|--g++ --gnu_version=$VER --c++23 $CXX_SYS";;
  esac
  IFS='|'
  for opt in $stds; do
    unset IFS
    ( cd cir_src/test/writer/probes/$(dirname $f) &&
      $FE --edg_base $W/bases/docker/cir-env/gcc $opt --no_wrap --no_wrap_diagnostics \
          --ecil_output=$W/$out/$name.ecil $(basename $f) > $W/$out/$name.fe 2>&1 )
    grep -q "error" $out/$name.fe || break
    IFS='|'
  done
  unset IFS
  [ -s "$out/$name.ecil" ] && ok=$((ok+1)) || rm -f "$out/$name.ecil"
  grep -q "error" $out/$name.fe && echo "front end errors: $f"
done
echo "$n probe sources, $ok streams in $out"
