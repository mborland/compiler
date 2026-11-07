#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
'''Draw the charts of the ClangIR executive summary deck (exsum.md).

usage: make_charts.py

Reads stats.json beside this script (written by collect_stats.py from a
gate's evidence) and writes pass_rates.svg and failure_reasons.svg beside
it.  A CIR-specific failure is one whose test passes in the reference
configuration (edg_x86_64: C generation plus gcc).

The style is the dark one of monthly_report/make_report.py, which suits
Deckset's "Work, 1" theme: transparent SVG, text in the ink colors.
'''

import html
import json
from pathlib import Path
from typing import Dict, List, Tuple

FONT = "'Helvetica Neue', Helvetica, Arial, sans-serif"
STYLE = {
  'ink': '#E7EFF2',
  'ink_secondary': '#A3B1B9',
  'ink_muted': '#6B7C86',
  'grid': '#22333C',
  'pass': '#3987E5',
  'cir_fail': '#D95926',
  'ref_fail': '#6B7C86'
}


def text(x: float, y: float, content: str, *, size: int = 22,
         anchor: str = 'start', fill: str = STYLE['ink'],
         weight: str = 'normal') -> str:
  # Deckset's SVG renderer ignores a font-family inherited from <svg>.
  return (
    f"<text x=\"{x:.1f}\" y=\"{y:.1f}\" font-size=\"{size}\" "
    f"font-family=\"{FONT}\" text-anchor=\"{anchor}\" fill=\"{fill}\" "
    f"font-weight=\"{weight}\">{html.escape(content)}</text>"
  )


def text_width(content: str, size: int) -> float:
  '''Estimate the rendered width (sans faces average about 0.55em).'''
  return len(content) * size * 0.55


def bar(left: float, right: float, top: float, height: float, fill: str,
        *, round_left: bool, round_right: bool, radius: float = 4) -> str:
  '''A horizontal bar; the named ends get rounded corners.'''
  r_left = min(radius, (right - left) / 2) if round_left else 0
  r_right = min(radius, (right - left) / 2) if round_right else 0
  bottom = top + height
  return (
    f"<path fill=\"{fill}\" d=\"M{left + r_left:.1f},{top:.1f} "
    f"H{right - r_right:.1f} "
    f"Q{right:.1f},{top:.1f} {right:.1f},{top + r_right:.1f} "
    f"V{bottom - r_right:.1f} "
    f"Q{right:.1f},{bottom:.1f} {right - r_right:.1f},{bottom:.1f} "
    f"H{left + r_left:.1f} "
    f"Q{left:.1f},{bottom:.1f} {left:.1f},{bottom - r_left:.1f} "
    f"V{top + r_left:.1f} "
    f"Q{left:.1f},{top:.1f} {left + r_left:.1f},{top:.1f} Z\"/>"
  )


def legend(items: List[Tuple[str, str]], x: float, y: float) -> List[str]:
  body = []
  for label, color in items:
    body.append(
      f"<rect x=\"{x:.1f}\" y=\"{y - 17:.1f}\" width=\"20\" height=\"20\" "
      f"rx=\"4\" fill=\"{color}\"/>"
    )
    body.append(text(x + 30, y, label))
    x += 30 + len(label) * 22 * 0.5 + 36
  return body


def svg(width: int, height: int, body: List[str]) -> str:
  return '\n'.join(
    [f"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{width}\" "
     f"height=\"{height}\" viewBox=\"0 0 {width} {height}\">"] +
    body + ['</svg>', '']
  )


