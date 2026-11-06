# EDG ClangIR back end — handoff after M3 (session 2, 2026-10-11)

This document is the design guidance for the next session. It follows the
spirit of `cir_design_guidance.md` (repo root), which remains the binding
brief, and of `cir_src/doc/handoff.md`, the session-1 handoff. Read it first,
then:

- `cir_src/doc/design.md` (§10 has the M3 decisions);
- `cir_src/doc/ecil_format.md` (ECIL **1.0**; §11 is C++);
- `cir_src/doc/writer.md` (§9 is the C++ writer);
- the research notes cited below.

The approved session-2 plan is
`~/.claude/plans/in-our-last-session-distributed-hinton.md`.

## 0. Where things stand

**Commits** (branch `cir`; never pushed):

| Commit | Gate | Contents |
| --- | --- | --- |
| `c8f11f51f5` | R | Design, ECIL spec, contract headers, toolchain spike |
| `fee14b0c21` | M0 | `BACK_END_TYPE` configuration macro |
| `f4609f1cb2` | M1 | `cpfe-cir`, ECIL writer, library skeleton |
| `59a33cffa7` | M2 | A C subset through ClangIR on x86-64 |
| `501d0888e7` | M2.1 | Refactor and efficiency (writer, library, tooling) ahead of C++ |
| `908f7b680a` | M3a | The ECIL v1 C++ contract and research (spikes, IL map) |
| *(this commit)* | M3 | Core C++ through ClangIR; ECIL 1.0; this document |

**Format and toolchain**
- **ECIL 1.0** (`ECIL_VERSION_MAJOR 1`, `MINOR 0`) is frozen. 0.x streams are
  rejected. Later additions are 1.x minors; the first will be exceptions in
  M4.
- The llvm-project pin is unchanged: `894bfff5ca9f…` (main, 2026-10-09), in
  `cir_src/LLVM_REVISION`. Every CIR feature M3 needed exists at the pin.
- The toolchain image is still `edgcpp/cir-env:latest`, on the x86-64 box.

**M3 results** (final gate run, `~/edg-cir-config/m3-final`, evidence
`.tmp/checkpoint-20261011-124817`; details in §9):
- **C++ corpus** (`tests/.edgy/filters/cir-cpp-corpus.txt`: imported/gnu/cpp
  rp+lp minus out-of-scope directories, plus edg/ and cwg/; Microsoft-mode
  variants are skipped for cpfe-cir):
  - **980/1732 pass (56.6%)**. That is **64.0% of what `edg_x86_64` passes**
    (980/1532; 200 of the failures also fail in the reference).
  - **0 CIR-specific crashes**, 0 killed or timed out. The one FE crash,
    `DRs/dr2867-2`, is the same assertion in the reference.
  - The failures:

    | Bucket | Cases |
    | --- | ---: |
    | UNSUPPORTED (diagnosed) | 620 |
    | FE_DIAG | 71 |
    | RUN_FAIL | 30 (29 also fail in the reference) |
    | LINK_ERROR | 29 (23 in the reference) |
    | LIB_ERROR | 1 (the reference also rejects it) |
    | FE_CRASH | 1 (excused) |

  - By feature:
    - vtables and virtual functions: 243;
    - exceptions: 87 (`throw` 57, `try` 15, …);
    - by-value class parameters and results: 18;
    - member pointers: 16;
    - other: 358 (vector types 19, inline asm 16, and many small items).
  - The top reasons are virtual function (133), constructors and destructors
    of classes with virtual functions or bases (71), and `throw` (57).
    **M4 (virtual functions, EH, member pointers, RTTI) covers about 350
    of the failing cases.**
  - By language mode:

    | Mode | Pass |
    | --- | ---: |
    | c++03 | 1/5 |
    | c++11 | 202/351 |
    | c++14 | 53/74 |
    | c++17 | 41/67 |
    | c++20 | 50/94 |
    | c++23 | 20/30 |
    | c++26 | 2/3 |
    | default | 611/1108 |
- **C++ smoke** (`cir-cpp-smoke.txt`, 56 tests covering every M3 feature):
  **56/56** in `edg_x86_64_cir` and `_2p`, and through the release front end.
- **C corpus**: **1649/2561 pass (64.4%)**, +50 over M2.1 (1599). That is
  80.1% of what the reference passes. The +50 are the `__int128` tests.
  - There are 0 crashes and no PASS→FAIL against M2.1.
  - Assembly is identical to M3a for 2510 files. Of the 54 status changes, 51
    are `__int128` tests that now compile. The other 3 (`initpri1`,
    `initpri3`, `ipa/pr70306`) are now the `constructor`-attribute error;
    before, the attribute was silently ignored and the constructor never ran.
- **In-process vs two-process**: 0 assembly differences on both corpora (C
  2557 identical, C++ 1672 identical; the rest differ only in diagnostics).
