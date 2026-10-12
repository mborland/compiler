# Gate M3.1: re-assessment of the §5 items that are not fixed

Each section is written by the owner named in its heading; sections are
appended, not rewritten. Every item here is "measured and re-assessed", with
the milestone it moves to.

## Microsoft mode and native Windows (cir-config, C7)

Status: re-assessed 2026-10-12 at HEAD (3592b2b3f7 plus the M3.1 work in
progress). Nothing was changed for this item. Outcome: **both move to M7**,
unchanged from `handoff_2.md` §5 and §6. Counts are from the recorded
expectations and from the M3 gate's runs; the statements about the MSVC ABI are
an assessment, not a measurement.

### What is skipped today

A test case whose options select Microsoft mode, C++/CLI or C++/CX is reported
`REQUIREMENTS NOT MET` when the front end was built without Microsoft
extensions (`edgtest.exec._case_requests_unsupported_mode`).
`cmake/macro-conf/default/cpfe-cir.cmakedef` imports
`support/feature/microsoft-extensions/off`, so `MICROSOFT_EXTENSIONS_ALLOWED`
is 0 in `cpfe-cir` (both the debug test configuration and the release one).

Counts of `REQUIREMENTS NOT MET` lines, from `tests/expectations/`:

| Config | Suite | Language | Cases | Tests |
| --- | --- | --- | ---: | ---: |
| `edg_x86_64_cir` | `edg` | C++ | 46 | 38 |
| `edg_x86_64_cir` | `imported` | C | 3 | 3 |
| `edg_x86_64_cir` | `cwg`, `cir` | | 0 | 0 |
| `edg_x86_64_cir_2p` | all | | 0 recorded | 0 |
| `edg_x86_64` (reference) | `edg` 20, `imported` 9, `regressions` 1 | C and C++ | 30 | 20 |

Notes:
- `edg_x86_64_cir_2p` records only the smoke lists, so its expectations hold no
  skips. A full corpus run of `edg_x86_64_cir_2p` (M3 gate, run
  `2026.10.11-14.10.35`) skips the same 46 `edg` cases.
- The reference's 30 skips have other causes (test types `fp`, `cp`, `cppbe`,
  other requirements), not Microsoft mode. Only 4 of the 49 skipped cir cases
  are also skipped in the reference.
