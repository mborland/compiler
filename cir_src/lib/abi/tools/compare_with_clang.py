#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Compare lib/abi's vtable layouts with clang's.

For each C++ file: the front end's IL display (cpfe-cir --il_display) gives
the class facts (the same ones the ECIL 1.2 records will carry); they are
written in abi_text.h's form and fed to abi_dump, whose output has the format of
clang -cc1 -fdump-vtable-layouts.  Both texts go through edg-cir-abi-diff's
canonical form and then through a function-name normalization (the result type
of a function is not a fact), and the blocks present on both sides are
compared: vtable, construction-vtable, vtable-indices, vbase-offset-offsets
and thunks.

Run it inside the cir-env image from the repository root, after building
build/gcc-cir (cpfe-cir) and lib/abi (cmake -S cir_src/lib/abi -B
build/cir-abi && ninja -C build/cir-abi):

  python3 cir_src/lib/abi/tools/compare_with_clang.py [-v] F.cpp ...
  python3 cir_src/lib/abi/tools/compare_with_clang.py --list FILES.txt
  python3 cir_src/lib/abi/tools/compare_with_clang.py --facts F.cpp  # print
                                                    the facts text and stop

Exit status 0 if every compared block is equal, 1 if not, 2 for a setup
problem.
"""
import argparse, importlib.machinery, importlib.util, os, re, shutil, \
    subprocess, sys, tempfile
from pathlib import Path


def load(root, name):
  loader = importlib.machinery.SourceFileLoader(
    name.replace('-', '_'), str(root / 'dev_tools/bin' / name))
  spec = importlib.util.spec_from_loader(name.replace('-', '_'), loader)
  module = importlib.util.module_from_spec(spec)
  loader.exec_module(module)
  return module


def fe_il(root, fe_build, std, fe_opts, source, work):
  """cpfe-cir --il_display of one file (without the system headers)."""
  fm = load(root, 'edg-cir-fe-facts')
  exports = fm.load_exports(root / fe_build / 'environment.sh')
  env = dict(os.environ)
  env.update(exports)
  env['EDG_BASE'] = str(root / 'bases/docker/cir-env/gcc')
  env['ECCP_LIBDIR'] = exports.get('EDG_CIR_RT_LIBDIR', '')
  env['CPFE'] = exports['CPFE_CIR']
  env['EDG_USE_SYSTEM_HEADERS'] = '0'
  local = work / source.name
  shutil.copy2(source, local)
  for extra in source.parent.glob('*.h'):
    shutil.copy2(extra, work / extra.name)
  proc = subprocess.run(
    [str(root / 'util/eccp.sh'), '-S', '-tused', f"--{std}", '--il_display',
     *fe_opts, local.name], cwd = work, env = env, stdout = subprocess.PIPE,
    stderr = subprocess.STDOUT, text = True, timeout = 600)
  return proc.stdout


class FactsBuilder:
  """The class facts of lib/abi from the front end's IL entries."""

  def __init__(self, fm, facts):
    self.fm, self.facts = fm, facts
    self.class_ids = {}        # entry address -> id
    self.names = {}            # id -> unique name
    self.fn_ids = {}           # routine address -> id
    self.fns = []              # (FeFunction, owner id)
    self.sigs = {}

  def class_name(self, cls):
    return cls.name

  def build(self):
    fm, facts = self.fm, self.facts
    classes = [c for c in facts.classes.values()]
    used = set()
    # Template patterns and other non-real classes ("I<T#(1,1)>") have no
    # layout.
    classes = [c for c in classes if not c.entry.flag('is_nonreal_class')]
    for c in classes:
      name = c.name
      n = 2
      while name in used:
        name = f"{c.name}#{n}"
        n += 1
      used.add(name)
      self.class_ids[c.addr] = len(self.class_ids)
      self.names[c.addr] = name
    for c in classes:
      for f in c.functions:
        if f.number < 0 or f.entry.addr in self.fn_ids:
          continue
        self.fn_ids[f.entry.addr] = len(self.fns)
        self.fns.append((f, self.class_ids[c.addr]))
    lines = ['pointer_size 8']
    overridden = self.overridden_sets(classes)
    for c in classes:
      lines += self.class_lines(c)
    for index, (f, owner) in enumerate(self.fns):
      lines += self.fn_lines(index, f, owner, overridden)
    return '\n'.join(lines) + '\n'

  def q(self, text):
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'

  def base_id(self, base):
    return self.class_ids.get(base.type_addr)

  def is_empty(self, c, memo = None):
    """clang's CXXRecordDecl::isEmpty: no data fields, no vptr, no virtual
    bases and only empty bases."""
    memo = {} if memo is None else memo
    if c.addr in memo:
      return memo[c.addr]
    memo[c.addr] = False
    result = not (c.dynamic or c.has_vbases or c.own_virtual or c.fields)
    if result:
      for b in c.bases:
        if b.direct:
          base = self.facts.classes.get(b.type_addr)
          if base is None or not self.is_empty(base, memo):
            result = False
            break
    memo[c.addr] = result
    return result

  def class_lines(self, c):
    cid = self.class_ids[c.addr]
    dynamic = c.dynamic or c.has_vbases or c.own_virtual
    primary = -1
    pvirtual = 0
    if c.primary_entry is not None:
      primary = self.class_ids.get(c.primary_entry.type_addr, -1)
      pvirtual = 1 if c.primary_virtual else 0
    nslots = c.highest + 1 if c.highest >= 0 else 0
    out = [f"class {cid} {self.q(self.names[c.addr])} size {c.size} nvsize "
           f"{c.nvsize} dynamic {1 if dynamic else 0} primary {primary} "
           f"pvirtual {pvirtual} nslots {nslots}"
           f"{' empty 1' if self.is_empty(c) else ''}"]
    seen_v = set()
    direct = sorted((b for b in c.bases if b.direct),
                    key = lambda b: b.entry.number('direct_base_number'))
    for b in direct:
      bid = self.base_id(b)
      if bid is not None:
        out.append(f"  base {bid} {1 if b.virtual else 0} {b.offset}")
    for b in c.bases:
      bid = self.base_id(b)
      if bid is None:
        continue
      if b.virtual and bid not in seen_v:
        seen_v.add(bid)
        out.append(f"  vbase {bid} {b.offset}")
    fids = [str(self.fn_ids[f.entry.addr]) for f in c.functions
            if f.entry.addr in self.fn_ids]
    if fids:
      out.append('  fns ' + ' '.join(fids))
    out += self.override_lines(c)
    return out

  def item_walk(self, base):
    fm = self.fm
    head = fm.pointer(base.entry.get('variant.overriding_virtual_functions'))
    seen = set()
    while head is not None and head[1] not in seen and \
        head[1] in self.facts.entries:
      seen.add(head[1])
      item = self.facts.entries[head[1]]
      over = fm.pointer(item.get('overriding_function'))
      prim = fm.pointer(item.get('primary_function'))
      holder = fm.pointer(item.get('base_class'))
      yield (over[1] if over else None, prim[1] if prim else None,
             holder[1] if holder else None)
      head = fm.pointer(item.get('next'))

  def derived_from(self, derived_cls, base_cls):
    """Whether class `derived_cls` (id) has `base_cls` (id) among its bases."""
    for c in self.facts.classes.values():
      if self.class_ids[c.addr] == derived_cls:
        return any(self.base_id(b) == base_cls for b in c.bases)
    return False

  def override_lines(self, c):
    out = []
    for b in c.bases:
      per_fn = {}
      for over, prim, holder in self.item_walk(b):
        if over not in self.fn_ids or prim not in self.fn_ids:
          continue
        per_fn.setdefault(self.fn_ids[prim], []).append((over, holder))
      for pid, items in per_fn.items():
        best = items[0]
        for item in items[1:]:
          if self.derived_from(self.fns[self.fn_ids[item[0]]][1],
                               self.fns[self.fn_ids[best[0]]][1]):
            best = item
        over, holder = best
        if holder is None or holder not in self.facts.entries:
          ovr_off = 0
        else:
          ovr_off = self.facts.entries[holder].number('offset')
        out.append(f"  ovr {pid} {b.offset} {self.fn_ids[over]} {ovr_off}")
    return out

  def overridden_sets(self, classes):
    result = {}
    for c in classes:
      for b in c.bases:
        for over, prim, holder in self.item_walk(b):
          if over in self.fn_ids and prim in self.fn_ids:
            result.setdefault(self.fn_ids[over], set()).add(
              self.fn_ids[prim])
    # The facts state the whole set (what a function overrides directly or
    # not): close the lists the front end gives.
    closed = {}
    for fn in result:
      seen, work = set(), list(result[fn])
      while work:
        g = work.pop()
        if g not in seen:
          seen.add(g)
          work.extend(result.get(g, ()))
      closed[fn] = seen
    return closed

  def ret_class(self, f):
    fm, facts = self.fm, self.facts
    ftype = fm.pointer(f.entry.get('type'))
    if ftype is None or ftype[1] not in facts.entries:
      return -1
    ret = fm.pointer(facts.entries[ftype[1]].get('return_type'))
    if ret is None or ret[1] not in facts.entries:
      return -1
    pointee = fm.pointer(facts.entries[ret[1]].get('type_pointed_to'))
    if pointee is None:
      return -1
    label = re.sub(r'\b(?:struct|class|union|const|volatile) ', '',
                   pointee[2]).strip()
    for c in facts.classes.values():
      if c.name == label:
        return self.class_ids[c.addr]
    return -1

  def fn_lines(self, index, f, owner, overridden):
    special = 0
    if f.dtor:
      special = 1
    elif f.name == 'operator=':
      special = 3 if '&&' in f.params else 2
    key = f.key()
    if f.dtor:
      sig = 0
    else:
      sig = self.sigs.setdefault((f.name, f.params), len(self.sigs) + 1)
    out = [f"fn {index} {self.q(key)} owner {owner} slot {f.number} pure "
           f"{1 if f.pure else 0} deleted {1 if f.deleted else 0} special "
           f"{special} implicit {1 if f.implicit else 0} sig {sig} ret "
           f"{self.ret_class(f)}"]
    over = sorted(overridden.get(index, ()))
    if over:
      out.append('  overridden ' + ' '.join(map(str, over)))
    return out


