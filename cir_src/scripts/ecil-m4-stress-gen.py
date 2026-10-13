#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Synthetic ECIL 1.2 streams for the M4 stress evidence (gate M4).

  ecil-m4-stress-gen.py KIND N > stream.txt     (ecil-asm input, header included)

KIND:
  wide N      one class with N virtual functions (gen_s1_stress.py)
  vchain N    N classes, each overriding its base's function at every level
              (gen_s1_stress.py chain)
  diamond N   a lattice of 2N classes: every class of layer k has both classes
              of layer k-1 as virtual bases (so 2k virtual bases)
  vbases N    one class with N direct virtual bases
  tuple N     recursive tuple-like inheritance: class T_k : T_(k-1) { int x; },
              each with a constructor that runs its base's
  tries N     N nested try blocks (each with a catch-all) around a call
  news N      N `new T(i)` expressions in ONE full expression, each with a
              DEACTIVATABLE eh cleanup (the free of the allocation)
  unwindcall N  as unwind, but the unwinding starts in a call that may throw
              (no THROW_BEGIN: no cleanup that frees the exception object)
  unwind N    a function with N nested locals that have destructors, and a
              throw at the innermost point (a throw through N cleanups); the
              host (--host) counts the destructors that run
  --host unwind   prints the host program for `unwind` instead

The streams need only the headers of cir_src/test/cirgen/Inputs.
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TEST = os.path.join(HERE, '..', 'test', 'cirgen')


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def header(name):
    return open(os.path.join(TEST, 'Inputs', name)).read()


class Ids:
    def __init__(self):
        self.next_sid = 100
        self.next_gid = 1000
        self.lines = []

    def string(self, text):
        self.next_sid += 1
        self.lines.append('  STR %d "%s"' % (self.next_sid, text))
        return self.next_sid

    def gid(self):
        self.next_gid += 1
        return self.next_gid

    def emit(self, line):
        self.lines.append('  ' + line)

    def text(self):
        return '\n'.join(self.lines) + '\n'


def vbase_classes(ids, count_fields, first_fields):
    pass


def diamond(n):
    """2n classes; layer 0 is the root V (a dynamic class with one virtual
    function); the classes of layer k >= 1 are virtually derived from both
    classes of layer k-1."""
    ids = Ids()
    ids.lines.append('frame DECLS')
    ids.emit('TY_VPTR 90')
    ids.emit('TY_PTR 91 11')
    classes = []      # (rec gid, vbases list of indices, name)
    layers = [[0]]
    names = ['V']
    vb = [[]]         # vbases (class indices) of each class, postorder
    for k in range(1, n + 1):
        layers.append([])
        for side in 'AB':
            idx = len(names)
            names.append('%s%d' % (side, k))
            layers[k].append(idx)
        for idx in layers[k]:
            order = []
            for base in layers[k - 1]:
                for x in vb[base] + [base]:
                    if x not in order:
                        order.append(x)
            vb.append(order)
        assert len(vb) == len(names)
    recs = []
    fn_type = None
    # the root
    for idx, name in enumerate(names):
        s_name = ids.string(name)
        s_mangle = ids.string('%d%s' % (len(name), name))
        rec = ids.gid()
        recs.append(rec)
        ids.emit('TY_RECORD_DECL %d 0 %d' % (rec, s_name))
        if idx == 0:
            ids.emit('TY_RECORD_BODY %d 8 8 8 4 1' % rec)
            ids.emit('REC_FIELD 90 0 nil nil nil 1')
            ids.emit('CLASS_MANGLE %d %d' % (rec, s_mangle))
            ids.emit('CLASS_INFO %d 8 8 nil 1' % rec)
            ids.emit('CLASS_BASES %d []' % rec)
            ptr = ids.gid()
            ids.emit('TY_PTR %d %d' % (ptr, rec))
            fn_type = ids.gid()
            ids.emit('TY_FUNC %d 11 0 [%d]' % (fn_type, ptr))
            s_f = ids.string('_ZN1V1fEv')
            fn = ids.gid()
            ids.emit('DECL_FUNC %d %d 0 1 %d' % (fn, fn_type, s_f))
            ids.emit('CLASS_VFUNCS %d [%d, 0, 0, 1]' % (rec, fn))
            ids.emit('CLASS_OVERRIDES %d []' % rec)
        else:
            order = vb[idx]
            size = 8 + 8 * len(order)
            ids.emit('TY_RECORD_BODY %d %d %d 8 4 %d' %
                     (rec, size, size, 1 + len(order)))
            ids.emit('REC_FIELD 90 0 nil nil nil 1')
            for i, b in enumerate(order):
                ids.emit('REC_FIELD %d %d nil nil nil 65' %
                         (recs[b], 8 + 8 * i))
            ids.emit('CLASS_MANGLE %d %d' % (rec, s_mangle))
            ids.emit('CLASS_INFO %d 8 8 nil 0' % rec)
            direct = [order.index(b) + 1 for b in layers[
                next(k for k in range(len(layers)) if idx in layers[k]) - 1]]
            ids.emit('CLASS_BASES %d [%s]' %
                     (rec, ', '.join('%d, 1' % d for d in direct)))
            ids.emit('CLASS_VFUNCS %d []' % rec)
            ids.emit('CLASS_OVERRIDES %d []' % rec)
        ids.emit('DECL_ABI_DATA %d 2 %d 0 1 nil' % (ids.gid(), rec))
        ids.emit('DECL_ABI_DATA %d 0 %d 0 1 nil' % (ids.gid(), rec))
    ids.lines.append('frame REGION')
    ids.lines.append('frame END')
    ids.lines.append('  END auto 0')
    return header('header12_cxx_rtti.txt') + ids.text()


