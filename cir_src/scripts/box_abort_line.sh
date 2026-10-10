#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env, repo root: like box_fe_run.sh, but the Nth abort () becomes
# exit (N); the failing check's source text is then printed.
# usage: box_abort_line.sh file.c [opts]
f=$1; shift
d=/tmp/abl; rm -rf $d; mkdir -p $d
python3 - "$f" $d/in.c <<'P'
import re, sys
src = open(sys.argv[1]).read()
n = [0]
def sub(m):
    n[0] += 1
    return "exit (%d)" % n[0]
out = re.sub(r"\babort\s*\(\s*\)", sub, src)
open(sys.argv[2], "w").write(out)
P
r=$(bash "$(dirname "$0")/box_fe_run.sh" $d/in.c "$@" 2>&1 | tail -3)
echo "$r"
code=$(echo "$r" | sed -n 's/^exit=//p')
if [ -n "$code" ] && [ "$code" != 0 ]; then
  python3 - $d/in.c $code <<'P'
import re, sys
src = open(sys.argv[1]).read()
k = sys.argv[2]
i = src.find("exit (%s)" % k)
print("failing check:", src[max(0, i - 160):i + 20].replace("\n", " "))
P
fi
