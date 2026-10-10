#!/usr/bin/env python3
"""Minimal ECIL decoder for writer development (the real tool is
cir_src/tools/ecil-dump).  Usage: ecil_dump.py [-q] file.ecil
The opcode table is parsed from cir_src/include/edg_cir/ecil_format.h.
Prints one record per line, bracketing frames.  Exit status 1 on a decode
error (desync, reserved tag, stack imbalance is not checked)."""
import re, struct, sys, os

HDR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   '../../../include/edg_cir/ecil_format.h')
OPS = {}
for m in re.finditer(r'X\((\w+),\s*(0x[0-9A-Fa-f]+),\s*(\d+)\)', open(HDR).read()):
    OPS[int(m.group(2), 16)] = (m.group(1), int(m.group(3)))

FRAMES = {1: 'F_HEADER', 2: 'F_DECLS', 3: 'F_REGION', 4: 'F_GLOBALS',
          5: 'F_NAMES', 0x0E: 'F_ABORT', 0x0F: 'F_END'}

class Err(Exception):
    pass

def operand(b, i):
    t = b[i]; i += 1
    if t <= 0x7F: return t, i
    if 0x80 <= t <= 0x9F:
        n = t - 0x80; return repr(bytes(b[i:i+n]))[1:], i + n
    if 0xA0 <= t <= 0xBF or t in (0xDC, 0xDD):
        if t <= 0xBF: n = t - 0xA0
        elif t == 0xDC: n = struct.unpack_from('<H', b, i)[0]; i += 2
        else: n = struct.unpack_from('<I', b, i)[0]; i += 4
        out = []
        for _ in range(n):
            v, i = operand(b, i); out.append(v)
        return out, i
    if t == 0xC0: return 'nil', i
    if t == 0xC1: raise Err('poison tag')
    if t == 0xC2: return 'false', i
    if t == 0xC3: return 'true', i
    if t in (0xC4, 0xC5, 0xC6):
        w = {0xC4: 1, 0xC5: 2, 0xC6: 4}[t]
        n = int.from_bytes(b[i:i+w], 'little'); i += w
        return repr(bytes(b[i:i+n]))[1:], i + n
    if t in (0xC7, 0xC8, 0xC9): raise Err('supp tag as operand')
    if t == 0xCA: return 'f32:%08x' % struct.unpack_from('<I', b, i)[0], i + 4
    if t == 0xCB: return 'f64:%016x' % struct.unpack_from('<Q', b, i)[0], i + 8
    fm = {0xCC: ('<B', 1), 0xCD: ('<H', 2), 0xCE: ('<I', 4), 0xCF: ('<Q', 8),
          0xD0: ('<b', 1), 0xD1: ('<h', 2), 0xD2: ('<i', 4), 0xD3: ('<q', 8)}
    if t in fm:
        f, w = fm[t]; return struct.unpack_from(f, b, i)[0], i + w
    if t in (0xD4, 0xD5):
        v = int.from_bytes(b[i:i+16], 'little', signed=(t == 0xD5))
        return v, i + 16
    if t == 0xD6:
        n = b[i]; i += 1
        return 'fbits:' + bytes(b[i:i+n])[::-1].hex(), i + n
    if t >= 0xE0: return t - 256, i
    raise Err('reserved tag 0x%02x' % t)

def main():
    args = [a for a in sys.argv[1:] if a != '-q']
    data = open(args[0], 'rb').read()
    if data[:8] != b'ECIL\r\n\x1a\n': raise Err('bad magic')
    major, minor, feat = struct.unpack_from('<HHI', data, 8)
    print('ECIL %d.%d features=0x%x' % (major, minor, feat))
    pos = 16
    while pos < len(data):
        ln, kind, flags, rsv = struct.unpack_from('<IBBH', data, pos)
        pos += 8
        if pos + ln > len(data): raise Err('truncated frame')
        p = data[pos:pos+ln]
        print('%s flags=%d len=%d {' % (FRAMES.get(kind, 'F_0x%02x' % kind), flags, ln))
        i = 0
        while i < len(p):
            op = p[i]; i += 1
            if op not in OPS: raise Err('unknown opcode 0x%02x at %d' % (op, i - 1))
            name, arity = OPS[op]
            vals = []
            for _ in range(arity):
                v, i = operand(p, i); vals.append(v)
            print('  %s(%s)' % (name, ', '.join(str(v) for v in vals)))
        print('}')
        pos += ln
    return 0

try:
    sys.exit(main())
except Err as e:
    print('DECODE ERROR: %s' % e); sys.exit(1)
