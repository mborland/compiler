#!/bin/bash
# Build and run imported GCC tests through the CIR pipeline on the box.
# usage: rt_run.sh 'path.sft.c:options' ...   (paths relative to
# tests/tests/imported/gnu/c).  Prints compile diagnostics and "run rc=N".
# Run `box.sh sync` and build cpfe-cir first.  Needs BOX_HOST.
here=$(cd "$(dirname "$0")" && pwd)
box=$here/box.sh
tree=${BOX_TREE:-edg-cir-writer}
$box ssh "rm -rf ~/$tree/.tmp-rt; mkdir -p ~/$tree/.tmp-rt"
args="set --"
for s in "$@"; do args="$args '$s'"; done
$box ssh "cat > ~/$tree/.tmp-rt/args.sh" <<< "$args"
$box run rt cir_src/test/writer/tools/rt_box.sh
$box wait rt 200
