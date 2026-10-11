# EDG ClangIR back end — handoff after M2 (session 1, 2026-10-09)

This document is the design guidance for the next session, in the same
spirit as `cir_design_guidance.md` (repo root), which remains the binding
brief. It covers:

- what was built and why;
- how to build and test it;
- what to do next, milestone by milestone;
- what to look at again.

Read it first, then `cir_src/doc/design.md`, `cir_src/doc/ecil_format.md`
and `cir_src/doc/writer.md`. The approved plan for session 1 is in
`~/.claude/plans/we-want-to-add-pure-snowflake.md`.

## 0. Where things stand

**Commits** (branch `cir`; never pushed):

| Commit | Gate | Contents |
| --- | --- | --- |
| `c8f11f51f5` | R | Design, ECIL spec v0.3, contract headers, toolchain spike, research notes |
| `fee14b0c21` | M0 | `BACK_END_TYPE` configuration macro (pure refactor, proven identical) |
| `f4609f1cb2` | M1 | `cpfe-cir` target, ECIL writer, front-end streaming hooks, `cir_src` library skeleton, tooling |
| *(M2 commit)* | M2 | v0 writer completed, EDG-CIRGen for C, corpus tooling, expectations, this document |

**Format and toolchain**
- The format is at ECIL **0.6**: `ECIL_VERSION_MAJOR 0`, `ECIL_VERSION_MINOR 6`.
- llvm-project is pinned at `894bfff5ca9fbe8ffc4171d5f5a4e68d11537f6d` (main, 2026-10-09, clang 24.0.0git), recorded in `cir_src/LLVM_REVISION`.
- The toolchain image is `edgcpp/cir-env:latest` on the x86-64 box.

**M2 results** (filled in from the gate-evidence run at the M2 commit; see
§9):
- C corpus (`imported/gnu/c` rp/lp tests: top level, `torture/`,
  `tree-ssa/`, `ipa/`; 2561 cases), `edg_x86_64_cir`:
  - **1599 pass (62.4%)**, with **0 FE crashes and 0 library crashes**.
  - Excluding the 506 failures that also fail under the C-generating
    `edg_x86_64`, **1599/2055 = 77.8%** pass.
  - Failure buckets: UNSUPPORTED 572, FE_DIAG 168 (all also fail in
    `edg_x86_64`), LINK_ERROR 118 (84 are `link_error` tests that need the
    optimizer, failing in both configs), RUN_FAIL 104 (100 fail in
    `edg_x86_64` too).
  - Top unsupported reasons: inline asm 93, vector types 57, integer types
    over 64 bits / `_BitInt` 55, operations on opaque scalars 47, complex
    30, VLA 27.
- The `cir-c-smoke` list (40 tests): **40/40 in both `edg_x86_64_cir` and
  `edg_x86_64_cir_2p`**.
- Writer sweep over the 2564 corpus files: 0 crashes, 0 invalid streams,
  0 nondeterministic streams, 0 value-stack violations, 0 ASan reports.
  1893 files are clean, 458 hit diagnosed unsupported constructs, and 213
  fail in the front end itself (they fail under `edg_x86_64` too).
- **Memory.** A 20,000-function TU with the debug ASan front end (quarantine
  not yet disabled):

  | Configuration | Peak RSS |
  | --- | --- |
  | Two-process: front end | 522 MB |
  | Two-process: `edg-cir` | 1321 MB |
  | Plain `cpfe` (C generation) | 782 MB |
  | In-process | 2553 MB |

  - Per-function streaming makes the front end *smaller* than the
    C-generating front end.
  - In-process is large because the whole MLIR module stays resident until
    finish (§6, M5).
  - With the ASan quarantine off, streaming used about 45% less front-end
    memory than `--ecil_keep_regions` (103 vs 185 MB, 6000 functions).
  - **Gate measurement, non-ASan release build**, 6000-function generated
    TU (78k lines):

    | Configuration | Time | Peak RSS |
    | --- | --- | --- |
    | Two-process: front end | 0.26 s | 69 MB |
    | Two-process: `edg-cir` | 4.3 s | 433 MB |
    | In-process streaming | 4.7 s | 478 MB |
    | `--ecil_keep_regions` | 4.8 s | 543 MB |
    | Release C-generating `cpfe` | 6.8 s | 531 MB |

    ASan build with the quarantine off: in-process 698 MB, keep-regions
    815 MB, two-process front end 116 MB.
