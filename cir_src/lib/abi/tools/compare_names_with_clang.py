#!/usr/bin/env python3

# Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
# Exceptions.
# See https://edgcpp.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Compare lib/abi's special symbols and RTTI layouts with clang's.

For each C++ file: the class facts come from the front end's IL (as in
compare_with_clang.py), the mangled names of the classes and functions from
clang's own LLVM IR (c++filt maps a symbol to the same "Owner::name(params)"
key the facts use; a key that two symbols share is left unnamed).  abi_dump
--symbols and --rtti then print what the library would emit, and the result is
checked against the IR clang produced:

  vtt       a VTT is defined for exactly the classes with a defined vtable that
            the library gives one
  ctor      the set of _ZTC symbols (construction vtables) equals clang's
  thunk     the set of _ZTh/_ZTv/_ZTc symbols the library names equals the set
            clang defines (the library's names that need a function symbol that
            could not be found are counted, not compared)
  rtti      for every class clang defines a _ZTI for: the same kind of
            type_info, the same field values, the same base type_infos

Run from the repository root, inside the cir-env image, with build/cir-abi
built:

  python3 cir_src/lib/abi/tools/compare_names_with_clang.py [-v] F.cpp ...
  python3 cir_src/lib/abi/tools/compare_names_with_clang.py --list FILES.txt

