#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
'''Collect the numbers of the executive summary charts from a gate.

usage: collect_stats.py EVIDENCE_DIR [GATE] > stats.json

EVIDENCE_DIR is a checkpoint evidence directory of edg-cir-checkpoint (on
clang4, for example ~/edg-cir-config/m51-int/.tmp/checkpoint-<stamp>); it
needs the corpus and cpp-corpus steps.  GATE names the gate in the output
("M5.1").  Only the standard library is needed, so it runs on clang4 as it
is.

For each corpus (configuration edg_x86_64_cir) a test either passes, fails
only under CIR (its test passes in the reference configuration edg_x86_64),
or fails in the reference too.  The CIR-only failures are grouped by the
first rule of REASONS whose pattern matches the bucket's message; a
failure that is not a diagnosed drop (bucket other than UNSUPPORTED) is
"not diagnosed".
'''

import json
import re
import sys
from pathlib import Path
from typing import Dict, List

CONFIG = 'edg_x86_64_cir'
CORPORA = [('C corpus', 'corpus', 'c'), ('C++ corpus', 'cpp-corpus', 'cpp')]
# (label, pattern); the first match wins, so the order matters.
REASONS = [
  ('Inline assembly', r'inline assembly'),
  ('Vector types and builtins',
   r'vector|__builtin_shuffle|builtin operation'),
  ('_Complex', r'complex'),
  ('Atomic builtins', r'__sync_|__atomic_'),
  ('_BitInt, other parameter types',
   r'parameter of an unsupported type|return type that is unsupported'),
  ('Variable-length arrays', r'variable length array'),
  ('Other floating-point formats', r'floating constant of this format'),
  ('GNU attributes',
   r'alias|weakref|cleanup attribute|constructor or destructor attribute|'
   r'init_priority|multiversioning'),
  ('Other GNU extensions',
   r'statement expression|omitted operand|__builtin_apply|__builtin_return|'
   r'__builtin_longjmp|va_arg_pack|computed goto|case label nested'),
  ('Other unmapped builtins', r'the builtin function'),
  ('Initializers, conversions, other', r'.')
]
NOT_DIAGNOSED = 'Not diagnosed: link or run'


def reason_of(entry: Dict) -> str:
  '''The REASONS label of a CIR-only failure.'''
  label = NOT_DIAGNOSED
  if entry['bucket'] == 'UNSUPPORTED':
    label = next(name for name, pattern in REASONS
                 if re.search(pattern, entry['message']))
  return label


def collect(evidence: Path, gate: str) -> Dict:
  corpora: List[Dict] = []
  counts: Dict[str, Dict[str, int]] = {
    name: {'c': 0, 'cpp': 0} for name, _ in REASONS + [(NOT_DIAGNOSED, '')]
  }
  for name, step, language in CORPORA:
    path = evidence / step / f"{CONFIG}.json"
    entries = json.loads(path.read_text(encoding = 'utf-8'))[CONFIG]
    passed = sum(1 for entry in entries if entry['bucket'] == 'PASS')
    failed = [entry for entry in entries if entry['bucket'] != 'PASS']
    cir_only = [entry for entry in failed if not entry['ref_fail']]
    for entry in cir_only:
      counts[reason_of(entry)][language] += 1
    corpora.append({
      'name': name, 'kind': 'run and link tests', 'total': len(entries),
      'pass': passed, 'cir_only': len(cir_only),
      'ref_fail': len(failed) - len(cir_only)
    })
  order = {name: index for index, (name, _) in enumerate(REASONS)}
  diagnosed = sorted(
    (name for name, _ in REASONS),
    key = lambda name: (-(counts[name]['c'] + counts[name]['cpp']),
                        order[name])
  )
  reasons = [{'label': name, **counts[name]}
             for name in diagnosed + [NOT_DIAGNOSED]]
  return {'gate': gate, 'evidence': str(evidence), 'config': CONFIG,
          'corpora': corpora, 'reasons': reasons}


def main() -> None:
  if len(sys.argv) not in (2, 3):
    sys.exit(__doc__.strip().splitlines()[2])
  evidence = Path(sys.argv[1]).expanduser()
  gate = sys.argv[2] if len(sys.argv) == 3 else ''
  print(json.dumps(collect(evidence, gate), indent = 2))


if __name__ == '__main__':
  main()
