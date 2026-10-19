#!/usr/bin/env python3
"""cg_writer.py [--top N] [--delete] FILE...  callgrind outputs of the CIR FE.
Prints: total Ir; Ir on lines of the writer's sources (self); writer inclusive
(sum of the calls from non-writer lines into writer functions: includes the
callees of the writer, libc and FE helpers); the writer's calls out of the
writer by callee (inclusive); the writer functions by self Ir."""
import os, re, sys

args = sys.argv[1:]
top = 25
delete = False
while args and args[0] in ('--top', '--delete'):
    if args[0] == '--top':
        top = int(args[1]); args = args[2:]
    else:
        delete = True; args = args[1:]

def is_writer(f):
    return f.startswith('ecil_') or f == 'cir_be.c'

total = 0
writer_self = 0
entry_incl = 0
entry_by_fn = {}
out_by_callee = {}
all_by_callee = {}
calls_by_callee = {}
out_pair = {}
wself_by_fn = {}
wself_by_file = {}
tot_self_by_file = {}

def resolve(table, spec):
    m = re.match(r'\((\d+)\)\s*(.*)', spec)
    if not m:
        return spec
    if m.group(2):
        table[m.group(1)] = m.group(2)
    return table.get(m.group(1), '')

def short(n):
    return n.split('(')[0]

for path in args:
    files, fns = {}, {}
    cur_file = cur_fn = callee = callee_file = ''
    skip = False
    with open(path, errors='replace') as f:
        for line in f:
            if line.startswith(('fl=', 'fi=', 'fe=')):
                cur_file = os.path.basename(resolve(files, line[3:].strip()))
            elif line.startswith(('cfi=', 'cfl=')):
                callee_file = os.path.basename(resolve(files, line[4:].strip()))
            elif line.startswith('fn='):
                cur_fn = resolve(fns, line[3:].strip())
            elif line.startswith('cfn='):
                callee = short(resolve(fns, line[4:].strip()))
            elif line.startswith('calls='):
                skip = True
                ncalls = int(line[6:].split()[0])
                calls_by_callee[callee] = calls_by_callee.get(callee, 0) + ncalls
            elif line.startswith('totals:'):
                total += int(line.split()[1])
            elif line[:1] in '0123456789+-*':
                p = line.split()
                if len(p) >= 2 and p[1].isdigit():
                    n = int(p[1])
                    if skip:
                        skip = False
                        all_by_callee[callee] = all_by_callee.get(callee, 0) + n
                        if is_writer(cur_file):
                            kp = (short(cur_fn), callee)
                            out_pair[kp] = out_pair.get(kp, 0) + n
                        if is_writer(callee_file) and not is_writer(cur_file):
                            entry_incl += n
                            entry_by_fn[callee] = entry_by_fn.get(callee, 0) + n
                        elif is_writer(cur_file) and not is_writer(callee_file):
                            out_by_callee[callee] = out_by_callee.get(callee, 0) + n
                    else:
                        tot_self_by_file[cur_file] = tot_self_by_file.get(cur_file, 0) + n
                        if is_writer(cur_file):
                            writer_self += n
                            k = short(cur_fn)
                            wself_by_fn[k] = wself_by_fn.get(k, 0) + n
                            wself_by_file[cur_file] = wself_by_file.get(cur_file, 0) + n
    if delete:
        os.remove(path)

pct = lambda n: 100.0 * n / total if total else 0.0
print('files %d total_ir %d' % (len(args), total))
print('writer self (writer source lines): %d  %.2f%%' % (writer_self, pct(writer_self)))
print('writer inclusive (entered from outside): %d  %.2f%%' % (entry_incl, pct(entry_incl)))
print('-- writer self by file')
for k, v in sorted(wself_by_file.items(), key=lambda x: -x[1]):
    print('  %-20s %12d %6.2f%%' % (k, v, pct(v)))
print('-- writer entry points (inclusive)')
for k, v in sorted(entry_by_fn.items(), key=lambda x: -x[1])[:top]:
    print('  %-44s %12d %6.2f%%' % (k, v, pct(v)))
print('-- writer functions by self Ir')
for k, v in sorted(wself_by_fn.items(), key=lambda x: -x[1])[:top]:
    print('  %-44s %12d %6.2f%%' % (k, v, pct(v)))
print('-- writer functions by inclusive Ir (all callers; nested, do not add)')
for k, v in sorted(((k, all_by_callee.get(k, 0)) for k in wself_by_fn), key=lambda x: -x[1])[:top]:
    print('  %-44s %12d %6.2f%%' % (k, v, pct(v)))
print('-- calls out of the writer to non-writer functions, by callee (inclusive)')
for k, v in sorted(((k, v) for k, v in all_by_callee.items() if k not in wself_by_fn and any(c == k for (_, c) in out_pair)),
                   key=lambda x: -x[1])[:top]:
    wc = {}
    for (a, b), n in out_pair.items():
        if b == k: wc[a] = wc.get(a, 0) + n
    best = sorted(wc.items(), key=lambda x: -x[1])[:2]
    print('  %-40s %12d %6.2f%%  from writer: %s' % (k, sum(wc.values()), pct(sum(wc.values())), ', '.join('%s %d' % b for b in best)))
if False:
  for k, v in sorted(out_by_callee.items(), key=lambda x: -x[1])[:top]:
    print('  %-44s %12d %6.2f%%' % (k, v, pct(v)))
print('-- call counts of selected functions')
for k in ('edg::ecil_function_id','edg::compute_func_state','edg::routine_symbol_text','edg::get_mangled_function_name_full','edg::cir_entity_is_tu_local','edg::tu_walk','edg::cir_structor_name','edg::ecil_type_id','edg::emit_function','edg::ecil_reissue_function','edg::ecil_submit_frame','edg::ecil_new_gid','edg::ecil_intern_string','edg::ecil_rec_begin','edg::ecil_put_uint','edg::ecil_buf_reserve'):
    print('  %-44s %10d calls  %10d incl' % (k, calls_by_callee.get(k, 0), all_by_callee.get(k, 0)))
print('-- top files by self Ir (all)')
for k, v in sorted(tot_self_by_file.items(), key=lambda x: -x[1])[:12]:
    print('  %-24s %12d %6.2f%%' % (k, v, pct(v)))