def vbases(n):
    """One class C with n direct virtual bases V0..V(n-1), each a dynamic
    class with a vptr and one virtual function."""
    ids = Ids()
    ids.lines.append('frame DECLS')
    ids.emit('TY_VPTR 90')
    recs = []
    for k in range(n):
        name = 'V%d' % k
        s_name = ids.string(name)
        s_mangle = ids.string('%d%s' % (len(name), name))
        s_f = ids.string('_ZN%d%s1fEv' % (len(name), name))
        rec, ptr, fn_type, fn = ids.gid(), ids.gid(), ids.gid(), ids.gid()
        recs.append(rec)
        ids.emit('TY_RECORD_DECL %d 0 %d' % (rec, s_name))
        ids.emit('TY_RECORD_BODY %d 8 8 8 4 1' % rec)
        ids.emit('REC_FIELD 90 0 nil nil nil 1')
        ids.emit('CLASS_MANGLE %d %d' % (rec, s_mangle))
        ids.emit('CLASS_INFO %d 8 8 nil 1' % rec)
        ids.emit('CLASS_BASES %d []' % rec)
        ids.emit('TY_PTR %d %d' % (ptr, rec))
        ids.emit('TY_FUNC %d 11 0 [%d]' % (fn_type, ptr))
        ids.emit('DECL_FUNC %d %d 0 1 %d' % (fn, fn_type, s_f))
        ids.emit('CLASS_VFUNCS %d [%d, 0, 0, 1]' % (rec, fn))
        ids.emit('CLASS_OVERRIDES %d []' % rec)
    s_name = ids.string('C')
    s_mangle = ids.string('1C')
    rec = ids.gid()
    size = 8 + 8 * n
    ids.emit('TY_RECORD_DECL %d 0 %d' % (rec, s_name))
    ids.emit('TY_RECORD_BODY %d %d %d 8 4 %d' % (rec, size, size, 1 + n))
    ids.emit('REC_FIELD 90 0 nil nil nil 1')
    for k in range(n):
        ids.emit('REC_FIELD %d %d nil nil nil 65' % (recs[k], 8 + 8 * k))
    ids.emit('CLASS_MANGLE %d %d' % (rec, s_mangle))
    ids.emit('CLASS_INFO %d 8 8 nil 0' % rec)
    ids.emit('CLASS_BASES %d [%s]' %
             (rec, ', '.join('%d, 1' % (k + 1) for k in range(n))))
    ids.emit('CLASS_VFUNCS %d []' % rec)
    ids.emit('CLASS_OVERRIDES %d []' % rec)
    for r in recs + [rec]:
        ids.emit('DECL_ABI_DATA %d 2 %d 0 1 nil' % (ids.gid(), r))
        ids.emit('DECL_ABI_DATA %d 0 %d 0 1 nil' % (ids.gid(), r))
    ids.emit('DECL_ABI_DATA %d 1 %d 0 1 nil' % (ids.gid(), rec))   # VTT
    ids.lines.append('frame REGION')
    ids.lines.append('frame END')
    ids.lines.append('  END auto 0')
    return header('header12_cxx_rtti.txt') + ids.text()