def pass_rates(corpora: List[Dict]) -> str:
  '''One 100% bar per corpus: passing, failing only under CIR, failing in
  the reference too.'''
  width, height = 1200, 350
  left, right = 8, width - 8
  gap, bar_height = 2, 60
  body = legend([('Pass', STYLE['pass']),
                 ('Fails only with CIR', STYLE['cir_fail']),
                 ('Fails in the reference too', STYLE['ref_fail'])],
                left, 30)
  for row, corpus in enumerate(corpora):
    name, kind, total = corpus['name'], corpus['kind'], corpus['total']
    passed, cir, ref = corpus['pass'], corpus['cir_only'], corpus['ref_fail']
    title_y = 110 + row * 130
    top = title_y + 18
    fair = 100.0 * passed / (passed + cir)
    body.append(text(left, title_y, name, size = 26, weight = 'bold'))
    body.append(text(left + text_width(name, 26) + 18, title_y,
                     f"{total:,} {kind}", fill = STYLE['ink_secondary']))
    body.append(text(right, title_y,
                     f"{fair:.1f}% of the tests the reference passes",
                     anchor = 'end', size = 24, weight = 'bold'))
    segments = [(passed, STYLE['pass']), (cir, STYLE['cir_fail']),
                (ref, STYLE['ref_fail'])]
    span = right - left - gap * (len(segments) - 1)
    x = left
    for index, (count, color) in enumerate(segments):
      segment = span * count / total
      body.append(bar(x, x + segment, top, bar_height, color,
                      round_left = index == 0,
                      round_right = index == len(segments) - 1))
      label = f"{count:,}"
      if text_width(label, 24) + 16 <= segment:
        body.append(text(x + 12, top + bar_height / 2 + 8, label, size = 24,
                         weight = 'bold'))
      x += segment + gap
  return svg(width, height, body)


def failure_reasons(reasons: List[Dict]) -> str:
  '''CIR-specific failures by reason: one panel per language on a shared
  scale, the undiagnosed (last) row set apart.'''
  width = 1200
  label_right = 400
  panels = [('C', 'c', 440), ('C++', 'cpp', 830)]
  largest = max([row[key] for row in reasons for _, key, _ in panels] + [1])
  unit = min(3.3, 320 / largest)
  row_height, bar_height = 40, 24
  top = 78
  divider = 18
  height = top + row_height * len(reasons) + divider + 6
  body = []
  for name, key, x in panels:
    total = sum(row[key] for row in reasons)
    body.append(text(x, 34, name, size = 26, weight = 'bold'))
    body.append(text(x + text_width(name, 26) + 14, 34,
                     f"{total} CIR-specific failures",
                     fill = STYLE['ink_secondary']))
    body.append(
      f"<line x1=\"{x:.1f}\" y1=\"{top - 12}\" x2=\"{x:.1f}\" "
      f"y2=\"{height - 6}\" stroke=\"{STYLE['grid']}\" stroke-width=\"1\"/>"
    )
  for index, row in enumerate(reasons):
    last = index == len(reasons) - 1
    y = top + index * row_height + (divider if last else 0)
    if last:
      body.append(
        f"<line x1=\"8\" y1=\"{y - divider / 2 - 2:.1f}\" "
        f"x2=\"{width - 8}\" y2=\"{y - divider / 2 - 2:.1f}\" "
        f"stroke=\"{STYLE['grid']}\" stroke-width=\"1\"/>"
      )
    body.append(text(label_right, y + bar_height / 2 + 7, row['label'],
                     anchor = 'end', size = 21,
                     fill = STYLE['ink_secondary'] if last else STYLE['ink']))
    for _, key, x in panels:
      value = row[key]
      length = value * unit
      body.append(bar(x, x + length, y, bar_height, STYLE['cir_fail'],
                      round_left = False, round_right = True))
      body.append(text(x + length + 10, y + bar_height / 2 + 7, str(value),
                       size = 21, fill = STYLE['ink_secondary']))
  return svg(width, height, body)


def main() -> None:
  here = Path(__file__).resolve().parent
  stats = json.loads((here / 'stats.json').read_text(encoding = 'utf-8'))
  charts: Dict[str, str] = {
    'pass_rates.svg': pass_rates(stats['corpora']),
    'failure_reasons.svg': failure_reasons(stats['reasons'])
  }
  for name, content in charts.items():
    (here / name).write_text(content, encoding = 'utf-8')
    print(f"wrote {here / name}")


if __name__ == '__main__':
  main()
