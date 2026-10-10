#!/usr/bin/env python3
"""Value-stack discipline checker for ECIL streams (writer development).

Usage: ecil_dump.py file.ecil | stackcheck.py [name]

Reads the text of ecil_dump.py and simulates the consumer's value stack for
every function body.  Reports (one line each, prefixed with the function):
  - a pop below the base of the enclosing TERN/LAND/LOR arm (a value
    consumed inside an arm that was produced outside it: it does not
    dominate the use);
  - depth at TERN_ELSE/TERN_END/LAND_END/LOR_END different from the depth at
    the matching BEGIN (+1 for a valued arm);
  - a statement boundary with values left on the stack;
  - an underflow, or a DUPed value consumed in an arm that did not DUP it.
Exit status 1 if any violation was found.  Stack effects follow
cir_src/include/edg_cir/ecil_format.h (stack columns of the opcode table)
and spec 8.x; CALL results are void only if the call type's return type is
the TY_VOID gid.
"""
import re
import sys

LINE = re.compile(r'^\s+(\w+)\((.*)\)\s*$')

POP1_PUSH1 = {'load', 'field', 'extract', 'deref', 'addr', 'addr_offset',
              'neg', 'bit_not', 'log_not', 'to_bool', 'inc_dec', 'cast',
              'va_arg', 'materialize_temp'}
POP2_PUSH1 = {'add', 'sub', 'mul', 'div', 'rem', 'shl', 'shr', 'bit_and',
              'bit_or', 'bit_xor', 'cmp', 'ptr_stride', 'ptr_diff'}
PUSH1 = {'lref', 'gref', 'const_int', 'const_float', 'const_null',
         'const_ptr'}
POP1 = {'va_start', 'va_end', 'drop', 'if_begin', 'while_body', 'do_end',
        'switch_begin', 'init_const', 'init_zero', 'agg_init_begin'}
BOUNDARY = {'block_begin', 'block_end', 'if_else', 'if_end', 'while_begin',
            'while_end', 'do_begin', 'do_cond', 'for_begin', 'for_body',
            'for_end', 'switch_end', 'break_stmt', 'continue_stmt',
            'goto_stmt', 'label', 'case_value', 'case_range', 'case_default',
            'unreachable', 'func_end'}


def args_of(text):
    out = []
    depth = 0
    cur = ''
    for ch in text:
        if ch in '[(':
            depth += 1
        elif ch in '])':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else '-'
    void_gids = set()
    func_ret = {}
    problems = []
    func = None
    stack = []            # entries: value id
    brackets = []         # (kind, base, valued, in_else)
    next_id = [0]
    dups = {}

    def report(msg):
        problems.append('%s: %s: %s' % (name, func, msg))

    def new():
        next_id[0] += 1
        return next_id[0]

    def pop(n, what):
        base = brackets[-1][1] if brackets else 0
        for _ in range(n):
            if len(stack) <= base:
                if len(stack) == 0:
                    report('%s: stack underflow' % what)
                else:
                    report('%s: consumes a value produced outside the '
                           'enclosing %s arm' % (what, brackets[-1][0]))
                return
            stack.pop()

    for raw in sys.stdin:
        m = LINE.match(raw)
        if not m:
            continue
        op, a = m.group(1), args_of(m.group(2))
        if op == 'ty_void':
            void_gids.add(a[0])
        elif op == 'ty_func':
            func_ret[a[0]] = a[1]
        elif op == 'func_begin':
            func = 'g' + a[0]
            stack.clear()
            brackets.clear()
        elif op == 'func_end':
            if stack:
                report('values left at FUNC_END (%d)' % len(stack))
            if brackets:
                report('open bracket at FUNC_END')
            func = None
        elif func is None:
            continue
        elif op in PUSH1:
            stack.append(new())
        elif op in POP1_PUSH1:
            pop(1, op)
            stack.append(new())
        elif op in POP2_PUSH1:
            pop(2, op)
            stack.append(new())
        elif op in POP1:
            pop(1, op)
        elif op == 'va_copy':
            pop(2, op)
        elif op == 'store':
            pop(2, op)
            if len(a) > 1 and a[1] not in ('0', 'false'):
                stack.append(new())
        elif op == 'dup':
            if not stack or (brackets and len(stack) <= brackets[-1][1]):
                report('dup: nothing to duplicate inside the arm')
            else:
                stack.append(stack[-1])
        elif op == 'agg_sel':
            stack.append(new())
        elif op == 'return_stmt':
            if a and a[0] in ('true', '1'):
                pop(1, op)
            if stack:
                report('values left after RETURN_STMT (%d)' % len(stack))
                stack.clear()
        elif op == 'for_step':
            if stack:
                pop(1, op)       # The condition's bool.
        elif op == 'builtin':
            pop(int(a[1]), op)
            if a[2] != 'nil':
                stack.append(new())
        elif op == 'call' or op == 'call_indirect':
            if op == 'call':
                t_call, nargs = a[1], int(a[2])
                pop(nargs, op)
            else:
                t_call, nargs = a[0], int(a[1])
                pop(nargs + 1, op)
            if func_ret.get(t_call) not in void_gids:
                stack.append(new())
        elif op == 'tern_begin':
            pop(1, op)
            brackets.append(('TERN', len(stack), a[0] != 'nil', False))
        elif op == 'tern_else':
            kind, base, valued, _e = brackets[-1]
            want = base + (1 if valued else 0)
            if len(stack) != want:
                report('TERN_ELSE depth %d, expected %d' % (len(stack), want))
            del stack[base:]
            brackets[-1] = (kind, base, valued, True)
        elif op == 'tern_end':
            kind, base, valued, _e = brackets.pop()
            want = base + (1 if valued else 0)
            if len(stack) != want:
                report('TERN_END depth %d, expected %d' % (len(stack), want))
            del stack[base:]
            if valued:
                stack.append(new())
        elif op in ('land_begin', 'lor_begin'):
            pop(1, op)
            brackets.append(('LAND' if op == 'land_begin' else 'LOR',
                             len(stack), True, False))
        elif op in ('land_end', 'lor_end'):
            kind, base, valued, _e = brackets.pop()
            if len(stack) != base + 1:
                report('%s depth %d, expected %d' %
                       (op.upper(), len(stack), base + 1))
            del stack[base:]
            stack.append(new())
        elif op in BOUNDARY:
            if stack:
                report('%d value(s) left on the stack at %s' %
                       (len(stack), op.upper()))
                del stack[:]
    for p in problems:
        print(p)
    return 1 if problems else 0


sys.exit(main())
