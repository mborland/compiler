# M5 design: performance and streaming (gate M5a, session 4)

Owner: the orchestrator. Inputs:
- `m5_library.md` (cir-lib: the dead-body census L1, the lazy-emission
  prototype L2, chunk spikes L3, the optimization-facts inventory L4, the
  -O2 shake-down L5, the profile L6, and the pin study);
- `m5_writer.md` (cir-writer: writer cost W3, region keeps W4, FE facts
  W5);
- `m5_baseline.md` (cir-config: the -O2 baseline);
- `m5_upstream_drift.md`.

The plan is `~/.claude/plans/we-are-going-to-graceful-riddle.md`. The
contract additions are in `ecil_format.md` §13 (ECIL 1.3) and in
`edg_cir.h` API 2.

## 1. User decisions that shape M5

| Decision | Effect |
| --- | --- |
| Concurrency: room only | No threads, sockets or io_uring. Frames, deferred bodies and chunks stay independent units of work behind the sink API (§7). |
| Lazy decode: the library defers bodies | No format change; the FE is unchanged; finish-only replay (§2) |
| Bounded memory: chunk design (a) behind a size threshold | Chunked CIR is linked into one LLVM module, only for big TUs, after lazy emission (§3) |
| Code quality: parity with `clang -fclangir` | The facts clangir emits at the pin; `attr-diff` is the check (§4) |
| Pin 2d6f180e (M5p) | Every M5 measurement is at the new pin |
| Lean gates | Slice gates run the steps their areas can affect; the full gate runs at M5 |

## 2. S1: lazy emission

The prototype (`m5_library.md` §L2) is byte-identical to eager emission
over every stream set (both corpora, writer goldens, probes and 8
libstdc++ TUs at -O0 and -O2). The one exception is the position of a
library-created `.hidden __dso_handle` line.

Library time is 76.8% of eager on the C++ corpus and 63.0% on the TUs.
The oracle bound is 73.0% and 61.4%. GlobalDCE erases 68% of emitted C++
corpus bodies and 63% on the TUs, but only 3.3% on the C corpus.