- **Run tests** in `tests/tests/cir/cirgen/cxx-*.sft.cpp`: 13 real-FE C++
  programs that match g++ (ctor/dtor order, temporaries, NRVO address
  identity, statics, new/delete cookies, lambdas, thread_local). The one
  deliberate difference is `cxx-s3-statics`: initialization order follows
  clang.
- **C++ performance** (generated 6000-function C++ TU through the real FE,
  release preset, gate run):

  | Configuration | Instructions (M) | CPU s | Peak RSS MB |
  | --- | ---: | ---: | ---: |
  | 2p FE | 557.9 | 0.15 | 57 |
  | 2p edg-cir | 11,856 | 1.29 | 152 |
  | In-process | 12,394 | 1.51 | 186 |
  | `--ecil_keep_regions` | 12,406 | 1.51 | 198 |
  | C-gen `cpfe` only | 868 | 0.20 | 84 |
  | C-gen `cpfe` + gcc | — | 1.54 | 172 |

  - The streaming FE is lighter than C-gen's (57 MB vs 84 MB).
  - End to end, in-process CIR is about as fast as C-gen plus gcc at -O0.
- **C performance vs M2** (the 6000-function C TU):
  - in-process +0.41% Ir (4.75 s, 468 MB);
  - 2p FE −0.5% (63 MB, was 69);
  - over the corpus's common cases: FE −0.85% Ir, library +0.57%.
- **Stress** (`edg-cir` under `ulimit -s 256`): every C and C++ shape passes.
  - `cxx_temps:5000` takes 7.4 s and `cxx_locals:2000` 2.1 s; realistic
    shapes take well under 1 s.
  - The cost grows quadratically or cubically with nesting depth in the
    upstream verifier, passes and lowering, not in our emitter. See §5.
- **Non-CIR configurations unchanged**:
  - preprocessed output is IDENTICAL to M0 (720 files, 4 presets);
  - day-suite statuses are identical.

## 1. What exists (architecture as implemented)

```
cpfe-cir (C++14 FE; C and C++)                    libedg_cir.so (C++17, -O2 -g)
 parse → non-lowered IL (exceptions on)            lib/format  ecil_ids (dense id layer, string arena,
 pop_scope → check_for_done_with_memory_region                 declaration facts), a_bracket_tracker
   → stream F_REGION per function (+ lambdas)                  (scope regions, cleanup rules), validator
 ecil_write/type/const/code.c (heap work stacks)   lib/cirgen  frame machine: records with .base types,
   names: lower_name.c CIR helpers (C1/C2, ZZ,                 casts, cleanup scopes (conditional flags,
   TU-local, deterministic internal names)                     FULLEXPR spill), return slot, structors
 ecil_sink.c: file | stdout | in-process ─frames──►            (alias/C5/substitution), local_init and
 cir_be.c: back_end(); diagnostics; cir_be_error               global ctor/dtor regions, new/delete,
                                                               __int128, finish-time substitution map
                                                   lib/driver  C API, worker, CIR passes → LLVM → .s/.o
```

The shared files are still only
`cir_src/include/edg_cir/{ecil_format.h,edg_cir.h}`.

**What is new in session 2:**

| Area | Files | Notes |
| --- | --- | --- |
| Writer, C++ | `src/ecil_{code,type,const,write}.c/h`, `src/ecil_kind_names.h` (generated) | `writer.md` §9: structors, MEMINIT, lifetimes and cleanups, statics, new/delete, lambdas and invokers, range-for, structured bindings, 128-bit constants |
| Names | `src/lower_name.{c,h}` (CIR-gated) | `cir_structor_name`, `cir_local_static_name`, `cir_entity_is_tu_local`, deterministic internal manglings |
| FE hooks | `src/mem_manage.c`, `src/scope_stk.c` (CIR-gated) | Region keeps (prototype instantiations, `expr_node_refs`, lambda call operators); ASan poisoning of pooled blocks; `cir_name_may_change` |
| Library | `cir_src/lib/format/{ecil_ids,ecil_brackets}.*`, `lib/cirgen/*` | Single-pass validation (M2.1); all v1 records |
| Config | `cmake/macro-conf/default/cpfe-cir.cmakedef` (`tim_used`), `cmd_line.c` (C++ accepted), `bases/docker/cir-env/gcc/edg_eccp_config` (EDG headers like dev-env) | |
| Builtins | `src/builtin_libc.h` + `dev_tools/builtins/generate-libc-builtins.py` | The libc builtin list is derived from scrape data (user decision) |
| Tooling | `dev_tools/bin/edg-cir-{checkpoint,perf,rss,poison-check,writer-stress,record,summary}`, `edg-cir-gate-evidence` (new steps, crash rules), `edg-docker-test --posture cir-release` | §3 |
| Tests | `tests/tests/cir/writer/*.sft.{c,cpp}` (C and C++ goldens), `tests/tests/cir/cirgen/cxx-*.sft.cpp` (run tests), `cir_src/test/{cirgen,lit,format}` (91 lit tests, 207 format fixtures), probes in `cir_src/test/writer/probes` | Filters: `cir-cpp-{smoke,corpus}.txt` |