- **Stack safety.** A 50k-term `+` chain, a 20k-rung else-if ladder,
  5k-deep initializers and 2k nested blocks all compile through `edg-cir`
  under `ulimit -s 256`. The front end itself needs a large stack for such
  inputs; that is a pre-existing front-end property.
- **Non-CIR configurations unchanged.** The preprocessed output of every
  cpfe, cpfe-cp, cdisp and util TU is identical to M0 for four presets,
  except the two new diagnostics. Day-suite statuses are identical to M0
  (45,432 tests in each of `edg_x86_64` and `edg_x86_64_cp`).
- **In-process vs two-process.** The generated assembly is byte-identical
  for all 2564 corpus files.
  - 24 files differ only in diagnostic order or count, or in exit status
    (4 vs 2). The in-process front end stops at its `-e 200` error limit;
    `edg-cir` has no limit.
  - These are documented exceptions.

## 1. What exists (architecture as implemented)

```
cpfe-cir (C++14 FE)                                libedg_cir.so (C++17, -O2 -g)
 parse → non-lowered IL                             lib/format  reader, validator (strict)
 pop_scope → check_for_done_with_memory_region      lib/cirgen  EDG-CIRGen frame machine
   → ecil_hand_off_region (stream; free if allowed) lib/driver  C API, worker thread,
 ecil_write/type/const/code.c (heap work stacks)               runCIRToCIRPasses → LLVM → .s/.o
 ecil_sink.c: file | stdout | in-process ─frames──► edg_cir_session_consume
 cir_be.c: back_end() = finish; diagnostics queue  tools: edg-cir, ecil-dump, ecil-asm
```

The only shared files are `cir_src/include/edg_cir/{ecil_format.h,edg_cir.h}`.
`edg-cir-check-isolation` enforces this (5 checks).

| Area | Files | Notes |
| --- | --- | --- |
| Back-end selection | `src/back_end_type.h` (from `basics.h`) | `BACK_END_TYPE_{C_GEN=1, CP_GEN=2, CIR=3, EXTERNAL=4}`; 0 is an error, so a misspelled name in `#if` is caught. `BACK_END_IS_*` are derived (`il_def.h` still uses them). Also `BACK_END_GENERATES_SOURCE` and `BACK_END_HAS_OUTPUT_FILE` |
| CIR config | `cmake/macro-conf/*/cpfe-cir.cmakedef`, `src/cmake/cpfe-cir/`, `host_envir.h`/`lang_feat.h`/`targ_def.h` CIR arms | `DO_IL_LOWERING=0`, `IL_SHOULD_BE_WRITTEN_TO_FILE=0`, `FREE_MEMORY_REGIONS_EARLY=1`, `MAINTAIN_NEEDED_FLAGS=0`, `NEED_NAME_MANGLING=1`, `MANGLE_ALL_NAMES=0`, `AUTOMATIC_TEMPLATE_INSTANTIATION=0` + `INSTANTIATE_TEMPLATES_EVERYWHERE_USED=1`, `IA64_ABI=1`, MS extensions off, `COROUTINE_ENABLING_POSSIBLE=0`; `#error`s guard every incompatible combination. `IL_REGIONS_DISPOSED_INCREMENTALLY` = IL file or CIR |
| Options | `cmd_line.c` | `--ecil_output=<file>` (`-` = stdout), `--ecil_keep_regions` (also implied by `--il_display`), `--gen_c_file_name` / `--old_line_commands` accepted (eccp passes them). C++, PCH and modules are rejected; C++ only when a source would be compiled to IL. Diagnostics `cir_error` (3600) / `cir_warning` (3601) |
| Writer | `src/ecil_write.{c,h}`, `ecil_type.c`, `ecil_const.c`, `ecil_code.c`, `ecil_sink.c`, `cir_be.{c,h}` | About 8.7k lines; documented in `cir_src/doc/writer.md` |
| Streaming hooks | `mem_manage.c` (`hand_off_memory_region`, stream vs free), `scope_stk.c` (late bodies), `fe_wrapup.c` (`F_GLOBALS`/`F_END`, free-all), `fe_init.c` (open), `error.c` (`ecil_abort`), `mem_tables.h` (`ecil_id` prefix slot) | All CIR-gated |
| Library | `cir_src/lib/{format,cirgen,driver}`, `tools/`, `runtime/edg_cir_rt` (placeholder) | About 11k lines; standalone CMake project (`cir_src/CMakeLists.txt`, own presets), exports exactly 8 `edg_cir_*` symbols (version script) |
| Tests | `cir_src/test/{format,driver,cirgen,lit}`, `cir_src/test/writer/*`, `tests/tests/cir/writer/*.sft.c` | 33 lit tests (many execute), 86 format fixtures, 18 writer goldens; corpus filters `tests/.edgy/filters/cir-c-{corpus,smoke}.txt` |
| Tooling | `dev_tools/bin/edg-cir-*`, `cpfe-cir-2p`, `edg-build-llvm-cir`, `edg-macro-identity-check`, `dev_tools/docker/{llvm-cir,cir-env}`, `bases/docker/cir-env/gcc` | See §3 |