def normalize_blocks(abi, fm, text):
  """{header: [lines]} of a clang-format dump, function names reduced to
  "Owner::name(params)" keys."""
  canon = simple_names(abi.canonical(text))
  blocks = {}
  order = []
  header = None
  for line in canon.splitlines():
    if line.startswith('## '):
      header = norm_header(fm, line[3:])
      blocks[header] = []
      order.append(header)
    elif header is not None and not header.startswith('record ') and \
        _KEEP.match(line):
      blocks[header].append(norm_line(fm, line))
  return blocks


def simple_names(text):
  """Class names without template arguments and anonymous namespace
  qualifiers (both sides print them differently); a line that names an
  operator is left alone."""
  def one(line):
    if 'operator' in line:
      return line
    line = line.replace('(anonymous namespace)::', '')
    while True:
      reduced = re.sub(r'<[^<>]*>', '', line)
      if reduced == line:
        return line
      line = reduced
  return '\n'.join(one(l) for l in text.split('\n'))


def func_text(fm, text):
  return fm.clang_function_key(text)


def norm_header(fm, header):
  m = re.match(r'^thunks (.*) \((\d+ entr(?:y|ies))\)$', header)
  if m:
    return f"thunks {func_text(fm, m.group(1))} ({m.group(2)})"
  return header