## 2. Decisions taken

Keep these unless there is a reason to change them. They add to `handoff.md`
§2.

1. **M2.1 before M3.** Refactors were proven against the C corpus with no
   behavior change. The library validates once, through a shared dense id
   layer. Renames at finish are one substitution walk.
2. **Exceptions** (user decision). The FE compiles C++ with exceptions **on**,
   and the writer sends a **no-exceptions** v1 stream:
   - functions with try or throw are diagnosed and dropped;
   - EH-only cleanups are sent as `eh` and dropped by the v1 library;
   - every function and call is `nothrow`.
   M4 adds a 1.x minor with exceptions.
3. **Names.**
   - Everything uses Itanium names. Internal entities, and entities
     individuated by EDG's module id, get deterministic Itanium names (`_ZL…`,
     `_ZN12_GLOBAL__N_1…`); there is no mtime/cwd module id (user decision).
   - A TU-local predicate (`cir_entity_is_tu_local`) forces internal linkage
     before the vague-linkage rules. This is essential: without the module id,
     identical names would otherwise merge distinct entities across TUs.
     Two-TU probes check it.
   - Late names are nil names plus `NAME_BIND` at the file-scope hand-off, with
     a CHECKING re-mangle.
4. **Linkage.**
   - `routine_might_exist_in_multiple_copies` and not static gives
     `linkonce_odr` + COMDAT; an explicit instantiation gives `weak_odr`.
   - Explicit specializations are external, except inline variable templates.
   - Vague linkage applies to definitions only. A dropped definition is
     re-issued as external.
5. **Structors.**
   - One body, streamed under C2/D2.
   - C1 is an alias for external linkage, a C5/D5 comdat plus alias for
     weak_odr, and a finish-time substitution for discardable linkage.
   - Array elements and `new` use C1/D1.
6. **Lifetimes.**
   - Cleanups are pushed right after construction and protect until the end
     of their scope region.
   - Conditional cleanups use flags, with the scope opened before the
     outermost conditional.
   - While a cleanup is open, value returns go through `__retval` (FlattenCFG
     sinks plain returns past cleanups).
   - The NRVO variable gets a normal cleanup, which the library guards with an
     nrvo flag.
7. **Copy elision.** `NONTRIVIAL_ABI` records follow exact store/load shapes
   (spike 1), and CallConvLowering handles sret and byval.
8. **Static init.**
   - `cir.local_init` and the `cir.global` ctor/dtor regions.
   - Guards are named from the variable name (`_ZGV`).
   - `UseInitArray=true`.
   - Initialization order is module order, which matches clang (and differs
     from gcc for some inline and template statics).
9. **Unsupported policy** (user decision, 2026-10-11).
   - An unreferenced dropped *function* with discardable linkage is erased
     with a warning.
   - Dropped variables and referenced or external dropped functions are
     errors.
   - Dropping a root is a `cir_error`. Roots are functions or variables with
     `constructor`, `destructor`, `used` or `init_priority`.
10. **libc builtins.** The list is in a separate generated header (user
    decision). The large builtin tables are untouched; their generator does
    not reproduce them (`research/builtin_table_regen.md`).
11. **Test harness.**
    - Orphan killing and a 120 s per-program timeout are unconditional (user
      decision).
    - The cir-env base uses EDG headers, like dev-env.
    - Microsoft-mode test variants are REQUIREMENTS NOT MET for cpfe-cir.
12. **Configurations.**
    - `DEFAULT_INSTANTIATION_MODE=tim_used` for cpfe-cir (user decision).
    - The test configs use the debug (`linux-gcc`) FE.
    - The shipping release FE (`linux-gcc-release`) has different macros; the
      `release-smoke` gate step covers it.
13. **Process.**
    - Slice checkpoints run `edg-cir-checkpoint` on a synced box tree.
    - Incremental reviews read a **frozen snapshot** (`build/review-snap-*`),
      not the live tree.
    - Gate commits happen on local `cir` only.

## 3. How to build, run and test (cookbook)

Box access, images and presets are unchanged (`handoff.md` §3). New in
session 2:

- **Slice checkpoint** (Mac, repo root, socket open):
  `PYTHONPATH=dev_tools/pylibs python3 dev_tools/bin/edg-cir-checkpoint --tree <name> [--steps …|--quick]`.
  - It syncs with `rsync --checksum --no-times`, wipes the CIR build
    directories, runs the gate steps in the background and prints the table.
  - The default steps are build, identity, smoke, corpus, asm-diff,
    writer-stress, cpp-smoke, cpp-writer-stress and ctest. The C++ and gate
    steps are added with `--steps`.