## 2. Decisions taken (keep unless there is a reason; rationale in `design.md`)

1. **Non-lowered IL only.** Lowering-only artifacts (vtables, ctor/dtor
   variants, typeinfo, guards, cleanups) are built in `cir_src` from FE
   facts. The FE sends what EDG computes (layout, offsets, mangled names,
   virtual function numbers); the library computes Itanium mechanics.
2. **Wire format.** ECIL is length-prefixed frames of MessagePack-like
   little-endian tagged records:
   - It is forward-only and pointer-free.
   - Post-order expressions, bracketed control flow, flat arrays: no
     recursion is needed to decode.
   - Optional data goes in supp blobs or `EXT` records, flagged by stream
     feature bits (0–15 ignorable, 16–31 required) and the frame flag
     `HAS_SUPP`.
   - Minor versions only add things. A reader accepts any minor and fails
     only on unknown opcodes, enum values, flag bits or required feature
     bits.
   - **The library validates strictly; writer contract violations are format
     errors, never accommodated.**
3. **Per-function streaming = region discard.**
   - Every finished function is streamed from
     `check_for_done_with_memory_region`.
   - The region is freed only if the existing keep tests allow it.
   - Kept regions (constexpr; C functions with local struct types) are
     streamed at pop when finished.
   - A bitmap indexed by `function_def_number` prevents re-streaming.
   - `MAINTAIN_NEEDED_FLAGS=0`: dead internal functions are removed by LLVM
     GlobalDCE, which runs at every optimization level.
4. **IDs and names.**
   - G-ids and L-ids are dense integers; sids intern strings.
   - The `ecil_id` slot in the IL entry prefix is reset by
     `clear_il_entry_prefix`.
   - One G-id per symbol name: the writer deduplicates, e.g.
     `__builtin_memcpy` → `memcpy`.
   - Declarations may be re-issued to update a type. The consumer retypes and
     fixes earlier uses without recursive symbol walks.
   - `NAME_BIND`/`F_NAMES` exist for late (mangled) names but are unused
     in C.
5. **ABI.**
   - The library emits plain signatures with records by value.
     CallConvLowering at the pin does x86-64 SysV only; other triples are
     rejected.
   - Unprototyped and mismatched calls are bitcast to a zero-fixed-parameter
     variadic function type and called indirectly, as clang does.
   - Aggregate call results go through `agg.tmp` plus `cir.copy`, and
     aggregate returns through one `__retval` slot. Both work around
     CallConvLowering's NRVO forwarding.
   - All allocas are placed at function entry.
6. **Build isolation.**
   - `cir_src` is never added to the FE's CMake tree. It is built separately
     (RelWithDebInfo) and found through a hand-written `EdgCirConfig.cmake`.
   - The FE links `libedg_cir.so.0` with `LINK_DEPENDS_NO_SHARED`.
   - Touching FE headers rebuilds no library code; rebuilding the library
     relinks nothing in the FE.
7. **Threading and robustness.**
   - Decoding and op construction run on the caller's thread.
   - Every MLIR walk, verify, pass and destruction runs on a 256 MiB worker.
   - Diagnostics are queued to the calling thread, and callbacks never
     re-enter.
   - LLVM fatal errors and crashes are converted to `EDG_CIR_ERROR_INTERNAL`
     by crash recovery under one mutex (about 2.3 µs per consumed frame).
   - A poisoned session leaks its MLIR state on purpose.
8. **Test runtime.** CIR output links libstdc++/libsupc++/libgcc_s plus
   `edg_cir_rt`, never `edgrt` (which uses setjmp EH and its own
   `__dynamic_cast`). The cir-env EDG base uses gcc to assemble and link;
   `.prefalign` is avoided with `DisableIntegratedAS` for asm output.
