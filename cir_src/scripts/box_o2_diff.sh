# usage: box_o2_diff.sh DIR COUNT SKIP  -- build/cir-s1 against build/cir
timeout 580 python3 cir_src/scripts/box_o2_diff.py build/cir-s1 build/cir $1 4 $2 $3 2>&1 | tail -${LINES_MAX:-30}