**Design** (as in the plan's "Lazy emission design" section):
- **Deferral rule.** A frame is deferred only if it holds at least one
  `FUNC_BEGIN` and every function in it is discardable: internal or
  `linkonce_odr`, and not `used` (key 11).
- **On arrival.** Declaration sections are processed. Body sections are
  validated, then copied. Their G-id references are collected while they
  are validated.
- **Finish phases.**
  1. Class facts and vtable layouts.
  2. Close reachability.
  3. Replay in stream order.
  4. `finish_classes`.
  5. `finish_entities`.
- **Replay mode.** The brackets and L-ids run, but the G-id definition
  checks are skipped. `HAS_SUPP` and the minor are snapshotted, and the
  location is saved and restored.
- **`body_arrived`** replaces `has_body` in the four emitter format checks.
- **Roots:**
  - eager bodies;
  - `VAR_INIT` constants, global initializers and `STATIC_INIT`;
  - defined vtables, including construction vtables, thunks, D0 → D1 and
    operator delete;
  - C1→C2 aliases and structor pairs;
  - `used`;
  - a non-discardable final linkage;
  - library-synthesized functions;
  - a guarded static local in use, which makes its owner's frame live;
  - comdat groups, which are live together.
- **Missing edges the prototype found:**
  - a vptr inside a constant aggregate;
  - `#cir.method<@f>` symbol uses, which `SymbolTable::getSymbolUses` does
    not report.

  S1 adds **one shared helper** that walks attribute dictionaries for
  symbol uses. Lazy emission, chunking (S3) and the existing
  `entity_liveness` use it.
- **Replay timing.** Finish-only. Incremental replay was 1.5 points slower
  in-process. 47% of the lazy emit time moves to finish on the TUs.
- **Quick win in S1:** turn off the per-pass MLIR verifier in our lowering
  pass managers, keeping it under `EDG_CIR_FLAG_VERIFY_EACH`. That is
  -26% on the lowering stage and -13 to -16% wall, with identical assembly
  (`m5_library.md` §L6).
- **The flag** `EDG_CIR_FLAG_EAGER_BODIES` keeps eager emission for A/B
  runs and the `lazy-identity` step.

## 3. S3: chunked lowering, design (a)

The spike (`m5_library.md` §L3) measured these peaks at 500 functions per
chunk:

| TU | before | after |
| --- | ---: | ---: |
| c12000 | 810 MB | 395 MB |
| cxx6000 | 157 MB | 118 MB |

The cost is +11% to +37% time. The output was identical on everything
tried: 1883 C and 1464 C++ corpus programs at one function per chunk, 339
at -O2, and the libstdc++ TUs.

**The spike's fixes, which S3 implements:**
- symbol uses through attribute dictionaries (the S1 helper);
- internal and private linkage, plus `dso_local` on declarations,
  restored after the merge;
- aliases stripped and re-created after the merge;
- comdat groups re-attached;
- ctors in the globals chunk;
- catch-copy thunks cloned into each user;
- the globals chunk linked first.

**Threshold.** Chunk only when the TU has at least `chunk_threshold`
emitted bodies. The default is fixed from S3 measurements on real
header-heavy TUs; the spike suggests about 2000 bodies, or a CIR module of
about 100 MB. The chunk size `chunk_functions` defaults to 500.

**Bound.** The CIR module stays resident until its chunk is lowered:
names are chosen at finish, and the CIR passes are whole-module. A failed
chunk cannot fall back in-process, so S3 diagnoses it.

**Rejected:** design (b), per-chunk codegen. Its output differs, it keeps
dead linkonce functions, it has no cross-chunk optimization, and it needs
a text post-processor.

## 4. S2: optimization facts (parity with clangir at the pin)

| Fact | Source | Notes |
| --- | --- | --- |
| `nounwind` on function definitions and declarations | the library, from `DECL_FUNC NOTHROW` (FE predicates) | **The largest gap**, measured on clangir 2d6f180e IR (`m5_library.md` §8). We cover 8% of declarations against clangir's 56%, 51% of definitions against 56%, and 57% of calls against 45%. Stripping it changes -O2 `.text` by +12% to +23%. Our -O2 text is 15-24% larger than clangir's. Do it first. |
| noreturn and nounwind at call sites | the library, from `CALL` flags and `DECL_FUNC` | The declarations already carry noreturn |
| nonnull, noundef, dereferenceable and align on references and `this` | key 15 (writer) plus the library | Up to +5.5% `.text` when stripped. Lands with the static-invoker change. |
| noundef on other parameters and results | the library, from types and `DECL_FUNC EXTERN_C` | noundef on results only for C++ functions that are not `extern "C"`, and never for a function with a value-less `return;` (seen in the stream) |
| mustprogress | the library, at `FUNC_END` | C++11 and later, unless the function has a trivial infinite loop. The stream shows folded constant conditions and empty bodies. |
| inlinehint | `DECL_FUNC INLINE_HINT` (writer) plus the library | +4.8% `.text` on one TU; §6 F |
| `used` → `llvm.compiler.used` | key 11 (writer) plus the library | Also an S1 root |
| available_externally for C99/gnu_inline inline-only bodies | the library | Today they become declarations, so they are never inlined at -O1 and up (`m5_baseline.md` §5) |
| `builtin` on new-expression allocation calls | `CALL ALLOC_EXPR` (writer) plus the library | Confirmed at the pin: clangir's LLVM IR gives the new-expression calls `builtin allocsize(0)`, operator delete `builtin nounwind`, and an explicit `::operator new` only `allocsize(0)`. The declarations carry `nobuiltin` (`m5_library.md` §8.4). |
| Branch weights / `[[likely]]` | not at parity: CIRGen ignores it at the pin | Optional, last; `cir.expect` covers `__builtin_expect` |
| `builtin-ctype-1` (our IR at -O2) | to be diagnosed in S2 | |

**Hazards** (reviewer checklist):
- the static invoker's null `this` versus nonnull: the invoker passes a
  real closure object (spec 11.9);
- noundef on a GNU value-less `return;`;
- mustprogress on loops the FE folds differently from clang (a
  differential on the C++ smoke list).

## 5. -O semantics

- **Levels.** `--cir_opt_level=N` (0-3) is passed to the library: CIR
  simplification at N ≥ 1, the LLVM default pipeline at N ≥ 1, and the
  codegen level.
- **Predefined macros.** `__OPTIMIZE__` at N ≥ 1 and `__NO_INLINE__` at
  N = 0, as gcc does.
- **eccp mapping.** `-Og` → 1; `-Os` and `-Oz` → 2, without
  `__OPTIMIZE_SIZE__` (a documented approximation); `-Ofast` → 3, without
  fast-math.
- **wrapv.** `HDR_LANG` wrapv follows the FE. Tests that need `-fwrapv`
  through a `dg-option` the harness ignores are triaged as `dg-option`.
- **Strict aliasing / TBAA.** Neither clangir nor we emit TBAA at the pin,
  so LLVM's default rules for untyped IR apply. `-fno-strict-aliasing`
  tests are triaged as `dg-option`.
- **The -O2 watch items** (CIRSimplify and canonicalize cost):
  - ternary:5000 and land:5000 take about 200 s at -O2, against 0.3 s at
    -O0;
  - elseif:20000 takes 288 s.

  They are generated shapes, and they are listed in handoff_4.

## 6. Questions from cir-lib (L4), answered

| Q | Answer |
| --- | --- |
| A: does the FE mark noreturn library builtins? | Yes: `DECL_FUNC NORETURN` from `does_not_return`, on declarations too (W5 §8). The gap is the library's call-site attribute. |
| B: is `extern "C"` visible? | No (`m5_writer.md` §3.11). The FE has it: `name_linkage == nlk_external` in C++. 1.3 adds the `DECL_FUNC` flag `EXTERN_C` (0x100). clangir gives an `extern "C"` result no `noundef`. A value-less GNU `return;` in a non-void function is seen by the library itself (a `RETURN` without a value), so it needs no fact. |
| C: what does `DECL_FUNC NOTHROW` cover? | The FE predicates `is_non_throwing_routine` and `is_nothrow_type`, on declarations and definitions. S2 maps it to `nounwind` on functions. |
| D: reference and restrict flags | Key 15 (references, `this`). `restrict` is beyond parity: clangir does not emit `noalias` at the pin. |
| E: the C++ standard level | `HDR_LANG.std` |
| F: does INLINE mean declared inline or inline linkage? | Neither exactly. It is `rout->is_inline`: the keyword, in-class definitions, `constexpr`, implicit members and lambda members. clangir sets `inlinehint` only for the keyword and lambda call operators, so today we over-hint. 1.3 adds `DECL_FUNC` `INLINE_HINT` (0x200). The FE keeps no "keyword written" bit and `il_def.h` cannot change, so S2 records it with a CIR-gated hook where the specifier is processed (a writer side table) and falls back to the documented approximation only if that hook is not clean. |

## 7. Room for concurrency (no implementation)

- **Frames** are self-contained after their declaration sections, and the
  sink API already transfers buffer ownership.
- **Deferred bodies** are copied byte ranges with collected references. A
  consumer thread could replay them, or a worker could validate them, in
  any order before finish.
- **Chunks** in design (a) lower independently into their own LLVM
  modules before the link. A future design could lower chunks on worker
  threads with one `LLVMContext` each, then link them in order.
- Per W3, buffer pooling costs under 0.3% of the FE and is not worth
  building.

## 8. S4: writer and FE performance

The writer is 18.6% of release-FE instructions on the 6000-function C TU
and 23.8% on the C++ one (W3). The targets:
- a cache for `compute_func_state`, which runs 4.3 times per function, and
  for the mangled name, built 2.5 times per function: about 2-3%;
- a memo for `cir_entity_is_tu_local` on final names: about 2%;
- inline record-encoding fast paths: 2-3% on C, 1.5% on C++;
- the purity rule: `enk_field` becomes a leaf in `expression_is_pure`.
  That is C++17 only, about half the spills go, and the stream shape
  changes;
- the lambda/local-class region keep, which holds about 20% of the FE peak
  on the template TU: measure and narrow it.

Keep #2 (prototype instantiations) stays, because requires-expressions
need it (W4). Its comment is corrected.

## 9. Targets for the M5 gate

Each target is measured against the M5p baselines (`m5p-base-*`).
Instructions mean cachegrind Ir of `edg-cir`, as `edg-cir-perf` counts
them. Time is reported next to Ir but is not gated, because the box is
shared.

**The pinned real-world TU list:** the units that compile under CIR at M5p
at both -O0 and -O2. They are libstdc++ `chrono`, `functional` and
`iostream`, and Boost `mp11`. `circular_buffer` and `lexical_cast` compile
at -O0, and at -O2 since the shared-symbol fix. Every listed TU must be
measured: a TU that stops compiling fails the target. A TU that starts
compiling joins the list at the next baseline.

| Slice | Target |
| --- | --- |
| S1 | **`lazy-identity`:** lazy against eager at -O0, per-symbol bodies identical, non-vacuous (deferred, replayed and dropped counts above 0 on every pinned TU). **Instructions:** library Ir on each pinned TU at most 70% of M5p, and at most 90% on the generated 6000-function C++ TU (few dead bodies). The research measured CPU time (63% on 8 libstdc++ TUs, with lazy emission plus verifier-off). S1 re-measures in Ir before it is gated, and the target moves only with the user's agreement. |
| S2 | **`attr-diff`:** at -O0 and -O2, on the probe set and the pinned TUs. It reports the number of compared symbols and the number of explained differences. Every new entry in the known-differences file is reviewed, and the unexplained count is 0. **`opt-corpus`:** no new our-IR case. **`codequality`** is reported. **nounwind coverage:** declarations and definitions at least at clangir's share on the pinned TUs. |
| S3 | **Peak RSS** (minimum of 3 runs) on c12000 at most 55% of M5p with chunking forced on. On the largest pinned TU it is reported. **Below the threshold**, time and output are unchanged. **Forced to 1 function per chunk**, chunked output is identical to unchunked on both corpora. |
| S4 | **Writer share of release-FE Ir:** at least 2 points lower on the 6000-function C TU (W3: inline record primitives) and at least 4 points lower on the 6000-function C++ TU (W3: the func-state cache, the TU-local memo, the primitives). Streams are identical, except the C++17 purity spills, which are attributed. |
| All | **Both corpora at -O0:** 0 PASS→FAIL against M5p. **-O2:** no new our-IR. **Crash-sweep:** 0. **The full gate at M5.** |

**Slice gate steps.** S1 and S3 change memory, speed and recursion. Their
lean gates add `rss`, `perf`, `stress`, `cpp-stress`, `m4-stress` and
`crash-sweep` to the derived areas, which the library area alone leaves
out. S2 adds `crash-sweep`, `symbols` and `mixed-tu`.

## 10. Behavior changes and trade-offs to state

- **Lazy emission and diagnostics.** A deferred body that is never
  replayed is validated (so format errors still report) but never emitted.
  So the emitter-only "unsupported" diagnostics for constructs inside such
  a body disappear. They would only ever have been warnings for erased
  bodies, but the corpus expectations may change in text.
- **Finish-only replay against brief item 8** (overlap of FE and back
  end). In-process, the FE and the library run on one thread, so
  incremental replay buys no overlap and measured 1.5 points slower.
  Finish-only replay moves 47% of emit time to finish. That would matter
  only with a concurrent consumer, which the user deferred ("room only").
  §7 keeps the deferred bodies independent, so a future consumer thread can
  replay them early.
- **Defects tracked into the slices:**
  - the writer's empty-class assignment `LOAD; DROP` of a NONTRIVIAL_ABI
    record (`m5_writer.md` §4, boost/multiprecision) → S2 or S4;
  - the `--keep-going` crash of `c/pr114907` is fixed at M5a. A
    `_Float16` ↔ `__bf16` cast goes through `float`, and only that pair, so
    `long double` ↔ `__float128` on a binary128 target keeps the plain
    cast.
