#!/usr/bin/env python3
"""Sum cachegrind output files: total instructions (Ir) and the instructions
executed on lines of the writer's own source files (src/ecil_*.c, cir_be.c).
Usage: cg_sum.py FILE...   (prints "total_ir writer_ir files")"""
import os, sys

total = 0
writer = 0
for path in sys.argv[1:]:
    cur = ''
    with open(path, errors='replace') as f:
        for line in f:
            if line.startswith(('fl=', 'fi=', 'fe=')):
                cur = os.path.basename(line[3:].strip())
            elif line[:1].isdigit():
                parts = line.split()
                if len(parts) >= 2:
                    n = int(parts[1])
                    if cur.startswith('ecil_') or cur == 'cir_be.c':
                        writer += n
            elif line.startswith('summary:'):
                total += int(line.split()[1])
print(total, writer, len(sys.argv) - 1)
