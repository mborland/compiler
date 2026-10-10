#!/bin/bash
# Inside cir-env: lit failures of a build dir (default build/cir-release).
d=${1:-build/cir-release}
cd $d/test/lit && python3 /lit/lit.py -v . 2>&1 | grep -v "^# executed\|^cat \|^/edg" | grep -B2 -A14 "^FAIL" | head -${LINES_MAX:-50}
