# usage: box_lazy_rw.sh OPT  -- the libstdc++ TUs (--keep-going)
export KEEP=1
python3 cir_src/scripts/box_lazy_check.py ${1:--O0} 4 build/streams/rw/*.ecil 2>&1 | tail -20
