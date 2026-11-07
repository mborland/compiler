# ClangIR executive summary deck

A short [Deckset](https://www.deckset.com) deck on the state of the CIR
back end: headline numbers, milestones, test results, why tests still
fail, performance, gaps, next steps, open decisions and risks. It uses
the "Work, 1" theme of `monthly_report/` and is refreshed after each
milestone gate.

| File | Role |
| --- | --- |
| `exsum.md` | The deck: text by hand, details in the presenter notes (`^`) |
| `collect_stats.py` | Reads a gate's evidence and prints `stats.json` (runs on clang4) |
| `stats.json` | The numbers behind the charts, and the gate they come from |
| `make_charts.py` | Draws `pass_rates.svg` and `failure_reasons.svg` from `stats.json` |
| `exsum.pdf` | The exported deck |

## Making the next report

1. Pick the evidence directory of the latest milestone gate on clang4
   (`project.md` §9 names it; it needs the `corpus` and `cpp-corpus`
   steps).
2. Collect the chart numbers, from the repository root:

   ```sh
   ssh -o BatchMode=yes -i ~/.ssh/id_rsa matt@clang4.cpp.al \
     python3 - EVIDENCE_DIR GATE < cir_src/doc/exsum/collect_stats.py \
     > cir_src/doc/exsum/stats.json
   python3 cir_src/doc/exsum/make_charts.py
   ```

3. Update the text of `exsum.md`: the title slide (date, commit), then
   every number and list from the sources below. Keep each slide to what
   fits; Deckset does not shrink text unless the slide says
   `[.autoscale: true]`.
4. Export the PDF with Deckset (the terminal needs Automation permission
   for Deckset), then look at every page for clipped text. Deckset writes
   into a fresh directory so that no "replace?" dialog stops it:

   ```sh
   D=$PWD/cir_src/doc/exsum/exsum.md; T=$(mktemp -d)
   osascript -e "tell application \"Deckset\"
     set deck to open POSIX file \"$D\"
     delay 3
     export deck to POSIX file \"$T/exsum.pdf\" as \"PDF\"
     close deck
   end tell" && mv "$T/exsum.pdf" "${D%.md}.pdf" && rmdir "$T"
   ```

## Where the numbers come from

| Slide | Source |
| --- | --- |
| At a glance, Test pass rates | `stats.json`; a pass rate is `pass / (pass + cir_only)`, the raw rate `pass / total` |
| Why tests still fail | `stats.json` `reasons`; the diagnosed share is 1 minus the last row over `cir_only` |
| Crashes, gate steps, suites, ABI mixes, regression run | The evidence's `summary.md` step table (`crash-sweep`, `smoke`, `cpp-smoke`, `cir-suite`, `mixed-tu`, `day`, `day32`) |
| Kernels against clang | `codequality/report.txt` |
| Library cost, peak memory | `perf/targets.txt`, the `chunk-rss` step |
| Real-world units | `PINNED_UNITS` in `dev_tools/bin/edg-cir-perf` against the units in `dev_tools/cir-realworld/` |
| Milestones, gaps, next steps, decisions, risks | `project.md` §0, §4, §6, §8 and §5 |

## How failures are counted

A failure is CIR-specific when its test passes in the reference
configuration `edg_x86_64` (C generation plus gcc); the other failures are
not the back end's. `collect_stats.py` puts each CIR-specific failure in
the first group of `REASONS` whose pattern matches its message, and
counts any failure that is not a diagnosed drop (a link error, a wrong
result, a front-end diagnostic) as "not diagnosed". When a new reason
appears in a gate, it lands in "Initializers, conversions, other": check
that group's size and add a rule if a new reason dominates it.
