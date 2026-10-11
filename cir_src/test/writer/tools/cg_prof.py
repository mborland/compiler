#!/usr/bin/env python3
"""Aggregate callgrind output files: total instructions (Ir), the Ir executed
on lines of the writer's own source files (src/ecil_*.c, cir_be.c) and, for
the functions named with --fn, their self and inclusive Ir.
Usage: cg_prof.py [--fn NAME[,NAME...]] [--callees NAME] [--top N] FILE...
(callgrind out files).  --callees prints the callees of one function by
inclusive Ir.
A name matches a function when it is the function's name up to the argument
list, ignoring a namespace prefix (a name containing "(" is matched whole)."""
import os, re, sys

args = sys.argv[1:]
wanted = []
callers_of = None
top_n = 0
while args and args[0] in ('--fn', '--callees', '--top'):
    if args[0] == '--top':
        top_n = int(args[1])
    elif args[0] == '--fn':
        wanted = [w for w in args[1].split(",") if w]
    else:
        callers_of = args[1]
    args = args[2:]
callee_cost = {}

total = 0
writer = 0
self_cost = {}
incl_cost = {}


def short(name):
    base = name.split('(')[0]
    return base.split('::')[-1]


for path in args:
    files = {}
    fns = {}
    cur_file = ''
    cur_fn = ''
    callee = ''
    skip = False

    def resolve(table, spec):
        m = re.match(r'\((\d+)\)\s*(.*)', spec)
        if not m:
            return spec
        if m.group(2):
            table[m.group(1)] = m.group(2)
        return table.get(m.group(1), '')

    with open(path, errors='replace') as f:
        for line in f:
            if line.startswith(('fl=', 'fi=', 'fe=')):
                cur_file = os.path.basename(resolve(files, line[3:].strip()))
            elif line.startswith(('cfi=', 'cfl=')):
                resolve(files, line[4:].strip())
            elif line.startswith('fn='):
                cur_fn = resolve(fns, line[3:].strip())
            elif line.startswith('cfn='):
                callee = short(resolve(fns, line[4:].strip()))
            elif line.startswith('calls='):
                skip = True
            elif line.startswith(('totals:', 'summary:')):
                if line.startswith('totals:'):
                    total += int(line.split()[1])
            elif line[:1] in '0123456789+-*':
                parts = line.split()
                if len(parts) >= 2 and parts[1].isdigit():
                    n = int(parts[1])
                    key = short(cur_fn)
                    if skip:
                        incl_cost[key] = incl_cost.get(key, 0) + n
                        if key == callers_of:
                            callee_cost[callee] = callee_cost.get(callee,
                                                                  0) + n
                        skip = False
                    else:
                        self_cost[key] = self_cost.get(key, 0) + n
                        incl_cost[key] = incl_cost.get(key, 0) + n
                        if cur_file.startswith('ecil_') or cur_file == 'cir_be.c':
                            writer += n
print('total_ir %d writer_line_ir %d files %d' % (total, writer, len(args)))
for name in wanted:
    print('%-40s self %12d  incl %12d' %
          (name, self_cost.get(name, 0), incl_cost.get(name, 0)))
if callers_of:
    for name, n in sorted(callee_cost.items(), key=lambda x: -x[1])[:25]:
        print('  callee %-40s %12d' % (name, n))
if top_n:
    for name, n in sorted(self_cost.items(), key=lambda x: -x[1])[:top_n]:
        print('  top %-40s self %12d  incl %12d' % (name, n,
                                                   incl_cost.get(name, 0)))
