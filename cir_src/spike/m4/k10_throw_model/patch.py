#!/usr/bin/env python3
# K10: the final THROW model.  Rewrite CIRGen's throw sites into the shape the
# library emits for ECIL THROW_BEGIN ... THROW_END:
#
#   %e = cir.alloc.exception N
#   %f = cir.alloca "cleanup.isactive"   (function level)      store true -> %f
#   cir.cleanup.scope {                  (implicit DEACTIVATABLE eh free)
#     ... temporaries' constructors, their cleanups, the thrown object's
#         constructor ...
#     store false -> %f                  (right before cir.throw: the runtime
#     cir.throw %e, @_ZTI, @dtor          owns the object from here on)
#   } cleanup eh { if %f: __cxa_free_exception(%e) }
#
# The free scope is outermost: a throwing constructor first runs the
# temporaries' cleanups (inner scopes), then frees; during the throw itself
# the temporaries are destroyed while the exception propagates (clang).
import re, sys
p = sys.argv[1]
L = open(p).read().split('\n')
out = []
nsite = 0
def indent(s):
    return len(s) - len(s.lstrip(' '))
i = 0
hdr = None            # index in `out` of the current function's marker line
allocas = {}
while i < len(L):
    s = L[i]
    m = re.match(r'(\s*)(%\w+) = cir\.alloc\.exception (\d+) -> (!cir\.ptr<.*>)$', s)
    if re.match(r'  cir\.func .*\{$', s):
        out.append(s); out.append('@@ALLOCAS@@'); hdr = len(out) - 1
        allocas[hdr] = []
        i += 1; continue
    if not m:
        out.append(s); i += 1; continue
    ind, v, sz, ty = m.group(1), m.group(2), m.group(3), m.group(4)
    nsite += 1
    flag = f'%k10f{nsite}'
    allocas[hdr].append(f'    {flag} = cir.alloca "cleanup.isactive" align(1) : !cir.ptr<!cir.bool>')
    j = i + 1
    while j < len(L) and (L[j].strip() == '' or L[j].lstrip().startswith('^bb') or indent(L[j]) >= len(ind)):
        j += 1
    body = L[i + 1:j]
    tail = []
    if body and body[-1].strip() == 'cir.yield':
        tail = [body.pop()]          # statement position: yield follows
        body.append(ind + 'cir.yield')
    else:
        tail = [ind + 'cir.unreachable']    # arm of ?: : never completes
        body += [f'^bb{900 + nsite}:  // no predecessors (a cleanup scope body needs an exit)', ind + 'cir.yield']
    # store false right before the throw (before the br to its block)
    t = max(k for k, l in enumerate(body) if 'cir.throw' in l)
    k = t
    if body[k - 1].strip().startswith('^bb') and body[k - 2].strip().startswith('cir.br '):
        k -= 2
    body[k:k] = [ind + '  %k10c' + str(nsite) + ' = cir.const #false',
                 ind + '  cir.store %k10c' + str(nsite) + ', ' + flag + ' : !cir.bool, !cir.ptr<!cir.bool>']
    out.append(s)
    out.append(f'{ind}%k10t{nsite} = cir.const #true')
    out.append(f'{ind}cir.store %k10t{nsite}, {flag} : !cir.bool, !cir.ptr<!cir.bool>')
    out.append(f'{ind}cir.cleanup.scope {{')
    out.extend(['  ' + b if b.strip() else b for b in body])
    out.append(f'{ind}}} cleanup eh {{')
    out.append(f'{ind}  %k10l{nsite} = cir.load align(1) {flag} : !cir.ptr<!cir.bool>, !cir.bool')
    out.append(f'{ind}  cir.if %k10l{nsite} {{')
    out.append(f'{ind}    %k10e{nsite} = cir.cast bitcast {v} : {ty} -> !cir.ptr<!void>')
    out.append(f'{ind}    cir.call @__cxa_free_exception(%k10e{nsite}) : (!cir.ptr<!void>) -> ()')
    out.append(f'{ind}  }}')
    out.append(f'{ind}  cir.yield')
    out.append(f'{ind}}}')
    out.extend(tail)
    i = j
if nsite == 0:
    sys.exit('no cir.alloc.exception found')
res = []
for k, l in enumerate(out):
    if l == '@@ALLOCAS@@':
        res.extend(allocas.get(k, []))
    else:
        res.append(l)
s = '\n'.join(res)
if '@__cxa_free_exception' not in s.split('cir.func', 1)[0]:
    s = s.replace('\n  cir.func', '\n  cir.func private @__cxa_free_exception(!cir.ptr<!void>)\n  cir.func', 1)
open(p, 'w').write(s)
print('patched', nsite, 'throw sites')
