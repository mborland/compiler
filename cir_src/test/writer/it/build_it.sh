#!/bin/sh
# Builds $OUT (default build/ecil_it/ecil_it): the native cpfe-cp front end
# (objects from build/clang) with its back end replaced by it_driver.c, plus
# the writer sources compiled in test mode.  Run it as
#   ecil_it -d-skip_il_read --c11 file.c
# Needs a built build/clang (ninja bin/cpfe-cp).  Test builds only.
set -e
root=$(cd "$(dirname "$0")/../../../.." && pwd)
here=$root/cir_src/test/writer
out=${OUT:-$root/build/ecil_it}
mkdir -p "$out"
flags="-DBACK_END_IS_CP_GEN_BE=1 -DBACK_END_IS_C_GEN_BE=0 -DUSE_CMAKE_DEFINES"
flags="$flags -I$root/build/clang/src/includes -std=c++14 -g -Wall -Wconversion"
flags="$flags -Wsign-conversion -Werror=shadow -fno-exceptions -fno-rtti"
flags="$flags -x c++ -DECIL_COMPILE_FOR_TEST -DECIL_STUB_DECLS $EXTRA_FLAGS"
flags="$flags -I$root/cir_src/include -I$root/src"
flags="$flags -include $here/unit/ecil_stub.h"
cxx=${CXX:-c++}
for f in ecil_write ecil_sink ecil_type ecil_const ecil_code; do
  $cxx $flags -c "$root/src/$f.c" -o "$out/$f.o" 2>&1 | grep -v "treating 'c'" || true
done
$cxx $flags -Dback_end=cir_be_real_back_end -c "$root/src/cir_be.c" \
     -o "$out/cir_be.o" 2>&1 | grep -v "treating 'c'" || true
$cxx $flags -c "$here/it/it_driver.c" -o "$out/it_driver.o" 2>&1 \
     | grep -v "treating 'c'" || true
objs=$(cd "$root/build/clang" && ls src/cmake/cpfe-cp/CMakeFiles/cpfe-cp.dir/__/__/*.o \
        | grep -v 'cp_gen_be.c.o' | sed "s#^#$root/build/clang/#")
$cxx -arch arm64 $LDFLAGS_EXTRA $objs "$out"/ecil_write.o "$out"/ecil_sink.o "$out"/ecil_type.o \
     "$out"/ecil_const.o "$out"/ecil_code.o "$out"/cir_be.o \
     "$out"/it_driver.o -o "$out/ecil_it"
echo "built $out/ecil_it"
