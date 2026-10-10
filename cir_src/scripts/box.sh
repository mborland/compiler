#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Drive the x86-64 build box from the Mac (cir_src only; see doc/design.md).
#
#   EDG_BOX=user@host            required
#   EDG_BOX_SOCKET=~/.ssh/cm-edgbox   an open ssh ControlMaster socket
#   EDG_BOX_TREE=edg-cir-lib     the box tree (holds cir_src/ and build/)
#
#   box.sh ssh CMD...            run a command on the box
#   box.sh sync                  rsync cir_src/ to the box tree
#   box.sh run JOB CMD...        run CMD in a cir-env container named
#                                cir-lib-JOB-<pid> at the tree root
#   box.sh chown                 give build/ back to the box user (the posture's
#                                containers leave root-owned files)
#   box.sh test JOB              sync, build and run lit (compact output)
set -e
: "${EDG_BOX:?set EDG_BOX=user@host}"
SOCK=${EDG_BOX_SOCKET:-$HOME/.ssh/cm-edgbox}
TREE=${EDG_BOX_TREE:-edg-cir-lib}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SSH=(ssh -o ControlPath="$SOCK" -o ControlMaster=no -o BatchMode=yes)
cmd=$1; shift
case "$cmd" in
  ssh)  exec "${SSH[@]}" "$EDG_BOX" "$@" ;;
  sync) rsync -a --delete -e "${SSH[*]}" "$ROOT/cir_src/" \
              "$EDG_BOX:$TREE/cir_src/" ;;
  run)  job=$1; shift
        exec "${SSH[@]}" "$EDG_BOX" "sg docker -c \"docker run --rm --user \$(id -u):\$(id -g) -e HOME=/tmp --name cir-lib-$job-\$\$ -v \$HOME/$TREE:/edg/workspace -v \$HOME/edg-cir-config/llvm-project/llvm/utils/lit:/lit:ro -w /edg/workspace edgcpp/cir-env:latest $*\"" ;;
  chown) exec "${SSH[@]}" "$EDG_BOX" "sg docker -c \"docker run --rm -v \$HOME/$TREE:/w edgcpp/cir-env:latest chown -R \$(id -u):\$(id -g) /w/build\"" ;;
  test) job=$1
        "$0" sync
        "$0" run "$job" bash cir_src/scripts/box_ninja.sh 2>&1 |
            grep -i "error\|FAILED" | head -20 || true
        "$0" run "${job}l" bash cir_src/scripts/box_lit.sh 2>&1 |
            grep "^FAIL\|Passed\|Failed\|error:\|exit status\|edg-cir: " |
            head -"${N:-30}" ;;
  *) echo "usage: box.sh ssh|sync|run|test ..." >&2; exit 2 ;;
esac