9. **Team process.** An Opus orchestrator, three Sonnet implementers with
   exclusive file ownership (cir-config, cir-writer, cir-lib) and a
   read-only Opus reviewer. There is a reviewer gate per milestone, and one
   commit per approved gate on the local `cir` branch; never push.

## 3. How to build, run and test (cookbook)

**Box access** (x86-64 Ubuntu 24.04, 16 threads; the user opens the master
socket):
`ssh -fNM -o ControlPath=~/.ssh/cm-edgbox -o ControlPersist=12h mborland@192.168.1.167`.
Docker on the box needs `sg docker -c …`. Each agent works in its own tree:
`~/edg-cir-config`, `~/edg-cir-writer`, `~/edg-cir-lib`.

**Toolchain**
- `dev_tools/bin/edg-build-llvm-cir <src> <build> <prefix>` builds the
  toolchain, about 27 minutes on the box.
- The images are `edgcpp/llvm-cir` and `edgcpp/cir-env` (`FROM dev-env`,
  prefix `/edg/llvm-cir`, LLVM bin not on PATH).
- To bump the pin: change `cir_src/LLVM_REVISION`, rebuild, run
  `ninja check-clang-cir`, rebuild the images, then run the cir posture and
  the gate evidence.
- Prefer to move to `release/24.x` when it branches (user decision).

**Builds**
- Library:
  `edg-exec --img cir-env -- cmake -S cir_src --preset linux-cir-relwithdebinfo`,
  which builds into `build/cir`. Other presets: `linux-cir-release`,
  `linux-cir-asan`, `cir-format-only` (Mac, no LLVM).
- Front end: the `linux-gcc-debug-cir` preset (`build/gcc-cir`, ASan,
  library linked) or `linux-gcc-release-cir` (non-ASan, for timing/RSS).
- Mac writer-only loop: `macos-arm-clang-debug-cir` (`build/clang-cir`).
  Its streams carry an aarch64 triple and are not authoritative.

**Running**
- In-process: `eccp --c11 x.c` with the cir-env base (cpfe-cir writes
  `.int.s`, gcc assembles), or `cpfe-cir --c11 --gen_c_file_name=x.s x.c`.
  **Always pass a C mode: the bare `cpfe-cir` defaults to C++, which is
  rejected.**
- Two-process: `cpfe-cir --c11 --ecil_output=x.ecil x.c`, then
  `edg-cir --emit=asm|obj|cir|cir-lowered|llvm -o out x.ecil`.
- Inspecting streams: `ecil-dump x.ecil` (validates);
  `cir_src/test/writer/tools/stackcheck.py`.
- Library debug aids: `EDG_CIR_TEE=<file>`, `EDG_CIR_NO_VERIFY=1`,
  `EDG_CIR_DEBUG_NOTES=1`.

**Tests**
- `edg-docker-test --non-interactive --posture cir [filter]`:
  - builds `cir_src`, `cpfe-cir` and the util tools in cir-env;
  - runs the configs `edg_x86_64_cir` and `edg_x86_64_cir_2p` (not in the
    default sets);
  - uses a 60 s per-program timeout and kills orphans.
- Corpus: `--types rp,lp @tests/.edgy/filters/cir-c-corpus.txt`, then
  `dev_tools/bin/edg-cir-summary <run>`. It reports overall and
  CIR-specific results against `tests/expectations/edg_x86_64`.
- Writer goldens: `tests/tests/cir/writer` (`//script:edg-cir-ecil-dump`,
  `//require:BACK_END_TYPE 3`). **Re-record only on the x86-64 box.**
- Library: `ctest` in each cir_src preset. That covers the lit tests
  (`cir_src/test/lit.cfg.py`; lit comes from the llvm-project source tree
  on the box) and `capi_tests`.
- Gate evidence:
  `dev_tools/bin/edg-cir-gate-evidence --tree <box tree> --identity-baseline <M0 capture> --day-baseline-run <M0 day run>`.
  It runs build, identity, isolation, smoke, corpus, modes, stress, rss, day
  and ctest, prints a PASS/FAIL table, and can resume with `--out`.
- Writer tools (`cir_src/test/writer/tools/README.md`): the corpus sweep,
  `cmp_run.sh` (GCC vs CIR execution comparison) and `rt_run.sh`.
- Library scripts: `cir_src/scripts/*`, i.e. layout comparison with clang,
  `cir-compare.py`, the stress generator and the gdb replay.

