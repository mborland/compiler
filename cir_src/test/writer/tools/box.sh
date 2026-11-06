#!/bin/bash
# Helper for running the writer corpus tooling on the x86-64 host (clang4).
#   box.sh ssh CMD...        run CMD on the host
#   box.sh sync              rsync the repository to $BOX_TREE on the host
#   box.sh run LOG SCRIPT    run SCRIPT (a path relative to the tree) in the
#                            cir-env container in the background; log in ~/LOG.log
#   box.sh wait LOG [N]      wait for the run to finish, show the last N lines
#   box.sh fetch REMOTE DIR  copy REMOTE (relative to the tree) into DIR
# Environment (the host variables of doc/cookbook.md; nothing is required):
#   EDG_CIR_HOST       user@host, default matt@clang4.cpp.al
#   EDG_CIR_SSH_KEY    identity file, default ~/.ssh/id_rsa
#   EDG_BOX_SOCKET     ssh ControlPath of an open master connection; used
#                      only when set
#   EDG_CIR_SG_DOCKER  non-empty and not 0: run docker through `sg docker -c`
#                      (not needed on clang4)
#   BOX_TREE           directory under $HOME on the host, default edg-cir-writer
# The detached job runs with TMPDIR=$HOME/tmp (the host's /tmp is a tmpfs).
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
host=${EDG_CIR_HOST:-matt@clang4.cpp.al}
key=${EDG_CIR_SSH_KEY:-$HOME/.ssh/id_rsa}; key=${key/#\~/$HOME}
tree=${BOX_TREE:-edg-cir-writer}
SSH=(ssh -o BatchMode=yes -o ConnectTimeout=20 -i "$key")
if [ -n "${EDG_BOX_SOCKET:-}" ]; then
  SSH+=(-o ControlPath="${EDG_BOX_SOCKET/#\~/$HOME}" -o ControlMaster=no)
fi
dock() {
  if [ -n "${EDG_CIR_SG_DOCKER:-}" ] && [ "$EDG_CIR_SG_DOCKER" != 0 ]; then
    printf "sg docker -c \\\"%s\\\"" "$*"
  else
    printf "%s" "$*"
  fi
}
cmd=$1; shift
case $cmd in
  ssh) exec "${SSH[@]}" "$host" "$@" ;;
  sync)
    exec rsync -a --checksum --no-times --delete -e "${SSH[*]}" \
      --exclude=/build/ --exclude=/.git/ \
      --exclude=/tests/runs/ --exclude=/.tmp/ --exclude='/.tmp-*/' \
      --exclude=/cmake-build-debug/ --exclude=/.idea/ --exclude=/.mypy_cache/ \
      --exclude=/dev-pref.json --exclude=__pycache__/ \
      "$repo/" "$host:$tree/" ;;
  run)
    log=$1; script=$2
    inner=$(dock "edg-exec --img cir-env -- bash $script")
    exec "${SSH[@]}" "$host" "cd ~/$tree && rm -f ~/$log.exit && mkdir -p ~/tmp && nohup bash -c 'export TMPDIR=\$HOME/tmp PATH=\$PWD/dev_tools/bin:\$PATH PYTHONPATH=\$PWD/dev_tools/pylibs; nice -n 10 $inner > ~/$log.log 2>&1; echo \$? > ~/$log.exit' > /dev/null 2>&1 &" ;;
  wait)
    until "${SSH[@]}" "$host" "test -f ~/$1.exit"; do sleep 10; done
    "${SSH[@]}" "$host" "cat ~/$1.exit; tail -${2:-5} ~/$1.log | cut -c1-200" ;;
  fetch)
    exec rsync -a --exclude='*.ecil' -e "${SSH[*]}" "$host:$tree/$1/" "$2/" ;;
  *) echo "usage: box.sh ssh|sync|run|wait|fetch ..." >&2; exit 2 ;;
esac
