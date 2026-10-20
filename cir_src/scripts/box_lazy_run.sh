# usage: box_lazy_run.sh DIR OPT COUNT [SKIP]   (build/streams/DIR)
DIR=$1; OPT=${2:--O0}; CNT=${3:-100000}; SKIP=${4:-0}
ls build/streams/$DIR/*.ecil | tail -n +$((SKIP+1)) | head -$CNT > /tmp/lzlist.txt
timeout 580 python3 cir_src/scripts/box_lazy_check.py $OPT 4 $(cat /tmp/lzlist.txt) 2>&1 | tail -${LINES_MAX:-25}
