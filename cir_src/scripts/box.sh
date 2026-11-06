#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Drive the x86-64 host from the Mac (cir_src only; see doc/design.md).
#
#   EDG_CIR_HOST=user@host       default matt@clang4.cpp.al
#   EDG_CIR_SSH_KEY=~/.ssh/id_rsa     identity file (a leading ~/ is expanded)
#   EDG_CIR_SSH_SOCKET=          optional ssh ControlMaster socket; used only
#                                when set (no default)
#   EDG_CIR_JOBS=48              job cap for ninja and lit inside the container
#   EDG_CIR_SG_DOCKER=1          run docker through `sg docker -c` (off by
#                                default; not needed on clang4)
#   EDG_BOX_TREE=edg-cir-lib     the host tree (holds cir_src/ and build/)
#
#   box.sh ssh CMD...            run a command on the box
#   box.sh sync                  rsync cir_src/ to the box tree
#   box.sh run JOB CMD...        run CMD in a cir-env container named
#                                cir-lib-JOB-<pid> at the tree root
#   box.sh chown                 give build/ back to the box user (the posture's
#                                containers leave root-owned files)
#   box.sh test JOB              sync, build and run lit (compact output)
set -e
HOST=${EDG_CIR_HOST:-matt@clang4.cpp.al}
KEY=${EDG_CIR_SSH_KEY:-$HOME/.ssh/id_rsa}
KEY=${KEY/#\~/$HOME}
TREE=${EDG_BOX_TREE:-edg-cir-lib}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SSH=(ssh -o BatchMode=yes -o ConnectTimeout=20 -i "$KEY")
if [ -n "${EDG_CIR_SSH_SOCKET:-}" ]; then
  SSH+=(-o ControlPath="${EDG_CIR_SSH_SOCKET/#\~/$HOME}" -o ControlMaster=no)
fi
# dock CMD: CMD, wrapped in `sg docker -c` only when EDG_CIR_SG_DOCKER is set.
dock() {
  if [ -n "${EDG_CIR_SG_DOCKER:-}" ] && [ "$EDG_CIR_SG_DOCKER" != 0 ]; then
    printf "sg docker -c %q" "$*"
  else
    printf "%s" "$*"
  fi
}
cmd=$1; shift
case "$cmd" in
  ssh)  exec "${SSH[@]}" "$HOST" "$@" ;;
  sync) rsync -a --delete -e "${SSH[*]}" "$ROOT/cir_src/" \
              "$HOST:$TREE/cir_src/" ;;
  run)  job=$1; shift
        # (the remote shell expands $(id -u), $HOME and $$)
        exec "${SSH[@]}" "$HOST" "$(dock "docker run --rm --user \$(id -u):\$(id -g) -e HOME=/tmp -e EDG_CIR_JOBS=${EDG_CIR_JOBS:-48} --name cir-lib-$job-\$\$ -v \$HOME/$TREE:/edg/workspace -v \$HOME/edg-cir-config/llvm-project/llvm/utils/lit:/lit:ro -w /edg/workspace edgcpp/cir-env:latest nice -n 10 $*")" ;;
  chown) exec "${SSH[@]}" "$HOST" "$(dock "docker run --rm -v \$HOME/$TREE:/w edgcpp/cir-env:latest chown -R \$(id -u):\$(id -g) /w/build")" ;;
  test) job=$1
        "$0" sync
        "$0" run "$job" bash cir_src/scripts/box_ninja.sh 2>&1 |
            grep -i "error\|FAILED" | head -20 || true
        "$0" run "${job}l" bash cir_src/scripts/box_lit.sh 2>&1 |
            grep "^FAIL\|Passed\|Failed\|error:\|exit status\|edg-cir: " |
            head -"${N:-30}" ;;
  *) echo "usage: box.sh ssh|sync|run|test ..." >&2; exit 2 ;;
esac
