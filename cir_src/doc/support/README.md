# CIR back end support table

This directory is the one source of truth for what the ClangIR (CIR) back end
supports, in the style of cppreference's "compiler support" pages. Each
section lives in its own JSON file, and `make_support.py` renders all of them
into:

- `support.md`, the full table in GitHub Markdown;
- `doc/source/cir_support.inc`, included by the Sphinx chapter
  `doc/source/cir_be.rst`;
- `cir_src/doc/exsum/support.svg`, the chart that the executive summary
  deck embeds.

Edit the JSON files, never the rendered files. Then run
`python3 cir_src/doc/support/make_support.py` to re-render, and
`make_support.py --check` to validate (project.md decision 51).

## Files and owners

| File | Section | Owner |
| --- | --- | --- |
| `cxx.json` | C++ core language, one group per standard (C++98/03 … C++26) | cir-writer |
| `c.json` | C language, one group per standard (C89/C90 … C23) | cir-lib |
| `gnu.json` | GNU extensions: language, attributes | cir-lib |
| `builtins.json` | Builtin functions, by family | cir-lib |
| `codegen.json` | Code generation and options | cir-lib |
| `hardening.json` | Security hardening of the generated code | cir-lib |
| `platforms.json` | Targets, ABIs, runtimes, interoperability | cir-lib |
| `libstdcxx.json` | libstdc++ headers (one row per header; filled in M6.1a) | cir-config (from M6.1a; user, 2026-11-08) |
| `make_support.py` | Renderer and checker | cir-config |
| `README.md` (this file) | Schema | orchestrator |

## File format

```json
{
  "schema": 1,
  "section": "cxx",
  "title": "C++ core language",
  "intro": "One or two sentences shown above the section's tables.",
  "groups": [
    {"id": "cxx20", "title": "C++20", "rows": [ ... ]}
  ]
}
```

Each group becomes one table in the documents and one bar in the chart.
Order the groups and the rows as they should appear.

## Rows

```json
{"id": "cxx20.three_way", "feature": "Three-way comparison",
 "papers": ["P0515R3", "P0768R1"],
 "macro": "__cpp_impl_three_way_comparison", "macro_value": "201711L",
 "status": "yes", "since": "M3.1", "planned": null, "note": "",
 "tests": [{"path": "tests/tests/cir/cirgen/cxx-spaceship.sft.cpp",
            "kind": "e2e-run"},
           {"path": "cir_src/test/cirgen/cxx-cmp3way.test",
            "kind": "lib"}],
 "refs": []}
```

| Field | Required | Meaning |
| --- | --- | --- |
| `id` | always | `<group id>.<slug>`, matching `[a-z0-9_]+\.[a-z0-9_]+`; unique across all files |
| `feature` | always | Short name in cppreference's wording |
| `papers` | always (may be empty) | WG21 or WG14 documents and core issues, e.g. `P0515R3`, `N2927`, `CWG2387`, `N3096` |
| `macro` | C++ rows with a feature-test macro | e.g. `__cpp_lambdas`; otherwise `null` |
| `macro_value` | when `macro` is set | The value this row stands for, e.g. `201711L` |
| `status` | always | One of the statuses below |
| `since` | `yes`, `partial` | The gate that made the row true: `M2`, `M3`, `M3.1`, `M4`, `M4.1`, `M5`, `M5.1`, then `M6.x`. For rows that were already true at M6.0, use the milestone whose log in `design.md` §10–§16 delivered the feature, or `≤M5.1` when that is unclear. Set it by hand when a gate changes the row. `null` for the other statuses |
| `planned` | `no`, `ignored`, `unverified`; `partial` when an M6 item closes the residue | The gate that makes the row `yes`: an M6 sub-milestone id (`M6.R`, `M6.1a`, `M6.1b`, `M6.1c`, `M6.Pb`, `M6.2`, `M6.3`, `M6.4a`, `M6.4b`, `M6.5`, `M6.6a`, `M6.6b`, `M6.7`, `M6.8`, `M6.9`, `M6.10`, `M6.11`, `M6.12`), `M7`, `after M7`, or `Ext` (GNU or MSVC extensions, after M7). An `ignored` row must still stop being silent by M6.4a (diagnosed: `no`) even if it becomes `yes` later; say so in the note |
| `mode_independent` | optional, C++ and C rows | `true` when the row's semantics do not depend on the language mode, so an `e2e-run` test in an earlier mode (a feature accepted as an extension) proves it. The note says why |
| `note` | `partial`; optional otherwise | One or two plain sentences: what is missing, what is diagnosed, a caveat |
| `tests` | see the status rules | Test files, each with a `kind` (below), path relative to the repository root |
| `refs` | `no` (see the rules); optional otherwise | Other evidence as `path:line` or `path §N` strings. Paths are relative to the repository root; a bare CIR document name (`project.md`, `writer.md`, `research/…`) resolves under `cir_src/doc/` |

