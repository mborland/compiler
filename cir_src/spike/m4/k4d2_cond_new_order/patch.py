#!/usr/bin/env python3
# What the library emits for a conditional new whose constructor arguments have
# temporaries with destructors: the storage-freeing eh cleanup is the OUTERMOST
# scope of the full-expression (so it runs after the temporaries' destructors,
# as in g++ and classic clang), the saved allocation lives in function-level
# slots, and a flag is cleared once the constructor has completed.  CIRGen
# instead closes the delete scope inside the conditional arm, which runs the
# operator delete before ~A (a CIRGen ordering bug in the conditional case).
import sys
p = sys.argv[1]
s = open(p).read()

def must(old, new, count=1):
    global s
    if s.count(old) < 1:
        sys.exit("pattern not found: " + old[:60])
    s = s.replace(old, new, count)

# 1. hoist the two save slots to the function level
sv1 = '              %24 = cir.alloca "cond-cleanup.save" align(8) : !cir.ptr<!cir.ptr<!void>>\n'
sv2 = '              %25 = cir.alloca "cond-cleanup.save" align(8) : !cir.ptr<!u64i>\n'
must(sv1, ''); must(sv2, '')
anchor = '    %5 = cir.alloca "cleanup.cond" align(1) : !cir.ptr<!cir.bool>\n'
must(anchor, anchor + sv1.strip().replace('%24', '%sv1', 1).join(['    ', '\n']) + sv2.strip().replace('%25', '%sv2', 1).join(['    ', '\n']))
must('cir.store align(8) %28, %24 :', 'cir.store align(8) %28, %sv1 :')
must('cir.store align(8) %27, %25 :', 'cir.store align(8) %27, %sv2 :')
# 2. dissolve the arm's delete scope; clear the flag after the constructor
must('''              cir.cleanup.scope {
                %30 = cir.const #true
                cir.store %30, %3 : !cir.bool, !cir.ptr<!cir.bool>
''', '''              {
                %30 = cir.const #true
                cir.store %30, %3 : !cir.bool, !cir.ptr<!cir.bool>
''')
s = s.replace('''              {
                %30 = cir.const #true''', '''              cir.scope {
                %30 = cir.const #true''')
import re
pat = re.compile(r"(                cir\.call @_ZN1TC1ERK1Ai\([^\n]*\n)                cir\.yield\n              \} cleanup eh \{\n"
                 r"                %30 = cir\.load align\(1\) %3[^\n]*\n                cir\.if %30 \{\n[^\n]*\n[^\n]*\n                \}\n"
                 r"                cir\.yield\n              \}\n")
m = pat.search(s)
if not m:
    sys.exit("delete scope end not found")
s = s[:m.start()] + m.group(1) + ("                %35 = cir.const #false\n"
     "                cir.store %35, %3 : !cir.bool, !cir.ptr<!cir.bool>\n"
     "                cir.yield\n              }\n") + s[m.end():]
# 3. the outer (empty) cleanup becomes the eh delete
must('''        } cleanup all {
          cir.yield
        }
        %10 = cir.load align(8) %7''', '''        } cleanup eh {
          %40 = cir.load align(1) %3 : !cir.ptr<!cir.bool>, !cir.bool
          cir.if %40 {
            %41 = cir.load align(8) %sv1 : !cir.ptr<!cir.ptr<!void>>, !cir.ptr<!void>
            cir.call @_ZN1TdlEPv(%41) nothrow : (!cir.ptr<!void> {llvm.noundef}) -> ()
          }
          cir.yield
        }
        %10 = cir.load align(8) %7''')
open(p, 'w').write(s)
print("patched cond_new")