Exit status 0 if there is no difference, 1 if there is one, 2 for a setup
problem.
"""
import argparse, importlib.machinery, importlib.util, os, re, subprocess, \
    sys, tempfile
from pathlib import Path


def load_module(path, name):
  loader = importlib.machinery.SourceFileLoader(name, str(path))
  spec = importlib.util.spec_from_loader(name, loader)
  module = importlib.util.module_from_spec(spec)
  loader.exec_module(module)
  return module


HERE = Path(__file__).resolve().parent
cwc = load_module(HERE / 'compare_with_clang.py', 'compare_with_clang')


def demangle(symbols):
  """{symbol: demangled text} for a list of _Z symbols (c++filt)."""
  if not symbols:
    return {}
  proc = subprocess.run(['c++filt'], input = '\n'.join(symbols) + '\n',
                        stdout = subprocess.PIPE, stderr = subprocess.PIPE,
                        text = True)
  lines = proc.stdout.split('\n')
  return {s: lines[i] for i, s in enumerate(symbols) if i < len(lines)}


_DEFINE = re.compile(r'^(define|declare)\b[^@\n]*@("?[^\s(")]+"?)\(')
_GLOBAL = re.compile(r'^@("?[^\s"=]+"?) = (.*)$')


def parse_ir(text):
  """(functions {name: defined?}, globals {name: rest of line})."""
  funcs, globs = {}, {}
  for line in text.split('\n'):
    m = _DEFINE.match(line)
    if m:
      name = m.group(2).strip('"')
      funcs[name] = funcs.get(name, False) or m.group(1) == 'define'
      continue
    m = _GLOBAL.match(line)
    if m:
      globs[m.group(1).strip('"')] = m.group(2)
  return funcs, globs


def split_top(text):
  parts, depth, cur = [], 0, ''
  for ch in text:
    if ch in '({[':
      depth += 1
    elif ch in ')}]':
      depth -= 1
    if ch == ',' and depth == 0:
      parts.append(cur.strip())
      cur = ''
    else:
      cur += ch
  if cur.strip():
    parts.append(cur.strip())
  return parts


def typeinfo_fields(rest):
  """Normalized fields of a _ZTI initializer line (after "@name = "):
  ["vtable SYM+N", "name", "base M", "u32 N", "i64 N"]; None if the line is a
  declaration."""
  if 'external' in rest.split('{')[0]:
    return None
  start = rest.find(' } {')
  if start < 0:
    return None
  body = rest[start + 4:]
  end = body.find(' }')
  if end < 0:
    return None
  fields = []
  for part in split_top(body[:end]):
    m = re.search(r'@(_ZTVN10__cxxabiv\d+__\w+E).*i64 (\d+)\)?$', part)
    if part.startswith('ptr getelementptr') and m:
      offset = int(m.group(2))
      if '(i8,' in part:      # bytes; the library counts pointers
        offset //= 8
      fields.append(f"vtable {m.group(1)}+{offset}")
    elif re.match(r'^ptr @_ZTS', part):
      fields.append('name')
    elif '_ZTS' in part:
      fields.append('name nonunique')
    elif part.startswith('ptr @_ZTI'):
      fields.append('base ' + part[len('ptr @_ZTI'):])
    elif part.startswith('i32 ') or part.startswith('i64 '):
      fields.append(('u32 ' if part[1] == '3' else 'i64 ') + part[4:])
    else:
      fields.append('? ' + part)
  return fields


def class_key(text, simple = False):
  """The comparison key of a class name or a demangled class: the name with
  its template arguments and without anonymous-namespace qualifiers; with
  `simple` also without the template arguments."""
  text = re.sub(r'\b(?:struct|class|union) ', '', text)
  text = text.replace('(anonymous namespace)::', '').replace('{anonymous}::',
                                                              '')
  text = re.sub(r'\s+', ' ', text.replace(', ', ','))
  if simple:
    text = cwc.simple_names(text)
  return text.strip()


class Builder(cwc.FactsBuilder):
  """FactsBuilder plus mangled names, access and emptiness."""

  def __init__(self, fm, facts, class_m, fn_m):
    super().__init__(fm, facts)
    self.class_m, self.fn_m = class_m, fn_m
    self.empty_memo = {}

  def class_lines(self, c):
    out = cwc.FactsBuilder.class_lines(self, c)
    name = self.names[c.addr]
    m = self.class_m.get(name)
    direct = sorted((b for b in c.bases if b.direct),
                    key = lambda b: b.entry.number('direct_base_number'))
    direct = [b for b in direct if self.base_id(b) is not None]
    result, index = [], 0
    for line in out:
      if line.startswith('class '):
        if m:
          line += f' mangled "{m}"'
      elif line.startswith('  base '):
        if not direct[index].entry.flag('has_public_derivation'):
          line += ' public 0'
        index += 1
      result.append(line)
    return result

  def fn_lines(self, index, f, owner, overridden):
    out = cwc.FactsBuilder.fn_lines(self, index, f, owner, overridden)
    sym = self.fn_m.get(f.key())
    if sym:
      out[0] += f' mangled "{sym}"'
    return out


def compare_one(args, root, fm, abi, source):
  with tempfile.TemporaryDirectory(prefix = 'abi-names.') as tmp:
    work = Path(tmp)
    il = cwc.fe_il(root, args.fe_build, args.std, args.fe_opts.split(),
                   source, work)
    entries = fm.parse_il(il)
    if not entries:
      return None, 'the front end printed no IL'
    facts = fm.FeFacts(entries)
    runner = fm.Runner(root, args.std, args.fe_build, [], '')
    cmd = [runner.clang, '-cc1', '-triple', runner.triple,
           f"-std={args.std}", '-fcxx-exceptions', '-fexceptions',
           *abi.include_options(runner.clang), '-w', '-emit-llvm', '-o',
           '-', str(source)]
    proc = subprocess.run(cmd, stdout = subprocess.PIPE,
                          stderr = subprocess.PIPE, text = True)
    if proc.returncode != 0:
      return None, f"clang failed ({proc.returncode})"
    funcs, globs = parse_ir(proc.stdout)
    # Class mangled names: the _ZTS strings.
    zts = [g for g in globs if g.startswith(('_ZTS', '_ZTI'))
           and not g[4:].startswith('N10__cxxabiv')]
    dem = demangle(zts)
    seen, seen_simple = {}, {}
    for g in zts:
      d = dem.get(g, '')
      for prefix in ('typeinfo name for ', 'typeinfo for '):
        if d.startswith(prefix):
          for table, simple in ((seen, False), (seen_simple, True)):
            found = table.setdefault(class_key(d[len(prefix):], simple), [])
            if g[4:] not in found:
              found.append(g[4:])
    # Function mangled names.
    syms = [s for s in funcs if s.startswith('_Z') and
            not s.startswith(('_ZT', '_ZGV', '_ZZ'))]
    dem = demangle(syms)
    by_key = {}
    for s in syms:
      key = fm.clang_function_key(dem.get(s, ''))
      if re.search(r'D[012]E', s) and '~' in key:
        s = re.sub(r'D[012]E(?!.*D[012]E)', 'D1E', s)
      by_key.setdefault(key, set()).add(s)
    fn_m = {k: next(iter(v)) for k, v in by_key.items() if len(v) == 1}
    # Names of the classes the facts know, and which are unambiguous.
    probe = cwc.FactsBuilder(fm, facts)
    probe.build()
    names = [re.sub(r'#\d+$', '', n) for n in probe.names.values()]
    count, count_simple = {}, {}
    for name in names:
      count[class_key(name)] = count.get(class_key(name), 0) + 1
      count_simple[class_key(name, True)] = count_simple.get(
        class_key(name, True), 0) + 1
    class_m = {}
    for unique in probe.names.values():
      name = re.sub(r'#\d+$', '', unique)
      key, skey = class_key(name), class_key(name, True)
      if count[key] == 1 and len(seen.get(key, [])) == 1:
        class_m[unique] = seen[key][0]
      elif count_simple[skey] == 1 and len(seen_simple.get(skey, [])) == 1:
        class_m[unique] = seen_simple[skey][0]
    builder = Builder(fm, facts, class_m, fn_m)
    text = builder.build()
    facts_path = work / 'facts.txt'
    facts_path.write_text(text)
    run = lambda *opts: subprocess.run(
      [str(root / args.abi_dump), *opts, str(facts_path)],
      stdout = subprocess.PIPE, stderr = subprocess.PIPE, text = True)
    sym_out, rtti_out = run('--symbols'), run('--rtti')
    if args.dump:
      print(text)
      print(sym_out.stdout)
      print(sym_out.stderr)
  m_to_name = {m: n for n, m in class_m.items()}
  result = {'diffs': [], 'checked': 0, 'unnamed_thunks': 0, 'inexact': 0,
            'unmatched_vtables': 0}
  if sym_out.returncode == 2 or rtti_out.returncode == 2:
    return None, 'abi_dump: ' + (sym_out.stderr + rtti_out.stderr).strip()
  for line in (sym_out.stderr + rtti_out.stderr).splitlines():
    result['diffs'].append(('abi_dump', line))
  # The classes whose vtable clang defines.
  vt_defined = {g[4:] for g, rest in globs.items()
                if g.startswith('_ZTV') and
                not g.startswith('_ZTVN10__cxxabiv') and
                not rest.startswith('external')}
  matched = {m for m in vt_defined if m in m_to_name}
  result['unmatched_vtables'] = len(vt_defined - matched)
  mine = {'vtt': set(), 'ctor': set(), 'thunk': set(), 'opt': set()}
  for line in sym_out.stdout.splitlines():
    parts = line.split()
    if parts[0] == 'vtt':
      mine['vtt'].add(parts[1])
    elif parts[0] == 'ctor-vtable':
      mine['ctor'].add(parts[1])
      if 'inexact' in parts:
        result['inexact'] += 1
    elif parts[0] == 'thunk-opt':
      if parts[1] != '?':
        mine['opt'].add(parts[1])
    elif parts[0] == 'thunk':
      if class_m.get(' '.join(parts[2:])) in matched:
        if parts[1] == '?':
          result['unnamed_thunks'] += 1
        else:
          mine['thunk'].add(parts[1])
  # Only classes clang defined a vtable for count.
  clang_vtt = {g for g, rest in globs.items() if g.startswith('_ZTT') and
               not rest.startswith('external') and g[4:] in matched}
  mine_vtt = {s for s in mine['vtt'] if s[4:] in matched}
  result['checked'] += len(matched)
  for s in sorted(mine_vtt - clang_vtt):
    result['diffs'].append(('vtt', f"only in lib/abi: {s}"))
  for s in sorted(clang_vtt - mine_vtt):
    result['diffs'].append(('vtt', f"only in clang: {s}"))
  clang_ctor = {g for g in globs if g.startswith('_ZTC')}
  mine_ctor = {s for s in mine['ctor']
               if any(s.startswith('_ZTC' + m) for m in matched)}
  for s in sorted(mine_ctor - clang_ctor):
    result['diffs'].append(('ctor', f"only in lib/abi: {s}"))
  if result['unmatched_vtables'] == 0:
    for s in sorted(clang_ctor - mine_ctor):
      result['diffs'].append(('ctor', f"only in clang: {s}"))
  clang_thunk = {s for s, d in funcs.items() if d and
                 re.match(r'^_ZT(?:h|v|c)', s)}
  clang_thunk_any = {s for s in funcs if re.match(r'^_ZT(?:h|v|c)', s)}
  for s in sorted(mine['thunk'] - clang_thunk_any):
    result['diffs'].append(('thunk', f"only in lib/abi: {s}"))
  known = {re.sub(r'D[012]E(?!.*D[012]E)', 'D1E', v) for v in fn_m.values()}
  def target(sym):
    rest = sym[3:]
    if rest.startswith('c'):
      rest = rest[1:]
    while True:
      m = re.match(r'^(?:h-?n?\d+_|vn?\d+_n?\d+_)', rest)
      if not m:
        break
      rest = rest[m.end():]
    return '_Z' + rest
  if result['unmatched_vtables'] == 0 and result['unnamed_thunks'] == 0:
    for s in sorted(clang_thunk - mine['thunk'] - mine['opt']):
      t = re.sub(r'D[012]E(?!.*D[012]E)', 'D1E', target(s))
      if t in known:
        result['diffs'].append(('thunk', f"only in clang: {s}"))
  # RTTI.
  mine_rtti = {}
  cur = None
  for line in rtti_out.stdout.splitlines():
    if line.startswith('rtti '):
      parts = line.split()
      cur = parts[1]
      mine_rtti[cur] = {'kind': parts[2], 'fields': []}
    elif line and cur:
      mine_rtti[cur]['fields'].append(line)
  names_m = class_m
  kinds = {'class': '_ZTVN10__cxxabiv117__class_type_infoE+2',
           'si': '_ZTVN10__cxxabiv120__si_class_type_infoE+2',
           'vmi': '_ZTVN10__cxxabiv121__vmi_class_type_infoE+2'}
  rtti_checked = 0
  for g, rest in globs.items():
    if not g.startswith('_ZTI') or g.startswith('_ZTIN10__cxxabiv'):
      continue
    cf = typeinfo_fields(rest)
    name = m_to_name.get(g[4:])
    if cf is None or name is None or name not in mine_rtti:
      continue
    rtti_checked += 1
    want = []
    for f in mine_rtti[name]['fields']:
      if f.startswith('base '):
        bm = names_m.get(f[5:])
        want.append('base ' + (bm if bm else '?'))
      elif f.startswith('vtable '):
        want.append('vtable ' + f[len('vtable '):].replace('+', '+', 1))
      else:
        want.append(f)
    if want != cf:
      # An unknown base mangling is not a difference.
      if len(want) == len(cf) and all(w == c or (w == 'base ?' and
                                      c.startswith('base ')) for w, c in
                                      zip(want, cf)):
        continue
      result['diffs'].append(('rtti', f"{name}: clang {cf} lib/abi {want}"))
  result['checked'] += rtti_checked
  result['rtti'] = rtti_checked
  result['named_classes'] = len(class_m)
  return result, ''


def main():
  ap = argparse.ArgumentParser(description = __doc__.split('\n')[0])
  ap.add_argument('files', nargs = '*')
  ap.add_argument('--list', default = '')
  ap.add_argument('--std', default = 'c++17')
  ap.add_argument('--fe-opts', default = '')
  ap.add_argument('--fe-build', default = 'build/gcc-cir')
  ap.add_argument('--abi-dump', default = 'build/cir-abi/abi_dump')
  ap.add_argument('--root', default = '.')
  ap.add_argument('-v', dest = 'verbose', action = 'store_true')
  ap.add_argument('--dump', action = 'store_true',
                  help = 'print the facts and the symbols of each file')
  args = ap.parse_args()
  root = Path(args.root).resolve()
  fm = cwc.load(root, 'edg-cir-fe-facts')
  abi = cwc.load(root, 'edg-cir-abi-diff')
  files = list(args.files)
  if args.list:
    files += [l.strip() for l in Path(args.list).read_text().splitlines()
              if l.strip() and not l.startswith('#')]
  bad = setup = checked = rtti = 0
  inexact = unnamed = unmatched = 0
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
    checked += result['checked']
    rtti += result['rtti']
    inexact += result['inexact']
    unnamed += result['unnamed_thunks']
    unmatched += result['unmatched_vtables']
    for kind, why in result['diffs']:
      bad += 1
      print(f"DIFF {name}: {kind}: {why}")
    if args.verbose:
      print(f"ok   {name}: {result['checked']} checks ({result['rtti']} "
            f"type_info), {result['named_classes']} classes named, "
            f"{result['unmatched_vtables']} vtables unmatched, "
            f"{result['unnamed_thunks']} thunks unnamed, "
            f"{result['inexact']} inexact _ZTC")
  print(f"{len(files)} files ({setup} not run), {checked} checks "
        f"({rtti} type_info layouts), {unmatched} vtables with an unmatched "
        f"class, {unnamed} thunks that could not be named, {inexact} _ZTC "
        f"not byte-exact, {bad} problems")
  sys.exit(1 if bad else (2 if setup == len(files) else 0))


if __name__ == '__main__':
  main()
