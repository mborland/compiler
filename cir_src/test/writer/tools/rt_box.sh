#!/bin/bash
# Container side of rt_run.sh: build and run corpus tests through eccp
# (cpfe-cir).  Arguments come from
# .tmp-rt/args.sh; each is "path-under-imported/gnu/c:options".
W=${EDG_WORKSPACE:-/edg/workspace}
cd $W
. build/gcc-cir/environment.sh
export CPFE=$CPFE_CIR
. .tmp-rt/args.sh
mkdir -p .tmp-rt/out; cd .tmp-rt/out
for spec in "$@"; do
  f=${spec%%:*}; opts=${spec#*:}; [ "$opts" = "$spec" ] && opts=""
  n=$(basename $f .sft.c)
  cp $W/tests/tests/imported/gnu/c/$f $n.c
  echo "=== $n [$opts]"
  $W/dev_tools/bin/eccp --c $opts $n.c -o $n.exe 2>&1 | head -5
  if [ -x $n.exe ]; then ./$n.exe; echo "run rc=$?"; fi
done