# The lines a layout block can have; others are clang diagnostics that end up
# in the dump (inaccessible base warnings and the like).
_KEEP = re.compile(r'^(\d+ \| |\s+-- \(.*\) vtable address --$|\s+\[(this|'
                   r'return) adjustment|\s+(this|return) adjustment|'
                   r'\s+\S+ \| -?\d+$)')

_NON_FN = re.compile(r'^(vcall_offset|vbase_offset|offset_to_top|\[unused\]$)')


def norm_line(fm, line):
  m = re.match(r'^(\d+ \| )(.*)$', line)
  if not m:
    return line
  prefix, rest = m.group(1), m.group(2)
  if _NON_FN.match(rest) or rest.endswith(' RTTI') or \
      rest.startswith(('return adjustment', 'this adjustment')):
    return line
  unused = ''
  if rest.startswith('[unused] '):
    unused, rest = '[unused] ', rest[len('[unused] '):]
  head, sep, method = rest.partition(' method: ')
  out = unused + func_text(fm, head)
  if sep:
    out += ' method: ' + func_text(fm, method)
  return prefix + out


def norm_continuation(fm, line):
  if ' method: ' in line:
    head, _, method = line.partition(' method: ')
    return head + ' method: ' + func_text(fm, method)
  return line


def compare_one(args, root, fm, abi, source):
  with tempfile.TemporaryDirectory(prefix = 'abi-compare.') as tmp:
    work = Path(tmp)
    il = fe_il(root, args.fe_build, args.std, args.fe_opts.split(), source,
               work)
    entries = fm.parse_il(il)
    if not entries:
      return None, 'the front end printed no IL'
    facts = fm.FeFacts(entries)
    builder = FactsBuilder(fm, facts)
    text = builder.build()
    facts_path = work / 'facts.txt'
    facts_path.write_text(text)
    if args.facts:
      print(text)
      return {}, ''
    proc = subprocess.run([str(root / args.abi_dump), '--check',
                           str(facts_path)], stdout = subprocess.PIPE,
                          stderr = subprocess.PIPE, text = True)
    if proc.returncode == 2:
      return None, 'abi_dump: ' + proc.stderr.strip()
    runner = fm.Runner(root, args.std, args.fe_build, [], '')
    code, dump = abi.run_clang_dump(source, runner.clang, runner.triple,
                                    args.std, ['-w'])
    if code != 0:
      return None, f"clang failed ({code})"
  clang = normalize_blocks(abi, fm, dump)
  mine = normalize_blocks(abi, fm, proc.stdout)
  diffs = []
  compared = 0
  for header, lines in clang.items():
    kind = header.split(' ')[0]
    if kind == 'record':
      continue
    if header not in mine:
      diffs.append((header, 'missing in lib/abi', []))
      continue
    compared += 1
    a = [norm_continuation(fm, l) for l in lines
         if not re.match(r'^(\S+:\d+:\d+: |\d+ \| [|^]|\d+ warnings? )', l)]
    b = [norm_continuation(fm, l) for l in mine[header]]
    if a != b:
      import difflib
      diffs.append((header, 'differs',
                    list(difflib.unified_diff(a, b, 'clang', 'lib/abi',
                                              lineterm = '', n = 1))))
  slot_problems = [l for l in proc.stderr.splitlines() if l.strip()]
  return {'compared': compared, 'diffs': diffs, 'slots': slot_problems,
          'extra': [h for h in mine if h not in clang and
                    not h.startswith('record ')]}, ''


