#!/bin/bash
# Compare GCC-built and CIR-built behavior of small C programs on the box.
# usage: cmp_run.sh file.c...   (run `box.sh sync` and build cpfe-cir first;
# the programs may #include the system headers of the container)
# A program prints what it wants to compare; the script reports SAME or DIFF
# (with the first differing lines) for each file.  Set CMP_OPTIONS for
# extra eccp options and CMP_GCC_OPTIONS extra gcc options.  Needs BOX_HOST (see box.sh).
here=$(cd "$(dirname "$0")" && pwd)
box=$here/box.sh
tree=${BOX_TREE:-edg-cir-writer}
$box ssh "rm -rf ~/$tree/.tmp-cmp; mkdir -p ~/$tree/.tmp-cmp/files"
for f in "$@"; do $box ssh "cat > ~/$tree/.tmp-cmp/files/$(basename "$f")" < "$f"; done
args="set --"
for f in "$@"; do args="$args $(basename "$f")"; done
$box ssh "cat > ~/$tree/.tmp-cmp/args.sh" <<< "$args; export CMP_OPTIONS='${CMP_OPTIONS:-}' CMP_GCC_OPTIONS='${CMP_GCC_OPTIONS:-}'"
$box run cmp cir_src/test/writer/tools/cmp_box.sh
$box wait cmp 100