- **Gate evidence** (box): `edg-cir-gate-evidence --tree <tree> --with-cpp …`.
  The steps are:
  - build, identity, isolation, smoke, corpus, asm-diff (`--asm-baseline`),
    modes;
  - writer-stress (ASan FE ± `ECIL_GID_STRESS`), stress, rss, perf;
  - cpp-smoke, cpp-corpus, cpp-modes, cpp-writer-stress;
  - cpp-stress and cpp-stress-asan;
  - poison (poisoned vs unpoisoned FE streams);
  - release-smoke (the shipping macro configuration);
  - day and ctest.

  Crash rules:
  - a library crash always fails the step;
  - an FE crash is excused (INFO, listed in `excused-crashes.txt`) only when
    the reference crashes with the same class and the message names no
    `ecil_*`/`cir_be*` file;
  - killed and timed-out runs always fail the step.

  Useful flags:
  - `--smoke-all-pass`: C++ smoke must be 100%;
  - `--allow-status-changes`: for an asm-diff whose status changes are all
    explained.

  The M3 gate ran with edgy `-T 180`, so a hanging test program
  (`gnu/c/ssp-1`) reports as a normal FAIL instead of being cancelled.
- **C++ expectations**: `dev_tools/bin/edg-cir-record` records on the box with
  the 100 KB rule and fetches the result back. The procedure is in
  `research/test_plumbing.md` §11.
- **Performance**:
  - `edg-cir-perf` uses cachegrind Ir, because `perf` is blocked on the box.
  - `--only cxx` measures the generated C++ TU and the C-gen baseline;
    `--stream KIND:N` measures generated streams.
  - `edg-cir-rss --generate-cxx-functions N` writes the C++ source.
- **C++ stress**:
  `bash cir_src/scripts/box_stress.sh cxx_locals:2000 …` (in cir-env). The
  kinds are listed in `ecil-stress-gen.py`.
- **Probes against g++**:
  - `cir_src/test/writer/tools/cmp_cxx_box.sh`; set `FE_BUILD=gcc-release-cir`
    to use the release FE;
  - the multi-TU probes: `cmp_cxx_multi_box.sh`;
  - the clang CIR differential: `cir-compare.py --cxx`,
    `research/cxx_cir_differential.md`;
  - box drivers in `cir_src/scripts/box_*` and `cir-diff-classify.py` (run
    inside cir-env). Each has a usage header. The main ones:
    - `box_diff.sh`/`box_side.sh`: the clang shape differential;
    - `box_realfe.sh`: real-FE run tests against g++;
    - `box_runall.py`/`box_corpus.py`/`box_ccorpus.py`: corpus runs through
      the FE, library, link and run;
    - `box_reasons.py`: why corpus cases were dropped.
- **Debug aids**:
  - `ECIL_GID_STRESS=1` makes the CHECKING writer move and poison the G-id
    table;
  - ASan builds poison pooled FE regions;
  - `--stats` prints library statistics;
  - the generated `src/ecil_kind_names.h` gives kind names in diagnostics.
    Regenerate it with `cir_src/test/writer/tools/gen_kind_names.py` when
    `il_def.h` enums change.
- **Box disk hygiene.** Every checkpoint tree is 10–15 GB, and the disk filled
  once (ENOSPC killed a run). Prune old trees under `~/edg-cir-config`
  (`build/` and `tests/runs/`; keep `.tmp` evidence), and delete root-owned
  files through a container.

## 4. Known gaps

Everything below is diagnosed, except the short **Silent** list at the end of
this section.

**Unsupported in M3** (the body is dropped and the declaration kept; the
reason is named):
- **M4 work:**
  - virtual functions, virtual bases, polymorphic classes' ctors/dtors and
    constants (a vptr needs vtables);
  - exception handling (`try`, `throw`, function-try-blocks), `typeid`,
    `dynamic_cast`;
  - pointers to members.
- **Other C++ features:**
  - the built-in `<=>` and its synthesized operators;
  - coroutines;
  - `thread_local` with dynamic init at namespace or class scope;
  - a `for` with a condition declaration and an increment;
  - a conditionally lifetime-extended temporary;
  - a temporary bound to a reference member by a mem-initializer (`MEMINIT`
    ends its locals);
  - a class conditional used as an operand (not as an initializer);
  - a non-trivial class passed through `...`;
  - ctor-initializers of anonymous-union members;
  - a capture of a variable already captured by the enclosing closure, and
    of arrays of arrays of objects;
  - variadic lambda invokers;
  - destroying and type-aware `operator delete`;
  - `new` with a non-throwing allocation function;
  - `new[]` with a run-time count and an initializer list;
  - a for-init variable with a destructor, or a for-init reference to such
    a class, under `--old_for_init`;
  - C++17 order of evaluation, where the right operand must be evaluated
    first and cannot be spilled (§9.9 of `writer.md`):
    - a built-in assignment of a `NONTRIVIAL_ABI` class;
    - an overloaded assignment or reversed comparison whose right operand is
      a class passed by value that is not trivially copyable;
    - an `eval_right_to_left` call with more than two operands where any has
      side effects;
  - a constant initialization of a `[[no_unique_address]]` member;
  - libstdc++'s `std::map` and other node containers. Their `_Rb_tree`
    members use constructs that are not supported, mostly EH (see §6, M4).
- **GNU and Microsoft:**
  - `constructor`/`destructor`/`init_priority`, and target multiversioning
    (all `cir_error`);
  - Microsoft mode.