**C++ macro values and papers.** One row per (value, papers) pair of SD-6,
the feature-test document; no two rows share a `(macro, macro_value)`.
Macro values are cumulative: a value is listed when the front end
(`src/macro.c`: `feature_support_list[]` and the mode-dependent values at
about 12107–12249) defines that value or a later one in some mode. Values
above the front end's maximum, and features it does not implement, are not
listed. Rows with no macro take their papers from cppreference's compiler
support pages.

### Statuses

| Status | Shown as | Means | Rule |
| --- | --- | --- | --- |
| `yes` | **Yes** · since (green) | Compiles and runs correctly | At least one `e2e-run` test, in the row's language mode or later unless `mode_independent` |
| `partial` | **Partial** · since (yellow) | Common forms work; the rest is diagnosed | A `note` naming what is diagnosed; at least one `e2e-run` test; a `negative` test for the diagnosed rest (the checker warns until M6.1a) |
| `no` | **No** · planned (orange) | Fails loudly: diagnosed and dropped, or a compile or link error. Never miscompiled | `planned`; a `refs` entry `path:line` into code (`src/`, `cir_src/lib/`, `cir_src/tools/`, `util/`, `dev_tools/`, `cmake/`) naming the diagnose or rejection site, or a `failing` test for a loud failure without a named reason; a `negative` test (the checker warns until M6.1a) |
| `ignored` | **Ignored** · planned (red) | Accepted and silently ignored, or silently wrong output: a safety gap | `planned`; must reach 0 at M6.4a |
| `unverified` | **Unverified** · planned (grey) | Expected to work, but no CIR test proves it | `planned` (normally `M6.1a`); no `failing` tests; must reach 0 at M6.1a |

Colors come from the dataviz reference status palette:

| Status | Color |
| --- | --- |
| yes (good) | `#0ca30c` |
| partial (warning) | `#fab219`, with dark text |
| unverified (neutral) | `#6B7C86` |
| no (serious) | `#ec835a`, with dark text (white text reaches only 2.6:1) |
| ignored (critical) | `#d03b3b` |

Charts stack the statuses in that order. The validator passes the CVD and
normal-vision separation of adjacent pairs on the deck's dark surface. The
cells always carry the text label, so color never carries the meaning
alone.

Be honest. A feature that "should just work through the IL" is `unverified`
until a test compiles and runs it under CIR. An attribute that gcc and clang
also ignore on x86-64 (for example `regparm`, `stdcall`, `fastcall`) is not
a gap: it is `yes` when a test shows the same behavior, with a note.

### Test kinds