def tuple_chain(n):
    """T_k : T_(k-1) { int x_k; } with a constructor each, running its
    base's constructor (MEMINIT base) and storing x_k."""
    ids = Ids()
    ids.lines.append('frame DECLS')
    s_x = ids.string('x')
    recs, ptrs, ftypes, ctors = [], [], [], []
    for k in range(n):
        name = 'T%d' % k
        s_name = ids.string(name)
        rec, ptr, ftype = ids.gid(), ids.gid(), ids.gid()
        recs.append(rec)
        ptrs.append(ptr)
        ftypes.append(ftype)
        ids.emit('TY_RECORD_DECL %d 0 %d' % (rec, s_name))
        if k == 0:
            ids.emit('TY_RECORD_BODY %d 4 4 4 0 1' % rec)
            ids.emit('REC_FIELD 1 0 nil nil %d 0' % s_x)
        else:
            ids.emit('TY_RECORD_BODY %d %d %d 4 0 2' %
                     (rec, 4 * (k + 1), 4 * (k + 1)))
            ids.emit('REC_FIELD %d 0 nil nil nil 16' % recs[k - 1])
            ids.emit('REC_FIELD 1 %d nil nil %d 0' % (4 * k, s_x))
        ids.emit('TY_PTR %d %d' % (ptr, rec))
        ids.emit('TY_FUNC %d 11 0 [%d]' % (ftype, ptr))
        c1, c2 = ids.gid(), ids.gid()
        s1 = ids.string('_ZN%d%sC1Ev' % (len(name), name))
        s2 = ids.string('_ZN%d%sC2Ev' % (len(name), name))
        ids.emit('DECL_FUNC %d %d 0 0 %d' % (c1, ftype, s1))
        ids.emit('DECL_FUNC %d %d 0 1 %d' % (c2, ftype, s2))
        ids.emit('STRUCTOR %d %d 0' % (c1, c2))
        ctors.append((c1, c2))
    ids.lines.append('frame REGION')
    for k in range(n):
        ids.emit('FUNC_BEGIN %d 0' % ctors[k][1])
        ids.emit('PARAM 1 %d %d nil 1' % (ptrs[k], ptrs[k]))
        if k > 0:
            ids.emit('MEMINIT_BEGIN 1 0 0')
            ids.emit('LREF 1')
            ids.emit('LOAD 0')
            ids.emit('BASE_CAST %d %d 0 1' % (recs[k], recs[k - 1]))
            ids.emit('CALL %d %d 1 0' % (ctors[k - 1][1], ftypes[k - 1]))
            ids.emit('MEMINIT_END')
        ids.emit('MEMINIT_BEGIN 2 %d 1' % (1 if k > 0 else 0))
        ids.emit('LREF 1')
        ids.emit('LOAD 0')
        ids.emit('DEREF')
        ids.emit('FIELD %d %d' % (recs[k], 1 if k > 0 else 0))
        ids.emit('CONST_INT 1 %d' % k)
        ids.emit('STORE 2 0')
        ids.emit('MEMINIT_END')
        ids.emit('BLOCK_BEGIN')
        ids.emit('BLOCK_END')
        ids.emit('FUNC_END 1')
    ids.lines.append('frame END')
    ids.lines.append('  END auto 0')
    return header('header12_cxx.txt') + ids.text()


def gen_eh():
    return load(os.path.join(TEST, 'Inputs', 'gen_eh.py'), 'gen_eh')