- **Library:** a switch on `__int128` with negative or wider-than-64-bit case
  labels (an upstream lowering limit).
- **Writer limits:** a constant `new[]` count of 2^32 elements or more.

**Documented differences from the gcc-built reference:**
- Falling off the end of a non-void C++ function traps, as clang does
  (`parse/crash78a`, `rma/mri_bug03`).
- `return;` in a non-void function (a GNU extension) returns an uninitialized
  value.
- Initialization order of inline and template statics, and static vs dynamic
  initialization choices, follow clang (`cxx-s3-statics`,
  `cpp0x/constexpr-static4`).
- In GNU mode the FE's IL does not lifetime-extend a scalar member of a
  temporary (`init/ref19`); default mode does.
- A gnu_inline function in GNU C++ mode is always inline-only: the IL cannot
  tell whether its definition is `extern`.
- `link_error` tests that rely on folding at -O0 fail. The reference fails
  them too (~90 across both corpora).
- C++17 assignment order follows the standard, as clang does. g++ differs in
  two places:
  - `*p = next(&p)` for a class: g++ stores through the old `p`;
  - `a[i++] += i`: g++ reads the new `i`.

  Both are golden-only (`writer-cxx-class-assign-order`,
  `writer-cxx-assign-order`), not g++ probes.

**Approximations:**
- No debug info, as at M2.
- The library emits no `noundef`/`nonnull`/`dereferenceable`, no ctor/dtor
  `func_info` and no `builtin`/`allocsize` on operator new/delete. These are
  optimization facts the stream does not carry yet (M5 supp keys).
- The writer streams every header inline function, referenced or not.
  GlobalDCE removes the unused ones, but they cost stream size and time (M5:
  REFS-driven lazy decode).

**Silent (not diagnosed; carried over from `handoff.md` §4):**
- The attributes `used`, `section`, `transparent_union` and `ms_abi` are
  ignored when the entity is otherwise supported. GlobalDCE at -O0
  (`lib/driver/pipeline.cpp`) can delete an unreferenced `used` static.
  - Dropping a `used` entity is now a `cir_error` (§2 item 9), but a
    supported one still loses the attribute.
  - Fix it with a declaration flag (a 1.x minor) that the library maps to
    `llvm.used`/`section`.

## 5. Needs re-look (from the session-2 reviews and implementers)

**Writer and FE**
- **Region keeps** (prototype instantiations, `expr_node_refs`, lambda call
  operators) are correctness fixes for FE code that assumes regions persist.
  Measure their RSS on large template TUs. The FE use-after-free in
  `find_binding_for_pack` (`gnats-27775-run`) is mitigated, not root-caused in
  FE terms.
- **OS capture.** Capture the triple's OS when the predefined macros are
  entered, not at the first hand-off. Today a source-level `#undef __linux__`
  mislabels the triple (an error, not a miscompile).
- **Pending records.** `ecil_complete_pending_records` has no dirty
  tracking, because the FE has no completion signal (it would need a
  `class_decl.c` hook).
- **Plausible concerns from the M3a review, not yet run-tested:**
  - P1: forwarding a `NONTRIVIAL_ABI` by-value parameter through
    CallConvLowering (static invokers, inheriting ctors);
  - P3: classification of trivially copyable records that reuse tail padding,
    or of empty classes passed by value;
  - P4: unnamed nested classes made internal (one copy of their inline
    members' statics per TU).
- **Open C++ items:**
  - `cpp1z/elide2` (DR1697 rvalue-reference member in aggregate init);
  - spaceship-synthesized operators;
  - nested-lambda capture (`gnats20538-lambda-3`);
  - range-for with `try` (M4);
  - W11, folding constant `if` conditions so the `link_error` tests pass;
  - W5, static vs dynamic initialization choices.
- **Conservative diagnoses added at the M3 gate.** Two gate fixes cost
  corpus cases; both could be implemented properly:
  - A for-init variable with a destructor under `--old_for_init` (cfront
    modes) is diagnosed (`edg/rma/lifetime25`, which was replaced in the
    smoke list by `lifetime11`). Better: hoist the declaration, its
    initialization and its `CLEANUP_BEGIN` before `FOR_BEGIN`, so the cleanup
    protects to the end of the enclosing scope.
  - A built-in assignment of a `NONTRIVIAL_ABI` class whose operands must be
    evaluated right first is diagnosed (`opt/vrp3`). Better: materialize the
    value as a temporary with its normal cleanup first.
- **`pr77550` (`--c++14`): LIB_ERROR** "the type of function changed after
  its body was emitted". The reference fails the test too, and it is
  diagnosed. It shows the writer re-declaring a function with a different
  type after its body was streamed, probably through a deduced return type or
  the noexcept part of the type. Root-cause it in M4.
- **Truncated reason text.** The reason in some writer diagnostics is cut by
  a fixed-size buffer, for example "…must be evaluated in the othe" (from
  `src/ecil_code.c` near the B4a diagnostic). The output is bounded, so this
  is cosmetic. Fix it and re-record the affected goldens and expectations.
- **Drops in system headers are invisible in the FE.** `check_severity`
  (`error.c`) raises the threshold to error for diagnostics located in system
  headers, so the writer's `cir_warning` for a dropped libstdc++ function is
  suppressed. The library still reports the drop when the function is
  referenced, but without the reason.
  - Today's workaround: strip the system-header flags (`# n "f" 3` →
    `# n "f"`) from the preprocessed file and re-run.
  - Better: carry the reason string with the unsupported declaration (a 1.x
    supp key), or report CIR drops regardless of the system-header rule.
