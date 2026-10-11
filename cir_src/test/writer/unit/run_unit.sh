#!/bin/sh
# Mac fast loop for the ECIL writer (no CIR library, no cpfe-cir needed).
# Compiles ecil_write.c, ecil_sink.c and cir_be.c with the FE headers of the
# native debug build (build/clang) in test mode (ECIL_COMPILE_FOR_TEST), links
# them with harness.c and stub FE symbols, writes streams and compares them
# with golden/*.hex.  Usage: run_unit.sh [-r]   (-r re-records the goldens)
set -e
root=$(cd "$(dirname "$0")/../../../.." && pwd)
here=$root/cir_src/test/writer
tmp=${TMPDIR:-/tmp}/ecil-unit.$$
mkdir -p "$tmp"
flags="-DBACK_END_IS_CP_GEN_BE=1 -DBACK_END_IS_C_GEN_BE=0 -DUSE_CMAKE_DEFINES"
flags="$flags -I$root/build/clang/src/includes -std=c++14 -g -Wall -Wconversion"
flags="$flags -Wsign-conversion -Werror=shadow -fno-exceptions -fno-rtti"
flags="$flags -x c++ -DECIL_COMPILE_FOR_TEST -DECIL_STUB_DECLS"
flags="$flags -I$root/cir_src/include -I$root/src"
flags="$flags -include $here/unit/ecil_stub.h"
cxx=${CXX:-c++}
for f in "$root/src/ecil_write.c" "$root/src/ecil_sink.c" \
         "$root/src/ecil_type.c" "$root/src/ecil_const.c" "$root/src/ecil_code.c" "$root/src/cir_be.c" "$here/unit/harness.c" "$here/unit/stubs.c"; do
  $cxx $flags -c "$f" -o "$tmp/$(basename "$f" .c).o" 2>&1 \
    | grep -v "treating 'c' input" || true
done
$cxx "$tmp"/*.o -o "$tmp/h"
status=0
run() {  # name, harness-mode
  "$tmp/h" "$tmp/$1.ecil" "$2" 2>"$tmp/$1.err" || true
  "$tmp/h" "$tmp/$1.2.ecil" "$2" 2>/dev/null || true
  cmp -s "$tmp/$1.ecil" "$tmp/$1.2.ecil" || { echo "$1: NOT DETERMINISTIC"; status=1; }
  od -An -v -tx1 "$tmp/$1.ecil" > "$tmp/$1.hex"
  if [ "$RERECORD" = 1 ]; then cp "$tmp/$1.hex" "$here/golden/$1.hex"; fi
  if cmp -s "$tmp/$1.hex" "$here/golden/$1.hex"; then echo "$1: ok"
  else echo "$1: MISMATCH"; diff "$tmp/$1.hex" "$here/golden/$1.hex" | head; status=1; fi
}
[ "$1" = "-r" ] && RERECORD=1
run empty 0
run abort 1
"$tmp/h" "$tmp/reasons.ecil" 2 || status=1
rm -rf "$tmp"
exit $status
