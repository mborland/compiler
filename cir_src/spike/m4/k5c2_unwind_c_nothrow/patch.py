#!/usr/bin/env python3
# Our M3 library marks every C function "nothrow" and (before decision 3) sets
# no uwtable: make CIRGen's output look like that.  With UWTABLE=1 in the
# environment the uwtable attribute is added too (decision 3 of session 3).
import os, re, sys
p = sys.argv[1]
s = open(p).read()
def fix(m):
    attrs = m.group(2)
    attrs = re.sub(r'uwtable = #cir\.uwtable<\w+>,? ?', '', attrs)
    extra = ['nothrow']
    if os.environ.get('UWTABLE') == '1':
        extra.append('uwtable = #cir.uwtable<async>')
    return m.group(1) + 'attributes {' + ', '.join(extra) + '}'
# one-line "cir.func ... attributes {...}" headers: replace the attribute dict
s = re.sub(r'(cir\.func [^\n]*?\) (?:-> [^\n]*? )?)attributes \{([^\n]*)\}(?= \{)', fix, s)
open(p, 'w').write(s)