## 4. Known gaps in v0 (all diagnosed, none silent; verify against §9)

**Not translated** (diagnosed "unsupported in CIR back end"; the function
or variable is dropped, and dropped definitions are errors unless
`EDG_CIR_FLAG_KEEP_GOING`):
- VLAs, `_Complex`, vectors, `_Atomic`, `_BitInt` (also as bit-fields; the
  record becomes opaque storage);
- inline asm, GNU statement expressions, computed goto, nested functions;
- the constructor/destructor/alias/ifunc/weakref/cleanup attributes;
- setjmp/longjmp builtins, `__sync_*`/`__atomic_*`, and builtins with no
  mapping;
- C23 `_FloatN` constants, wide-string-then-elements initializers,
  flexible-array-member initializers, and a GNU range designator followed by
  more initializers;
- C++ (rejected outright).

**Approximations:**
- **Braced override vs designator** is handled (`explicit_braces_on_aggregate`).
  Wide-string-then-element initializers are still unsupported (`c99-init-1`
  `l`).
- **`__builtin_object_size`** falls back to the GCC "unknown" value. It
  needs an `object_size` builtin plus inlining (pr39343).
- **Optimization.** The library runs the LLVM pipeline at the requested
  opt level (C tests use -O0). No CIR simplification passes are enabled.
  Tests relying on `link_error` elimination fail under both C configs.
- **Ignored attributes.** `used`, `section`, `transparent_union` and
  `ms_abi` are silently ignored. With GlobalDCE at -O0 a `used` static can
  be deleted.
- **`-fwrapv`** is not plumbed: the FE has no option and nsw is always
  emitted. `EDG_CIR_WRAPV=1` sets it in CHECKING builds only.
- **Function attributes.** Only `nothrow` is emitted (definitions; calls in
  C). There is no noreturn attribute, no inline hints and no visibility.
  Supplementary keys are never produced or consumed.
- **Debug info:** none. This is a CIR limitation at the pin.
- **`cir.record_layouts`.** Every record is `can_pass_in_regs` with a
  trivial destructor, and `data_size` is always equal to `size`. That is
  right for C and wrong for C++.
- **`_Float16`/`_Float128`** reach the library as half/quad arithmetic.
  Behavior is untested beyond the corpus.

## 5. Needs re-look (from the M1/M2 reviews and the implementers)

**Writer**
- **Bounded recursion.** `emit_leaf → ecil_variable_id →
  ecil_define_variable → ecil_emit_constant` recurses once per level of
  compound literals nested in static initializers. Convert it to a
  work-stack item; it breaks the brief's no-recursion rule in principle.
- **For-init scoping.** The writer emits the init declaration in a `BLOCK`
  that closes before `FOR_COND`. The library compensates by putting allocas
  at entry. This **must be fixed before C++ lifetimes** (M3): keep the init
  scope open over the loop.
- **Type completion across frames.** Pending records are now completed at
  the start of every function frame. Records whose bodies failed are marked
  failed and their uses diagnosed. Re-review this when C++ adds templates.
- **The 6.7.9 normaliser** (merge vs replace; union "last wins"; dynamic
  merged path): spot-checked with `cmp_run.sh`. Build a differential fuzz
  against gcc for initializers.
- **Merge vs replace in initializers** was resolved in the last M2 round.
  - The IL constant carries `explicit_braces_on_aggregate`.
  - A later brace-enclosed initializer replaces the subobject; a continued
    designator path merges.
  - `c99-init-1`'s `m` and `{.s.b=1, .s={2}}` now match GCC.
  - Still worth a differential initializer fuzz against gcc.
- **Unsupported internal entities and inline-only bodies.** An entity
  referenced only from a removed C99 inline-only body keeps
  `referenced == true`, so an unsupported internal entity in that position
  fails with "still referenced".
- **Re-issue after a body is streamed.** The defined type is kept, and the
  library rejects later type changes. Templates and inline functions in M3
  will stress this.
- **The libc builtin list** (`ecil_type.c`) is hand-kept, because the FE's
  builtin tables don't mark libc functions. Consider adding a flag to the
  FE's builtin table; that is an FE change and needs a decision.
- **Triple derivation.** The OS comes from the FE build host, so a
  cross-configured FE would be mislabelled (the library rejects it). Derive
  it from FE target macros if cross configurations matter.
