#!/bin/bash
# Helper for running the writer corpus tooling on a remote x86-64 box.
#   box.sh ssh CMD...        run CMD on the box
#   box.sh sync              rsync the repository to $BOX_TREE on the box
#   box.sh run LOG SCRIPT    run SCRIPT (a path relative to the tree) in the
#                            cir-env container in the background; log in ~/LOG.log
#   box.sh wait LOG [N]      wait for the run to finish, show the last N lines
#   box.sh fetch REMOTE DIR  copy REMOTE (relative to the tree) into DIR
# Environment: BOX_HOST (user@host), BOX_SOCKET (ssh ControlPath of an open
# master connection), BOX_TREE (directory on the box, default edg-cir-writer).
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
host=${BOX_HOST:?set BOX_HOST (user@host)}
tree=${BOX_TREE:-edg-cir-writer}
sock=${BOX_SOCKET:-$HOME/.ssh/cm-edgbox}
sshopts="-o ControlPath=$sock -o ControlMaster=no -o BatchMode=yes"
cmd=$1; shift
case $cmd in
  ssh) exec ssh $sshopts "$host" "$@" ;;
  sync)
    exec rsync -a --delete -e "ssh $sshopts" --exclude=/build/ \
      --exclude=/tests/runs/ --exclude=/.tmp/ --exclude='/.tmp-*/' \
      --exclude=/cmake-build-debug/ --exclude=/.idea/ --exclude=/.mypy_cache/ \
      --exclude=/dev-pref.json --exclude=/.git/hooks/ --exclude=__pycache__/ \
      "$repo/" "$host:$tree/" ;;
  run)
    log=$1; script=$2
    exec ssh $sshopts "$host" "cd ~/$tree && rm -f ~/$log.exit && nohup bash -c 'export PATH=\$PWD/dev_tools/bin:\$PATH PYTHONPATH=\$PWD/dev_tools/pylibs; sg docker -c \"edg-exec --img cir-env -- bash $script\" > ~/$log.log 2>&1; echo \$? > ~/$log.exit' > /dev/null 2>&1 &" ;;
  wait)
    until ssh $sshopts "$host" "test -f ~/$1.exit"; do sleep 10; done
    ssh $sshopts "$host" "cat ~/$1.exit; tail -${2:-5} ~/$1.log | cut -c1-200" ;;
  fetch)
    exec rsync -a --exclude='*.ecil' -e "ssh $sshopts" "$host:$tree/$1/" "$2/" ;;
  *) echo "usage: box.sh ssh|sync|run|wait|fetch ..." >&2; exit 2 ;;
esac
