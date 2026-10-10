#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Extra M1 checks, run inside cir-env from the repo root after box_build.sh.
set -e
B=build/cir
G=cir_src/test/writer/golden
echo "== format tests under a 256 KB stack"
sh -c "ulimit -s 256; $B/test/format/format_tests cir_src/test/format/fixtures $G"
python3 -I -c "import sys;open('/tmp/e.ecil','wb').write(bytes(int(x,16) for x in open('$G/empty.hex').read().split()))"
echo "== ecil-dump of the writer's empty.hex"
$B/tools/ecil-dump/ecil-dump /tmp/e.ecil
echo "== edg-cir --emit=asm"
$B/bin/edg-cir --emit=asm /tmp/e.ecil | head -8
echo "== sizes"
strip -o /tmp/stripped.so $B/lib/libedg_cir.so.0.1.0
ls -l $B/lib/libedg_cir.so.0.1.0 /tmp/stripped.so
echo "== edg-cir under a 256 KB main stack"
sh -c "ulimit -s 256; $B/bin/edg-cir --emit=obj -O2 -o /tmp/x.o /tmp/e.ecil && echo ok"
echo "== time and peak RSS"
/usr/bin/time -v $B/bin/edg-cir --emit=obj -o /tmp/x.o /tmp/e.ecil 2>&1 | grep -E "Maximum resident|Elapsed"
echo "== nm -D --defined-only"
nm -D --defined-only $B/lib/libedg_cir.so
echo "== readelf of the object"
readelf -h /tmp/x.o | head -12