- **Kept regions that can still grow** (`do_not_free_memory_region`,
  generic lambdas) are streamed at pop and later additions are swept at end
  of TU. Revisit at M3 against "regions that can still grow are not
  streamed yet".
- **The late-body hook's guard** compares `curr_il_region_number` with the
  innermost function's region, not the region of `n`. It matters for C++
  only; revisit at M3.

**Library**
- **Entry-block allocas** lose slot reuse and scoped lifetime markers.
  Revisit with the writer's for-init fix.
- **`agg.tmp` + single `__retval`** cost an extra copy per aggregate call
  or return. Revisit when upstream CallConvLowering NRVO is fixed, or if
  profiles show it. This interacts with C++ NRVO and copy elision.
- **Quadratic verifier at depth.** The MLIR verifier's isolated-from-above
  check is quadratic in region nesting (20k nested ifs take about 49 s).
  Real code is fine; consider unstructured emission past a depth
  threshold.
- **The in-process module stays resident until finish** (2.5 GB on 20k
  functions). M5: lower and erase functions in batches, or stream to
  per-function submodules.
- **`CIRCompat.h` churn coverage.** Several ops are still called directly
  (signbit, is_fp_class, the overflow ops, the bit ops). Route new ones
  through `CIRCompat.h` before an LLVM bump.
- **Strict validation** is deliberate. The validator does not track operand
  types, so some contract checks exist only in the emitter (e.g. bitcast
  ptr↔int).

**Both sides**
- **128-bit integers.** The corpus shows "integer type of 128 bits is not
  supported" (part of the 55 "integer type of N bits" cases). CIR has
  `!cir.int<s|u,128>` and `__int128` is common in real code. Find which
  side rejects it and support it, along with `bswap128`.
- **`_Float16`/`_Float128` arithmetic** (half/quad), and their constants:
  decide support or diagnosis explicitly.

**Configuration and tooling**
- **Orphan killing.** The opt-in per-program timeout and orphan killing
  (`EDG_TEST_EXEC_TIMEOUT`, `EDG_TEST_KILL_ON_CANCEL`) are enabled only for
  the CIR configs. Making orphan killing unconditional is a one-line
  change; it needs a user decision because it affects shared tooling.
- **FE-only test types** (fp/fn/fc) still run the CIR back end through
  `eccp --cpfe_only`. That is useful as a crash corpus (11,254 fp tests in
  `imported/gnu/c`), but they cannot be pass-rate tests.
- **Recordings.** `edg_x86_64_cir` expectations are recorded for the corpus
  (M2). Per-config recordings exist where output legitimately differs, e.g.
  the link-error file name `Test_name.c` vs `.int.c`.
- **macOS universal and Linux `-static` release builds** of `cpfe-cir` are
  not supported yet (M6).

**Tests still thin**
- The writer goldens check stream structure. Semantic coverage comes from
  `cmp_run.sh`, the probes and the corpus.
- About 26 corpus tests with recordings over 100 KB carry OUTPUT MISMATCH
  expectations instead of recordings, so they have no output regression
  coverage.
- The two-process config has separate expectations only for the smoke
  list. The 24 diagnostic/exit-status exceptions are documented, not
  recorded.

**Spec**
- `data_size` is unused.
- The `va_list` type role is never emitted.
- `NAME_BIND`, `F_NAMES`, `REFS` and `RETRACT` are specified but unused by
  the C writer.
- The library ignores `RETRACT`.
- About 16 GNU builtins were added to v0, because glibc headers and the
  corpus need them. That stretches brief item 7 ("standard first"); the
  user should accept it explicitly or move them behind a flag.

## 6. Future milestones (guidance)

### M3 — core C++ (ECIL v1)
- **Language and config.** Lift the C++ rejection once the pieces below
  exist; until then keep it.
