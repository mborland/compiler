#!/usr/bin/env python3
# Fallback for a member function pointer constant with a non-zero this
# adjustment in a global initializer (#cir.method cannot carry one; upstream
# CIRGen silently drops it): emit the global already in its CXXABILowering form,
# an array of {i64 ptr-or-vtable-offset+1, i64 adj} constants, and bitcast the
# get_global pointer back to the member pointer type at the use sites.
import re, sys
p = sys.argv[1]
s = open(p).read()
m = re.search(r'(  cir\.global external  @g_methods = )#cir\.const_array<\[(.*?)\]> : !cir\.array<(!cir\.method<[^\n]*? in !rec_D>) x 6> (align\(\d+\))', s)
if not m:
    sys.exit("g_methods not found")
elems = re.findall(r'#cir\.method<([^>]*)> : !cir\.method', m.group(2))
adj = {4: 16}                       # &B2::ng converted to D::*: B2 is at offset 16
recs = []
for i, e in enumerate(elems):
    if e == 'null':
        a, b = '#cir.int<0> : !s64i', '#cir.int<0> : !s64i'
    elif e.startswith('vtable_offset = '):
        a, b = '#cir.int<%d> : !s64i' % (int(e.split('=')[1]) + 1), '#cir.int<%d> : !s64i' % adj.get(i, 0)
    else:
        a, b = '#cir.global_view<%s> : !s64i' % e, '#cir.int<%d> : !s64i' % adj.get(i, 0)
    recs.append('#cir.const_record<{%s, %s}> : !rec_methrepr' % (a, b))
mt = m.group(3)
new = ('  cir.global external  @g_methods = #cir.const_array<[%s]> : !cir.array<!rec_methrepr x 6> %s'
       % (', '.join(recs), m.group(4)))
s = s[:m.start()] + new + s[m.end():]
s = '!rec_methrepr = !cir.struct<{data !cir.int<s, 64>, data !cir.int<s, 64>}>\n' + s
def uses(m2):
    return (f'{m2.group(1)}%raw{m2.group(2)} = cir.get_global @g_methods : !cir.ptr<!cir.array<!rec_methrepr x 6>>\n'
            f'{m2.group(1)}%{m2.group(2)} = cir.cast bitcast %raw{m2.group(2)} : !cir.ptr<!cir.array<!rec_methrepr x 6>> -> !cir.ptr<!cir.array<{mt} x 6>>')
s, n = re.subn(r'([ ]*)%(\w+) = cir\.get_global @g_methods : [^\n]*', uses, s)
open(p, 'w').write(s)
print("rewrote g_methods, %d uses" % n)
