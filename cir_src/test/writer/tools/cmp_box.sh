#!/bin/bash
# Container side of cmp_run.sh: compile each file with gcc and with eccp
# (cpfe-cir), run both, and diff their output.  Arguments come from
# .tmp-cmp/args.sh ("set -- file.c ..."), the files from .tmp-cmp/files/.
# Run in the cir-env container; the repository is at $EDG_WORKSPACE.
W=${EDG_WORKSPACE:-/edg/workspace}
cd $W
. build/gcc-cir/environment.sh
export CPFE=$CPFE_CIR
. .tmp-cmp/args.sh
cd .tmp-cmp/files
for f in "$@"; do
  n=${f%.c}
  echo "=== $n"
  gcc -w ${CMP_GCC_OPTIONS:-} -o $n.gcc $f 2>&1 | head -3
  $W/dev_tools/bin/eccp --c --gcc ${CMP_OPTIONS:-} $f -o $n.cir 2>&1 | head -40
  ./$n.gcc > $n.gcc.out 2>&1; echo "gcc rc=$?"
  ./$n.cir > $n.cir.out 2>&1; echo "cir rc=$?"
  diff $n.gcc.out $n.cir.out > $n.diff && echo SAME || { echo DIFF; head -20 $n.diff; }
done