- **Writer.**
  - **Names.** Mangled names through the non-mutating getters
    (`get_mangled_function_name_full`, `get_mangled_variable_name`), copied
    at once. Names that are not final yet (`must_wait_for_mangling_reasons`)
    go through `NAME_BIND`/`F_NAMES`. Note that
    `lowering_delayed_on_nested_function` exists only under
    `DO_IL_LOWERING`; use a writer-owned bitmap.
  - **References and members.** References, member functions, `this`, and
    access to base subobjects with FE-computed offsets.
  - **Constructors and destructors.** `MEMINIT` records for constructor
    initializer lists, with virtual-base inits tagged. Ctor/dtor variant
    names come from a new helper beside `get_mangled_function_name_full`
    (pass `ctor_dtor_kind`).
  - **Object lifetimes.** `FULLEXPR_BEGIN/END`, `TEMP_BEGIN/END`, and
    `CLEANUP_PUSH` after each construction that has a destructor (spec
    reserved range 0x60–0x6F; define the opcodes in v1). Fix the for-init
    scoping first. `GOTO`/`BREAK`/`CONTINUE` need cleanup counts.
  - **Statics and globals.** Guarded statics (`cir.local_init`); global
    dynamic init through `cir.global` ctor regions (LoweringPrepare
    outlines them).
  - **Templates.** `INSTANTIATE_TEMPLATES_EVERYWHERE_USED` with
    `linkonce_odr`/COMDAT linkage. Filter prototype instantiations
    yourself; the `ignore_*_in_back_end` macros are FALSE without lowering.
  - **Retraction.** `remove_unneeded_instantiations` → `RETRACT`. The
    library must implement it: drop the definition if it is unreferenced.
  - **Lambdas.** Closure records plus member functions; FUNC_BEGIN inside
    a region for the lambda body.
- **Library.**
  - **Cleanups.** A cleanup-scope stack mapped to `cir.cleanup.scope`;
    ctor/dtor variant emission (C1/C2/D0/D1/D2) from one streamed body.
  - **`cir.record_layouts`** with real `arg_passing_kind` and trivial-dtor
    values (non-trivial records are passed indirectly), `empty_for_abi` for
    empty bases, and `is_class`.
  - **Calls.** C++ `nothrow` call attributes; references as pointers.
- **Tests.**
  - A C++ run-pass corpus, `imported/gnu/cpp` and the EDG suites, filtered
    to features in scope.
  - Differential testing against clang's CIR output (`cir-compare.py`) and
    the C++-generating back end.

### M4 — virtual functions, RTTI, EH (ECIL v2)
- **Vtables.** The library computes vtable groups, VTT/construction
  vtables, vcall/vbase/offset-to-top and thunks (Itanium). It uses FE
  facts: virtual function numbers, overrider lists, primary base, vptr
  offset, key function from `inline_in_class_definition` (not
  `vtbl_decider_function_for_class`).
- **Derived symbols.** The `_ZTV`/`_ZTI`/`_ZTS`/`_ZTT` names are derived
  from the class encoding.
- **RTTI and casts.** `typeid`, `dynamic_cast` (`cir.dyn_cast`) and
  typeinfo globals.
- **Exceptions.** `cir.try`/`cir.throw`/catch, the EH paths of cleanups,
  noexcept termination, function-try-blocks.
- **Runtime library.** `edg_cir_rt` gains the gaps between EDG's headers
  and libstdc++ (e.g. `type_info::name`).
- **ABI verification.** A differential ABI corpus against clang,
  `-fdump-vtable-layouts` and the C-generating back end.

### M5 — performance and streaming
- **Supplementary information** (`supp` keys): nothrow, branch hints,
  pure/const, typedef names, visibility/section/used, alignment
  assumptions. Writers emit them; the library maps them to CIR/LLVM
  attributes.
- **Stable cross-TU IDs** through `NAME_BIND` (the ODR stretch goal).
- **REFS-driven lazy decode.** Emit shared definitions in `F_DECLS` so
  `F_REGION` frames become skippable.
- **Bounded memory in the library.** Batch-lower and erase functions so
  in-process peak memory stays bounded (today the module is resident).
- **Concurrency groundwork.** The sink API already transfers buffer
  ownership. Add buffer pools, a consumer thread, unix sockets and
  io_uring. **Measure first:** the in-process streaming run was slower than
  `--ecil_keep_regions` on the ASan build (127 s vs 77 s); re-measure with
  the non-ASan preset.

### M6 — release and platforms
- **Release builds.** A merged static archive for Linux `-static` release
  builds of `cpfe-cir`, and a decision on macOS universal builds.
- **Other targets.** AArch64/Apple Silicon and Windows only once upstream
  CallConvLowering supports them. The FE target ABI variables are kept only
  for C-gen with lowering today; check them for other targets.
- **Toolchain.** Move to `release/24.x` and re-validate.

### Extensions (after standard C/C++)
- GNU: statement expressions, vectors, `_Complex`, atomics, inline asm,
  attributes such as `used`/`section`/`alias`/`constructor`.
