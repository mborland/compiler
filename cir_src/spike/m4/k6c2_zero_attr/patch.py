#!/usr/bin/env python3
# Stands for what the library does today with C_ZERO / zero-fill of a record or
# array that contains pointers to data members: the zero attribute of the whole
# type (cir::ZeroAttr, builder.getZeroInitAttr) instead of CIRGen's expanded
# constant with #cir.data_member<null>.  Replaces every outermost
# const_record / const_array that consists only of zeros and nulls.
import re, sys
path = sys.argv[1]
text = open(path).read()

def match_end(s, i, open_ch, close_ch):
  depth = 0
  while i < len(s):
    if s[i] == open_ch:
      depth += 1
    elif s[i] == close_ch:
      depth -= 1
      if depth == 0:
        return i
    i += 1
  return -1

def type_end(s, i):
  # a type: !rec_X, !cir.array<... x N>, ...; angle brackets balanced
  depth = 0
  while i < len(s):
    c = s[i]
    if c == '<':
      depth += 1
    elif c == '>':
      depth -= 1
    elif c in ' \n,)' and depth == 0:
      return i
    i += 1
  return i

out, pos, n = [], 0, 0
pat = re.compile(r'#cir\.const_(record|array)<([{\[])')
while True:
  m = pat.search(text, pos)
  if not m:
    out.append(text[pos:])
    break
  open_ch = m.group(2)
  close_ch = '}' if open_ch == '{' else ']'
  start = m.start()
  end = match_end(text, m.end() - 1, open_ch, close_ch)       # the closing brace
  assert text[end + 1] == '>', text[end:end + 5]
  tstart = end + 2
  assert text.startswith(' : ', tstart), text[tstart:tstart + 10]
  tend = type_end(text, tstart + 3)
  body = text[start:end + 1]
  ty = text[tstart + 3:tend]
  nonzero = re.search(r'#cir\.int<(?!0>)', body) or '#cir.global_view' in body \
      or '#cir.fp' in body or '"' in body
  if 'data_member<null>' in body and not nonzero:
    out.append(text[pos:start])
    out.append(f'#cir.zero : {ty}')
    pos = tend
    n += 1
  else:
    out.append(text[pos:m.end()])
    pos = m.end()
open(path, 'w').write(''.join(out))
print(f'patch: {n} constants replaced by #cir.zero', file=sys.stderr)