def tries(n):
    ge = gen_eh()
    ctx = ge.Ctx()
    f = ge.Func(ctx, 'tries', ('k',))
    for _ in range(n):
        f.r('TRY_BEGIN 0')
        f.r('BLOCK_BEGIN')
    f.r('CONST_INT 1 1')
    f.r(ctx.call('mk'))
    f.r('DROP')
    for _ in range(n):
        f.r('BLOCK_END')
        f.r('CATCH_BEGIN nil')
        f.r('BLOCK_BEGIN')
        f.r('BLOCK_END')
        f.r('TRY_END')
    f.r('CONST_INT 1 0')
    f.r('RETURN_STMT true')
    f.finish()
    return header('header12_cxx_eh.txt') + ctx.gen.text()


def news(n):
    ge = gen_eh()
    ctx = ge.Ctx()
    f = ge.Func(ctx, 'news', ('k',))
    lp = f.new_l()
    f.locals['p'] = (lp, 'obj')
    f.r('LOCAL %d %d %d 8 0' % (lp, ctx.prec['Th'], ctx.gen.sid('p')))
    f.r('FULLEXPR_BEGIN')
    for i in range(n):
        records, _ = f.new_expr_records(i % 5 + 100, False)
        f.r('LREF %d' % lp)
        for x in records:
            f.r(x)
        f.r('STORE 0 0')
    f.r('FULLEXPR_END')
    f.r('CONST_INT 1 0')
    f.r('RETURN_STMT true')
    f.finish()
    return header('header12_cxx_eh.txt') + ctx.gen.text()


def unwind(n):
    ge = gen_eh()
    ctx = ge.Ctx()
    f = ge.Func(ctx, 'unwind', ('k',))
    body = [('obj', 'a%d' % i, i) for i in range(n)]
    body.append(('throw_int', 'k'))
    body.append(('ret', 0))
    f.stmts(body)
    f.finish()
    return header('header12_cxx_eh.txt') + ctx.gen.text()


def unwind_call(n):
    """As unwind, but the unwinding starts in a call that may throw (mk(k)),
    so there is no THROW_BEGIN (no free-the-exception cleanup)."""
    ge = gen_eh()
    ctx = ge.Ctx()
    f = ge.Func(ctx, 'unwind', ('k',))
    body = [('obj', 'a%d' % i, i) for i in range(n)]
    body.append(('note', ('call', 'mk', ['k'])))
    body.append(('ret', 0))
    f.stmts(body)
    f.finish()
    return header('header12_cxx_eh.txt') + ctx.gen.text()


UNWIND_HOST = '''// Host of `ecil-m4-stress-gen.py unwind N`: counts the T destructors that
// run when c_unwind throws (all N).
#include <cstdio>
struct T { int v; T(int); T(const T &); ~T(); };
struct Ex { int v; Ex(int); Ex(const Ex &); ~Ex(); };
static long ctors, dtors;
T::T(int i) : v(i) { ++ctors; }
T::T(const T &o) : v(o.v) {}
T::~T() { ++dtors; }
Ex::Ex(int c) : v(c) {}
Ex::Ex(const Ex &o) : v(o.v) {}
Ex::~Ex() {}
extern "C" void note(int) {}
extern "C" int mk(int k) { return k; }
extern "C" int c_unwind(int);
int main() {
  try { c_unwind(7); } catch (int) {}
  std::printf("ctors %ld dtors %ld\\n", ctors, dtors);
  return ctors == dtors ? 0 : 1;
}
'''


def main():
    if sys.argv[1] == '--host':
        sys.stdout.write(UNWIND_HOST)
        return
    kind, n = sys.argv[1], int(sys.argv[2])
    if kind in ('wide', 'vchain'):
        s1 = load(os.path.join(TEST, 'gen_s1_stress.py'), 'gen_s1_stress')
        body = s1.wide(n) if kind == 'wide' else s1.chain(n)
        out = header('header12_cxx_rtti.txt') + body
    else:
        out = {'diamond': diamond, 'vbases': vbases, 'tuple': tuple_chain,
               'tries': tries, 'news': news, 'unwind': unwind,
               'unwindcall': unwind_call}[kind](n)
    sys.stdout.write(out)


if __name__ == '__main__':
    main()
