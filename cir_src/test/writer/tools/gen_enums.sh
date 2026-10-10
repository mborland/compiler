#!/bin/bash
# Writes enums.json (enumerator names of the IL kinds the writer reports by
# number) from a preprocessed front-end header.  Run it where the front end
# was configured, in the build directory:  gen_enums.sh BUILD_DIR OUT.json
# (BUILD_DIR is the cpfe-cir build directory with a ninja build).
build=${1:?build dir}; out=${2:-enums.json}
here=$(cd "$(dirname "$0")" && pwd)
cd "$build" || exit 1
cmd=$(ninja -t commands src/cmake/cpfe-cir/CMakeFiles/cpfe-cir.dir/__/__/error.c.o | tail -1)
tmp=$(mktemp)
cmd=$(echo "$cmd" | sed "s# -MD -MT [^ ]* -MF [^ ]*##; s# -o [^ ]* -c # -E -P -o $tmp #")
eval "$cmd" || exit 1
python3 - "$tmp" "$out" <<'PY'
import json, re, sys
s = open(sys.argv[1]).read()
def enum(name):
    m = re.search(r'enum ' + name + r'\s*:\s*\w+(?:\s+\w+)*\s*\{(.*?)\}', s, re.S)
    return [x.strip().split('=')[0].strip() for x in m.group(1).split(',')
            if x.strip()]
json.dump({n: enum(n) for n in ('an_expr_operator_kind', 'an_expr_node_kind',
                                'a_constant_repr_kind')}, open(sys.argv[2], 'w'))
PY
rm -f "$tmp"
