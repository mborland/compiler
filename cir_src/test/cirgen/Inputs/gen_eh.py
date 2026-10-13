#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Generator of the ECIL 1.2 exception tests of the code generator (M4, S4).

A scenario is a small statement tree.  The generator writes it twice: as the
ECIL records the writer sends (a .test file whose RUN lines compile it with
edg-cir) and as C++ (the g++ reference, compiled by the RUN lines too).  The
host program runs both versions over a range of inputs and compares what each
logs through note() and what it returns, so the library is checked against
g++ (the throw/catch runtime is the same libstdc++/libgcc one).

  python3 gen_eh.py DIR     writes DIR/cxx-s4-*.test and DIR/Inputs/s4_*.cpp
"""
import os
import sys


class Gen:
    """ECIL text building: declarations by name, function bodies."""

    def __init__(self, lang=6):
        self.lang = lang
        self.decl = []
        self.body = []
        self.globals = []
        self.next_g = 100
        self.strs = {}
        self.next_s = 100
        self.g = {}

    def sid(self, text):
        if text not in self.strs:
            self.strs[text] = self.next_s
            self.decl.append('  STR %d "%s"' % (self.next_s, text))
            self.next_s += 1
        return self.strs[text]

    def gid(self, name=None):
        g = self.next_g
        self.next_g += 1
        if name:
            self.g[name] = g
        return g

    def d(self, line):
        self.decl.append('  ' + line)

    def text(self):
        out = ['frame DECLS'] + self.decl + ['frame REGION'] + self.body
        if self.globals:
            out += ['frame GLOBALS'] + self.globals
        out += ['frame END', '  END auto 0']
        return '\n'.join(out) + '\n'


INT = 1
VOID = 11


class Ctx:
    """The declarations every scenario file shares."""

    def __init__(self):
        g = self.gen = Gen()
        # types
        self.pint = g.gid('pint')
        g.d('TY_PTR %d %d' % (self.pint, INT))
        self.ppint = g.gid('ppint')
        g.d('TY_PTR %d %d' % (self.ppint, self.pint))
        self.rec = {}
        self.prec = {}
        for name, mangle, field in (('T', '1T', 'v'), ('Ex', '2Ex', 'v'),
                                    ('Th', '2Th', 'v'), ('Td', '2Td', 'v'), ('Tc', '2Tc', 'v'),
                                    ('Ar', '2Ar', 'v'), ('Ta', '2Ta', 'v'),
                                    ('Ua', '2Ua', 'v')):
            r = g.gid(name)
            sids = [g.sid(name), g.sid(field), g.sid(mangle)]
            g.d('TY_RECORD_DECL %d 0 %d' % (r, sids[0]))
            g.d('TY_RECORD_BODY %d 4 4 4 12 1' % r)
            g.d('REC_FIELD %d 0 nil nil %d 0' % (INT, sids[1]))
            g.d('CLASS_MANGLE %d %d' % (r, sids[2]))
            g.d('CLASS_INFO %d 4 4 nil 0' % r)
            g.d('CLASS_BASES %d []' % r)
            self.rec[name] = r
            p = g.gid('p' + name)
            g.d('TY_PTR %d %d' % (p, r))
            self.prec[name] = p
        # ExD : Ex { int w; }
        r = g.gid('ExD')
        sids = [g.sid('ExD'), g.sid('w'), g.sid('3ExD'), g.sid('Ex')]
        g.d('TY_RECORD_DECL %d 0 %d' % (r, sids[0]))
        g.d('TY_RECORD_BODY %d 8 8 4 12 2' % r)
        g.d('REC_FIELD %d 0 nil nil nil 16' % self.rec['Ex'])
        g.d('REC_FIELD %d 4 nil nil %d 0' % (INT, sids[1]))
        g.d('CLASS_MANGLE %d %d' % (r, sids[2]))
        g.d('CLASS_INFO %d 8 4 nil 0' % r)
        g.d('CLASS_BASES %d [0, 1]' % r)
        self.rec['ExD'] = r
        p = g.gid('pExD')
        g.d('TY_PTR %d %d' % (p, r))
        self.prec['ExD'] = p
        # P: a trivially copyable class
        r = g.gid('P')
        sids = [g.sid('P'), g.sid('v'), g.sid('1P')]
        g.d('TY_RECORD_DECL %d 0 %d' % (r, sids[0]))
        g.d('TY_RECORD_BODY %d 4 4 4 0 1' % r)
        g.d('REC_FIELD %d 0 nil nil %d 0' % (INT, sids[1]))
        g.d('CLASS_MANGLE %d %d' % (r, sids[2]))
        g.d('CLASS_INFO %d 4 4 nil 0' % r)
        g.d('CLASS_BASES %d []' % r)
        self.rec['P'] = r
        p = g.gid('pP')
        g.d('TY_PTR %d %d' % (p, r))
        self.prec['P'] = p
        self.ppP = g.gid('ppP')
        g.d('TY_PTR %d %d' % (self.ppP, p))
        self.ft = {}
        # typeinfo
        self.ti = {}
        t = g.gid('tiint')
        g.d('DECL_ABI_DATA %d 2 %d 0 0 %d' % (t, INT, g.sid('_ZTIi')))
        g.d('TYPEINFO_DEF %d 0 0 nil nil' % t)
        self.ti['int'] = t
        t = g.gid('tipint')
        g.d('DECL_ABI_DATA %d 2 %d 0 0 %d' % (t, self.pint, g.sid('_ZTIPi')))
        g.d('TYPEINFO_DEF %d 0 0 nil nil' % t)
        self.ti['pint'] = t
        for name in ('T', 'Ex', 'Th', 'ExD', 'P', 'Tc'):
            t = g.gid('ti' + name)
            g.d('DECL_ABI_DATA %d 2 %d 3 3 nil' % (t, self.rec[name]))
            self.ti[name] = t
        t = g.gid('tipP')
        g.d('DECL_ABI_DATA %d 2 %d 3 3 %d' % (t, self.prec['P'],
                                              g.sid('_ZTIP1P')))
        g.d('TYPEINFO_DEF %d 1 0 %d nil' % (t, self.ti['P']))
        self.ti['pP'] = t
        # host functions (declared, not defined here)
        self.fn = {}
        self.host('note', VOID, [INT], 'note')
        self.host('mk', INT, [INT], 'mk')
        self.host('ctorT', VOID, [self.prec['T'], INT], '_ZN1TC1Ei')
        self.host('copyT', VOID, [self.prec['T'], self.prec['T']],
                  '_ZN1TC1ERKS_')
        self.host('dtorT', VOID, [self.prec['T']], '_ZN1TD1Ev', nothrow=True)
        self.host('ctorEx', VOID, [self.prec['Ex'], INT], '_ZN2ExC1Ei')
        self.host('copyEx', VOID, [self.prec['Ex'], self.prec['Ex']],
                  '_ZN2ExC1ERKS_')
        self.host('dtorEx', VOID, [self.prec['Ex']], '_ZN2ExD1Ev',
                  nothrow=True)
        self.host('ctorExD', VOID, [self.prec['ExD'], INT], '_ZN3ExDC1Ei')
        self.host('dtorExD', VOID, [self.prec['ExD']], '_ZN3ExDD1Ev',
                  nothrow=True)
        self.host('ctorTc', VOID, [self.prec['Tc'], INT], '_ZN2TcC1Ei')
        self.host('copyTc', VOID, [self.prec['Tc'], self.prec['Tc']],
                  '_ZN2TcC1ERKS_')
        self.host('dtorTc', VOID, [self.prec['Tc']], '_ZN2TcD1Ev', nothrow=True)
        self.host('ctorTd', VOID, [self.prec['Td'], INT], '_ZN2TdC1Ei')
        self.host('dtorTd', VOID, [self.prec['Td']], '_ZN2TdD1Ev')
        self.host('ctorTh', VOID, [self.prec['Th'], INT], '_ZN2ThC1Ei')
        self.host('dtorTh', VOID, [self.prec['Th']], '_ZN2ThD1Ev',
                  nothrow=True)
        self.host('ctorAr', VOID, [self.prec['Ar']], '_ZN2ArC1Ev')
        self.host('dtorAr', VOID, [self.prec['Ar']], '_ZN2ArD1Ev',
                  nothrow=True)
        self.host('ctorTa', VOID, [self.prec['Ta']], '_ZN2TaC1Ev')
        self.host('dtorTa', VOID, [self.prec['Ta']], '_ZN2TaD1Ev')
        self.host('ctorUa', VOID, [self.prec['Ua'], self.prec['Td']],
                  '_ZN2UaC1ERK2Td')
        self.host('dtorUa', VOID, [self.prec['Ua']], '_ZN2UaD1Ev',
                  nothrow=True)
        self.host('tv', INT, [], 'tv')
        self.host('via_c', INT, [INT], 'via_c')
        self.host('set_fail', VOID, [INT], 'set_fail')
        self.host('fail_now', INT, [], 'fail_now')
        self.voidp = g.gid('voidp')
        g.d('TY_PTR %d %d' % (self.voidp, VOID))
        self.host('opnew', self.voidp, [2], '_Znwm')
        self.host('opdel', VOID, [self.voidp, 2], '_ZdlPvm')
        self.arr = {}
        gp = g.gid('g_P')
        g.d('DECL_VAR %d %d 0 0 4 %d' % (gp, self.rec['P'], g.sid('g_P')))
        self.g_P = gp
        gv = g.gid('g_i')
        g.d('DECL_VAR %d %d 0 0 4 %d' % (gv, INT, g.sid('g_i')))
        self.g_i = gv

    def functype(self, ret, params):
        key = (ret, tuple(params))
        if key not in self.ft:
            g = self.gen.gid()
            self.gen.d('TY_FUNC %d %d 0 [%s]' % (
                g, ret, ', '.join(str(p) for p in params)))
            self.ft[key] = g
        return self.ft[key]

    def host(self, name, ret, params, symbol, nothrow=False):
        g = self.gen.gid(name)
        self.gen.d('DECL_FUNC %d %d 0 %d %d' % (
            g, self.functype(ret, params), 0x80 if nothrow else 0,
            self.gen.sid(symbol)))
        self.fn[name] = (g, self.functype(ret, params), len(params))
        return g

    def call(self, name):
        g, ft, n = self.fn[name]
        return 'CALL %d %d %d 0' % (g, ft, n)


class Func:
    """One scenario function: the ECIL records and the C++ text."""

    def __init__(self, ctx, name, params=('k',), spec=None, lead=(),
                 ret=INT, symbol=None, gid=None):
        self.ctx = ctx
        self.name = name
        self.params = list(params)
        self.lead = list(lead)
        self.ret = ret
        self.symbol = symbol
        self.gid = gid
        self.spec = spec
        self.lines = []
        self.cpp = []
        self.locals = {}
        self.next_l = 1
        self.next_label = 1
        self.labels = {}
        self.indent = 1
        for (n, t, this) in self.lead:
            self.locals[n] = (self.new_l(), 'this')
        for p in self.params:
            self.locals[p] = (self.new_l(), 'int')

    def new_l(self):
        n = self.next_l
        self.next_l += 1
        return n

    def r(self, text):
        self.lines.append('  ' + text)

    def c(self, text):
        self.cpp.append('  ' * self.indent + text)

    # expressions: returns (records, C++ text, is_bool)
    def expr(self, e):
        ctx = self.ctx
        if isinstance(e, int):
            return ['CONST_INT 1 %d' % e], str(e), False
        if isinstance(e, str):
            l, kind = self.locals[e]
            if kind == 'int':
                return ['LREF %d' % l, 'LOAD 0'], e, False
            if kind == 'static':
                return ['GREF %d' % l, 'LOAD 0'], e, False
            raise ValueError(e)
        op = e[0]
        if op in ('+', '-', '*'):
            a, ta, _ = self.expr(e[1])
            b, tb, _ = self.expr(e[2])
            rec = {'+': 'ADD 1', '-': 'SUB 1', '*': 'MUL 1'}[op]
            return a + b + [rec], '(%s %s %s)' % (ta, op, tb), False
        if op in ('==', '<', '!='):
            a, ta, _ = self.expr(e[1])
            b, tb, _ = self.expr(e[2])
            k = {'<': 0, '==': 4, '!=': 5}[op]
            return a + b + ['CMP %d' % k], '(%s %s %s)' % (ta, op, tb), True
        if op == 'call':
            args = []
            texts = []
            for a in e[2]:
                r, t, _ = self.expr(a)
                args += r
                texts.append(t)
            return (args + [ctx.call(e[1])],
                    '%s(%s)' % (e[1], ', '.join(texts)), False)
        if op == 'fld':
            l, kind = self.locals[e[1]]
            if kind == 'ref':
                return (['LREF %d' % l, 'LOAD 0', 'DEREF',
                         'FIELD %d 0' % self.local_rec[e[1]], 'LOAD 0'],
                        '%s.v' % e[1], False)
            return (['LREF %d' % l, 'FIELD %d 0' % self.local_rec[e[1]],
                     'LOAD 0'], '%s.v' % e[1], False)
        if op == 'gptrfld':
            gv = self.ctx.gvars[e[1]]
            return (['GREF %d' % gv, 'LOAD 0', 'DEREF',
                     'FIELD %d 0' % self.ctx.rec['Th'], 'LOAD 0'],
                    'r_%s->v' % e[1], False)
        if op == 'gptrfld_or':
            gv = self.ctx.gvars[e[1]]
            return (['GREF %d' % gv, 'LOAD 0', 'TO_BOOL',
                     'TERN_BEGIN %d' % INT, 'GREF %d' % gv, 'LOAD 0', 'DEREF',
                     'FIELD %d 0' % self.ctx.rec['Th'], 'LOAD 0', 'TERN_ELSE',
                     'CONST_INT 1 %d' % e[2], 'TERN_END'],
                    '(r_%s ? r_%s->v : %d)' % (e[1], e[1], e[2]), False)
        if op == 'pderef':
            l, kind = self.locals[e[1]]
            return (['LREF %d' % l, 'LOAD 0', 'DEREF',
                     'FIELD %d 0' % self.ctx.rec['P'], 'LOAD 0'],
                    '%s->v' % e[1], False)
        if op == 'ppderef':
            l, kind = self.locals[e[1]]
            return (['LREF %d' % l, 'LOAD 0', 'DEREF', 'LOAD 0', 'DEREF',
                     'FIELD %d 0' % self.ctx.rec['P'], 'LOAD 0'],
                    '%s->v' % e[1], False)
        if op == 'fld1':
            l, kind = self.locals[e[1]]
            return (['LREF %d' % l, 'LOAD 0', 'DEREF',
                     'FIELD %d 1' % self.local_rec[e[1]], 'LOAD 0'],
                    '%s.w' % e[1], False)
        if op == 'deref':
            l, kind = self.locals[e[1]]
            if kind == 'ptr':
                return (['LREF %d' % l, 'LOAD 0', 'DEREF', 'LOAD 0'],
                        '*%s' % e[1], False)
            return (['LREF %d' % l, 'LOAD 0', 'DEREF', 'LOAD 0', 'DEREF',
                     'LOAD 0'], '*%s' % e[1], False)
        if op == '?:':
            c, tc, cb = self.expr(e[1])
            a, ta, _ = self.expr(e[2])
            b, tb, _ = self.expr(e[3])
            ra = a
            rb = b
            if isinstance(e[2], tuple) and e[2][0].startswith('throw_'):
                ra, ta = self.throw_records(e[2])
            if isinstance(e[3], tuple) and e[3][0].startswith('throw_'):
                rb, tb = self.throw_records(e[3])
            rc = c if cb else c + ['TO_BOOL']
            return (rc + ['TERN_BEGIN %d' % INT] + ra + ['TERN_ELSE'] + rb +
                    ['TERN_END'], '(%s ? %s : %s)' % (tc, ta, tb), False)
        if op.startswith('throw_'):
            r, t = self.throw_records(e)
            return r, t, False
        raise ValueError(e)

    local_rec = {}
    var_cls = {}

    def has_throw(self, e):
        if isinstance(e, tuple):
            if e[0] in ('throw_int', 'throw_ex', 'throw_T', 'throw_Th',
                        'throw_exd', 'throw_pod', 'throw_pptr', 'throw_Tc'):
                return True
            return any(self.has_throw(x) for x in e[1:])
        return False

    def throw_records(self, s):
        ctx = self.ctx
        kind = s[0]
        if kind == 'throw_int':
            v, tv, _ = self.expr(s[1])
            return (['THROW_BEGIN %d' % INT] + v + ['STORE 2 0',
                    'THROW_END %d nil' % ctx.ti['int']],
                    'throw %s' % tv)
        if kind == 'throw_pod':
            v, tv, _ = self.expr(s[1])
            return (['THROW_BEGIN %d' % ctx.rec['P'], 'FIELD %d 0' % ctx.rec['P']]
                    + v + ['STORE 2 0', 'THROW_END %d nil' % ctx.ti['P']],
                    'throw P{%s}' % tv)
        if kind == 'throw_pptr':
            return (['THROW_BEGIN %d' % ctx.prec['P'], 'GREF %d' % ctx.g_P,
                     'ADDR', 'STORE 2 0',
                     'THROW_END %d nil' % ctx.ti['pP']], 'throw &g_P')
        if kind == 'throw_ptr':
            return (['THROW_BEGIN %d' % ctx.pint, 'GREF %d' % ctx.g_i,
                     'ADDR', 'STORE 2 0',
                     'THROW_END %d nil' % ctx.ti['pint']], 'throw &g_i')
        name = {'throw_ex': 'Ex', 'throw_T': 'T', 'throw_Th': 'Th',
                'throw_exd': 'ExD', 'throw_Tc': 'Tc'}[kind]
        v, tv, _ = self.expr(s[1])
        ctor = 'ctor' + name
        return (['THROW_BEGIN %d' % ctx.rec[name], 'ADDR'] + v +
                [ctx.call(ctor),
                 'THROW_END %d %d' % (ctx.ti[name], ctx.fn['dtor' + name][0])],
                'throw %s(%s)' % (name, tv))

    def label(self, name):
        if name not in self.labels:
            self.labels[name] = self.new_l()
        return self.labels[name]

    # statements
    def stmts(self, body):
        for s in body:
            self.stmt(s)

    def fullexpr(self, records):
        self.r('FULLEXPR_BEGIN')
        for x in records:
            self.r(x)
        self.r('FULLEXPR_END')

    def stmt(self, s):
        ctx = self.ctx
        g = ctx.gen
        k = s[0]
        if k == 'decl':
            l = self.new_l()
            self.locals[s[1]] = (l, 'int')
            self.r('LOCAL %d %d %d 4 0' % (l, INT, g.sid(s[1])))
            rec, t, _ = self.expr(s[2])
            self.c('int %s = %s;' % (s[1], t))
            body = ['LREF %d' % l] + rec + ['STORE 2 0']
            if self.has_throw(s[2]):
                self.fullexpr(body)
            else:
                for x in body:
                    self.r(x)
        elif k == 'assign':
            l, _ = self.locals[s[1]]
            rec, t, _ = self.expr(s[2])
            self.c('%s = %s;' % (s[1], t))
            if self.has_throw(s[2]):
                self.fullexpr(['LREF %d' % l] + rec + ['STORE 0 0'])
            else:
                for x in ['LREF %d' % l] + rec + ['STORE 0 0']:
                    self.r(x)
        elif k == 'obj':
            name = s[3] if len(s) > 3 else 'T'
            l = self.new_l()
            self.locals[s[1]] = (l, 'obj')
            self.r('LOCAL %d %d %d 4 0' % (l, ctx.rec[name], g.sid(s[1])))
            rec, t, _ = self.expr(s[2])
            self.c('%s %s(%s);' % (name, s[1], t))
            for x in ['LREF %d' % l, 'ADDR'] + rec + [ctx.call('ctor' + name),
                      'CLEANUP_BEGIN 3 nil 0', 'LREF %d' % l, 'ADDR',
                      ctx.call('dtor' + name), 'CLEANUP_BODY']:
                self.r(x)
        elif k in ('tmp', 'tmp2'):
            # An expression statement with one or two Td temporaries: each
            # has an "all" cleanup that runs at the end of the full
            # expression, last constructed first.
            vals = s[1:]
            texts = []
            body = []
            cl = []
            for v in vals:
                l = self.new_l()
                rec, t, _ = self.expr(v)
                texts.append('Td(%s)' % t)
                body += ['LOCAL %d %d nil 4 0' % (l, ctx.rec['Td']),
                         'LREF %d' % l, 'ADDR'] + rec + [
                    ctx.call('ctorTd'), 'CLEANUP_BEGIN 3 nil 0',
                    'LREF %d' % l, 'ADDR', ctx.call('dtorTd'),
                    'CLEANUP_BODY']
            if len(texts) == 1:
                self.c('%s;' % texts[0])
            else:
                self.c('(void)(%s);' % ', '.join(texts))
            self.fullexpr(body)
        elif k == 'note':
            rec, t, _ = self.expr(s[1])
            self.c('note(%s);' % t)
            for x in rec + [ctx.call('note')]:
                self.r(x)
        elif k == 'call':
            rec, t, _ = self.expr(s)
            self.c('%s;' % t)
            for x in rec + ['DROP']:
                self.r(x)
        elif k == 'ret':
            rec, t, _ = self.expr(s[1])
            self.c('return %s;' % t)
            if self.has_throw(s[1]):
                self.fullexpr(rec)
                self.r('RETURN_STMT true')
            else:
                for x in rec + ['RETURN_STMT true']:
                    self.r(x)
        elif k == 'if':
            rec, t, isb = self.expr(s[1])
            self.c('if (%s) {' % t)
            for x in rec + ([] if isb else ['TO_BOOL']) + ['IF_BEGIN 0']:
                self.r(x)
            self.indent += 1
            self.stmts(s[2])
            self.indent -= 1
            if len(s) > 3 and s[3]:
                self.c('} else {')
                self.r('IF_ELSE')
                self.indent += 1
                self.stmts(s[3])
                self.indent -= 1
            self.c('}')
            self.r('IF_END')
        elif k == 'for':
            var, n, body = s[1], s[2], s[3]
            l = self.new_l()
            self.locals[var] = (l, 'int')
            self.c('for (int %s = 0; %s < %d; ++%s) {' % (var, var, n, var))
            self.r('FOR_BEGIN')
            self.r('LOCAL %d %d %d 4 0' % (l, INT, g.sid(var)))
            for x in ['LREF %d' % l, 'CONST_INT 1 0', 'STORE 2 0',
                      'FOR_COND 1', 'LREF %d' % l, 'LOAD 0',
                      'CONST_INT 1 %d' % n, 'CMP 0', 'FOR_STEP 1',
                      'LREF %d' % l, 'DUP', 'LOAD 0', 'CONST_INT 1 1',
                      'ADD 1', 'STORE 0 0', 'FOR_BODY']:
                self.r(x)
            self.indent += 1
            self.stmts(body)
            self.indent -= 1
            self.c('}')
            self.r('FOR_END')
        elif k == 'break':
            self.c('break;')
            self.r('BREAK_STMT')
        elif k == 'continue':
            self.c('continue;')
            self.r('CONTINUE_STMT')
        elif k == 'goto':
            lab = self.label(s[1])
            self.c('goto %s;' % s[1])
            self.r('GOTO_STMT %d 0' % lab)
        elif k == 'label':
            lab = self.label(s[1])
            self.c('%s:;' % s[1])
            self.r('LABEL %d nil' % lab)
        elif k in ('throw_int', 'throw_ex', 'throw_T', 'throw_Th', 'throw_ptr',
                   'throw_exd', 'throw_pod', 'throw_pptr', 'throw_Tc'):
            rec, t = self.throw_records(s)
            self.c('%s;' % t)
            self.fullexpr(rec)
        elif k == 'callv':
            args = []
            texts = []
            for a in s[2]:
                r, t, _ = self.expr(a)
                args += r
                texts.append(t)
            self.c('%s(%s);' % (s[1], ', '.join(texts)))
            for x in args + [ctx.call(s[1])]:
                self.r(x)
        elif k in ('newobj', 'newtmp', 'condnew'):
            self.new_stmt(s)
        elif k == 'delobj':
            l, _ = self.locals[s[1]]
            cls = self.var_cls.get(s[1], 'Th')
            self.c('delete %s;' % s[1])
            for x in ['LREF %d' % l, 'LOAD 0', 'DELETE %d %d %d 2' % (
                    ctx.fn['opdel'][0], ctx.fn['dtor' + cls][0],
                    ctx.rec[cls])]:
                self.r(x)
        elif k == 'arr':
            n = s[2]
            cls = s[3] if len(s) > 3 else 'Ar'
            key = (cls, n)
            if key not in ctx.arr:
                ga = g.gid()
                g.d('TY_ARRAY %d %d %d 0' % (ga, ctx.rec[cls], n))
                ctx.arr[key] = ga
            l = self.new_l()
            self.locals[s[1]] = (l, 'arr')
            self.c('%s %s[%d];' % (cls, s[1], n))
            self.r('LOCAL %d %d %d 4 0' % (l, ctx.arr[key], g.sid(s[1])))
            for x in ['LREF %d' % l, 'ARRAY_CTOR_BEGIN %d 1' % ctx.rec[cls],
                      'ARRAY_PARTIAL_DTOR %d' % ctx.fn['dtor' + cls][0],
                      ctx.call('ctor' + cls), 'ARRAY_CTOR_END',
                      'CLEANUP_BEGIN 3 nil 0', 'LREF %d' % l,
                      'ARRAY_DTOR_BEGIN %d 1' % ctx.rec[cls],
                      ctx.call('dtor' + cls), 'ARRAY_DTOR_END',
                      'CLEANUP_BODY']:
                self.r(x)
        elif k == 'arrua':
            # Ua name[n]; the constructor has a default argument, a Td
            # temporary whose destructor can throw: the element loop keeps the
            # element pointer in a local and the call has its own FULLEXPR.
            n = s[2]
            key = ('Ua', n)
            if key not in ctx.arr:
                ga = g.gid()
                g.d('TY_ARRAY %d %d %d 0' % (ga, ctx.rec['Ua'], n))
                ctx.arr[key] = ga
            l = self.new_l()
            lp = self.new_l()
            lt = self.new_l()
            self.locals[s[1]] = (l, 'arr')
            self.c('Ua %s[%d];' % (s[1], n))
            self.r('LOCAL %d %d %d 4 0' % (l, ctx.arr[key], g.sid(s[1])))
            for x in ['LREF %d' % l, 'ARRAY_CTOR_BEGIN %d 1' % ctx.rec['Ua'],
                      'ARRAY_PARTIAL_DTOR %d' % ctx.fn['dtorUa'][0],
                      'MATERIALIZE_TEMP %d %d' % (lp, ctx.prec['Ua']), 'DROP',
                      'FULLEXPR_BEGIN', 'LREF %d' % lp, 'LOAD 0',
                      'LOCAL %d %d nil 4 0' % (lt, ctx.rec['Td']),
                      'LREF %d' % lt, 'ADDR', ctx.call('tv'),
                      ctx.call('ctorTd'), 'CLEANUP_BEGIN 3 nil 0',
                      'LREF %d' % lt, 'ADDR', ctx.call('dtorTd'),
                      'CLEANUP_BODY', 'LREF %d' % lt, 'ADDR',
                      ctx.call('ctorUa'), 'FULLEXPR_END', 'ARRAY_CTOR_END',
                      'CLEANUP_BEGIN 3 nil 0', 'LREF %d' % l,
                      'ARRAY_DTOR_BEGIN %d 1' % ctx.rec['Ua'],
                      ctx.call('dtorUa'), 'ARRAY_DTOR_END', 'CLEANUP_BODY']:
                self.r(x)
        elif k == 'static_int':
            gv = g.gid()
            g.d('DECL_VAR %d %d 1 273 4 %d' % (gv, INT, g.sid(
                '_ZZ%d%sE%s' % (len('c_' + self.name), 'c_' + self.name,
                                s[1]))))
            rec, t, _ = self.expr(s[2])
            self.locals[s[1]] = (gv, 'static')
            self.c('static int %s = %s;' % (s[1], t))
            self.r('STATIC_INIT_BEGIN %d 0' % gv)
            for x in ['GREF %d' % gv] + rec + ['STORE 2 0']:
                self.r(x)
            self.r('STATIC_INIT_END')
        elif k == 'memobj':
            l = self.new_l()
            self.locals[s[1]] = (l, 'obj')
            self.r('LOCAL %d %d %d 4 0' % (l, ctx.mem['rec'], g.sid(s[1])))
            rec, t, _ = self.expr(s[2])
            self.c('RMem %s(%s);' % (s[1], t))
            for x in ['LREF %d' % l, 'ADDR'] + rec + [
                    'CALL %d %d 2 0' % (ctx.mem['c1'], ctx.mem['ct']),
                    'CLEANUP_BEGIN 3 nil 0', 'LREF %d' % l, 'ADDR',
                    'CALL %d %d 1 0' % (ctx.mem['d1'], ctx.mem['dt']),
                    'CLEANUP_BODY']:
                self.r(x)
        elif k == 'rethrow':
            self.c('throw;')
            self.r('RETHROW')
        elif k == 'try':
            self.try_stmt(s)
        else:
            raise ValueError(s)

    def new_expr_records(self, arg, with_tmp, cls='Th'):
        """The records of `new Th(arg)` (or `new Th(T(arg).v)`) that leave
        the pointer on the stack, and the C++ text."""
        ctx = self.ctx
        g = ctx.gen
        lsz = self.new_l()
        lq = self.new_l()
        out = ['LOCAL %d 2 nil 8 2' % lsz,
               'LREF %d' % lsz, 'CONST_INT 2 4', 'STORE 2 0',
               'LOCAL %d %d nil 8 2' % (lq, ctx.voidp),
               'LREF %d' % lq, 'LREF %d' % lsz, 'LOAD 0', ctx.call('opnew'),
               'STORE 2 0',
               'CLEANUP_BEGIN 2 nil 1', 'LREF %d' % lq, 'LOAD 0',
               'LREF %d' % lsz, 'LOAD 0', ctx.call('opdel'), 'CLEANUP_BODY',
               'LREF %d' % lq, 'LOAD 0', 'CAST 11 %d' % ctx.prec[cls],
               'DUP']
        r, t, _ = self.expr(arg)
        if with_tmp:
            lt = self.new_l()
            out += ['LOCAL %d %d nil 4 2' % (lt, ctx.rec['T']),
                    'LREF %d' % lt, 'ADDR'] + r + [ctx.call('ctorT'),
                    'CLEANUP_BEGIN 3 nil 0', 'LREF %d' % lt, 'ADDR',
                    ctx.call('dtorT'), 'CLEANUP_BODY',
                    'LREF %d' % lt, 'FIELD %d 0' % ctx.rec['T'], 'LOAD 0']
            t = 'T(%s).v' % t
        else:
            out += r
        out += [ctx.call('ctor' + cls), 'CLEANUP_END']
        return out, 'new %s(%s)' % (cls, t)

    def new_stmt(self, s):
        ctx = self.ctx
        g = ctx.gen
        k = s[0]
        var = s[1]
        cls = s[4] if len(s) > 4 else 'Th'
        self.var_cls = dict(self.var_cls)
        self.var_cls[var] = cls
        l = self.new_l()
        self.locals[var] = (l, 'obj')
        self.r('LOCAL %d %d %d 8 0' % (l, ctx.prec[cls], g.sid(var)))
        if k == 'condnew':
            cond, arg = s[2], s[3]
            cr, ct, cb = self.expr(cond)
            nr, nt = self.new_expr_records(arg, True)
            self.c('Th *%s = %s ? %s : nullptr;' % (var, ct, nt))
            rec = (['LREF %d' % l] + cr + ([] if cb else ['TO_BOOL']) +
                   ['TERN_BEGIN %d' % ctx.prec['Th']] + nr +
                   ['TERN_ELSE', 'CONST_NULL %d' % ctx.prec['Th'],
                    'TERN_END', 'STORE 2 0'])
        else:
            nr, nt = self.new_expr_records(s[2], k == 'newtmp', cls)
            self.c('%s *%s = %s;' % (cls, var, nt))
            rec = ['LREF %d' % l] + nr + ['STORE 2 0']
        self.fullexpr(rec)

    def try_stmt(self, s):
        ctx = self.ctx
        g = ctx.gen
        body, handlers = s[1], s[2]
        flags = s[3] if len(s) > 3 else 0
        self.c('try {')
        self.r('TRY_BEGIN %d' % flags)
        self.r('BLOCK_BEGIN')
        self.indent += 1
        self.stmts(body)
        self.indent -= 1
        self.r('BLOCK_END')
        for h in handlers:
            kind, var, hbody = h
            if kind == 'all':
                self.c('} catch (...) {')
                self.r('CATCH_BEGIN nil')
            elif kind == 'int':
                self.c('} catch (int %s) {' % var)
                self.r('CATCH_BEGIN %d' % ctx.ti['int'])
                l = self.new_l()
                self.locals[var] = (l, 'int')
                self.r('LOCAL %d %d %d 4 0' % (l, INT, g.sid(var)))
                self.r('CATCH_PARAM %d 2 nil' % l)
            elif kind == 'exref':
                self.c('} catch (const Ex &%s) {' % var)
                self.r('CATCH_BEGIN %d' % ctx.ti['Ex'])
                l = self.new_l()
                self.locals[var] = (l, 'ref')
                self.local_rec = dict(self.local_rec)
                self.local_rec[var] = ctx.rec['Ex']
                self.r('LOCAL %d %d %d 8 0' % (l, ctx.prec['Ex'], g.sid(var)))
                self.r('CATCH_PARAM %d 0 nil' % l)
            elif kind == 'exdref':
                self.c('} catch (const ExD &%s) {' % var)
                self.r('CATCH_BEGIN %d' % ctx.ti['ExD'])
                l = self.new_l()
                self.locals[var] = (l, 'ref')
                self.local_rec = dict(self.local_rec)
                self.local_rec[var] = ctx.rec['ExD']
                self.r('LOCAL %d %d %d 8 0' % (l, ctx.prec['ExD'],
                                               g.sid(var)))
                self.r('CATCH_PARAM %d 0 nil' % l)
            elif kind == 'stdref':
                self.c('} catch (const std::exception &%s) {' % var)
                self.r('CATCH_BEGIN %d' % ctx.std['ti'])
                l = self.new_l()
                self.locals[var] = (l, 'ref')
                self.r('LOCAL %d %d %d 8 0' % (l, ctx.std['ptr'],
                                               g.sid(var)))
                self.r('CATCH_PARAM %d 0 nil' % l)
            elif kind == 'pod':
                self.c('} catch (P %s) {' % var)
                self.r('CATCH_BEGIN %d' % ctx.ti['P'])
                l = self.new_l()
                self.locals[var] = (l, 'obj')
                self.local_rec = dict(self.local_rec)
                self.local_rec[var] = ctx.rec['P']
                self.r('LOCAL %d %d %d 4 0' % (l, ctx.rec['P'], g.sid(var)))
                self.r('CATCH_PARAM %d 4 nil' % l)
            elif kind == 'pptr':
                self.c('} catch (P *%s) {' % var)
                self.r('CATCH_BEGIN %d' % ctx.ti['pP'])
                l = self.new_l()
                self.locals[var] = (l, 'ptr')
                self.r('LOCAL %d %d %d 8 0' % (l, ctx.prec['P'], g.sid(var)))
                self.r('CATCH_PARAM %d 1 nil' % l)
            elif kind == 'ppref':
                self.c('} catch (P *&%s) {' % var)
                self.r('CATCH_BEGIN %d' % ctx.ti['pP'])
                l = self.new_l()
                self.locals[var] = (l, 'pref')
                self.r('LOCAL %d %d %d 8 0' % (l, ctx.ppP, g.sid(var)))
                self.r('CATCH_PARAM %d 7 nil' % l)
            elif kind == 'tcval':
                self.c('} catch (Tc %s) {' % var)
                self.r('CATCH_BEGIN %d' % ctx.ti['Tc'])
                l = self.new_l()
                self.locals[var] = (l, 'obj')
                self.r('LOCAL %d %d %d 4 0' % (l, ctx.rec['Tc'], g.sid(var)))
                self.r('CATCH_PARAM %d 5 %d' % (l, ctx.fn['copyTc'][0]))
                for x in ['CLEANUP_BEGIN 3 nil 0', 'LREF %d' % l, 'ADDR',
                          ctx.call('dtorTc'), 'CLEANUP_BODY']:
                    self.r(x)
            elif kind == 'tval':
                self.c('} catch (T %s) {' % var)
                self.r('CATCH_BEGIN %d' % ctx.ti['T'])
                l = self.new_l()
                self.locals[var] = (l, 'obj')
                self.local_rec = dict(self.local_rec)
                self.local_rec[var] = ctx.rec['T']
                self.r('LOCAL %d %d %d 4 0' % (l, ctx.rec['T'], g.sid(var)))
                self.r('CATCH_PARAM %d 5 %d' % (l, ctx.fn['copyT'][0]))
                for x in ['CLEANUP_BEGIN 3 nil 0', 'LREF %d' % l, 'ADDR',
                          ctx.call('dtorT'), 'CLEANUP_BODY']:
                    self.r(x)
            elif kind == 'ptr':
                self.c('} catch (int *%s) {' % var)
                self.r('CATCH_BEGIN %d' % ctx.ti['pint'])
                l = self.new_l()
                self.locals[var] = (l, 'ptr')
                self.r('LOCAL %d %d %d 8 0' % (l, ctx.pint, g.sid(var)))
                self.r('CATCH_PARAM %d 1 nil' % l)
            elif kind == 'pref':
                self.c('} catch (int *&%s) {' % var)
                self.r('CATCH_BEGIN %d' % ctx.ti['pint'])
                l = self.new_l()
                self.locals[var] = (l, 'pref')
                self.r('LOCAL %d %d %d 8 0' % (l, ctx.ppint, g.sid(var)))
                self.r('CATCH_PARAM %d 6 nil' % l)
            else:
                raise ValueError(kind)
            self.r('BLOCK_BEGIN')
            self.indent += 1
            self.stmts(hbody)
            self.indent -= 1
            self.r('BLOCK_END')
        self.c('}')
        self.r('TRY_END')

    def finish(self, falls_off=False):
        ctx = self.ctx
        g = ctx.gen
        ptypes = [t for (n, t, this) in self.lead] + [INT] * len(self.params)
        ft = ctx.functype(self.ret, ptypes)
        nothrow = self.spec is not None and self.spec[1] == 'noexcept'
        gid = self.gid
        if gid is None:
            gid = g.gid('f_' + self.name)
            g.d('DECL_FUNC %d %d 0 %d %d' % (
                gid, ft, 1 | (0x80 if nothrow else 0),
                g.sid(self.symbol or 'c_' + self.name)))
        ctx.fn.setdefault(self.name, (gid, ft, len(ptypes)))
        out = ['  FUNC_BEGIN %d 0' % gid]
        i = 0
        for (n, t, this) in self.lead:
            i += 1
            out.append('  PARAM %d %d %d %d %d' % (i, t, t, g.sid(n),
                                                   1 if this else 0))
        for p in self.params:
            i += 1
            out.append('  PARAM %d %d %d %d 0' % (i, INT, INT, g.sid(p)))
        if self.spec:
            out.append('  ' + self.spec[0])
        out += self.lines
        out.append('  FUNC_END %d' % (1 if falls_off else 0))
        g.body += out


def cpp_function(f, name_prefix, header_spec=''):
    if f.spec is not None:
        header_spec = f.spec[1]
        header_spec = ' ' + header_spec
    text = ['extern "C" int %s_%s(%s)%s {' % (
        name_prefix, f.name, ', '.join('int ' + p for p in f.params),
        header_spec)]
    text += f.cpp
    text.append('}')
    return '\n'.join(text)


HOST_PRELUDE = '''// Generated by gen_eh.py; do not edit.
extern "C" void note(int);
extern "C" int mk(int);
extern "C" int g_i;
extern "C" void set_fail(int);
extern "C" int fail_now();
extern "C" int via_c(int);
extern "C" int via_c_ref(int);
extern "C" void host_throw(int);
struct P { int v; };
extern P g_P;
struct T { int v; T(int); T(const T &); ~T(); };
struct Ex { int v; Ex(int); Ex(const Ex &); ~Ex(); };
struct ExD : Ex { int w; ExD(int); ~ExD(); };
struct Th { int v; Th(int); ~Th(); };
struct Td { int v; Td(int); ~Td() noexcept(false); };
struct Ar { int v; Ar(); ~Ar(); };
struct Ta { int v; Ta(); ~Ta() noexcept(false); };
extern "C" int tv();
struct Ua { int v; Ua(const Td &t = Td(tv())); ~Ua(); };
'''


def scenarios_core():
    """(name, ks, params, body)"""
    S = []

    def sc(name, ks, body, params=('k',)):
        S.append((name, ks, params, body))

    sc('order', range(0, 4), [
        ('try', [
            ('obj', 'a', 1), ('obj', 'b', 2),
            ('if', ('==', 'k', 1), [('throw_int', 5)]),
            ('obj', 'c', 3), ('note', 10),
            ('if', ('==', 'k', 2), [('throw_ex', 7)]),
            ('note', 11),
        ], [('int', 'v', [('note', 'v'), ('ret', 'v')]),
            ('exref', 'e', [('note', ('fld', 'e')),
                            ('ret', ('+', ('fld', 'e'), 1000))])]),
        ('ret', 0)])
    sc('nested', range(0, 5), [
        ('decl', 'r', 0),
        ('try', [
            ('obj', 'a', 1),
            ('try', [
                ('obj', 'b', 2),
                ('if', ('==', 'k', 1), [('throw_int', 11)]),
                ('if', ('==', 'k', 2), [('throw_ex', 12)]),
                ('if', ('==', 'k', 3), [('throw_T', 13)]),
            ], [('int', 'i', [('assign', 'r', 'i'), ('obj', 'c', 3),
                              ('if', ('==', 'k', 1), [('throw_ex', 13)])]),
                ('exref', 'e', [('assign', 'r', ('fld', 'e')),
                                ('rethrow',)])]),
            ('obj', 'd', 4),
        ], [('exref', 'e', [('assign', 'r', ('+', 'r',
                                              ('*', ('fld', 'e'), 2)))]),
            ('all', None, [('assign', 'r', -1)])]),
        ('ret', 'r')])
    sc('catch_all_rethrow', range(0, 3), [
        ('try', [
            ('try', [('obj', 'a', 1), ('if', 'k', [('throw_int', 'k')])],
             [('all', None, [('obj', 'b', 2), ('rethrow',)])]),
        ], [('int', 'i', [('ret', ('*', 'i', 2))])]),
        ('ret', 0)])
    sc('jumps', range(0, 8), [
        ('decl', 's', 0),
        ('for', 'i', 4, [
            ('try', [
                ('obj', 'a', 'i'),
                ('if', ('==', 'i', 1), [('if', ('==', 'k', 0),
                                         [('continue',)])]),
                ('if', ('==', 'i', 2), [('if', ('==', 'k', 1),
                                         [('break',)])]),
                ('if', ('==', 'i', 3), [('if', ('==', 'k', 2),
                                         [('ret', ('+', 's', 100))])]),
                ('if', ('==', 'i', 0), [('throw_int', ('+', 'i', 7))]),
                ('assign', 's', ('+', 's', 'i')),
            ], [('int', 'v', [
                ('obj', 'h', ('+', 50, 'v')),
                ('if', ('==', 'k', 3), [('continue',)]),
                ('if', ('==', 'k', 4), [('break',)]),
                ('if', ('==', 'k', 5), [('ret', ('+', 's', 200))]),
                ('assign', 's', ('+', 's', 'v'))])]),
        ]),
        ('try', [('obj', 'z', 9),
                 ('if', ('==', 'k', 6), [('goto', 'out')]),
                 ('assign', 's', ('+', 's', 1))],
         [('all', None, [])]),
        ('label', 'out'),
        ('ret', 's')])
    sc('ret_in_try', range(0, 3), [
        ('obj', 'outer', 1),
        ('try', [('obj', 'a', 2), ('if', 'k', [('ret', ('*', 'k', 2))]),
                 ('obj', 'b', 3), ('if', ('==', 'k', 0), [('throw_int', 4)])],
         [('all', None, [('ret', -1)])]),
        ('ret', 7)])
    sc('byval', range(0, 4), [
        ('try', [
            ('obj', 'a', 1),
            ('if', ('==', 'k', 1), [('throw_T', 21)]),
            ('if', ('==', 'k', 2), [('throw_ex', 22)]),
            ('if', ('==', 'k', 3), [('throw_int', 23)]),
        ], [('tval', 't', [('obj', 'x', 30), ('note', ('fld', 't')),
                           ('ret', ('fld', 't'))]),
            ('exref', 'e', [('ret', ('fld', 'e'))]),
            ('all', None, [('ret', -5)])]),
        ('ret', 0)])
    sc('pointers', range(0, 3), [
        ('try', [
            ('obj', 'a', 1),
            ('if', ('==', 'k', 1), [('throw_ptr',)]),
            ('if', ('==', 'k', 2), [('throw_int', 4)]),
        ], [('ptr', 'p', [('ret', ('deref', 'p'))]),
            ('int', 'i', [('ret', 'i')])]),
        ('try', [('if', ('==', 'k', 1), [('throw_ptr',)])],
         [('pref', 'p', [('ret', ('+', ('deref', 'p'), 1))])]),
        ('ret', 0)])
    sc('ternary_throw', range(0, 3), [
        ('try', [
            ('decl', 'x', ('?:', 'k', 1, ('throw_int', 77))),
            ('obj', 't', 'x'),
            ('ret', ('+', 'x', ('?:', ('<', 1, 'k'), ('call', 'mk', [0]),
                                ('throw_int', 5)))),
        ], [('int', 'i', [('ret', ('+', 'i', 1000))])]),
        ('ret', 0)])
    sc('cleanups_in_try', range(0, 4), [
        ('try', [
            ('obj', 'a', 1), ('note', ('call', 'mk', [0])),
            ('obj', 'b', 2),
            ('if', ('==', 'k', 1), [('note', ('call', 'mk', [91]))]),
            ('obj', 'c', 3),
            ('note', ('call', 'mk', [('?:', ('==', 'k', 2), 92, 0)])),
            ('obj', 'd', 4),
            ('ret', 0),
        ], [('int', 'i', [('ret', 'i')])]),
        ('ret', -1)])
    sc('escape', range(0, 3), [
        ('obj', 'a', 1),
        ('if', ('==', 'k', 1), [('throw_int', 61)]),
        ('obj', 'b', 2),
        ('if', ('==', 'k', 2), [('throw_ex', 62)]),
        ('ret', 0)])
    return S


MEM_REF = '''
struct RMem {
  T a; Th b; T c;
  RMem(int k) try : a(1), b(k == 1 ? 1 : 0), c(3) {
    note(50);
    if (k == 2) throw 70;
  } catch (...) { note(60); }
  ~RMem() noexcept(false) try { note(70); if (fail_now()) throw 71; }
  catch (...) { note(72); }
};
'''


def add_mem(ctx):
    """The class Mem of a function-try constructor (and its destructor)."""
    g = ctx.gen
    names = [g.sid(n) for n in ('Mem', 'a', 'b', 'c', '3Mem', '_ZN3MemC1Ei',
                                '_ZN3MemC2Ei', '_ZN3MemD1Ev', '_ZN3MemD2Ev')]
    r = g.gid('Mem')
    g.d('TY_RECORD_DECL %d 0 %d' % (r, names[0]))
    g.d('TY_RECORD_BODY %d 12 12 4 12 3' % r)
    g.d('REC_FIELD %d 0 nil nil %d 0' % (ctx.rec['T'], names[1]))
    g.d('REC_FIELD %d 4 nil nil %d 0' % (ctx.rec['Th'], names[2]))
    g.d('REC_FIELD %d 8 nil nil %d 0' % (ctx.rec['T'], names[3]))
    g.d('CLASS_MANGLE %d %d' % (r, names[4]))
    g.d('CLASS_INFO %d 12 4 nil 0' % r)
    g.d('CLASS_BASES %d []' % r)
    pm = g.gid('pMem')
    g.d('TY_PTR %d %d' % (pm, r))
    ct = ctx.functype(VOID, [pm, INT])
    dt = ctx.functype(VOID, [pm])
    c1, c2, d1, d2 = (g.gid() for _ in range(4))
    g.d('DECL_FUNC %d %d 0 0 %d' % (c1, ct, names[5]))
    g.d('DECL_FUNC %d %d 0 1 %d' % (c2, ct, names[6]))
    g.d('STRUCTOR %d %d 0' % (c1, c2))
    g.d('DECL_FUNC %d %d 0 0 %d' % (d1, dt, names[7]))
    g.d('DECL_FUNC %d %d 0 1 %d' % (d2, dt, names[8]))
    g.d('STRUCTOR %d %d 1' % (d1, d2))
    ctx.mem = {'rec': r, 'ptr': pm, 'c1': c1, 'd1': d1, 'ct': ct, 'dt': dt}

    def member(i):
        return ['LREF 1', 'LOAD 0', 'DEREF', 'FIELD %d %d' % (r, i), 'ADDR']

    def cleanup(i, dtor, kind):
        return (['CLEANUP_BEGIN %d nil 0' % kind] + member(i) +
                [ctx.call(dtor), 'CLEANUP_BODY'])
    f = Func(ctx, 'memc2', ('k',), lead=[('this', pm, True)], ret=VOID,
             gid=c2)
    f.r('TRY_BEGIN 1')
    for x in (['MEMINIT_BEGIN 2 0 0'] + member(0) + ['CONST_INT 1 1',
              ctx.call('ctorT'), 'MEMINIT_END'] + cleanup(0, 'dtorT', 2) +
              ['MEMINIT_BEGIN 2 1 0'] + member(1) +
              ['LREF 2', 'LOAD 0', 'CONST_INT 1 1', 'CMP 4',
               'TERN_BEGIN %d' % INT, 'CONST_INT 1 1', 'TERN_ELSE',
               'CONST_INT 1 0', 'TERN_END', ctx.call('ctorTh'),
               'MEMINIT_END'] + cleanup(1, 'dtorTh', 2) +
              ['MEMINIT_BEGIN 2 2 0'] + member(2) + ['CONST_INT 1 3',
              ctx.call('ctorT'), 'MEMINIT_END'] + cleanup(2, 'dtorT', 2) +
              ['BLOCK_BEGIN', 'CONST_INT 1 50', ctx.call('note'),
               'LREF 2', 'LOAD 0', 'CONST_INT 1 2', 'CMP 4', 'IF_BEGIN 0',
               'FULLEXPR_BEGIN', 'THROW_BEGIN %d' % INT, 'CONST_INT 1 70',
               'STORE 2 0', 'THROW_END %d nil' % ctx.ti['int'],
               'FULLEXPR_END', 'IF_END', 'BLOCK_END',
               'CATCH_BEGIN nil', 'BLOCK_BEGIN', 'CONST_INT 1 60',
               ctx.call('note'), 'RETHROW', 'BLOCK_END', 'TRY_END']):
        f.r(x)
    f.finish(falls_off=True)
    f = Func(ctx, 'memd2', (), lead=[('this', pm, True)], ret=VOID, gid=d2)
    for x in (['TRY_BEGIN 1'] + cleanup(0, 'dtorT', 3) +
              cleanup(1, 'dtorTh', 3) + cleanup(2, 'dtorT', 3) +
              ['BLOCK_BEGIN', 'CONST_INT 1 70', ctx.call('note'),
               ctx.call('fail_now'), 'TO_BOOL', 'IF_BEGIN 0',
               'FULLEXPR_BEGIN', 'THROW_BEGIN %d' % INT, 'CONST_INT 1 71',
               'STORE 2 0', 'THROW_END %d nil' % ctx.ti['int'],
               'FULLEXPR_END', 'IF_END', 'BLOCK_END',
               'CATCH_BEGIN nil', 'BLOCK_BEGIN', 'CONST_INT 1 72',
               ctx.call('note'), 'RETHROW', 'BLOCK_END', 'TRY_END']):
        f.r(x)
    f.finish(falls_off=True)


def add_std(ctx):
    """std::exception: a polymorphic class that the host (libstdc++) defines."""
    g = ctx.gen
    names = [g.sid(n) for n in (
        'std::exception', 'St9exception', '_ZNSt9exceptionD1Ev',
        '_ZNSt9exceptionD0Ev', '_ZNKSt9exception4whatEv', '_ZdlPvm')]
    vp = g.gid('stdvptr')
    g.d('TY_VPTR %d' % vp)
    r = g.gid('Std')
    g.d('TY_RECORD_DECL %d 0 %d' % (r, names[0]))
    g.d('TY_RECORD_BODY %d 8 8 8 12 1' % r)
    g.d('REC_FIELD %d 0 nil nil nil 1' % vp)
    g.d('CLASS_MANGLE %d %d' % (r, names[1]))
    g.d('CLASS_INFO %d 8 8 nil 3' % r)
    g.d('CLASS_BASES %d []' % r)
    pr = g.gid('pStd')
    g.d('TY_PTR %d %d' % (pr, r))
    dt = ctx.functype(VOID, [pr])
    chr_t = 4
    pc = g.gid('pchar')
    g.d('TY_PTR %d %d' % (pc, chr_t))
    wt = ctx.functype(pc, [pr])
    dt2 = ctx.functype(VOID, [ctx.voidp, 2])
    d1, d0, wh = (g.gid() for _ in range(3))
    dl = ctx.fn['opdel'][0]
    g.d('DECL_FUNC %d %d 0 0 %d' % (d1, dt, names[2]))
    g.d('DECL_FUNC %d %d 0 0 %d' % (d0, dt, names[3]))
    d2 = g.gid()
    g.d('DECL_FUNC %d %d 0 0 %d' % (d2, dt, g.sid('_ZNSt9exceptionD2Ev')))
    g.d('STRUCTOR %d %d 1' % (d1, d2))
    g.d('STRUCTOR_DELETING %d %d %d 1' % (d0, d1, dl))
    g.d('DECL_FUNC %d %d 0 0 %d' % (wh, wt, names[4]))
    g.d('CLASS_VFUNCS %d [%d, 0, 0, 0, %d, 2, 0, 1]' % (r, d1, wh))
    g.d('CLASS_OVERRIDES %d []' % r)
    ti = g.gid('tiStd')
    g.d('DECL_ABI_DATA %d 2 %d 0 0 nil' % (ti, r))
    ctx.std = {'rec': r, 'ptr': pr, 'ti': ti}
    ctx.host('host_throw', VOID, [INT], 'host_throw')


def scenarios_std():
    S = []

    def sc(name, ks, body, params=('k',)):
        S.append((name, ks, params, body))

    sc('stdexc', range(0, 3), [
        ('try', [('obj', 'a', 1), ('callv', 'host_throw', ['k']),
                 ('note', 5)],
         [('stdref', 'e', [('note', 7), ('ret', 1)]),
          ('int', 'i', [('note', 8), ('ret', 'i')])]),
        ('ret', 0)])
    return S


def add_statics(ctx, specs):
    """Namespace-scope pointers to Th initialized by new (STATIC_INIT), with
    the DEACTIVATABLE free cleanup of the new expression in the initializer
    region.  specs: [(name, cond|None, arg)] -> C++ text list."""
    g = ctx.gen
    ctx.gvars = {}
    cpp = []
    for name, cond, arg in specs:
        gv = g.gid()
        g.d('DECL_VAR %d %d 0 1 8 %d' % (gv, ctx.prec['Th'], g.sid(name)))
        ctx.gvars[name] = gv
    for name, cond, arg in specs:
        gv = ctx.gvars[name]
        f = Func(ctx, 'ginit_' + name, ())
        nr, nt = f.new_expr_records(arg, False)
        g.globals.append('  STATIC_INIT_BEGIN %d 0' % gv)
        g.globals.append('  FULLEXPR_BEGIN')
        g.globals.append('  GREF %d' % gv)
        if cond:
            g.globals += ['  CONST_INT 1 1', '  TO_BOOL',
                          '  TERN_BEGIN %d' % ctx.prec['Th']]
            g.globals += ['  ' + x for x in nr]
            g.globals += ['  TERN_ELSE', '  CONST_NULL %d' % ctx.prec['Th'],
                          '  TERN_END']
            cpp.append('Th *r_%s = 1 ? %s : nullptr;' % (name, nt))
        else:
            g.globals += ['  ' + x for x in nr]
            cpp.append('Th *r_%s = %s;' % (name, nt))
        g.globals += ['  STORE 2 0', '  FULLEXPR_END', '  STATIC_INIT_END']
    return cpp


def scenarios_static():
    S = []
    S.append(('gnew', range(0, 1), ('k',), [
        ('ret', ('+', ('gptrfld', 'g_a'), ('gptrfld_or', 'g_b', 100)))]))
    return S


def scenarios_ctor():
    S = []

    def sc(name, ks, body, params=('k',)):
        S.append((name, ks, params, body))

    sc('ctor_try', range(0, 3), [
        ('try', [('memobj', 'm', 'k'), ('note', 1)],
         [('int', 'i', [('note', 'i'), ('ret', 'i')]),
          ('all', None, [('note', 99), ('ret', 99)])]),
        ('ret', 0)])
    sc('dtor_try', range(0, 2), [
        ('callv', 'set_fail', [('?:', 'k', 99, -1)]),
        ('try', [('memobj', 'm', 0), ('note', 1)],
         [('int', 'i', [('note', 'i'), ('ret', 'i')]),
          ('all', None, [('note', 99), ('ret', 99)])]),
        ('ret', 0)])
    return S


def scenarios_new():
    S = []

    def sc(name, ks, body, params=('k',)):
        S.append((name, ks, params, body))

    sc('new_obj', range(0, 2), [
        ('try', [('newobj', 'p', 'k'), ('note', 5), ('delobj', 'p')],
         [('int', 'i', [('note', 'i'), ('ret', 'i')])]),
        ('ret', 0)])
    sc('new_tmp', range(0, 2), [
        ('try', [('newtmp', 'p', 'k'), ('note', 5), ('delobj', 'p')],
         [('int', 'i', [('note', 'i'), ('ret', 'i')])]),
        ('ret', 0)])
    sc('cond_new', range(0, 3), [
        ('try', [('condnew', 'p', ('<', 0, 'k'), ('+', 'k', -1)),
                 ('note', 6), ('delobj', 'p')],
         [('int', 'i', [('note', 'i'), ('ret', 'i')])]),
        ('ret', 0)])
    sc('arr', range(0, 4), [
        ('callv', 'set_fail', ['k']),
        ('try', [('arr', 'a', 3), ('note', 7)],
         [('int', 'i', [('note', 'i'), ('ret', 'i')])]),
        ('ret', 0)])
    sc('del_throws', range(0, 2), [
        ('try', [('newobj', 'p', ('?:', 'k', 7, 3), 'Td'), ('note', 5),
                 ('delobj', 'p')],
         [('int', 'i', [('note', 'i'), ('ret', 'i')])]),
        ('ret', 0)])
    sc('throw_th', range(0, 2), [
        ('try', [('obj', 'a', 1), ('throw_Th', 'k')],
         [('int', 'i', [('ret', 'i')]),
          ('all', None, [('note', 9), ('ret', 99)])]),
        ('ret', 0)])
    sc('pods', range(0, 5), [
        ('try', [
            ('obj', 'a', 1),
            ('if', ('==', 'k', 1), [('throw_pod', 31)]),
            ('if', ('==', 'k', 2), [('throw_pptr',)]),
            ('if', ('==', 'k', 3), [('throw_pptr',)]),
            ('if', ('==', 'k', 4), [('throw_pod', 32)]),
        ], [('pod', 'p', [('note', ('fld', 'p')), ('ret', ('fld', 'p'))]),
            ('pptr', 'q', [('ret', ('+', ('pderef', 'q'), 1))])]),
        ('try', [('if', ('==', 'k', 3), [('throw_pptr',)])],
         [('ppref', 'q', [('ret', ('+', ('ppderef', 'q'), 2))])]),
        ('ret', 0)])
    sc('hier', range(0, 4), [
        ('try', [('obj', 'a', 1),
                 ('if', ('==', 'k', 1), [('throw_exd', 10)]),
                 ('if', ('==', 'k', 2), [('throw_ex', 20)]),
                 ('if', ('==', 'k', 3), [('throw_int', 30)])],
         [('exdref', 'e', [('note', ('fld1', 'e')),
                           ('ret', ('fld1', 'e'))]),
          ('exref', 'e', [('note', ('fld', 'e')), ('ret', ('fld', 'e'))]),
          ('all', None, [('ret', -3)])]),
        ('ret', 0)])
    sc('static_local', range(0, 6), [
        ('static_int', 's', ('call', 'mk', [('+', 'k', -3)])),
        ('ret', 's')])
    sc('throw_ctor_arg', range(0, 2), [
        ('try', [('obj', 'a', 1), ('throw_ex', ('call', 'mk', ['k']))],
         [('int', 'i', [('ret', 'i')]),
          ('exref', 'e', [('ret', ('fld', 'e'))])]),
        ('ret', 0)])
    return S


def build(scenarios, mem=False, std=False, statics=None):
    ctx = Ctx()
    static_cpp = []
    if statics:
        static_cpp = add_statics(ctx, statics)
    if mem:
        add_mem(ctx)
    if std:
        add_std(ctx)
    cpp = [HOST_PRELUDE]
    if mem:
        cpp.append(MEM_REF)
    if static_cpp:
        cpp.append('\n'.join(static_cpp))
    if std:
        cpp.append('#include <stdexcept>\nextern "C" void host_throw(int);\n')
    funcs = []
    for name, ks, params, body in scenarios:
        f = Func(ctx, name, params)
        f.stmts(body)
        f.finish()
        funcs.append(f)
        cpp.append(cpp_function(f, 'r'))
    return ctx, funcs, '\n\n'.join(cpp) + '\n'


HOST = '''// Generated by gen_eh.py; do not edit.  Runs the CIR and the g++ version of
// every scenario over its inputs and compares the logs and the results.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
%(extra)s
extern "C" void note(int);
extern "C" int mk(int);
extern "C" int g_i;
extern "C" void set_fail(int);
extern "C" int fail_now();
struct P { int v; };
int g_i = 42;
P g_P = {77};
struct T { int v; T(int); T(const T &); ~T(); };
struct Ex { int v; Ex(int); Ex(const Ex &); ~Ex(); };
struct ExD : Ex { int w; ExD(int); ~ExD(); };
struct Th { int v; Th(int); ~Th(); };
struct Td { int v; Td(int); ~Td() noexcept(false); };
struct Ar { int v; Ar(); ~Ar(); };
struct Ta { int v; Ta(); ~Ta() noexcept(false); };
extern "C" int tv();
struct Ua { int v; Ua(const Td &t = Td(tv())); ~Ua(); };
static int log_[8192];
static int nlog_;
extern "C" void note(int v) { if (nlog_ < 8192) log_[nlog_++] = v; }
extern "C" int mk(int k) { if (k) throw k; return 1; }
static int ar_n, ar_fail = -1;
extern "C" void set_fail(int k) { ar_n = 0; ar_fail = k; }
extern "C" int fail_now() { return ar_fail == 99; }
extern "C" void __real___cxa_free_exception(void *);
extern "C" void __wrap___cxa_free_exception(void *p) {
  note(-3);   // the exception object freed because its initializer threw
  __real___cxa_free_exception(p);
}
%(hostfns)s
T::T(int i) : v(i) { note(i); }
T::T(const T &o) : v(o.v) { note(200 + o.v); }
T::~T() { note(100 + v); }
Ex::Ex(int c) : v(c) { note(500 + c); }
Ex::Ex(const Ex &o) : v(o.v) { note(700 + o.v); }
Ex::~Ex() { note(600 + v); }
ExD::ExD(int c) : Ex(c), w(c + 1) { note(1300 + c); }
ExD::~ExD() { note(1400 + w); }
Th::Th(int f) : v(f) { note(800 + f); if (f) throw f; }
Th::~Th() { note(900 + v); }
Td::Td(int f) : v(f) { note(950 + f); }
Td::~Td() noexcept(false) { note(960 + v); if (v == 7) throw 77; }
Ar::Ar() : v(ar_n) { note(1000 + ar_n); if (ar_n++ == ar_fail) throw ar_n * 10; }
Ar::~Ar() { note(1100 + v); }
Ta::Ta() : v(ar_n++) { note(1200 + v); }
Ta::~Ta() noexcept(false) { note(1500 + v); if (v == ar_fail) throw v + 50; }
Ua::Ua(const Td &) : v(ar_n++) { note(1600 + v); }
Ua::~Ua() { note(1700 + v); }
extern "C" int tv() { return ar_n == ar_fail ? 7 : 3; }
void *operator new(std::size_t n) { note(-1); return std::malloc(n); }
void operator delete(void *p) noexcept { note(-2); std::free(p); }
void operator delete(void *p, std::size_t) noexcept { note(-2); std::free(p); }
%(decls)s
static int run(int (*f)(int), int k) {
  int r;
  try { r = f(k); }
  catch (int i) { r = 10000 + i; }
  catch (const Ex &e) { r = 20000 + e.v; }
  catch (...) { r = -9999; }
  return r;
}
static int failures;
static void check(const char *name, int k, int (*c)(int), int (*r)(int)) {
  static int lc[8192];
  nlog_ = 0;
  int rc = run(c, k);
  int nc = nlog_;
  std::memcpy(lc, log_, sizeof(int) * nc);
  nlog_ = 0;
  int rr = run(r, k);
  if (rc != rr || nc != nlog_ || std::memcmp(lc, log_, sizeof(int) * nc)) {
    ++failures;
    std::printf("MISMATCH %%s(%%d): cir=%%d ref=%%d\\n  cir log:", name, k, rc, rr);
    for (int i = 0; i < nc; ++i) std::printf(" %%d", lc[i]);
    std::printf("\\n  ref log:");
    for (int i = 0; i < nlog_; ++i) std::printf(" %%d", log_[i]);
    std::printf("\\n");
  }
}
int main() {
%(setup)s
%(calls)s
  if (failures) { std::printf("%%d failures\\n", failures); return 1; }
  std::printf("ok\\n");
  return 0;
}
'''


def write_test(outdir, name, scenarios, comment, std='', mem=False,
               extra='', setup='', c_source=None, ref_edit=None, std_exc=False,
               hostfns='', statics=None):
    ctx, funcs, ref = build(scenarios, mem, std_exc, statics)
    if ref_edit:
        ref = ref_edit(ref)
    decls = []
    calls = []
    for (sname, ks, params, body) in scenarios:
        decls.append('extern "C" int c_%s(int);' % sname)
        decls.append('extern "C" int r_%s(int);' % sname)
        calls.append('  for (int k = %d; k < %d; ++k) check("%s", k, c_%s, '
                     'r_%s);' % (ks[0], ks[-1] + 1, sname, sname, sname))
    inputs = os.path.join(outdir, 'Inputs')
    tag = name.replace('cxx-s4-', 's4_').replace('-', '_')
    open(os.path.join(inputs, tag + '_ref.cpp'), 'w').write(ref)
    open(os.path.join(inputs, tag + '_host.cpp'), 'w').write(
        HOST % {'extra': extra, 'setup': setup, 'hostfns': hostfns, 'decls': '\n'.join(decls),
                'calls': '\n'.join(calls)})
    cxx = 'g++ %s' % std if std else 'g++'
    cobj = ''
    cline = ''
    if c_source:
        open(os.path.join(inputs, tag + '_c.c'), 'w').write(c_source)
        cline = '; RUN: gcc -c %%S/Inputs/%s_c.c -o %%t.c.o\n' % tag
        cobj = ' %t.c.o'
    hdr = comment + cline + '''; RUN: cat %%S/Inputs/header12_cxx_eh.txt %%s > %%t.txt
; RUN: %%ecil-asm %%t.txt -o %%t.ecil
; RUN: %%edg-cir --emit=cir %%t.ecil | %%FileCheck %%s
; RUN: %%edg-cir --emit=obj -o %%t.o %%t.ecil
; RUN: %(cxx)s %%S/Inputs/%(tag)s_host.cpp %%S/Inputs/%(tag)s_ref.cpp %%t.o%(cobj)s -Wl,--wrap=__cxa_free_exception -o %%t.exe
; RUN: %%t.exe
; RUN: %%edg-cir --emit=obj -O2 --verify-each -o %%t2.o %%t.ecil
; RUN: %(cxx)s %%S/Inputs/%(tag)s_host.cpp %%S/Inputs/%(tag)s_ref.cpp %%t2.o%(cobj)s -Wl,--wrap=__cxa_free_exception -o %%t2.exe
; RUN: %%t2.exe

''' % {'cxx': cxx, 'tag': tag, 'cobj': cobj}
    return hdr, ctx


CORE_COMMENT = '''; C++ (ECIL 1.2, slice S4): exceptions in the core statements.  The
; scenarios (gen_eh.py) are written as ECIL and as C++.  The host runs both over
; their inputs, the C++ one compiled by g++, and compares the log of every
; constructor and destructor and the results: cleanups in the order of
; unwinding, nested try, catch by value, by reference, of pointers, rethrow,
; return/break/continue/goto out of try bodies and handlers, a throwing arm of
; ?:, and exceptions that leave the function.
'''

NEW_COMMENT = '''; C++ (ECIL 1.2, slice S4): new and the freeing of storage when a constructor
; throws (a DEACTIVATABLE eh cleanup, also in a ?: arm), the partial destructor
; of an array constructor, and a throw whose initializer throws (the exception
; object is freed).  The scenarios (gen_eh.py) are compared with g++.
'''


STD_COMMENT = '''; C++ (ECIL 1.2, slice S4): catching a class that the host's libstdc++\n; defines: std::exception (external typeinfo of a polymorphic class), thrown\n; as a std::runtime_error by the host; an int passes the handler.\n'''


CFRAME_COMMENT = '''; C++ (ECIL 1.2, slice S4): an exception that crosses a C frame.  The C
; function via_c (compiled by gcc as C, with unwind tables) calls the CIR
; function cb, which throws; the exception passes through the C frame (no
; cleanups there) to the handler above it, and the cleanups of cb run.  The C++
; reference does the same through via_c_ref.
'''


def scenarios_cframe():
    S = []

    def sc(name, ks, body, params=('k',)):
        S.append((name, ks, params, body))

    sc('cb', range(0, 3), [
        ('obj', 'a', 1), ('if', ('==', 'k', 1), [('throw_int', 11)]),
        ('if', ('==', 'k', 2), [('throw_ex', 12)]), ('ret', 5)])
    sc('cframe', range(0, 3), [
        ('try', [('obj', 'b', 2), ('ret', ('call', 'via_c', ['k']))],
         [('int', 'i', [('note', 'i'), ('ret', 'i')]),
          ('exref', 'e', [('ret', ('fld', 'e'))])]),
        ('ret', -1)])
    return S


CFRAME_C = '''/* Generated by gen_eh.py.  Compiled as C: no cleanups, unwind tables only. */
int c_cb(int);
int r_cb(int);
int via_c(int k) { return c_cb(k) + 1; }
int via_c_ref(int k) { return r_cb(k) + 1; }
'''


STATIC_COMMENT = '''; C++ (ECIL 1.2, slice S4): namespace-scope pointers initialized by new
; (STATIC_INIT with the DEACTIVATABLE free cleanup of the allocation in the
; initializer region, also in a ?: arm).  Compared with g++.
'''


def write_all(outdir):
    hdr, ctx = write_test(outdir, 'cxx-s4-eh-core', scenarios_core(),
                          CORE_COMMENT)
    check = '''; CHECK-DAG: cir.try {
; CHECK-DAG: } catch [type #cir.global_view<@_ZTIi>
; CHECK-DAG: cir.begin_catch
; CHECK-DAG: cir.throw
; CHECK-DAG: cir.alloc.exception

'''
    open(os.path.join(outdir, 'cxx-s4-eh-core.test'), 'w').write(
        hdr + check + ctx.gen.text())
    hdr, ctx = write_test(
        outdir, 'cxx-s4-eh-cframe', scenarios_cframe(), CFRAME_COMMENT,
        c_source=CFRAME_C,
        ref_edit=lambda r: r.replace('via_c(', 'via_c_ref(', 1).replace(
            'return via_c(', 'return via_c_ref('))
    check = '''; CHECK-LABEL: cir.func {{.*}}@c_cframe(
; CHECK: cir.try {
; CHECK: cir.call @via_c

'''
    open(os.path.join(outdir, 'cxx-s4-eh-cframe.test'), 'w').write(
        hdr + check + ctx.gen.text())
    hdr, ctx = write_test(
        outdir, 'cxx-s4-eh-std', scenarios_std(), STD_COMMENT,
        std_exc=True, extra='#include <stdexcept>',
        hostfns='extern "C" void host_throw(int k) { if (k == 1) throw '
                'std::runtime_error("x"); if (k == 2) throw 9; }')
    check = '''; CHECK: } catch [type #cir.global_view<@_ZTISt9exception>
; CHECK: cir.begin_catch

'''
    open(os.path.join(outdir, 'cxx-s4-eh-std.test'), 'w').write(
        hdr + check + ctx.gen.text())
    hdr, ctx = write_test(
        outdir, 'cxx-s4-eh-static', scenarios_static(), STATIC_COMMENT,
        statics=[('g_a', None, 0), ('g_b', 1, 0)])
    check = '''; CHECK: cir.global
; CHECK: cleanup.isactive

'''
    open(os.path.join(outdir, 'cxx-s4-eh-static.test'), 'w').write(
        hdr + check + ctx.gen.text())
    hdr, ctx = write_test(outdir, 'cxx-s4-eh-new', scenarios_new(),
                          NEW_COMMENT)
    check = '''; CHECK-DAG: cir.array.ctor
; CHECK-DAG: partial_dtor
; CHECK-DAG: cleanup.isactive
; CHECK-DAG: cleanup.cond
; CHECK-DAG: } cleanup eh {
; CHECK-DAG: @__cxa_free_exception

'''
    open(os.path.join(outdir, 'cxx-s4-eh-new.test'), 'w').write(
        hdr + check + ctx.gen.text())


CLEANUP_COMMENT = '''; C++ (ECIL 1.2, slice S4): a noexcept(false) destructor that throws as a
; normal-path cleanup while other cleanups are still pending: the other
; locals of the scope (and of the enclosing scopes), the other temporaries of
; the full expression.  The exception must leave through the EH cleanups of
; the enclosing scopes, so the pending destructors still run (the writer
; does not send these shapes yet: it diagnoses them as unsupported).  The
; scenarios (gen_eh.py) are compared with g++.
'''


def scenarios_cleanup():
    S = []

    def sc(name, ks, body, params=('k',)):
        S.append((name, ks, params, body))

    handler = [('int', 'i', [('note', 'i'), ('ret', 'i')])]
    sc('inner', range(0, 2), [
        ('try', [('obj', 'a', 3, 'Td'),
                 ('obj', 'b', ('?:', 'k', 7, 3), 'Td'), ('note', 5)],
         handler),
        ('ret', 0)])
    sc('outer', range(0, 2), [
        ('try', [('obj', 'a', ('?:', 'k', 7, 3), 'Td'),
                 ('obj', 'b', 3, 'Td'), ('note', 5)],
         handler),
        ('ret', 0)])
    sc('three', range(0, 2), [
        ('try', [('obj', 'a', 3, 'Td'), ('obj', 'b', 4, 'Td'),
                 ('obj', 'c', ('?:', 'k', 7, 3), 'Td'), ('note', 5)],
         handler),
        ('ret', 0)])
    sc('nested', range(0, 2), [
        ('try', [('obj', 'a', 3, 'Td'),
                 ('try', [('obj', 'b', 4, 'Td'),
                          ('obj', 'c', ('?:', 'k', 7, 3), 'Td')],
                  [('int', 'i', [('note', 'i'), ('throw_int', 11)])]),
                 ('note', 6)], handler),
        ('ret', 0)])
    sc('tmp', range(0, 2), [
        ('try', [('obj', 'a', 3, 'Td'), ('tmp', ('?:', 'k', 7, 3)),
                 ('note', 5)], handler),
        ('ret', 0)])
    sc('tmp_pair', range(0, 2), [
        ('try', [('obj', 'a', 3, 'Td'),
                 ('tmp2', 4, ('?:', 'k', 7, 3)), ('note', 5)], handler),
        ('ret', 0)])
    sc('tmp_pair2', range(0, 2), [
        ('try', [('obj', 'a', 3, 'Td'),
                 ('tmp2', ('?:', 'k', 7, 3), 4), ('note', 5)], handler),
        ('ret', 0)])
    # An array whose element destructor throws.  Only the first element
    # (the last one destroyed) and no element are compared with g++: the pin
    # (like clang -fclangir) does not destroy the elements before an element
    # whose destructor throws, g++ and classic clang do.
    sc('arr_elem', range(0, 2), [
        ('try', [('callv', 'set_fail', [('?:', 'k', 99, 0)]),
                 ('obj', 'a', 3, 'Td'), ('arr', 'z', 3, 'Ta'),
                 ('obj', 'b', 4, 'Td'), ('note', 5)], handler),
        ('ret', 0)])
    sc('arr_only', range(0, 2), [
        ('try', [('callv', 'set_fail', [('?:', 'k', 99, 0)]),
                 ('arr', 'z', 3, 'Ta'), ('note', 5)], handler),
        ('ret', 0)])
    sc('escape', range(0, 2), [
        ('obj', 'a', 3, 'Td'),
        ('obj', 'b', ('?:', 'k', 7, 3), 'Td'),
        ('ret', 0)])
    return S


CTOR_COMMENT = '''; C++ (ECIL 1.2, slice S4): a constructor with a function-try-block and
; a destructor, compared with g++: the member initializers each have an eh
; cleanup that destroys the members already constructed (in reverse order)
; before the handler runs; the handler ends with the implicit rethrow; the
; exception then reaches the caller, which does not run the destructor of the
; incomplete object.
'''


def write_ctor(outdir):
    hdr, ctx = write_test(outdir, 'cxx-s4-eh-ctor', scenarios_ctor(),
                          CTOR_COMMENT, mem=True)
    check = '''; CHECK-LABEL: cir.func {{.*}}@_ZN3MemC2Ei
; CHECK: cir.try {
; CHECK: cleanup eh {
; CHECK: } catch all
; CHECK: cir.throw
; CHECK-LABEL: cir.func {{.*}}@_ZN3MemD2Ev

'''
    open(os.path.join(outdir, 'cxx-s4-eh-ctor.test'), 'w').write(
        hdr + check + ctx.gen.text())


def write_dyn(outdir):
    # A dynamic exception specification (C++14): the library's filter and
    # unexpected handlers, compared with g++ -std=c++14.
    ctx = Ctx()
    cpp = [HOST_PRELUDE]
    scen = [
        ('dyn', range(0, 3), [
            ('obj', 'a', 1),
            ('if', ('==', 'k', 1), [('throw_int', 5)]),
            ('if', ('==', 'k', 2), [('throw_ex', 6)]),
            ('ret', 0)], ('FUNC_EH_SPEC 2 [%d]' % ctx.ti['int'],
                          'throw(int)')),
        ('dyn_empty', range(0, 1), [
            ('obj', 'a', 1), ('ret', 0)], ('FUNC_EH_SPEC 2 []', 'throw()')),
        ('dyn_caller', range(0, 3), [
            ('try', [('ret', ('call', 'dyn', ['k']))],
             [('int', 'i', [('note', 'i'), ('ret', 'i')])]),
            ('ret', -1)], None),
    ]
    funcs = []
    for name, ks, body, spec in scen:
        f = Func(ctx, name, ('k',), spec=spec)
        f.stmts(body)
        f.finish()
        cpp.append(cpp_function(f, 'r'))
    # dyn_caller calls dyn: the reference calls r_dyn, not the CIR one
    ref = '\n\n'.join(cpp) + '\n'
    ref = ref.replace('return dyn(k);', 'return r_dyn(k);')
    ref = ref.replace('int r_dyn(int k)', 'int r_dyn(int k)', 1)
    decls = []
    calls = []
    for name, ks, body, spec in scen:
        decls.append('extern "C" int c_%s(int);' % name)
        decls.append('extern "C" int r_%s(int);' % name)
        calls.append('  for (int k = %d; k < %d; ++k) check("%s", k, c_%s, '
                     'r_%s);' % (ks[0], ks[-1] + 1, name, name, name))
    inputs = os.path.join(outdir, 'Inputs')
    open(os.path.join(inputs, 's4_eh_dyn_ref.cpp'), 'w').write(ref)
    open(os.path.join(inputs, 's4_eh_dyn_host.cpp'), 'w').write(
        HOST % {'extra': '#include <exception>', 'hostfns': '', 'decls': '\n'.join(decls),
                'setup': '  std::set_unexpected([] { throw 77; });',
                'calls': '\n'.join(calls)})
    hdr = '''; C++14 (ECIL 1.2, slice S4): a dynamic exception specification.  An
; allowed exception passes through, any other goes to the unexpected handler
; (the host's throws an int, which the specification allows), and the cleanups
; run on both paths, as with g++ (compiled with -std=c++14).
; RUN: cat %S/Inputs/header12_cxx_eh.txt %s > %t.txt
; RUN: %ecil-asm %t.txt -o %t.ecil
; RUN: %edg-cir --emit=cir %t.ecil | %FileCheck %s
; RUN: %edg-cir --emit=obj -o %t.o %t.ecil
; RUN: g++ -std=c++14 -Wno-deprecated %S/Inputs/s4_eh_dyn_host.cpp %S/Inputs/s4_eh_dyn_ref.cpp %t.o -Wl,--wrap=__cxa_free_exception -o %t.exe
; RUN: %t.exe
; RUN: %edg-cir --emit=obj -O2 --verify-each -o %t2.o %t.ecil
; RUN: g++ -std=c++14 -Wno-deprecated %S/Inputs/s4_eh_dyn_host.cpp %S/Inputs/s4_eh_dyn_ref.cpp %t2.o -Wl,--wrap=__cxa_free_exception -o %t2.exe
; RUN: %t2.exe

; CHECK-LABEL: cir.func {{.*}}@c_dyn(
; CHECK: } filter [#cir.global_view<@_ZTIi>
; CHECK: cir.resume
; CHECK: } unexpected (
; CHECK: cir.eh.unexpected
; CHECK-LABEL: cir.func {{.*}}@c_dyn_empty(
; CHECK: } filter []
; CHECK: cir.unreachable

'''
    open(os.path.join(outdir, 'cxx-s4-eh-dynspec.test'), 'w').write(
        hdr + ctx.gen.text())


NOEXCEPT_HOST = '''// Generated by gen_eh.py; do not edit.
#include <cstdio>
#include <cstdlib>
#include <exception>
extern "C" void note(int);
extern "C" int mk(int);
extern "C" int c_nx(int);
extern "C" int c_nx_inner(int);
extern "C" int c_nx_call(int);
struct T { int v; T(int); T(const T &); ~T(); };
static int log_[64];
static int nlog_;
extern "C" void note(int v) { log_[nlog_++] = v; }
extern "C" int mk(int k) { if (k) throw k; return 1; }
T::T(int i) : v(i) { note(i); }
T::T(const T &o) : v(o.v) { note(200 + o.v); }
T::~T() { note(100 + v); }
static void on_terminate() {
  std::printf("terminate:");
  for (int i = 0; i < nlog_; ++i) std::printf(" %d", log_[i]);
  std::printf("\\n");
  std::fflush(stdout);
  std::_Exit(66);
}
int main(int argc, char **) {
  std::set_terminate(on_terminate);
  if (c_nx(0) != 0 || c_nx_inner(0) != 0 || c_nx_inner(1) != 11 ||
      c_nx_call(0) != 0)
    return 1;
  nlog_ = 0;
  if (argc > 2)
    c_nx_call(3);
  else if (argc > 1)
    c_nx(5);
  std::printf("returned\\n");
  return 0;
}
'''


TERM_HOST = '''// Generated by gen_eh.py; do not edit.  Runs one scenario (argv[1]) that
// must end in std::terminate: the handler prints the log and exits with 66.
#include <cstdio>
#include <cstdlib>
#include <exception>
extern "C" void note(int);
struct T { int v; T(int); T(const T &); ~T(); };
struct Td { int v; Td(int); ~Td() noexcept(false); };
struct Tc { int v; Tc(int); Tc(const Tc &); ~Tc(); };
static int log_[64];
static int nlog_;
extern "C" void note(int v) { log_[nlog_++] = v; }
extern "C" int mk(int k) { if (k) throw k; return 1; }
T::T(int i) : v(i) { note(i); }
T::T(const T &o) : v(o.v) { note(200 + o.v); }
T::~T() { note(100 + v); }
Td::Td(int f) : v(f) { note(950 + f); }
Td::~Td() noexcept(false) { note(960 + v); if (v == 7) throw 77; }
Tc::Tc(int i) : v(i) { note(2000 + i); }
Tc::Tc(const Tc &o) : v(o.v) { note(2100 + o.v); throw 5; }
Tc::~Tc() { note(2200 + v); }
extern "C" int c_copy_throws(int);
extern "C" int c_dtor_unwinding(int);
extern "C" int c_dtor_normal(int);
extern "C" int c_dtor_unwinding_ok(int);
static void on_terminate() {
  std::printf("terminate:");
  for (int i = 0; i < nlog_; ++i) std::printf(" %d", log_[i]);
  std::printf("\\n");
  std::fflush(stdout);
  std::_Exit(66);
}
int main(int argc, char **argv) {
  std::set_terminate(on_terminate);
  int which = argc > 1 ? std::atoi(argv[1]) : 0;
  int r = 0;
  try {
    if (which == 1) r = c_copy_throws(0);
    else if (which == 2) r = c_dtor_unwinding(1);
    else if (which == 4) r = c_dtor_unwinding_ok(1);
    else r = c_dtor_normal(0);
  } catch (int i) {
    // 1 and 2 must terminate and never get here
    std::printf("caught %d\\n", i);
    return 0;
  }
  std::printf("returned %d\\n", r);
  return 3;
}
'''


def write_terminate(outdir):
    # A catch-by-value whose copy constructor throws, and a noexcept(false)
    # destructor that throws during unwinding: both std::terminate.
    ctx = Ctx()
    f = Func(ctx, 'copy_throws', ('k',))
    f.stmts([('try', [('throw_Tc', 1)],
              [('tcval', 't', [('note', 5), ('ret', 1)])]),
             ('ret', 0)])
    f.finish()
    f = Func(ctx, 'dtor_unwinding', ('k',))
    f.stmts([('obj', 'a', 7, 'Td'), ('throw_int', 'k'), ('ret', 0)])
    f.finish()
    f = Func(ctx, 'dtor_unwinding_ok', ('k',))
    f.stmts([('obj', 'a', 3, 'Td'), ('throw_int', 'k'), ('ret', 0)])
    f.finish()
    f = Func(ctx, 'dtor_normal', ('k',))
    f.stmts([('obj', 'a', 7, 'Td'), ('ret', 0)])
    f.finish()
    inputs = os.path.join(outdir, 'Inputs')
    open(os.path.join(inputs, 's4_eh_terminate_host.cpp'), 'w').write(
        TERM_HOST)
    hdr = '''; C++ (ECIL 1.2, slice S4): the two cases where the standard says std::terminate
; for an exception that has to leave a cleanup or a handler head.
;   (1) catch (Tc t) with a copy constructor that throws: terminate.
;   (2) a noexcept(false) destructor that throws while an exception unwinds
;       through its scope: terminate (the cleanup of the EH path).
;   (3) the same destructor on the normal path: the exception propagates.
; The host's terminate handler prints the log and exits with 66.
; RUN: cat %S/Inputs/header12_cxx_eh.txt %s > %t.txt
; RUN: %ecil-asm %t.txt -o %t.ecil
; RUN: %edg-cir --emit=cir %t.ecil | %FileCheck %s
; RUN: %edg-cir --emit=obj -o %t.o %t.ecil
; RUN: g++ %S/Inputs/s4_eh_terminate_host.cpp %t.o -o %t.exe
; RUN: sh -c '%t.exe 1 > %t.out; test $? -eq 66'
; RUN: %FileCheck %s --check-prefix=T1 < %t.out
; RUN: sh -c '%t.exe 2 > %t.out; test $? -eq 66'
; RUN: %FileCheck %s --check-prefix=T2 < %t.out
; RUN: %t.exe 3 | %FileCheck %s --check-prefix=T3
; RUN: %t.exe 4 | %FileCheck %s --check-prefix=T4
; RUN: %edg-cir --emit=obj -O2 --verify-each -o %t2.o %t.ecil
; RUN: g++ %S/Inputs/s4_eh_terminate_host.cpp %t2.o -o %t2.exe
; RUN: sh -c '%t2.exe 1 > %t.out; test $? -eq 66'
; RUN: sh -c '%t2.exe 2 > %t.out; test $? -eq 66'

; CHECK: cir.begin_catch
; T1: terminate: 2001 2101
; T2: terminate: 957 967
; T3: caught 77
; T4: caught 1

'''
    open(os.path.join(outdir, 'cxx-s4-eh-terminate.test'), 'w').write(
        hdr + ctx.gen.text())


def write_noexcept(outdir):
    # noexcept: the body is a try with a catch-all that terminates; the
    # cleanups of the body run first (clang's order, not g++'s).
    ctx = Ctx()
    spec = ('FUNC_EH_SPEC 1 []', 'noexcept')
    f = Func(ctx, 'nx', ('k',), spec=spec)
    f.stmts([('obj', 'a', 1), ('if', 'k', [('throw_int', 'k')]),
             ('ret', 0)])
    f.finish()
    f = Func(ctx, 'nx_inner', ('k',), spec=spec)
    f.stmts([('obj', 'a', 1),
             ('try', [('if', 'k', [('throw_int', 11)])],
              [('int', 'i', [('ret', 'i')])]),
             ('ret', 0)])
    f.finish()
    f = Func(ctx, 'nx_call', ('k',), spec=spec)
    f.stmts([('obj', 'a', 1), ('obj', 'b', 2),
             ('note', ('call', 'mk', ['k'])), ('ret', 0)])
    f.finish()
    inputs = os.path.join(outdir, 'Inputs')
    open(os.path.join(inputs, 's4_eh_noexcept_host.cpp'), 'w').write(
        NOEXCEPT_HOST)
    hdr = '''; C++ (ECIL 1.2, slice S4): noexcept functions.  The body of a function
; with FUNC_EH_SPEC noexcept is a cir.try whose catch-all handler terminates:
; an exception that leaves the body runs the cleanups first (clang's order),
; then calls std::terminate; one caught inside the body does not.  The host
; installs a terminate handler that prints the log and exits with 66.
; RUN: cat %S/Inputs/header12_cxx_eh.txt %s > %t.txt
; RUN: %ecil-asm %t.txt -o %t.ecil
; RUN: %edg-cir --emit=cir %t.ecil | %FileCheck %s
; RUN: %edg-cir --emit=obj -o %t.o %t.ecil
; RUN: g++ %S/Inputs/s4_eh_noexcept_host.cpp %t.o -o %t.exe
; RUN: %t.exe | %FileCheck %s --check-prefix=OK
; RUN: sh -c '%t.exe 1 > %t.out; test $? -eq 66'
; RUN: %FileCheck %s --check-prefix=TERM1 < %t.out
; RUN: sh -c '%t.exe 1 2 > %t.out; test $? -eq 66'
; RUN: %FileCheck %s --check-prefix=TERM2 < %t.out
; RUN: %edg-cir --emit=obj -O2 --verify-each -o %t2.o %t.ecil
; RUN: g++ %S/Inputs/s4_eh_noexcept_host.cpp %t2.o -o %t2.exe
; RUN: sh -c '%t2.exe 1 > %t.out; test $? -eq 66'
; RUN: %FileCheck %s --check-prefix=TERM1 < %t.out

; CHECK-LABEL: cir.func {{.*}}@c_nx(
; CHECK: cir.try {
; CHECK: } catch all (
; CHECK: cir.eh.terminate
; OK: returned
; TERM1: terminate: 1 101
; TERM2: terminate: 1 2 102 101

'''
    open(os.path.join(outdir, 'cxx-s4-eh-noexcept.test'), 'w').write(
        hdr + ctx.gen.text())


def write_cleanup(outdir):
    scs = scenarios_cleanup()
    only = os.environ.get('CLEANUP_ONLY')
    if only:
        scs = [x for x in scs if x[0] in only.split(',')]
    hdr, ctx = write_test(outdir, 'cxx-s4-eh-cleanup-throw',
                          scs, CLEANUP_COMMENT)
    check = '''; CHECK-DAG: cir.cleanup.scope
; CHECK-DAG: cir.try {

'''
    open(os.path.join(outdir, 'cxx-s4-eh-cleanup-throw.test'), 'w').write(
        hdr + check + ctx.gen.text())


ARRDEF_COMMENT = '''; C++ (ECIL 1.2, slice S4): an array of a class whose constructor has a default
; argument with a temporary: the element loop keeps the element pointer in a
; local, the call has its own FULLEXPR, and the temporary's destructor (a
; noexcept(false) one that throws for element k) runs at its end on the
; normal path.  The exception must destroy the elements before the current one
; and the enclosing locals.  This is clang's behaviour (classic, and with
; -fclangir): the element whose full expression threw is not destroyed, g++
; destroys it, so the log is checked here and not against g++.
'''


def write_arrdef(outdir):
    scs = [('arr_default', range(0, 4), ('k',), [
        ('try', [('callv', 'set_fail', ['k']), ('obj', 'a', 3, 'Td'),
                 ('arrua', 'z', 3), ('note', 5)],
         [('int', 'i', [('note', 'i'), ('ret', 'i')])]),
        ('ret', 0)])]
    hdr, ctx = write_test(outdir, 'cxx-s4-eh-array-ctor-arg-throw', scs,
                          ARRDEF_COMMENT)
    tag = 's4_eh_array_ctor_arg_throw'
    hp = os.path.join(outdir, 'Inputs', tag + '_host.cpp')
    text = open(hp).read()
    a = text.index('int main() {')
    text = text[:a] + '''static void show(int k, int (*c)(int)) {
  nlog_ = 0;
  int rc = run(c, k);
  std::printf("k=%d r=%d log:", k, rc);
  for (int i = 0; i < nlog_; ++i) std::printf(" %d", log_[i]);
  std::printf("\\n");
}
int main() {
  for (int k = 0; k < 4; ++k) show(k, c_arr_default);
  return 0;
}
'''
    open(hp, 'w').write(text)
    hdr = hdr.replace('; RUN: %t.exe\n', '; RUN: %t.exe | %FileCheck %s --check-prefix=LOG\n')
    hdr = hdr.replace('; RUN: %t2.exe\n', '; RUN: %t2.exe | %FileCheck %s --check-prefix=LOG\n')
    check = '''; CHECK-DAG: cir.array.ctor
; CHECK-DAG: partial_dtor
; LOG: k=0 r=77 log: 953 957 1600 967 963 77
; LOG: k=1 r=77 log: 953 953 1600 963 957 1601 967 1700 963 77
; LOG: k=2 r=77 log: 953 953 1600 963 953 1601 963 957 1602 967 1701 1700 963 77
; LOG: k=3 r=0 log: 953 953 1600 963 953 1601 963 953 1602 963 5 1702 1701 1700 963

'''
    open(os.path.join(outdir, 'cxx-s4-eh-array-ctor-arg-throw.test'),
         'w').write(hdr + check + ctx.gen.text())


if __name__ == '__main__':
    write_all(sys.argv[1])
    write_ctor(sys.argv[1])
    write_dyn(sys.argv[1])
    write_noexcept(sys.argv[1])
    write_terminate(sys.argv[1])
    write_cleanup(sys.argv[1])
    write_arrdef(sys.argv[1])
