#!/usr/bin/env python3
# Wrap the construction of every thrown object in an eh-only cleanup scope that
# frees the exception storage, as clang's classic CodeGen does
# (CGException.cpp, FreeException): the cleanup runs only if initialization
# throws; once the cir.throw is reached the runtime owns the object.
import re, sys
p = sys.argv[1]
s = open(p).read()
# %N = cir.alloc.exception SIZE -> !cir.ptr<T> ... up to the "cir.br ^bbK" that ends the try body block
pat = re.compile(r'(?P<ind>[ ]*)(?P<v>%\w+) = cir\.alloc\.exception (?P<sz>\d+) -> (?P<ty>!cir\.ptr<[^\n]*?>)(?=\n)\n(?P<body>(?:(?!\s*cir\.br ).*\n)+?)(?P<br>[ ]*cir\.br \^bb\d+\n)')
def repl(m):
    ind = m.group('ind'); v = m.group('v')
    return (f"{ind}{v} = cir.alloc.exception {m.group('sz')} -> {m.group('ty')}\n"
            f"{ind}cir.cleanup.scope {{\n{m.group('body')}{ind}  cir.yield\n{ind}}} cleanup eh {{\n"
            f"{ind}  %fe = cir.cast bitcast {v} : {m.group('ty')} -> !cir.ptr<!void>\n"
            f"{ind}  cir.call @__cxa_free_exception(%fe) : (!cir.ptr<!void>) -> ()\n"
            f"{ind}  cir.yield\n{ind}}}\n{m.group('br')}")
s, n = pat.subn(repl, s)
if n == 0:
    sys.exit("no cir.alloc.exception found")
# declare the runtime function
s = s.replace('\n  cir.func', '\n  cir.func private @__cxa_free_exception(!cir.ptr<!void>)\n  cir.func', 1) if '@__cxa_free_exception' not in s.split('cir.func', 1)[0] else s
open(p, 'w').write(s)
print("patched", n, "throw sites")