def main():
  ap = argparse.ArgumentParser(description = __doc__.split('\n')[0])
  ap.add_argument('files', nargs = '*')
  ap.add_argument('--list', default = '')
  ap.add_argument('--std', default = 'c++17')
  ap.add_argument('--fe-opts', default = '')
  ap.add_argument('--fe-build', default = 'build/gcc-cir')
  ap.add_argument('--abi-dump', default = 'build/cir-abi/abi_dump')
  ap.add_argument('--root', default = '.')
  ap.add_argument('--facts', action = 'store_true',
                  help = 'print the facts text of each file and stop')
  ap.add_argument('-v', dest = 'verbose', action = 'store_true')
  args = ap.parse_args()
  root = Path(args.root).resolve()
  fm = load(root, 'edg-cir-fe-facts')
  abi = load(root, 'edg-cir-abi-diff')
  files = list(args.files)
  if args.list:
    files += [l.strip() for l in Path(args.list).read_text().splitlines()
              if l.strip() and not l.startswith('#')]
  bad = setup = compared = 0
  for name in files:
    try:
      result, message = compare_one(args, root, fm, abi,
                                    Path(name).resolve())
    except (subprocess.TimeoutExpired, KeyError, OSError) as error:
      result, message = None, f"error: {error!r}"
    if message:
      setup += 1
      print(f"SETUP {name}: {message}")
      continue
    if args.facts:
      continue
    compared += result['compared']
    for header, why, lines in result['diffs']:
      bad += 1
      print(f"DIFF {name}: {header}: {why}")
      for l in lines[:40]:
        print('    ' + l)
    for p in result['slots']:
      bad += 1
      print(f"SLOTS {name}: {p}")
    if args.verbose:
      print(f"ok   {name}: {result['compared']} blocks compared; "
            f"extra in lib/abi: {len(result['extra'])}")
  print(f"{len(files)} files ({setup} not run), {compared} blocks compared, "
        f"{bad} problems")
  sys.exit(1 if bad else (2 if setup == len(files) else 0))


if __name__ == '__main__':
  main()