- MSVC: everything.
- Each extension needs an explicit format addition (minor bump) and its own
  review.

## 7. Process notes for the next session

- **Agent types.** Agent definitions are in `~/.claude/agents/cir-{config,writer,lib,review}.md`.
  New definitions only load at session start; mid-session, fall back to
  `general-purpose` with model sonnet for implementers and `Plan` with model
  opus for the reviewer.
- **Ownership.** File ownership is exclusive. Contract headers and spec docs
  belong to the orchestrator, and every contract change bumps the minor
  version.
- **Process hygiene.** No broad `pkill`/`docker stop` on the box or the Mac,
  because other jobs (std-big-int timing, `~/edg-compiler`) share the
  account. Name containers `cir-<agent>-<job>` and stop by exact ID.
- **Temporary files.** Agents share `/tmp` on both machines and clobbered
  each other's scripts; keep scratch under `build/` (gitignored) or the
  agent's own box tree.
- **Commits.** Pause the implementers at a green checkpoint before each
  gate commit. Never commit `cir_design_guidance.md`; it is the user's
  untracked brief. `cir_src/.gitignore` ignores `__pycache__`.
- **Memory measurements.** The ASan quarantine distorts RSS; use the
  `linux-gcc-release-cir` preset or `ASAN_OPTIONS=quarantine_size_mb=0`.
- **Root-owned files.** Posture containers leave root-owned files in
  `build/`; `cir_src/scripts/box.sh chown` fixes that.

## 8. Open questions for the user

1. **GNU builtins in v0.** Accept the roughly 16 GNU builtins as part of
   v0? They are needed by glibc's `<math.h>`/byteswap macros and by the
   corpus.

I ACCEPT

2. **Orphan killing.** Make test-harness orphan killing (and a default
   per-program timeout) unconditional for all configs?

YES

3. **libc flag in the builtin tables.** Add a "libc function" flag to the
   FE builtin tables, so the writer's library-builtin rule is derived
   rather than hand-listed? This is an FE change.

YES

4. **Next milestone.** Priority after M2: M3 (C++) as planned, or M5
   memory/concurrency first? The in-process memory result argues for
   per-function lowering early.

M3

## 9. M2 gate evidence (current tree, box, clean rebuild; `~/edg-cir-config/gate-m2b`)

| Step | Result |
| --- | --- |
| build | PASS |
| identity vs M0 (4 presets) | PASS: IDENTICAL 720/720 (only the two new diagnostics allowed) |
| isolation | PASS: 5/5 |
| smoke | PASS: 40/40 `edg_x86_64_cir`, 40/40 `edg_x86_64_cir_2p` |
| corpus | PASS: 1599/2561 (62.4%); FE_CRASH 0, LIB_CRASH 0; 77.8% excluding reference failures |
| modes | 0 assembly differences over 2564; 24 diagnostic/exit-status exceptions (documented) |
| stress | PASS (two-process, `edg-cir` under `ulimit -s 256`) |
| rss | PASS (table in §0) |
| day suite vs M0 | PASS: 45,432 tests per default config, statuses identical |
| ctest | PASS: 4 presets |

**Reviews**

| Gate | Verdict | Notes |
| --- | --- | --- |
| R | APPROVE WITH FOLLOW-UPS | After one BLOCK round |
| M0 | APPROVE WITH FOLLOW-UPS | |
| M1 | APPROVE WITH FOLLOW-UPS | After one BLOCK round: post-wrap-up diagnostic use-after-free, duplicate F_ABORT error, C++ accepted |
| M2 | APPROVE WITH FOLLOW-UPS | After one BLOCK round: initializer merge drop, ABI-opaque definitions, late record completion, tentative `T a[]`, bit-field compound promotion, plus three crash classes |

**Final check** on the frozen M2 tree (`~/edg-cir-config/gate-final`):
- build, identity (IDENTICAL 720/720) and isolation (5/5) pass;
- smoke is 40/40 in both configs;
- the corpus has 1599/2561 passing, 0 crashes and 0 status changes against
  the recorded expectations;
- the cir suite passes 21/21.

The expectations are `tests/expectations/edg_x86_64_cir/imported.log` plus
825 per-config recordings (3.9 MB). The 26 recordings over 100 KB are
deliberately excluded, and those tests carry OUTPUT MISMATCH statuses.

The final pre-commit items for M2 (pending-record completion isolation, a
real H1 test, failed-record propagation, the validator bit-field type
check) are in the M2 commit.