- Of the 49 skipped cir cases, **45 pass in the reference**, so 45 is the
  upper bound of what Microsoft mode could add: all 45 are C++ cases of the
  `edg` suite, 2.6% of the C++ corpus (1732 cases). The other 4 (the 3 C
  cases and one `edg` case) are skipped in the reference as well. By option on
  the line: `--microsoft_version` 20, `--microsoft` 4, `--microsoft_v` 2,
  `--microsoft_bugs` 2, `--ms_c` 3, and 18 lines whose own option list names no
  Microsoft option (the case's mode comes from the test description).
- The skips do not hide failures: they are neither FE crashes nor library
  errors. The gate's asm-diff and corpus steps count them as skipped, like the
  reference's.

### Why Microsoft mode is not an M4 item

- The library and ECIL are Itanium-only, and so is the M4 contract (ECIL 1.2).
  At the LLVM pin (`894bfff5`), upstream ClangIR has only a stub of the MSVC C++
  ABI: `clang/lib/CIR/CodeGen/CIRGenMicrosoftCXXABI.cpp` is 324 lines with 44
  `errorNYI` calls (constructors, destructors, vtables, virtual calls and
  deletes, among others), against 2477 lines for the Itanium ABI. Name
  mangling, vtable and vbtable layout, RTTI, EH and pointers to members would
  all have to be written for the MSVC ABI.
- The "Extensions" paragraph of `handoff_2.md` §6 lists MSVC after the GNU
  features and says each needs its own format addition and review.
- Enabling the macro for `cpfe-cir` alone would not turn the 45 cases into
  passes: the front end would then accept Microsoft constructs that the
  writer has to diagnose one by one.

### Native Windows

- `HACKING.md` (Windows section) states that native testing is not supported:
  `edgy` and `edg-docker-test` depend on Bash (`util/eccp.sh`, `bin/eccp`,
  `dev_tools/bin/cpfe-cp-gen-be`), and building and running the front end
  there is supported but has no official test process.
- `cir_src` has one preset that is documented as "Mac/Windows ok"
  (`lib/format`, `ecil-dump`, `ecil-asm`, no LLVM). The LLVM/MLIR/CIR toolchain
  image and the `cpfe-cir` presets exist for Linux x86-64 only; the
  `windows-msvc-*` presets build the cpfe back ends without CIR.
- `handoff.md` §6 and `handoff_2.md` §6 (M7) make Windows depend on upstream
  CallConvLowering support for the target. Nothing in this session changes
  that. The local Parallels VM is ARM64 Windows (cross builds only), so it
  cannot run an x86-64 CIR toolchain natively.

### Re-assessment

| Item | Status | Moves to | Evidence |
| --- | --- | --- | --- |
| Microsoft-mode test variants skipped for `cpfe-cir` | measured, 49 cases (46 C++, 3 C), 45 of them (all C++) pass in the reference | M7 (MSVC as an extension, after GNU) | counts above; MSVC ABI stub in upstream CIR at the pin |
| Native Windows edgy and `cpfe-cir` | unchanged: unsupported | M7 (with upstream Windows CallConvLowering) | `HACKING.md`, `cir_src/CMakePresets.json` |

Re-run the counts after any change to the corpus or the expectations:

```
cd tests/expectations
grep -c 'REQUIREMENTS NOT MET' edg_x86_64_cir/*.log
```

## Library: cost growth and in-process residency (cir-lib, L5)

Status: measured 2026-10-12 on the x86-64 box (`~/edg-cir-lib`, the release
`edg-cir`, `ulimit -s 256`, a shared machine with a load average of 4 to 6, so
absolute times carry about +-30% noise; the ratios between sizes do not).
Nothing was changed for these two items except one measurement aid: `edg-cir
--stats` now also prints the peak RSS at the end of each stage (see
"Measurement aid" below). Outcomes:

- **Upstream cost growth: unchanged, still "watch upstream".** The LLVM pin
  is the same (`894bfff5`) and an A/B of the M3 gate build of `edg-cir`
  (`m3-final`) against the current tree on the same streams, interleaved,
  differs by less than the noise (cxx_locals:2000 6.3 s and 3.8 s against
  6.3 s and 4.0 s; cxx_cond_chain:1000 0.77 against 0.62 s; cxx_blocks:1000
  1.0 against 0.9 s; elseif:2000 0.53 against 0.53 s). Realistic code is far
  below the knee.
- **In-process residency: moved to M5, with numbers.** The MLIR module is
  about 20 KB per function and stays resident until the output is written;
  the CIR-to-LLVM lowering then adds about twice that, so the peak is reached
  while both are alive.

### a. Cost growth (the stress kinds of `ecil-stress-gen.py`)

Stage times in ms; `rss lower` is the peak RSS (MB) at the end of lowering (the
run's peak). The emitter (`emit`), `decode` and `finish` stay at or below about 30 ms in
every row, so they are left out. Reproduce with
`box_stress.sh`-style runs of `edg-cir --stats` (script in the evidence:
`build/mine/l5.sh` of the box tree, a loop over `ecil-stress-gen.py KIND N`).

| Kind (nesting) | N | verify | CIR passes | lower | codegen | rss lower | growth per doubling of N |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| elseif (nested ifs) | 1000 | 19 | 9 | 61 | 5 | 46 | |
| | 2000 | 95 | 39 | 245 | 10 | 52 | |
| | 4000 | 509 | 184 | 1016 | 20 | 65 | verify 5.3x, passes 4.8x, lower 4.1x |
| | 8000 | 2222 | 769 | 4077 | 45 | 92 | verify 4.4x, passes 4.2x, lower 4.0x |
| nested_blocks | 500 | 2.5 | 4.5 | 22 | 3.6 | 51 | |
| | 1000 | 9.9 | 13.6 | 53 | 5.5 | 54 | |
| | 2000 | 52 | 57 | 194 | 10 | 60 | verify 5.3x, passes 4.2x, lower 3.7x |
| cxx_blocks (a local with a destructor per level) | 500 | 21 | 24 | 101 | 6 | 45 | |
| | 1000 | 88 | 124 | 412 | 11 | 50 | |
| | 2000 | 417 | 658 | 2432 | 39 | 61 | verify 4.7x, passes 5.3x, lower 5.9x |
| cxx_cond_chain (nested `?:` arms with temporaries) | 500 | 18 | 33 | 132 | 22 | 61 | |
| | 1000 | 76 | 93 | 378 | 60 | 74 | verify 4.2x, passes 2.8x, lower 2.9x |
| cxx_locals (N nested cleanup scopes in a block) | 1000 | 63 | 75 | 971 | 91 | 70 | |
| | 2000 | 248 | 234 | 3747 | 98 | 89 | |
| | 4000 | 748 | 732 | 12201 | 240 | 126 | verify 3.0x, passes 3.1x, lower 3.3x |
| cxx_temps (one full expression) | 2500 | 347 | 460 | 1672 | 47 | 77 | |
| | 5000 | 1344 | 1350 | 7071 | 62 | 107 | |
| | 10000 | 6137 | 8848 | 52684 | 200 | 166 | verify 4.6x, passes 6.6x, lower 7.5x |
| ternary | 5000 | 974 | 329 | 1934 | 46 | 106 | |
| | 10000 | 3431 | 1177 | 6923 | 105 | 166 | verify 3.5x, passes 3.6x, lower 3.6x |
| loops (nested while) | 1000 | 11 | 5.6 | 133 | 4.7 | 55 | |
| | 2000 | 62 | 19 | 511 | 8.4 | 62 | verify 5.7x, passes 3.4x, lower 3.9x |
| cxx_array_nest (nested `cir.array.ctor`) | 25 | 0.2 | 3.7 | 5.5 | 2.4 | 49 | |
| | 50 | 0.2 | 19 | 8.2 | 3.0 | 50 | passes 5.1x |
| | 100 | 0.3 | 135 | 28 | 4.5 | 50 | passes 7.1x |
| | 200 | 0.4 | 692 | 30 | 3.8 | 53 | passes 5.1x |

Reading:
- **Nested ifs, loops, blocks and cleanups are quadratic** in the nesting depth
  (about 4x per doubling, up to 6x for cleanups with the flag logic): the verifier,
  the CIR passes (FlattenCFG, CallConvLowering, LoweringPrepare) and the CIR to
  LLVM lowering all contribute, in that order of weight lowering > verify ~ passes.
- **Nested `cir.array.ctor` is cubic in the CIR passes** (5x to 7x per doubling) and
  nothing else grows. It needs `T[a][b][c]...` with each level a separate body;
  the writer sends multi-dimensional arrays flattened (one `ARRAY_CTOR` with the
  total count), so depth 100 does not occur in real code.
- **Memory is not the limit:** 126 MB for 4000 nested cleanup scopes, 166 MB for
  10000 temporaries, 92 MB for 8000 nested ifs.
- **Time limits.** The cpp-stress gate cases are inside its 30 s limit (cxx_locals:2000
  4.3 s, cxx_temps:5000 9.9 s at this load). Past those sizes the cost is real
  (cxx_temps:10000 takes 68 s) but the shapes are generated: a full
  expression with ten thousand temporaries, ten thousand nested `if`s.
- **Remedies, if a real input ever needs one** (none has): emit past a depth
  threshold with an unstructured form (not available for cleanups in CIR), or
  split the TU. Upstream fixes would be the proper answer.

### b. In-process MLIR residency on the 6000-function translation units

The generated inputs of `edg-cir-rss` (`--generate-functions 6000`: 78 004
lines of C, 12 008 lines and 11 728 G-ids of C++), the release front end
(`build/gcc-release-cir`) and the release `edg-cir`.

`edg-cir-rss` (peak RSS of the process that holds the module):

| Input | in-process (FE streams into the library) | two-process front end | two-process `edg-cir` |
| --- | ---: | ---: | ---: |
| C, 6000 functions | 468 MB, 5.0 s | 63 MB, 0.3 s | 427 MB, 4.6 s |
| C++, 6000 functions | 184 MB, 1.6 s | 56 MB, 0.15 s | 149 MB, 1.4 s |

The numbers equal the M3 gate's (468 MB and 428 MB for C), so M3.1 changed
nothing here. The front end's own share is the 56 to 63 MB of the second
column (regions freed per function); the in-process peak is the library's peak
plus about 40 MB of front end.

Where the library's peak comes from (`edg-cir --stats`, peak RSS in MB at the
end of each stage; the process starts at 21 MB):

| Functions | stream | after emit | after CIR passes | after lowering (the peak) | peak per function |
| --- | ---: | ---: | ---: | ---: | ---: |
| C 3000 | 1.1 MB | 89 | 101 | 232 | 77 KB |
| C 6000 | 2.2 MB | 143 | 165 | 427 | 71 KB |
| C 12000 | 4.4 MB | 250 | 294 | 822 | 69 KB |
| C++ 3000 | 0.4 MB | 49 | 57 | 87 | |
| C++ 6000 | 0.8 MB | 63 | 78 | 136 | |
| C++ 12000 | 1.6 MB | 90 | 120 | 235 | |

Time on the 6000-function C input: decode and validate 82 ms, emit 233 ms,
finish 2 ms, verify 137 ms, CIR passes 457 ms, lowering 2856 ms, code
generation 717 ms. C++: 23, 66, 8, 24, 195, 632 and 387 ms.

Findings (the numbers go to M5):
- **The module is linear and moderate:** 17 to 21 KB of resident MLIR per
  C function (143 - 21 MB over 6004 functions is 20 KB; the slope between 6000
  and 12000 is 18 KB), 4 to 7 KB per generated C++ function. Finish, verify
  and the module itself cost 2 MB; the CIR passes add 22 MB (13%).
- **The peak is the lowering.** `lowerDirectlyFromCIRToLLVMIR` builds the whole
  LLVM module while the CIR module is alive: +262 MB for C (62% of the peak),
  +58 MB for C++. The peak therefore is reached at the end of the lowering
  stage; code generation does not raise it.
- **What M5 can reach.** Keeping the CIR module resident is 30% of the C peak;
  the other two thirds is LLVM state that exists in any compiler. Freeing CIR
  function bodies as they lower is not possible with the upstream whole-module
  entry point (and the CIR passes are whole-module too), so the options are a
  per-chunk module split (declarations for the cross references; changes the
  output order and comdat/alias handling) or a patched lowering (excluded: no
  local LLVM patches). Streaming decode already keeps the stream out of memory
  (the 2.2 MB stream is not held). For a 12000-function C file the peak is
  0.8 GB; the scaling is linear (69 to 77 KB per function), so the 2.5 GB
  recorded for 20k functions in `handoff.md` section 4 came from a heavier
  input than this one (about 125 KB per function).
- **The in-process path costs nothing extra:** 468 MB in-process against 427 MB
  for the same stream in a separate `edg-cir`.

### Measurement aid added in this session

`edg-cir --stats` (and `EDG_CIR_FLAG_PRINT_STATS`) prints one more note line,
the peak RSS in KiB at the end of each stage:

```
stats: peak RSS KiB at start 21584, after emit 146932, finish 147700, verify 148508, cir passes 169552, lower 437440
```

(`lib/driver/session.h`, `pipeline.cpp`, `session.cpp`). The values are the
process's maximum so far, so the growth between two of them is what the stage
added. A stage that does not run leaves its value 0. The line is printed only
with the flag, like the others.

### Re-assessment

| Item | Status | Moves to | Evidence |
| --- | --- | --- | --- |
| Upstream cost growth (quadratic nesting; cubic nested `cir.array.ctor`) | re-measured; unchanged since M3 (pin unchanged; A/B within noise) | watch upstream (no action) | table a; A/B of the M3 gate build |
| In-process MLIR residency | measured: about 20 KB of MLIR per C function; the peak is the CIR to LLVM lowering (CIR + LLVM alive together): 427 MB at 6000 C functions, 822 MB at 12000 | M5 (per-chunk split of the module is the only option without local LLVM patches) | table b |


## Writer (cir-writer, W10, W14, W15)

### W10. `cpp1z/elide2` (DR 1697): the front end's IL, not the writer

`T t = T{ {}, {} };` with `struct T { S &&s; X x; };` must extend the
temporary `S{}` bound to the reference member to the lifetime of `t`
([class.temporary]); the test expects `~S` after `f()`. The writer sends the
temporary inside the `FULLEXPR` of the initialization with its `CLEANUP_BEGIN`
there (`LOCAL 2; ... CLEANUP_BEGIN; ... FULLEXPR_END`), so `~S` runs too early
and the test aborts. The reason is in the IL: the `dynamic_init` of that
temporary has `lifetime->kind == olk_expr_temporary` (value 4, checked with a
print in the `enk_temp_init` handler), not a block lifetime. The writer's
extended-lifetime choice (`di->lifetime->kind != olk_expr_temporary` goes to the
pending cleanups) is therefore right, and the front end does not mark this
aggregate-initialization reference member as extended. The reference
(`edg_x86_64`, C generation and gcc) fails the test the same way
(`imported.log`: `cpp1z/elide2 --c++11: FAIL` in both), also with `--c++17`.
No writer change; the case stays a documented front-end limitation.

### W14. Region keeps

Findings:

- **`find_binding_for_pack` (keep #2) root cause.** `clear_pack_expansion_variables`
  (`scope_stk.c`) clears the variable pointer of a pack reference's symbol when the
  scope is popped only for `prk_variable`; a structured binding pack
  (`prk_binding`) keeps `symbol->variant.variable.ptr`, which points into the
  function's memory region, and `find_binding_for_pack` read
  `->storage_class` through it in a later real instantiation (AddressSanitizer:
  heap-use-after-free at `scope_stk.c:12416`, `gnats-27775-run` with the keep
  removed). In the CIR configuration the pack reference (file scope region)
  now keeps the one fact it needs, whether the pack's variable is static: a bit
  in `param_or_binding_num`, set where the binding number is computed.
- **The keep stays.** With it removed and that use fixed, `gnats-26823` still
  fails an assertion (`ecil_check_variable_use`, a stale variable id behind an
  address constant that the file scope holds), so the prototype
  instantiation's region still holds data that later instantiations read. Not
  root-caused further here; the keep and its comment record this.
- **Lambda keep.** Restricted to call operators that are constexpr (implicitly,
  from C++17) or whose closure has a conversion function to a function pointer.
  The second condition is needed: `define_lambda_conversion_function` writes to the
  call operator's region while the lambda is scanned (without it,
  `lambda-uneval17` hit a use-after-poison). A capturing lambda before C++17 is
  freed like any function.
- **Measurements** (release `build/gcc-release-cir`, in-process, generated
  template- and lambda-heavy TU of 300 units, 12001 function regions, C++17 so
  every call operator is constexpr and kept in both variants):

  | Variant | in-process wall s | peak RSS MB | `--ecil_keep_regions` RSS MB |
  | --- | ---: | ---: | ---: |
  | before (every lambda kept) | 5.65 | 721 | 745 |
  | after (restricted) | 5.65 | 719 | 744 |

  and the 6000-function C++ TU: 182 MB in both. The restriction changes nothing on
  C++17 inputs; it applies to C++11/14 lambdas that capture. The statistic
  "resident at finish" is 0 for the in-process rows (the kept regions are
  freed at the end of the compilation) and equals the region count with
  `--ecil_keep_regions`, so it does not show the keeps; the peak RSS does.
- **In-process slower than `--ecil_keep_regions` with the debug front end**
  (cir-config: 62 s against 23 s at N=300): that is the debug build. Its
  configuration has `OVERWRITE_FREED_MEM_BLOCKS` and AddressSanitizer poisoning,
  so freeing 12001 regions writes and poisons every freed block, which
  `--ecil_keep_regions` never does. In the release build, where neither exists, the
  two rows are equal (5.65 s against 5.5 to 5.6 s) and in-process uses 25 MB
  less. (Debug timings on the shared box also vary by up to 3x run to run: the
  same row took 64 s and 180 s in two builds of the same code.)

### W15. C++17 order of evaluation: spills, and the purity rule

Counted over the C++ corpus (`cir-cpp-corpus`, rp and lp, 1867 cases) by a
print at each spill site in a debug front end, writer only:

| Spill (right operand evaluated first, kept in a local) | Occurrences | Cases |
| --- | ---: | ---: |
| built-in scalar assignment | 1245 | 164 |
| compound assignment | 143 | 25 |
| overloaded assignment or reversed comparison (`CALL_REVERSE`) | 45 | 18 |
| class assignment, address of the right operand | 14 | 9 |
| class assignment, value of a fixed prvalue | 3 | 3 |
| total | 1450 | 178 |

Each spill is five records (`LOCAL`, `LREF`, the value, `STORE INIT`, and `LREF`
`LOAD` later): about 12 stream bytes and, at -O0, one stack slot and a
store and a load. Under 1 spill per translation unit on average: the cost is
negligible; the cost is not the reason to change the rule, the shape is
(performance of the generated code is M5).

**Finding:** `expression_is_pure` treats every member selection as impure: the
second operand of `p->x` and `a.x` is an `enk_field` node, which is neither a
variable, a constant nor an operation, so the walk returns "impure" for any
expression that selects a member (`this->val = m` in a constructor is spilled).
The documented rule says member selections are pure. Treating the `enk_field`
node as a leaf, measured with the same counters, removes about half of the
scalar spills (1245 to 637), compound ones 143 to 65, and class ones 14 to 7;
the calls with `CALL_REVERSE` are unchanged (45). It changes the C++17 stream
shape of those cases (fewer locals), not their meaning, and it does not apply to
C or C++ before C++17. Left as it is for M5 (a stream shape change with no
benefit for correctness); the "unsequenced" diagnostic stays M6.

### Re-assessment (writer)

| Item | Status | Moves to | Evidence |
| --- | --- | --- | --- |
| `cpp1z/elide2` (DR 1697) | root-caused: the front end's IL gives the temporary an expression lifetime; the reference fails too | none (front end) | W10 |
| Region keep #2 (prototype instantiations) | the use that it was for is fixed in the front end; the keep is still needed (`gnats-26823`) | M5, with the memory work | W14 |
| Lambda keep | restricted (constexpr or conversion function); no effect on C++17 inputs | none | W14 table |
| C++17 purity rule | counted; the member-selection finding; no benefit for correctness | M5 | W15 table |