- **Writer `src/Changes` entries** for C++ exist. The `builtin_defs.h`
  generator drift is an FE-maintenance item.
- **Lambda region keep.** Keeping a lambda's call operator keeps the whole
  region of the enclosing function (`mem_manage.c`), so a function that
  contains a lambda loses the streaming memory benefit. Restrict the keep to
  lambdas that can be constexpr-evaluated later, and measure.
- **C++17 sequencing.** The writer evaluates the right operand of an
  assignment first only where C++17 requires it (§9 review fixes B4/B4a). C
  and pre-17 C++ keep the old shape, so C streams are unchanged.
  - The purity rule is conservative: it spills the value of every built-in
    assignment whose target reads memory and whose value has side effects
    (`p->x = f()`). That costs a local and a copy (an M5 performance note).
  - A future "unsequenced" diagnostic could flag the C cases.

**Library**
- **Upstream cost growth.** The verifier, CIR passes and lowering grow
  quadratically with nesting depth (nested ifs, cleanups) and cubically with
  nested `cir.array.ctor`. Realistic code is fine. Watch upstream, or emit
  past a depth threshold if a real input ever needs it.
- **In-process residency.** The whole MLIR module stays resident until
  finish (M5).
- **Tolerated dsize.** `consistent_data_size` papers over an inconsistent
  dsize. The writer now sends Itanium dsize; consider making an inconsistent
  dsize a format error.

**Configuration and tooling**
- **Microsoft and Windows.** Microsoft-mode variants are skipped for
  cpfe-cir, and native Windows edgy is unsupported (POSIX-only paths).
- **Excused crashes.** The gate excuses an FE crash only when the reference
  crashes with the same class, never when the message names `ecil_*` or
  `cir_be*`. Excused crashes are listed in `excused-crashes.txt`. One
  heuristic gap remains: an FE assertion that the writer triggers inside
  shared FE code (for example `copy_expr_tree` in `il.c`) is still excused
  when the reference also aborts there. Read that file at every gate.
- **Checkpoint trees** should be reused or pruned automatically (§3).
- **Session-1 box trees** (`m0-tree`, `m1-tree`, `baseline-tree`, ~30 GB)
  are still on the box (§8).

## 6. Future milestones (guidance)

### M4 — virtual functions, RTTI, exceptions, member pointers (ECIL 1.x)
- **Vtables.** The library computes the vtable groups, VTT,
  construction vtables, vcall and vbase offsets, offset-to-top and thunks
  (Itanium). It works from FE facts:
  - virtual function numbers and overrider lists, the primary base, the vptr
    offset;
  - the key function, from `inline_in_class_definition`.

  Today the vptr is sent as an unnamed opaque field (§11.3); M4 replaces that
  with real vtable stores in ctors and dtors.
- **Virtual bases.**
  - `STRUCTOR.has_vbases`, D0, and C1 ≠ C2 bodies.
  - The library can build both variants by replaying the streamed C2 frame,
    with `MEMINIT(vbase)` groups skipped in the base variant. Keep the frame
    bytes for such structors.
  - VTT parameters.
  - Delegation targets and their `eh` cleanups currently use the complete
    variant inside C2. That is correct only without virtual bases.
