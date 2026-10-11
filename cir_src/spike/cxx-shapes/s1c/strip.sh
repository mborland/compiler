#!/bin/bash
# strip locations so the CIR can be edited by hand
grep -v "^#loc" $1 | sed -E 's/ loc\([^)]*\)//g' > $2
