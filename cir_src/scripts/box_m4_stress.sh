#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env, at the tree root: the M4 stress set (ecil-m4-stress-gen.py)
# through edg-cir under a 256 KiB main-thread stack, with --stats (stage
# times) and the peak RSS.  usage: box_m4_stress.sh kind:n [kind:n ...]
# Prints: kind n total_ms parse_ms cirgen_ms passes_ms lower_ms codegen_ms rss_kb
B=${EDG_CIR_BUILD:-build/cir}
asm=$B/tools/ecil-asm/ecil-asm
cir=$B/bin/edg-cir
gen=cir_src/scripts/ecil-m4-stress-gen.py
D=${STRESS_DIR:-/tmp/m4stress}
mkdir -p $D
for c in "$@"; do
  k=${c%%:*}; n=${c##*:}
  python3 $gen $k $n > $D/$k.$n.txt || { echo "$c GEN-FAIL"; continue; }
  $asm $D/$k.$n.txt -o $D/$k.$n.ecil 2> $D/$k.$n.asm.err || { echo "$c ASM-FAIL $(head -c 200 $D/$k.$n.asm.err)"; continue; }
  s=$(date +%s%N)
  ( ulimit -s 256; /usr/bin/env time -f "RSS %M" timeout ${TMO:-300} $cir --stats ${EXTRA:-} --emit=obj -o $D/$k.$n.o $D/$k.$n.ecil ) > $D/$k.$n.out 2> $D/$k.$n.err
  rc=$?
  e=$(date +%s%N)
  st=$(grep -h "decode+validate" $D/$k.$n.err | sed -E 's/.*decode\+validate ([0-9.]+) ms, emit ([0-9.]+) ms, finish ([0-9.]+) ms, verify ([0-9.]+) ms, cir passes ([0-9.]+) ms, lower ([0-9.]+) ms, codegen ([0-9.]+) ms.*/\1 \2 \3 \4 \5 \6 \7/')
  printf '%-8s %6d rc=%d total_ms=%d stages(decode emit finish verify passes lower codegen)=%s rss=%s %s\n' "$k" "$n" $rc $(( (e - s) / 1000000 )) "$st" "$(grep -h '^RSS' $D/$k.$n.err | cut -d' ' -f2)" "$(grep -hiE 'error' $D/$k.$n.err | head -1 | cut -c1-100)"
done