- **Exceptions.**
  - Turn on `HDR_LANG.exceptions` in a 1.x minor.
  - The v1 stream already carries `eh` cleanups for ctor subobjects. Add
    `CLEANUP_END` deactivation (reserved since M3a) for free-on-throw and
    partial-array destruction (CIRGen's `deactivateCleanupBlock` shape).
  - `cir.try`, catch and throw; function-try-blocks; noexcept termination; an
    EH cleanup for the NRVO variable.
- **RTTI.** `typeid`, `dynamic_cast` (`cir.dyn_cast`), typeinfo globals and the
  `_ZTI`/`_ZTS` names.
- **Pointers to members.** Data and function members, `.*`/`->*`, and
  constants. CIR has `get_runtime_member` and `get_method`.
- **Tests.** About 350 C++ corpus tests are waiting (§0): virtual 243,
  exceptions 87, member pointers 16, RTTI 1. Add a two-TU ABI differential against clang
  (vtables, `-fdump-vtable-layouts`).

### M5 — performance and streaming
- **Lazy decode.** REFS-driven, plus "emit inline bodies only when referenced".
  Header-heavy C++ TUs stream every inline function today.
- **Supp keys** for optimization facts: nothrow, the noundef/nonnull/
  dereferenceable family, ctor/dtor kind (`func_info`), allocation-function
  facts, branch hints.
- **Bounded library memory** (batch lowering).
- **Concurrency** groundwork (buffer pools, a consumer thread, sockets,
  io_uring).
- **Upstream cost growth** (§5).

### M6 – Address all remaining known gaps and document
- In each of these handoff files a list of known gaps is maintained.
Any gap that does not fit into M4 or M5 will be addressed in this milestone.
- Write two sets of documentation, one for the user and one for developers who will
continue to work on this functionallity in the future.

### M7 — release and platforms
As in `handoff.md` §6: a merged static archive for `-static` builds, a
decision on macOS universal builds, AArch64 and Windows once upstream
CallConvLowering supports them, and `release/24.x` when it branches.

### Extensions
GNU features (statement expressions, vectors, `_Complex`, atomics, inline
asm, attributes such as `constructor`, `used`, `section`, `init_priority`,
multiversioning, gnu_inline in C++) and then MSVC. Each needs its own format
addition and review.

## 7. Process notes for the next session

- **Team.** The same four agent types; the definitions in
  `~/.claude/agents/cir-*.md` point at the session-2 plan, so update them to
  point at this document and the next plan.
- **What worked:**
  - A separate refactor gate (M2.1).
  - A research and contract gate (M3a) with spikes at the pin before code.
  - Vertical slices with a checkpoint each (`edg-cir-checkpoint`).
  - Incremental reviews on **frozen snapshots**.
  - Independent gate steps that caught real bugs: the poison/release
    comparisons, the two-TU probes, the clang differential and the g++
    execution probes.
- **What to change:**
  - Start every review on a snapshot. Two reviews read a moving tree.
  - Prune box trees routinely.
  - Have the writer run its own probes against the *current* library build
    (it sometimes used a stale box library).
  - Keep one owner per file. Contract changes always went through the
    orchestrator, which worked.
- **Review every phase incrementally, including hardening.** The M3c rounds
  (corpus triage, `__int128`, range-for, structured bindings and many small
  fixes) had no incremental review. The final gate review then found four
  BLOCK defects in that code: a type-walker hang, a `new[]` count truncation,
  `[[no_unique_address]]` clobbering, and C++17 assignment order. Two more
  rounds were needed to close their sibling paths.
  - Review fixes need re-review: the B3a fix introduced B3b (a wrong member
    index with bases).
  - Give the reviewer a checkpoint after each hardening round.
- **Silent-miscompile vigilance.** Every unhandled construct must be
  diagnosed. Most serious M3 bugs were found by targeted probes and
  differential runs, not by the corpus:
  - TU-local merges;
  - value-initialization zeroing;
  - `new T[n][K]`;
  - 128-bit constants;
  - the release-only operator new;
  - the noreturn change in C.

## 8. Open questions for the user

1. **Session-1 box trees.** May the next session delete `~/edg-cir-config/{m0-tree,m1-tree,baseline-tree}`
   (~30 GB) and other stale trees? The M0 identity capture
   (`identity-m0c`) and recent evidence directories would be kept.

Yes, you may delete these files as needed. Anything you create you can delete; anything else you MUST ASK.

2. **Next milestone.** M4 (virtuals, RTTI, EH, member pointers: the biggest
   pass-rate gain) or M5 (performance: lazy decode of header inline
   functions, bounded memory)?

M4

3. **Folding constant conditions (W11).** Should the writer fold constant
   `if` conditions so the ~90 `link_error` tests pass, beyond what the
   C-generating reference passes? It is a small optimization in the writer.

Yes

4. **LLVM pin.** Keep the `main` pin until `release/24.x` branches (expected
   around January 2027)? Or bump earlier if M4's EH lowering needs newer
   upstream CIR work?

Bump earlier as needed.

## 9. Gate evidence

**M2.1** (`~/edg-cir-config/m21-int`, `.tmp/gate-m21-r4` and `r4perf`; commit
`501d0888e7`):
- identity IDENTICAL (720 files); isolation 5/5;
- smoke 40/40 in both configs;
- corpus 1599/2560, 0 crashes;
- asm vs M2: 2426 identical, plus 134 for-init layout-only files and 4 builtin
  status changes;
- modes 0 differences; writer-stress 66/66; ctest 4/4;
- day-suite statuses identical (120 s timeout);
- perf: −0.25 to −0.31% on the 6000-function TU, −0.83% FE on the corpus;
  micro-wins of −97.5% (`.str` naming) and −45% (initializers);
- call graph 0 cycles.

**M3a** (commit `908f7b680a`): the contract review, then the identity and
smoke runs:
- C smoke 40/40;
- the C corpus unchanged apart from two stdarg tests now matching the
  reference;
- ctest 4/4.

**M3b checkpoints:**

| Checkpoint | C side | C++ smoke | C++ corpus | Notes |
| --- | --- | --- | --- | --- |
| S1 | all PASS; asm identical to M3a | 5/53 | — | |
| S2 | all PASS; asm identical to M3a | 21/53 | — | |
| S3/S4 | all PASS; asm identical to M3a | 34/53 | 655/1771, 12 crashes | First corpus run |
| M3c-1 | 3 C regressions (noreturn change; fixed) | 42/53 | 895/1771, 2 crashes | |

**M3 gate**:
- Tree `~/edg-cir-config/m3-final`, created with
  `rsync --checksum --no-times --delete` and clean builds.
- Evidence: `.tmp/checkpoint-20261011-124817` (`summary.md`, one directory
  per step).
- 22 steps, run with `--smoke-all-pass` and edgy `-T 180`.
- A pre-gate on the tree before the last review fixes is in
  `~/edg-cir-config/m3-gate/.tmp/checkpoint-20261011-113535`.

| Step | Result | Detail |
| --- | --- | --- |
| build | PASS | `build/cir`, `build/gcc-cir`, `build/gcc-release-cir` |
| identity | PASS | IDENTICAL: 720 files, 4 presets |
| isolation | PASS | 5/5 |
| smoke (C) | PASS | 40/40 in `_cir` and `_2p` |
| cir-suite | PASS | 96/96 in both configurations, 0 lines differ |
| corpus (C) | PASS | 1649/2561; 0 FE or library crashes; 0 lines differ from the recorded expectations |
| modes | PASS | 2557 identical, 7 INFO (diagnostics only), 0 different |
| stress | PASS | 665 s (dominated by the pre-existing else-if ladder) |
| writer-stress | PASS | 123 tests (ASan FE ± `ECIL_GID_STRESS`) |
| rss | PASS | 10 measurements |
| day | PASS | 45432 tests each in `edg_x86_64` and `edg_x86_64_cp`; statuses identical |
| ctest | PASS | 4/4 presets |
| asm-diff | FAIL (by design) | vs `m3a-base-asm`: 2510 identical, 0 other differences. 54 status changes: 51 `__int128` tests now compile, 3 constructor-attribute errors |
| perf | PASS | Tables in §0 |
| cpp-smoke | PASS | 56/56 in both configurations, 0 crashes |
| cpp-corpus | PASS | 980/1732; 0 CIR-specific crashes; 1 excused crash (`dr2867-2`, same assertion in the reference) |
| cpp-modes | PASS | 1672 identical, 16 INFO, 0 different |
| cpp-writer-stress | PASS | 109 tests |
| cpp-stress | PASS | 9 cases (`cxx_locals:2000` 2.1 s, `cxx_temps:5000` 7.4 s, `cxx_funcs:6000` 1.8 s / 181 MB) |
| cpp-stress-asan | PASS | 9 cases |
| poison | PASS | 2564 + 192 tests. Debug vs unpoisoned: 0 differences. Release vs debug: 2 INFO (`builtin-noret-1/2`, FE macro configuration) |
| release-smoke | PASS | Release FE: C 40/40, C++ 56/56, nothing passes only in debug |

**Expectations** (`tests/expectations/edg_x86_64_cir*`, per-config `.rto`
recordings):
- **C corpus:** re-recorded against M2.1. 494 lines changed: 194 changed
  output or diagnostics only, 50 went FAIL(COMPILE)→PASS, 3 went
  FAIL→FAIL(COMPILE) (the constructor attribute), and none went PASS→FAIL.
- **C++ corpus:** 742 recordings, first recorded at M3.
- **Over 100 KB:** recordings above that size are deleted (26 C tests, 2 C++
  tests). Those tests report `--OUTPUT MISMATCH`.

**Reviews (session 2):**

| Gate | Verdict | Notes |
| --- | --- | --- |
| M2.1 design note (L1/L2/L4) | APPROVE WITH FOLLOW-UPS | Six required items |
| M2.1 | BLOCK → APPROVE WITH FOLLOW-UPS | Else-arm scope regions |
| M3a contract | BLOCK → APPROVE WITH FOLLOW-UPS | Free-on-throw nesting, internal precedence, exit-only init, F_GLOBALS L-ids, header parity; then the §11.6 bitwise-copy sentence |
| S1 | PROCEED WITH FIXES | TU-local linkage, DSIZE assignment, inherited vptr, void return, G-id bookkeeping, structor override |
| S2 | PROCEED WITH FIXES | A use-after-free at the end of the TU, value-init zeroing, for-condition variables, return temporaries, TU-local template args, failed-record temporaries |
| S3/S4 | PROCEED WITH FIXES | `new T[n][K]` overflow, static-init state leak, vague dropped re-issue, multi-dim new types, unnamed definitions |
| M3 final | BLOCK (4) → BLOCK (2 residual) → BLOCK (B3b, introduced by a fix) → APPROVE WITH FOLLOW-UPS | B1 type-walker hang; B2 `new[]` count truncation; B3/B3a/B3b `[[no_unique_address]]` writes; B4/B4a C++17 assignment order. The gate tooling now never excuses a library crash |
