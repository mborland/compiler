#!/usr/bin/env python3
"""Write the micro-benchmark translation units for the writer's per-item
instruction counts (perf_ir.sh ... file X.c): one TU per quadratic pattern.
Usage: gen_perf_tus.py OUTDIR [N]"""
import os, sys

out = sys.argv[1]
n = int(sys.argv[2]) if len(sys.argv) > 2 else 3000
os.makedirs(out, exist_ok=True)


def write(name, lines):
    with open(os.path.join(out, name), 'w') as f:
        f.write('\n'.join(lines) + '\n')


# Unique names: string literals and file-scope compound literals.
l = ['int puts(const char *);']
for i in range(n):
    l.append('int *cl_%d = (int[]){%d};' % (i, i))
l.append('void strs(void) {')
for i in range(n):
    l.append('  puts("string number %d");' % i)
l.append('}')
write('perf_names.c', l)

# One big struct, positional and out-of-order designated initializers.
fields = ' '.join('int f%d;' % i for i in range(n))
l = ['struct big { %s };' % fields,
     'struct big g_pos = { %s };' % ', '.join(str(i) for i in range(n)),
     'struct big g_des = { %s };' %
     ', '.join('.f%d = %d' % (i, i) for i in reversed(range(n)))]
write('perf_bigstruct.c', l)

# Many records declared incomplete and never completed, one frame each.
l = []
for i in range(n):
    l.append('struct S%d;' % i)
    l.append('int f%d(struct S%d *p) { return p != 0; }' % (i, i))
write('perf_pending.c', l)

# Builtins: the libc and the ECIL ones.
l = ['unsigned long strlen(const char *);',
     'int builtins(const char *s, unsigned x, char *d) {', '  int r = 0;']
for i in range(n):
    k = i % 4
    if k == 0:
        l.append('  r += __builtin_strlen(s);')
    elif k == 1:
        l.append('  r += __builtin_clz(x + %d);' % (i + 1))
    elif k == 2:
        l.append('  r += __builtin_expect(r, %d);' % i)
    else:
        l.append('  __builtin_memcpy(d, s, %d);' % (i % 16))
l.append('  return r;')
l.append('}')
write('perf_builtins.c', l)