| Kind | What it proves | Rule (checked) |
| --- | --- | --- |
| `e2e-run` | Source compiled by cpfe-cir, linked and run, with correct output | A file under `tests/tests/` whose header says `//type:rp` (compile, link and run; not `fp`, `fn`, `fc` or `lp`) and has no `//script:`; inside the CIR suite (`tests/tests/cir/`) or the corpus filters (`tests/.edgy/filters/cir-{c,cpp}-corpus.txt`); not listed as failing in `tests/expectations/edg_x86_64_cir/<suite>.log`. Its language mode is its `//options:` `--c++NN`/`--cNN`, or the configuration's default. From M6.1a, every per-case type override (`//options:A;rp:B;fn`) must also be `rp`, and the mode comes from the `rp` cases. Also allowed: the cases under `dev_tools/cir-mixed-tu-cases/`, which the gate's `mixed-tu` step links with g++ and clang objects and runs, unless the case has a non-empty `xfail` file |
| `writer` | The writer's stream shape (golden) | A `//script:edg-cir-ecil-dump` golden under `tests/tests/cir/`, or a file under `cir_src/test/writer/` |
| `lib` | The library alone, from hand-written ECIL | A file under `cir_src/test/` (not `cir_src/test/writer/`) |
| `negative` | cpfe-cir diagnoses the construct, eccp rejects the option, or the library rejects the target | A test under `tests/tests/cir/` whose type is `fp`, `fn`, `fc` or `cn` and whose recorded output (`.<stem>.rto/edg_x86_64_cir*.txt`, both configurations) contains a CIR diagnosis ("does not support" or "cannot express"), a `Command-line error: CIR back end:` line, or eccp's `unknown option:` line, in every case of the test (each `<config>.<case>` recording); a library `warning [1003]` line does not count; for targets and library options, a tool test under `cir_src/test/lit/` whose `CHECK` lines (not `CHECK-NOT`) expect the tool's `error` (for example `errors.test`). The `bad-*.test` lit tests check malformed ECIL and do not count |
| `failing` | The row's gap makes this test fail under CIR | Listed as failing in `tests/expectations/edg_x86_64_cir/<suite>.log` and *not* in the reference `tests/expectations/edg_x86_64/<suite>.log` (a test that fails in the reference shows a front-end or test problem, not a CIR gap). Allowed on `no` and `partial` rows only. On a `no` row (from M6.1a) the status must be a loud failure: `FAIL(COMPILE)` for an `rp` test, `FAIL` for an `lp` test |

With no `--c++NN`/`--cNN` option a test runs as C++03 or C89, unless GNU mode (`--g++`/`--gcc`, or `--gnu_version=N`) raises it per `src/cmd_line.c`: C++14 from N=60000 and C++17 from 110000, C11 from 50000, C17 from 80000 and C23 from 150000. The corpus `.run_test_flags` pass `--gnu_version=999999`, so corpus tests without a standard option run as C++17 or C23.

Rows with an empty `tests` list are fine where the rules allow them, for
example a `no` row whose only evidence before M6.1a is a `refs` entry.

## Checking and gates

- `make_support.py --check` validates the schema, the ids, the status rules
  and the test paths. It fails when a rendered file is stale, and it prints
  the counts per group and status.
- `make_support.py --check --gate M6.x` also enforces the thresholds due
  by that gate:
  - from M6.1a: 0 `unverified`; `no` and `partial` rows need a `negative`
    test; `yes` and `partial` rows need an `e2e-run` test in their own
    language mode unless `mode_independent` (warned before M6.1a);
  - from M6.4a: 0 `ignored`.
- Every gate updates the rows that its change set affects (project.md
  decision 51).
- The gate step `support-sweep` (from M6.1a) runs every test the rows
  name, then `make_support.py --check --gate` for the current gate.

## Feature probes: `tests/tests/cir/features/`

From M6.1a the probes and negative tests that prove the rows live in
`tests/tests/cir/features/<section>/`, one subdirectory per JSON file, each
owned by that file's owner (user, 2026-11-08): `cxx/` by cir-writer;
`c/`, `gnu/`, `builtins/`, `codegen/`, `hardening/` and `platforms/` by
cir-lib; `libstdcxx/` (the header sweep) by cir-config. They are part of
the `cir` suite, so every test there must pass in both CIR configurations.
A probe runs its feature at -O0 and -O2 through two cases
(`//options:--c++23:--c++23 -O2`). A construct that fails is proven by a
`negative` test instead, never by a failing probe.
