# EDG ClangIR back end — handoff after M5 (session 4, 2026-10-19/20)

This document is the design guidance for the next session. It follows the
spirit of `cir_design_guidance.md` (repo root), which remains the binding
brief, and of `handoff.md`, `handoff_2.md` and `handoff_3.md`, the earlier
handoffs. Read it first, then:

- `cir_src/doc/research/m5_design.md`: the M5 design, decisions, gate
  targets (§9) and trade-offs (§10);
- `cir_src/doc/design.md` §13 (M4.1), §14 (M5p) and §15 (M5);
- `cir_src/doc/ecil_format.md`: ECIL **1.3**, §13 lists the additions;
- `cir_src/doc/writer.md` §10 ("1.3") and §11 (writer performance);
- `research/m5_library.md`, `m5_writer.md`, `m5_baseline.md` and
  `m5_upstream_drift.md`.

The approved session-4 plan is
`~/.claude/plans/we-are-going-to-graceful-riddle.md`.

How the facts are marked: a number without a source comes from the M5 gate
evidence (§9). *(S-checkpoint)* marks a number measured at an earlier
checkpoint and not re-measured at the gate. *(not re-run)* marks a statement
about the code after the gate that no run has checked. **Open** marks an
item that is not done.

## 0. Where things stand

**Commits** (branch `cir`; never pushed):

| Commit | Gate | Contents |
| --- | --- | --- |
| `c8f11f51f5` … `6a52c93316` | R to M4a | Sessions 1–3 (`handoff_3.md` §0) |
| `0ce8b90333` | M4 | Virtual functions, virtual bases, RTTI, exceptions, member pointers; `handoff_3.md` |
| `9d518c3178` | M4.1 | The M4 follow-ups; -O plumbing (`--cir_opt_level`, `__OPTIMIZE__`); the `opt-corpus` step; the first three -O2 fixes (pr39188 linkage, `nobuiltin`, `noinline`); ECIL 1.3 opened |
| `e6bdad03f4` | M5p | The llvm-project pin moves to `2d6f180e` |
| `77a24448fa` | M5a | The ECIL 1.3 contract, C API 2, M5 research and design |
| *(this commit)* | M5 | S1 lazy emission, S2 optimization facts, S3 chunked lowering, S4 writer speed; this document |

**Format and toolchain**
- **ECIL 1.3** (`MINOR 3`) is frozen at this gate. Against 1.2 it adds
  supp keys 4/5 (semantics fixed), 11 `used`, 14 `noinline`, 15
  `nonnull_ptrs` (on `TY_FUNC`), 16 `always_inline`, 17 `cold`, 18 `hot`;
  several keys per supp blob; the `CALL` flag `ALLOC_EXPR` (0x40); the
  `DECL_FUNC` flags `EXTERN_C` (0x100) and `INLINE_HINT` (0x200); the
  preamble feature bit `INLINE_HINTS` (0x10); spec text for static
  anonymous unions and nested-routine linkage (§11.2), the static invoker
  (§11.9) and `ZERO_BYTES` (§11).
- **C API 2** (`EDG_CIR_API_VERSION 2`): `EDG_CIR_FLAG_EAGER_BODIES`
  (0x20), `EDG_CIR_FLAG_NO_LLVM_PASSES` (0x40), and the appended fields
  `chunk_functions` and `chunk_threshold` (0 = default, `UINT32_MAX` =
  never, 1 = force). An API 1 caller is accepted and gets the defaults.
- **llvm-project pin** `2d6f180e7dbad81db52f5163a34502c1b8058012` (main,
  2026-10-19; was `894bfff5`). Images `edgcpp/{llvm-cir,cir-env}:latest`
  point at it; the rollback images are tagged `:894bfff5`. No local
  llvm-project patch.

**M5 results** (tree `~/edg-cir-config/m5-int`, fingerprint `52e3c1ac…`
over 1809 files, evidence `.tmp/checkpoint-20261020-102543`; one full run
of the 38 milestone steps: 34 PASS, of which `opt-corpus` passed after one
triage line, and 4 FAIL; details in §9):
- **Open FAILs**:
  - `asm-diff` and `cpp-asm-diff`: expected; every file is attributed
    (§9.3);
  - `perf`: the generated 6000-function C++ unit misses its S1 target
    (102.6% at -O0, target 90%) and is +2.6% in two-process instructions
    against the 2.0% limit (§0.1, §4);
  - `attr-diff`: 539 differences in the gated attributes that no known
    entry explains (§4.1).
- **S1 lazy emission**: library instructions on the six pinned real-world
  units are 26.4–62.5% of M5a at -O0 and 27.1–62.0% at -O2 (limit 70%).
  Lazy output is identical to eager output on both corpora and the pinned
  units (`lazy-identity`). The generated C++ unit is the miss above: it was
  at 88.3% *(S-checkpoint)* before chunking became the default.
- **S2 optimization facts**: nounwind on definitions is at clangir's share
  on every set; declarations are below it on the probe set; the gated
  attributes leave 539 unexplained differences and the residue is 1913
  (§4.1, every row annotated).
- **S3 chunked lowering**: forced chunking of c12000 peaks at 408.3 MB,
  52.3% of the M5p baseline's 781.0 MB (limit 55%). Output is identical
  when forced to one function per chunk (pinned units, both corpora). The
  default threshold was 2000 emitted bodies at the gate and is **4000**
  now *(not re-run)*; §0.1.
- **S4 writer speed**: the writer's share of release-FE instructions went
  from 18.57% to 16.42% (C 6000 functions, -2.15 points, target 2) and from
  24.44% to 20.35% (C++ 6000 functions, -4.09 points, target 4)
  *(S-checkpoint)*.
- **C corpus**: 1661/2561 (M5p 1656). Five tests now pass because the
  inline-only definitions are kept as available_externally; four
  `torture/stackalign` cases move from CATASTROPHE(COMPILE) to
  FAIL(COMPILE).
- **C++ corpus**: 1471/1823 in both configurations (M5p 1472). The one
  PASS→FAIL (`torture/pr101373`) is triaged: g++ and clang fail it the same
  way at -O0 (§5).
- **-O2** (`opt-corpus`, -O2 against -O0): C 13 worse (all triaged:
  dg-option 8, upstream 5) and 69 FAIL→PASS; C++ 8 worse (dg-option 3,
  ref-fails 1, upstream 4) and 8 FAIL→PASS. The C++ smoke list at -O2 in
  `_2p` is 97/98 (`aligned-new3`, the triaged allocation elision).
- **Crash-sweep**: 30,191 cases (C 11,301, C++ 18,438, extra 452), 0
  problems, no CIR-specific crash.
- **Cost** against M5a (cachegrind instructions):
  - C corpus (2564 cases): FE +0.56%, library -10.35%;
  - C big file, two-process: library -11.6% at -O0 and -3.1% at -O2, FE
    -2.5%;
  - generated C++ unit, two-process: library +2.6% at -O0 and -9.7% at
    -O2, FE -4.5%;
  - pinned units, FE plus library, at -O0: -7.7% (mp11) to -42.3%
    (lexical_cast).

### 0.1 The chunk threshold after the gate (read before measuring)

After the gate the default `chunk_threshold` was raised from 2000 to 4000
emitted bodies (`cir_src/lib/driver/chunk.h`, `k_default_chunk_threshold`),
on the user's decision, **without any re-run**.
- **The S1 miss stays open.** The generated C++ unit has about 6000
  emitted bodies (3600 of them deferred and replayed, 0 dropped), so it is
  still chunked at 4000: 13 chunks of at most 500 functions. Chunking costs
  it +21% in the lower stage (600.7 ms against 495.3 ms unchunked, `edg-cir
  --stats`), although its peak is only 118.8 MB. The user chose to keep
  4000 and record the miss.
- **What 4000 changes.** Of the curve units only c3000 (3000 bodies)
  becomes unchunked by default. No test depends on the default *(checked by
  reading)*: the chunk tests and tools pass `--chunk-threshold`
  explicitly, and `capi_tests` and `chunk-lowering` expect 0 chunks only
  for modules of a few bodies.
- **Stale numbers.** Every S3 number in this document, the gate's perf,
  rss, chunk-rss default column and slow-below-threshold, was measured at
  2000.
- **Next session:** consider a threshold on the estimated CIR module size
  or the expected peak instead of a body count (the unit above chunks at
  118.8 MB while c3000 peaks at 224.6 MB unchunked); then re-run `perf`,
  `rss`, `slow-below-threshold` and `chunk-rss`. `chunk-identity` is
  unaffected (it forces chunking).

## 1. What exists (architecture as implemented)

New in session 4:

| Area | Files | Notes |
| --- | --- | --- |
| Lazy emission (S1) | `lib/cirgen/lazy.cpp`, `symbol_uses.h` | Deferral, finish-only replay, roots, dead namers, the missed-edge error (§1.1) |
| Signature facts (S2) | `lib/cirgen/signature.cpp`; S2 rules in `globals.cpp`, `expr.cpp`, `function.cpp`, `classes.cpp` | noundef, key-15 pointers, strict results, `operator new` results, thunk attributes (§1.2) |
| Chunked lowering (S3) | `lib/driver/chunk.cpp`, `chunk.h` | Design (a) (§1.3) |
| Lowering | `lib/driver/pipeline.cpp` (`lower_to_llvm_ir`) | `populateCIRToLLVMPasses` + `translateModuleToLLVMIR`, verifiers only under `VERIFY_EACH`; MLIR CL options registered (`capi.cpp`) |
| Writer facts (S2) | `src/ecil_type.c` (`compute_func_state`, `routine_has_inline_hint`, `library_function_facts`), `src/ecil_code.c`, `src/ecil_const.c`, `src/ecil_write.h` (`ECIL_FACT_*`) | §1.4 |
| Library function data | `src/builtin_libc_attrs.h` (generated), `dev_tools/builtins/generate-libc-builtins.py` | clang's `Builtins.inc` at the pin |
| Writer speed (S4) | `src/lower_name.c` (TU-local memo), `src/ecil_write.h` (inline record helpers), `ecil_resolve_late_names` (sampled check) | §1.4 |
| CIR-gated FE hooks | `src/decl_spec.c`, `decls.c`, `symbol_tbl.{h,c}`, `mem_tables.h`, `lower_name.c`, comment in `mem_manage.c` | §1.5 |
| Gate tools | `dev_tools/bin/edg-cir-{lazy-identity,chunk-identity,chunk-ir,chunk-rss,names-check}` (new); `edg-cir-{attr-diff,codequality,opt-triage,gate-evidence,checkpoint,perf,tools-test}` (extended) | §3 |
| Triage files | `tests/.edgy/filters/cir-{opt-triage,status-triage,lazy-triage,attr-known}.txt` | §3 |
| Tests | `cir_src/test/cirgen` (217 files: `lazy-*`, `bad-lazy-*`, `s2-*`, `chunk-*`, `lowering-drift`, `mlir-cl-options`), fixtures `good_v13_temperature`, `bad_v13_*`, writer goldens `tests/tests/cir/writer` (230 sources) | cir suite 259/259 at the gate |
| Box scripts | `cir_src/scripts/box_{attrdiff,attrdiff2,chunk_identity,chunk_o2,lazy_*,o2_diff,s1_*}`, `{chunk_identity,lazy_identity,lowering_drift}_tests.py` | §3 |

### 1.1 S1: lazy emission

- **Rule.** A function in a function-region frame that every TU defines
  itself (internal, private, `linkonce_odr`, or a C99/gnu_inline
  INLINE_ONLY definition) is deferred: its records are kept, validated
  by the reader and not emitted (`a_emitter::lazy_can_defer`). `used`
  functions, unsupported ones and external definitions are emitted at once.
- **Finish.** `lazy_finish` runs first in `finish`, before `finish_classes`
  and `finish_entities`. It computes reachability with `entity_liveness`
  (`globals.cpp`), replays the reachable bodies in stream order
  (`replay_function`, the reader's `replay_body` with a `ref_sink`), and
  erases the rest.
- **Roots and edges.**
  - Roots: the external, weak and `used` definitions, definitions with
    dynamic initialization, and uses from operations that belong to no
    entity.
  - Edges: every symbol reference of every operation (`symbol_uses.h`
    walks the attribute dictionaries; `SymbolTable::getSymbolUses` does
    not).
  - Plus the edges the module does not show yet: the references a deferred
    body made (`a_deferred_fn::refs`); the vtable and VTT of a class whose
    vptr a body initializes (`vptr_init`, `vtt_use`, `this_class`); the
    vtables of classes in constants (`agg_classes`); class-fact edges from
    a vtable/VTT to its group's functions; static locals initialized in a
    body (`statics`, reverse edge); `address_uses_` for method-pointer
    constants (`#cir.method` is a string attribute, not a `SymbolRefAttr`).
  - A STRUCTOR pair lives and dies together.
- **Missed edges are errors.** If a reachable entity still names a dropped
  function, the library stops with "lazy emission missed an edge to
  '<sym>'" (`lazy_erase_dropped`). Test hook `EDG_CIR_TEST_LAZY_MISS=<sym>`
  under `EDG_CIR_TEST_HOOKS`; test `lazy-missed-edge.test`.
- **Dead namers.** A linkonce vtable (or other entity) that only dropped
  bodies name is erased with them, so `NO_LLVM_PASSES` output has no
  undefined declaration (`lazy-dead-namer.test`). A declaration of a
  function whose dropped inline-only body defined it stays legal
  (`inline_only_declaration`).
- **Validation trade-off** (`m5_design.md` §10): dropped bodies are checked
  by the reader only, not by the emitter. Eager runs (`--eager-bodies`) at
  milestones keep the emitter checks exercised (`lazy-identity`).
- **Stats.** `edg-cir --lazy-symbols` lists each deferred body as replayed
  or dropped; `stats: lazy bodies: N deferred, N replayed, N dropped`
  (`a_session_stats`, `lib/driver/session.h`).

### 1.2 S2: optimization facts, library side

Each fact mirrors CIRGen at the pin, attribute by attribute; where clangir
lacks a fact the library does not add it.

| Fact | Library rule | Source in clangir | Test |
| --- | --- | --- | --- |
| nounwind on decls/defs | `DECL_FUNC` `NOTHROW` → `nothrow` | proto `isNothrow` | `s2-nounwind` |
| call-site noreturn/nothrow | `CALL` flags, callee declaration | `ConstructAttributeList` | `s2-call-attrs` |
| nonnull, dereferenceable, align | key-15 mask (bit 0 result, bit i+1 parameter i); class pointee: nvsize/nvalign unless final; incomplete or function pointee: none | `addAttributesFromFunctionProtoType` | `s2-nonnull-ptrs` |
| noundef | scalars yes, records and member pointers no; results only for C++ non-`extern "C"` functions without a value-less `return;` (`strict_result`) | `determineNoUndef`, `hasStrictReturn` | `s2-noundef`, `s2-noundef-c.inc` |
| mustprogress | C++11 and later, unless the function has a trivial infinite loop (`is_trivially_true`); also on D0 | `checkIfFunctionMustProgress` | `s2-mustprogress` |
| inlinehint | `INLINE_HINT` when feature bit 0x10 is set (else `INLINE`), -O1 and up; thunks take their target's | `isInlineSpecified` | `s2-inlinehint` |
| `llvm.compiler.used` | `used` definitions | `addUsedGlobal` | `s2-used` |
| available_externally | INLINE_ONLY bodies at -O1+ when referenced; a declaration at -O0, with `noinline`, or when the body names its own symbol (`calls_own_symbol`, PR9614); no comdat, not dso_local | `shouldEmitFunction`, `isTriviallyRecursive` | `inline-only-drop`, `s2-inline-only-guards`, `-unsupported` |
| `operator new`/`delete` | `ALLOC_EXPR` calls to the 48 replaceable names: `builtin`; synthesized scalar and D0 deletes: `builtin nounwind`; declarations `nobuiltin` (and `allocsize(0)` for new) | `buildNewCall` | `s2-builtin`, `cxx-nobuiltin-new` |
| pure/const (keys 4, 5) | memory(read)/memory(none) + `nounwind` + `willreturn` on the function and direct calls | `ConstructAttributeList` | `s2-pure-const`, `s2-pure-in-try` |
| keys 16–18 | `alwaysinline` (not with noinline; both: neither); `cold` + `optsize` on a definition at -O1+ unless optnone; `hot`; cold/hot also on calls; cold and hot together: neither | `CIRGenModule` inline kinds | `s2-temperature` |
| norecurse | C++ `main` | `startFunction` | `cxx-s1-dtor-thunks` |
| thunks | noundef on scalar parameters and result (result only under `strict_result(target)`), the target's nounwind and inlinehint; a thunk of a structor alias uses the base variant | thunk generation | `cxx-s1-dtor-thunks` |
| library-made calls | the callee's argument attributes, copied at finish to a `cir.call` that has none | call-site attribute lists | `s2-arg-attr-shift`, `s2-builtin` |

`nounwind` (LLVM attribute) and "cannot throw" (`nothrow`: no invoke) are
different: noexcept gives `nothrow`; GNU const/pure give `nounwind`, and the
call stays an invoke in a try body, as in CIRGen (the `pr100382` fix, §7).

### 1.3 S3: chunked lowering

Design (a) of `m5_design.md` §3.
- **When.** After lazy replay and the CIR passes, if the module has at
  least `chunk_threshold` emitted bodies (the emitter's counter
  `emitted_bodies()`, no module walk). Default 4000 (§0.1); 1 forces;
  `UINT32_MAX` never. `chunk_functions` default 500. `edg-cir
  --chunk-functions=N --chunk-threshold=N`.
- **How.** The globals chunk (everything that is not a function body, plus
  declarations of the body functions) is lowered and linked first. Then
  the bodies, `chunk_functions` per chunk: each chunk is a CIR module with
  declarations of what it names (`add_declarations`), lowered by
  `lower_to_llvm_ir`, linked into one `llvm::Module` with `llvm::Linker`,
  and its CIR erased; `malloc_trim(0)` after each chunk. GlobalDCE, the
  optimizer and code generation then run on the merged module unchanged.
- **The fixes** (all in `chunk.cpp`):
  - symbol uses by attribute walk;
  - internal and private symbols made external while lowering and
    restored after the merge, declarations `dso_local`;
  - available_externally made external and restored with no comdat and
    not dso_local;
  - aliases stripped and re-created as `GlobalAlias`;
  - comdats stripped from bodies and re-attached by group name;
  - ctors, dtors and `cir.module_asm` in the globals chunk only;
  - catch-copy thunks (`cir.eh.catch_copy_thunk`) cloned into every chunk
    that names them;
  - weak declarations stay `extern_weak`;
  - chunk declarations private (the verifier rejects public ones);
  - after the merge the global variables get the globals chunk's order
    back, and functions the lowering makes (`__clang_call_terminate`) go
    last.
- **Failure.** A chunk that fails to lower or link is diagnosed ("lowering
  chunk N of M to LLVM IR failed"). There is no in-process fallback: the
  CIR of lowered chunks is gone. Test hook `EDG_CIR_TEST_CHUNK_FAIL=N`,
  `chunk-failure.test`.
- **Stats.** `stats: chunked lowering: <N> chunks of at most <K> functions`
  (N counts the globals chunk; 0 when lowered whole).
- **The curve** *(S-checkpoint, chunks of 500, peak RSS in MB, minimum of
  3, unchunked → forced)*: C 500 bodies 76→77, 1000 106→93, 2000 166→119,
  3000 225→143, 4000 298→173, 6000 414→227, 12000 792→388; C++ cxx2000
  88→78, cxx6000 157→117, cxx12000 262→178. Time +3% to +21% on C, noise to
  +10% on C++. Chunking saves nothing measurable below about 1000 bodies.

### 1.4 The writer

The S2 facts (all in `ecil_type.c`, `ecil_code.c`, `ecil_const.c`; each
has a golden in `tests/tests/cir/writer`). Corpus counts are streams that
changed, C++/C *(S-checkpoint)*.

| Fact | What the writer sends | Source in the FE | Corpus (C++/C) |
| --- | --- | --- | --- |
| Static invoker object | a local of the closure type, not `CONST_NULL` (spec §11.9) | `emit_invoker_body` | 32/0 |
| `nonnull_ptrs` (key 15, `TY_FUNC`) | mask: bit 0 reference result, bit i+1 reference or `this` parameter i; part of the type key | `finish_function_type` | 1197/0 with the invoker |
| `used` (key 11) | `DECL_FUNC` and `DECL_VAR`, sticky | `has_gnu_used_attribute`; `ecil_variable_is_used` | 1/3 |
| `ALLOC_EXPR` (`CALL` 0x40) | allocation and deallocation calls of new/delete expressions and of `__builtin_operator_new/delete` | `name_reference` special kinds | 195/0 |
| `EXTERN_C` (0x100) | C++ and `name_linkage == nlk_external` | `compute_func_state` | 1089/0 |
| `INLINE_HINT` (0x200) + feature bit 0x10 | clang's `inlinehint`: the inline specifier on any declaration, instances of such templates, implicit special members, lambda call operator/conversion/invoker; never with `always_inline` or `noinline` | §1.5; `routine_has_inline_hint` | 705/23 |
| `pure` (4), `const` (5) | the GNU attributes, and 21 pure + 36 const library functions from clang's table | `library_function_facts` | 109/50 and 101/45 |
| `always_inline` (16), `cold` (17), `hot` (18) | the GNU attributes; cold/hot read from the attribute list (`find_attribute`); cold and hot together: only the first (gcc's rule) | `compute_func_state` | 299/25 |
| `NORETURN` of library functions | `abort`, `exit`, `_Exit`, `_exit` | table letter `r` | 301/815 |
| `NOTHROW` of library functions | C++ streams only; 231 names with clang's `n`, not `fprintf`, `vprintf`, `vfprintf` | table letter `n`; `NOTHROW_EXCLUSIONS` in the generator | 799/0 |

- **The library data is generated**: `dev_tools/builtins/
  generate-libc-builtins.py --clang-builtins-inc
  /edg/llvm-cir/include/clang/Basic/Builtins.inc --llvm-sha 2d6f180e
  --attrs-output src/builtin_libc_attrs.h`. Only unconditional letters
  (`U`, `c`, `r`, `n`); `e`/`g` are not used (Linux has math-errno on), so
  `sin` is not const. It applies to a routine of C language linkage, not a
  member or static, without an asm label, named like the library function
  (also as `__builtin_X`), with the table's result/parameter classes
  (`routine_has_signature`).
- **Writer fixes found by running things**: `EMPTY_ASSIGN_RHS` of a
  NONTRIVIAL_ABI value (boost/multiprecision); INLINE_ONLY on the drop
  issue (`defining || unsupported`: glibc's `_FORTIFY_SOURCE` wrappers);
  reading a `std::nullptr_t` is `DROP` + `CONST_NULL`; loop conditions
  (`loop_condition` folds a comma with a constant right operand and pure
  left operands, and `__builtin_expect` of constants); volatile accesses
  and dynamically initialized or extern `thread_local` variables are
  impure; the TU-local memo does not memoize walks of closures in default
  arguments and initializers (`tu_provisional`).
- **S4 items**: (1) TU-local memo in `cir_entity_is_tu_local`; (2) inline
  record helpers (`ecil_put_uint`, `ecil_rec_begin`, …) that keep the
  arity and introduction checks, because CHECKING is TRUE in every build,
  release included; (3) `enk_field` is a leaf in `expression_is_pure`
  (C++17 spills); (4) the sampled name check: the first
  `ECIL_NAME_CHECK_ALL` (2000) eligible functions and 1 in
  `ECIL_NAME_CHECK_STRIDE` (16) of the rest, every corpus case and golden
  in full, `ECIL_CHECK_ALL_NAMES=1` for all; (5) no `compute_func_state`
  cache (under 1%, and it depends on FE fields that change after the first
  issue); (6) the keep #2 comment names requires-expressions.
- **Region keep**: not narrowed (measured: every one of the 2400 large
  regions of the template TU is kept for four overlapping reasons;
  disabling the local-class reason moves neither RSS nor any stream).
  `m5_writer.md` §6.

### 1.5 New CIR-gated FE hooks

All under `BACK_END_TYPE == BACK_END_TYPE_CIR`. The other configurations
preprocess identically except for line numbers in assertion strings
(checked for 32 TUs of cpfe and cpfe-cp); the gate's `identity` step
passes (720 files).

| File | Hook |
| --- | --- |
| `src/decl_spec.c` | `cir_inline_keyword = TRUE` at the `inline` keyword |
| `src/decls.c` | `update_routine_decl_modifiers` calls `ecil_note_inline_specifier(routine)` |
| `src/symbol_tbl.h`, `.c` | `cir_inline_keyword` in `a_decl_modifiers_block`, cleared in `clear_decl_modifiers_block` |
| `src/mem_tables.h` | `ecil_inline_specifier:1` in the IL entry prefix next to `ecil_id` (the prefix does not grow; it dies with the routine's region) |
| `src/lower_name.c` | the `cir_entity_is_tu_local` memo and `tu_provisional` |
| `src/mem_manage.c` | comment only |

A template instance does not copy the bit:
`template_pattern_has_inline_specifier` asks the template's routine when the
hint is computed (clang's answer for a later `inline` redeclaration).

## 2. Decisions taken

Keep these unless there is a reason to change them. They add to
`handoff_3.md` §2. (user) marks a user decision.

1. **Gate commits** on local `cir` (M4.1, M5p, M5a, M5); never push (user).
2. **Concurrency**: room only, no implementation (user;
   `m5_design.md` §7).
3. **Lazy decode**: the library defers bodies per function and replays at
   finish only (user, plus the measurement: finish-only replay was 1.5
   points faster than incremental). A missed edge is an error, never a
   silent undefined reference. `EAGER_BODIES` is the escape hatch and the
   mode for tools that compare every body.
4. **Bounded memory**: chunk design (a) behind a threshold (user).
   Output-identical; design (b) (per-chunk code generation) was rejected
   (the output differs, dead linkonce functions are kept, no cross-chunk
   optimization). The CIR passes stay whole-module; a chunk failure is a
   diagnosed error.
5. **The chunk threshold is 4000** emitted bodies, raised from 2000 after
   the gate without re-runs (user: "Raise to 4000, but do not re-run
   anything"); the generated C++ unit's S1 miss is recorded, not fixed
   (user: "Keep 4000, annotate the miss"). §0.1.
6. **The LLVM pin** moved to 2d6f180e (user: "bump if it helps"; evidence
   in `m5_upstream_drift.md`).
7. **Code quality**: parity with clangir at the pin on the high-value
   attributes; the residue is reported, not gated (user, 2026-10-20).
   Where clangir lacks a fact the library does not add it.
8. **Library facts** come from clang's builtin table (U, c, r, n;
   unconditional letters only), excluding glibc's cancellation points
   (`fprintf`, `vprintf`, `vfprintf`) from nothrow.
9. **Allocation elision** follows clang: `builtin` on new-expression calls
   (user).
10. **The generated C++ unit's S1 target applies at -O0 only**; -O2 is
    reported (user).
11. **`--c` for C side files**, in the CIR configurations only (user).
12. **`__OPTIMIZE__` / `__NO_INLINE__`** predefined by gcc's rules (user).
13. **edgdocker forwards `EDG_CPFE_DEFAULT_OPTIONS`** only when set (user).
14. **Lean gates**: slices run the steps their change set can affect; the
    full gate only at milestones (user: "Do we actually need to spend
    several hours testing with each change? This seems wasteful").
15. **`used` → `llvm.compiler.used`**, as clang and clangir (not
    `llvm.used`). `used` static data members of class templates follow gcc
    (emitted when the class instance is complete; clang ignores `used`
    there).
16. **INLINE_HINT on macOS**: the native macOS cpfe-cir has no
    `DECL_MODIFIERS_IN_USE`, so it sends no `INLINE_HINT` and does not claim
    the feature bit (`ECIL_INLINE_HINTS` in `ecil_write.h`). Never record
    goldens on the Mac.
17. **Structor variants**: no canonicalization of C1/D1 to C2/D2 for
    classes without virtual bases (clangir does it; classic clang and we do
    not; the code is identical). **Library builtins stay plain calls**
    (`memcpy`, not `llvm.memcpy`); a possible M6 code-quality item.
18. **Verifiers** run only under `VERIFY_EACH`; the module is verified
    before and after the CIR passes then.
19. **The library never depends on FE macros**: INLINE_HINT, EXTERN_C and
    NOTHROW come from the stream.

## 3. How to build, run and test (cookbook)

Everything in `handoff_3.md` §3 still holds. New:

- **Gates** (on the Mac, repo root, SSH master socket open):
  - `edg-cir-checkpoint --lean AREAS --lean-base REF --tree T`: the steps
    the areas need; the areas are checked against `git diff REF` before
    anything is synced, and an uncovered change set is refused. Areas
    include `writer`, `library`, `toolchain`, `eccp`, `tests`,
    `tests-triage`, `tests-smoke`, `tooling`, `shared-src`, `lazy`,
    `speed`, `crashes`, `attr`, `chunk`, `names` (the map is at the top of
    `edg-cir-gate-evidence`).
  - `edg-cir-checkpoint --milestone --tree m5-int --jobs 12 --detach`:
    every step against the M5p baselines and `m5a-base-perf.json`.
  - `--only STEP` through `--extra` re-runs one step on the same evidence
    directory.
- **New steps**: `opt-corpus` (both corpora at -O2 against -O0, a canary,
  `cir-opt-triage.txt`), `lazy-identity` (lazy against eager,
  `cir-lazy-triage.txt`), `attr-diff` (gated attributes plus residue,
  `cir-attr-known.txt`), `codequality` (a report), `chunk-identity`,
  `chunk-rss`, `slow-below-threshold` (run at `-j 2`, `SLOW_JOBS`; `-j 16`
  flaked under load), `names-check` (`ECIL_CHECK_ALL_NAMES=1`), and status
  gating against `m5p-base-status` (`cir-status-triage.txt`).
- **Baselines**: `m5p-base-asm`, `m5p-base-cpp-asm`, `m5p-base-status`,
  `m5a-base-perf.json` (with `--perf-realworld pinned --perf-levels 0,2
  --perf-targets s1`), `m5a-base-rss-curve.json`, and the day baseline
  `day-base/tests/runs/2026.10.12-13.13.25`. The next milestone should take
  M5 baselines from `m5-int` first.
- **Library on the box** (`~/edg-cir-lib`, cir-env container):
  `cir_src/scripts/box_allpresets.sh` (three presets, ctest); `box_all.sh
  [preset]`; `box_lit.sh [-a "--filter=name -v"]`; `box_ninja.sh`.
- **A fresh FE in a scratch tree**: rsync `src cmake include_* lib_src util
  nt_util bases dev_tools CMakeLists.txt CMakePresets.json`, then `cmake
  --preset linux-gcc-release-cir && ninja -C build/gcc-release-cir
  cpfe-cir` (30 s); link `build/gcc-cir` to it for the dev_tools.
- **attr-diff as the gate runs it**: `scripts/box_attrdiff2.sh TAG`.
- **Chunk identity**: `scripts/box_chunk_identity.sh TAG --sets pinned` (or
  `--streams c=DIR --streams cpp=DIR`); the -O2 corpus: `box_chunk_o2.py
  DIR 2`.
- **Peak memory**: the `peak RSS KiB` line of `edg-cir --stats --emit=asm`,
  with `--chunk-threshold=4294967295` (never) or `--chunk-functions=500
  --chunk-threshold=1` (forced).
- **The eccp path in a scratch tree**: export the scrape variables
  (`edg-scrape-compiler`), `CPFE=$CPFE_CIR`,
  `EDG_BASE=bases/docker/cir-env/gcc`, `EDG_USE_SYSTEM_HEADERS=1`, then
  `util/eccp.sh -O2 --g++ file.cpp`.
- **Writer**: `cir_src/test/writer/tools/stream_sweep.py --body-hash`
  (corpus streams that change), `cg_writer.py` (the writer's share of FE
  instructions), `gen_perf_tus.py`; unit harness
  `cir_src/test/writer/unit/run_unit.sh`.
- **Regenerate the library data** after an LLVM bump with the generator
  command in §1.4 and diff the NOTHROW set (the generator reports the
  exclusions).

## 4. Known gaps

### 4.1 attr-diff: every difference, annotated

Source: `attr-diff` in the gate evidence, ours (`edg-cir
--no-llvm-passes`) against `clang -fclangir -disable-llvm-passes` at -O0
and -O2, over the probe set (the C and C++ smoke lists, 138 TUs; clangir
could not compile 11 of them and ours failed on 7) and the six pinned
units (all compile on both sides).

How to read it:
- The causes are **likely causes**, taken from the gate evidence and from
  what cir-lib and cir-writer found. They were **not verified entry by
  entry**.
- **RISK**: only ours has the fact. If it is wrong, it can miscompile.
- **MISSED**: only clangir has the fact; an optimization opportunity.
- Counts are pinned -O0 / pinned -O2 / probe -O0 / probe -O2.
- Symbols compared: probe 779 (-O0) and 853 (-O2); pinned 665 and 734.
  Symbols only one side has (ours / clangir): probe 22 / 149 at -O0 and 95
  / 237 at -O2; pinned 117 / 114 at -O0 and 1667 / 100 at -O2. Ours emits
  dead libstdc++ bodies that clangir omits at -O2 (`--no-llvm-passes`
  skips our GlobalDCE); that is not a fact difference.

**The 539 gated differences that no known entry explains** (probe 351,
pinned 188). Gated attributes: nounwind, noundef, nonnull,
dereferenceable, align, memory, mustprogress, noreturn, builtin,
nobuiltin, inlinehint, alwaysinline.

| attribute | side | position | count | examples | tag | likely cause |
| --- | --- | --- | --- | --- | --- | --- |
| nounwind | ours | call fn | 118 (0/0/59/59) | `exit`, `getfoo`, `bar` | RISK | calls of functions we mark nounwind and clangir does not, in the probe C tests (cir-lib: the library NOTHROW table). True for the library functions; to be checked for the user functions |
| alwaysinline | clangir | fn | 93 (43/50/0/0) | `__new_allocator<char>::max_size`, `<int>::max_size`, `allocator<_Rb_tree_node<…>>` | MISSED | libstdc++ functions declared `always_inline`: clangir marks them, so its AlwaysInliner removes the calls even at -O0; our streams carry no key 16 for them. Likely `always_inline` without the inline specifier, which the FE ignores (§4.2); cir-writer to confirm |
| nounwind | clangir | fn | 66 (1/1/32/32) | `memcmp`, `abort` | MISSED | probe C TUs: clangir marks every C function nounwind, the `abort` declaration included; library NOTHROW is sent in C++ streams only (§1.4). Pinned: `memcmp` |
| nounwind | ours | fn | 42 (4/6/16/16) | `std::terminate`, `abort` | RISK | `std::terminate` (noexcept in libstdc++) and the C++ declaration of `abort`; clangir's declarations lack it. The fact is true for both |
| noreturn | ours | fn | 40 (0/1/18/21) | `__cxa_bad_cast`, `abort`, `_exit` | RISK | runtime and library functions we declare noreturn and clangir does not; the fact is true for them |
| noreturn | ours | call fn | 34 (0/0/17/17) | `abort` | RISK | calls of `abort`; the fact is true |
| dereferenceable(N) | clangir | call arg | 22 (5/5/6/6) | `_Function_base::_Base_manager`, `function<int(int)>` constructor | MISSED | the call-site side of the parameter rows below (sizes 12 against 16, 1 against 4) |
| builtin | clangir | call fn | 21 (12/9/0/0) | `_ZdlPvm`, `_Znwm` | MISSED | calls of the replaceable `operator new`/`delete` that clangir marks `builtin` and we do not: we mark the new-expression and synthesized calls; clangir also delete-expression and implicit ones (likely) |
| dereferenceable(N) | clangir | param | 20 (5/5/5/5) | `_Function_base::_Base_manager` helpers | MISSED | a record-type reference parameter without our fact (likely a pointee whose size the writer does not send) |
| dereferenceable(N) | ours | call arg | 16 (2/2/6/6) | `_Tuple_impl::_M_head`, `__get_helper`, `BOOL::BOOL(int)` | RISK | the parameter rows below at their call sites; `BOOL::BOOL(int)`: `this` dereferenceable(4) ours against (1) clangir |
| dereferenceable(N) | ours | param | 14 (2/2/5/5) | `_Tuple_impl::_M_head`, `__get_helper`, `SvMetaSlot` constructors | RISK | (a) `SvMetaSlot` constructors: `this` dereferenceable(16) ours against (12) clangir; the class's data size is 12 and its size 16, so we likely use sizeof where clangir uses the nvsize; (b) `std::_Tuple_impl` helpers in mp11 where only ours has the fact |
| align N | clangir | call arg | 12 (2/2/4/4) | `llvm.memcpy`, `llvm.memset` | MISSED | `llvm.memset` argument `align 16` against our `align 8` (probe); `llvm.memcpy` `align 4` that we do not send (iostream) |
| nonnull | clangir | call arg | 8 (4/4/0/0) | `_Rb_tree::_Node_traits`, `_Rb_tree_insert_and_rebalance` | MISSED | the call-site side of the next row |
| nonnull | clangir | param | 8 (4/4/0/0) | `_Rb_tree::_Node_traits` helpers | MISSED | a reference parameter without our nonnull (likely) |
| align N | ours | call arg | 8 (0/0/4/4) | `llvm.memset` | RISK in kind only | ours `align 8` where clangir has `align 16`: we claim less alignment, which is conservative |
| mustprogress | clangir | fn | 6 (6/0/0/0) | `allocator<char>` copy constructor and destructor | MISSED | implicitly defined members: clangir adds mustprogress, we do not (likely) |
| nounwind | clangir | call fn | 2 (1/1/0/0) | `memcmp` | MISSED | the call of `memcmp` in the pinned units |
| noundef | ours | call arg | 2 (0/0/1/1) | `_ZdaPv` | RISK | the `delete[]` argument in probe `new42` |
| builtin | ours | call fn | 2 (0/0/1/1) | `_ZdaPv` | RISK | the same `delete[]` call marked `builtin` by us only |
| align N | clangir | call ret | 1 (0/1/0/0) | `use_facet<numpunct>` | MISSED | the returned reference in lexical_cast: clangir sends align, dereferenceable, nonnull and noundef; we do not |
| dereferenceable(N) | clangir | call ret | 1 (0/1/0/0) | same | MISSED | same call result |
| nonnull | clangir | call ret | 1 (0/1/0/0) | same | MISSED | same call result |
| noundef | clangir | call ret | 1 (0/1/0/0) | same | MISSED | same call result |
| noundef | clangir | ret | 1 (0/1/0/0) | same | MISSED | the declaration of `std::use_facet` |

**Three to look at first** (all RISK):
1. **dereferenceable on `this`**: `SvMetaSlot` (16 against 12) and
   `BOOL::BOOL(int)` (4 against 1). cir-lib should check which size the
   rule uses and what the writer sends as nvsize. Note on severity:
   `dereferenceable` only promises that loads are safe. 16 against 12 on a
   class whose tail padding a derived class may reuse stays inside the
   derived object. A size larger than the whole object (as 4 against 1
   may be) is a real wrong fact.
2. **Call-site nounwind and noreturn only on our side** (`abort`, `exit`
   and user functions in the probe C tests): correct for the library
   functions; check the user functions.
3. **`builtin` on a `delete[]` call** (`_ZdaPv`, probe `new42`): only ours.

**The residue** (1913 differences, not gated; examples in `residue.txt`
in the evidence):

| category | count | likely cause |
| --- | --- | --- |
| callset | 1805 (657/636/256/256) | a callee only one side calls: calls clangir inlines or folds (always_inline `__new_allocator` functions, `boost::wrapexcept` destructors), `basic_string` and `pair` constructors where the two sides call different functions, and `llvm.mem*` calls only ours has (the pair matchers explain only the memcpy/memmove/memset pairs and C1/D1 against C2/D2) |
| call sret | 30 (12/18/0/0) | the `sret` type is spelled differently (`%class.allocator.1` against `%"class.std::…"`); not a fact |
| param sret | 28 (12/16/0/0) | the same, on the parameter |
| call noalias | 18 (0/0/9/9) | `_Znwm` calls: only ours has `noalias` on the result. RISK in kind; the replaceable allocation function does return fresh memory |
| ret noalias | 16 (0/0/8/8) | the `_Znwm` declaration, the same |
| fn cold | 10 (4/6/0/0) | both directions: `std::terminate` cold only in ours; `__throw_*` were cold only in clangir *(S-checkpoint)* |
| fn uwtable | 6 (6/0/0/0) | `uwtable` only clangir has on implicitly defined functions, pinned -O0 |

**The 7 known entries** (`cir-attr-known.txt`) explained 1622 differences
at the gate (pinned 1616, probe 6). The split per entry is from the S2
checkpoint (`checkpoint-20261020-075708`, 1616 then); the gate evidence does
not keep it.

| entry | explains (S2 checkpoint) | reason |
| --- | ---: | --- |
| `call only-ours:_ZSt7forward*` | 804 | clangir emits the argument's address for `std::forward` (a builtin) instead of a call |
| `call only-ours:_ZSt4move*ONSt16remove_reference*` | 90 | the same for the one-argument `std::move` |
| `call only-ours:_ZSt9addressof*` | 84 | the same for `std::addressof` |
| `call only-ours:_ZSt11__addressof*` | 12 | the same for `std::__addressof` |
| `call only-ours:_ZnwmPv*` | 54 | a new-expression never calls the reserved placement `operator new` |
| `pair structor-variants` | 542 | we call C1/D1 where clangir calls C2/D2 of the same class, same parameter count (no VTT) |
| `pair builtin-mem-calls` | 30 | memcpy/memmove/memset are library calls in ours and `llvm.mem*` in clangir; only the intrinsic's fixed attributes and our noundef/nounwind are explained |

cir-review checked the first five against clangir's source at the pin
(`CIRGenBuiltin.cpp:3140-3145`, `:3038-3041`, `CIRGenExprCXX.cpp:1603-1610`).

**nounwind share** (symbols both sides have; ours against clangir):

| kind | pinned -O0 | pinned -O2 | probe -O0 | probe -O2 |
| --- | --- | --- | --- | --- |
| definitions | 264/545 (48.4%) vs 264 | 301/619 (48.6%) vs 301 | 305/584 (52.2%) vs 305 | 313/596 (52.5%) vs 313 |
| declarations | 38/114 (33.3%) vs 35 (30.7%) | 53/115 (46.1%) vs 48 (41.7%) | 64/195 (32.8%) vs 80 (41.0%) | 64/257 (24.9%) vs 80 (31.1%) |
| call sites | 537/1197 (44.9%) vs 475 (39.7%) | 585/1250 (46.8%) vs 525 (42.0%) | 437/740 (59.1%) vs 363 (49.1%) | 445/934 (47.6%) vs 371 (39.7%) |

Definitions are at parity everywhere. Declarations are ahead on the pinned
units and below clangir on the probe set (the `abort` declaration in the C
tests, `memcmp`); the step's nounwind target fails only there.

**The `dso_local` question** (answered): the new `callq f@PLT` calls in the
C++ assembly (17 files against M5p) are clang parity: a declaration without
`dso_local` goes through the PLT, as in clang (classic and `-fclangir`,
PIE default), unchanged since M3. The new entries come from the INLINE_ONLY
drops and from declarations that were defined lazily in the stream before.
No change needed.

### 4.2 Other gaps

- **The S1 miss** on the generated C++ unit and the stale threshold
  numbers (§0.1). **Open.**
- **The EDG mangler** (FE, every back end; `m5_writer.md` §7): dependent
  conditions in function types mangle differently from g++ and clang:
  `enable_if<ns::is_d<T>::value, T>` as `sr1n4is_d…` (g++
  `srNS_4is_dIT_EE…`, clang `sr4is_d…`), and a variable template in the
  condition as `L_ZNS_2vtEET_E` (both compilers `2vtIT_E`). It shows in
  libstdc++ (`duration_cast`, `__invoke_r`) and Boost (`lcast`). An ABI
  issue for mixed objects, in `lower_name.c`, not CIR-gated.
- **Builtins**: `__builtin_constant_p` (libstdc++ algorithms and
  containers, Boost algorithm and range; codequality's `sort` kernel does
  not build); `__atomic_*` (memory, regex, Boost format); x86
  `__builtin_ia32_addcarryx_u64` and `__builtin_ia32_sbb_u64`
  (boost/multiprecision still fails at -O0 and -O2).
- **`always_inline` without the inline specifier** is not sent (the FE
  ignores it, as gcc 4.7+); clangir honors it. This is the likely cause of
  the 93 alwaysinline MISSED rows (§4.1), not yet confirmed.
- **always_inline at -O0**: `inline_only_declaration` makes every
  INLINE_ONLY function a declaration at -O0; clang's `shouldEmitFunction`
  keeps an `always_inline` gnu_inline body (GCC `<*intrin.h>`). With key 16
  the rule can be added.
- **Library NOTHROW in C streams**: not sent (C++ only), so C declarations
  such as `abort` lack nounwind (the 66 MISSED rows).
- **Sized delete**: whether the FE picks `_ZdlPvm` or `_ZdlPv` differs per
  case from clangir (27 probe cases); not traced beyond D0.
- **clangir's D0 trap**: the deleting destructor of a class with virtual
  bases is `llvm.trap` in clangir (virtual15a: `_ZN2S3D0Ev`); ours is a real
  body, so 8 `_ZdlPvm` calls are only ours.
- **`.N` suffixes under chunking**: at -O2 about 1.7% of the corpus streams
  (forced to one function per chunk) differ from the whole module only by
  the numeric suffix of local data an LLVM pass names (`.L.crctable.1`
  against `.7`): the linker counts name collisions. The pinned units are
  byte-identical.
- **Debug info**: none. Every chunk would make its own `DICompileUnit`;
  chunking will need one per TU (comment in `chunk.cpp:copy_attributes`).
- **`isascii`/`toascii`** get their attributes from LLVM's inference only
  (not in clang's table).
- **Extern `thread_local`** variables are treated as possibly dynamically
  initialized (impure); a local one is not.
- The diagnosed lists of `handoff_3.md` §4 are otherwise unchanged
  (vector types, multiversioning, inline asm, …).

## 5. Needs re-look

- **NOTHROW truthfulness**: NOTHROW now has two sources, the FE
  (`is_non_throwing_routine`/`is_nothrow_type`) and the library table (231
  names). A wrong NOTHROW silently skips cleanups. Re-run the generator
  and diff the set on every LLVM bump.
- **User `pure`/`const` functions** give `nounwind willreturn`; in C++ such
  a function can throw (clang does the same). `torture/pr101373` (a `const`
  function that throws, result unused) fails at -O0 because FastISel drops
  a `nounwind willreturn memory(none)` call with an unused result; `clang++
  -O0`, `clang++ -fclangir -O0` and `g++ -O0` all segfault on it (the test
  is `dg-xfail-run-if PR100409`). Review if a program breaks.
- **INLINE_ONLY on the drop issue and the INLINE_HINT rules** were derived
  by probing clang and clangir at the pin; re-probe on an LLVM bump (the
  `always_inline` and implicit-member rules are CodeGen behavior).
- **Lowering drift on the next LLVM bump**: `lowering-drift.test` compares
  with upstream's `lowerDirectlyFromCIRToLLVMIR`. When it fails, read that
  function's diff, then update `lower_to_llvm_ir` and the chunk
  assumptions (the chunk path calls the same function per chunk).
- **A new lowering that reads a callee's body** must be told to the
  chunker (the catch-copy thunk is the one case today: `inline_bodies_` in
  `chunk.cpp`).
- **The attr-diff RISK rows** (§4.1), first the `this` sizes.
- **`a_entity::op` after chunking** points at erased operations; nothing
  dereferences them after `lower_in_chunks` (the ASan run of the chunk
  tests is clean).
- **The `-j` flake in slow-below-threshold**: at `-j 16`
  `edg-cir-chunk-ir` shows +0.2–0.3% instruction excess on some streams,
  at `-j 2` +0.0012%. The suspected cause (the main thread's wait for the
  worker under load) is not verified.
- **LeakSanitizer noise**: "Unable to get registers from thread" appears
  at random under `linux-cir-asan` in the container (ptrace); real ASan
  reports start with `ERROR: AddressSanitizer`.
- **The prefix bit** (`ecil_inline_specifier`): nothing depends on it
  surviving a region today; if the local-class keep is narrowed later,
  check `template_pattern_has_inline_specifier`.
- **`loop_condition`** drops the left operands of a comma only when
  `expression_is_pure`; revisit if `expression_is_pure` changes.
- **The sampled name check** checks 1 name in 16 beyond the first 2000
  functions; a mangler bug on a shape that only large TUs have would be
  seen late.
- **Release FE macro pins** (`handoff_3.md` §2 item 7): unchanged (M7).

## 6. Future milestones (guidance)

### M5 follow-ups (an M5.1, if the user wants one)
- A size-based chunk threshold, then the re-runs of §0.1 (perf, rss,
  slow-below-threshold, chunk-rss) and new M5 baselines.
- The attr-diff RISK rows (§4.1) and the cheap MISSED ones: library
  NOTHROW in C streams, `builtin` on delete-expression calls, mustprogress
  on implicit members, `always_inline` without the inline specifier (needs
  an FE answer), always_inline at -O0.

### M6 — remaining gaps and documentation
- The builtins of §4.2 (`__builtin_constant_p` first: it blocks libstdc++
  algorithms), the attr-diff residue, library builtins as `llvm.mem*`
  intrinsics (beyond clangir parity), debug info (one compile unit per TU
  with chunking).
- The diagnosed lists of `handoff_3.md` §4, then user and developer
  documentation.
- The EDG mangler issue as FE maintenance (not CIR-gated; every back end).

### M7 — release and platforms
- As in `handoff_3.md` §6: Microsoft mode, native Windows, the release FE
  macro pins.

## 7. Process notes for the next session

- **What worked**:
  - review of code before landing: it found two silent -O2 miscompiles
    (available_externally of self-recursive glibc inlines such as
    `memchr`, BLOCK in the S2 library review; volatile purity, the
    required fix of the S4/S5 review) and invalid IR (`hot` with `cold`,
    final review). The checkpoint triage found a third miscompile
    (`pr100382`: pure/const calls marked `nothrow` lost their invoke in a
    try body);
  - scratch patch series (one commit per item, prepared in a box scratch
    tree while a gate ran), applied with `git apply --check` first and
    built and lit-tested per commit;
  - identity oracles for behavior-preserving work (lazy against eager,
    chunked against whole) over both corpora and the pinned units;
  - lean gates with the change-set check;
  - checking a claim with the real compilers before acting on it.
- **Lessons**:
  - The user wants gate effort matched to the change: lean gates, narrow
    open-ended targets to the high-value items, fewer intermediate
    checkpoints, and an honest time-left estimate when asked.
  - Check a count before using it in a decision: "3600 bodies" (deferred
    and replayed) was taken for the emitted total (about 6000), which made
    the threshold proposal wrong; it was corrected before the user decided.
  - CHECKING is TRUE in every build: writer speed work must keep the
    checks; "assertions off" is not a release saving.
  - A memo keyed on addresses needs the lifetime of the key (the
    file-scope guard of the TU-local memo, the region-bound prefix bit).
  - The profile tool needs the same care as the code (`cg_writer.py`
    took the previous call line's callee file).
  - zsh/BSD: no word splitting of a quoted list, `sed -i ''`, no `timeout`
    on the Mac; put nested commands in a script and scp it; `/tmp` inside
    the container is private to one `run`; `rsync --delete` resets mtimes.
- **Team**: the same four agent types; update `~/.claude/agents/cir-*.md`
  to point at this document.

## 8. Open questions for the user

1. **Next milestone**: an M5.1 first (size-based chunk threshold and the
   re-runs, the attr-diff RISK rows, the cheap MISSED facts; §6), or
   straight to M6?

M5.1 Only.

2. **attr-diff gating**: keep the step FAIL until the RISK rows are
   checked, or record the checked rows as known entries so the step can
   pass?

Record the checked rows as known entries so the step can pass.

3. **The EDG mangler issue** (§4.2) is an FE change outside the CIR gate
   that changes symbols for every back end. Should the CIR project fix it,
   or report it as FE maintenance?

CIR should fix it.

4. **Box disk**: 45 GB free (90% used). May the next session prune
   `m5p-int` and `m41-int` (already pruned to their evidence and KEEP
   markers) or `m3-final`/`m4-final` after taking the M5 baselines?

Prune everything that's not needed. You are allowed to delete anything you make.

## 9. Gate evidence

**M4.1** (commit `9d518c3178`; lean gate, fingerprint `a56fc639`, evidence
`checkpoint-20261019-151413`): build, identity (720 files), isolation, smoke
40/40, cir-suite, C corpus 1656/2561, C++ corpus 1472/1823 (M4 1461), modes,
cpp-modes, stress, writer-stress, ctest, day, poison, release-smoke,
symbols, mixed-tu 70/70 and opt-corpus pass (one our-IR case left,
`builtin-ctype-1`, fixed in S2). asm-diff and cpp-asm-diff fail as expected,
attributed.

**M5p** (commit `e6bdad03f4`; lean toolchain gate, fingerprints `9ed74d7b`
and `9e1c852a`, evidence `checkpoint-20261019-171311`): build, smoke,
cpp-smoke, cir-suite 233/233, both corpora (statuses identical to M4.1),
opt-corpus, ctest, symbols, mixed-tu 70/70, eh-probes, m4-stress,
cpp-stress, stress, crash-sweep (0 problems over 29,739 cases), perf (-1%
to -3.4%), release-smoke and tools-test pass. asm-diff and cpp-asm-diff fail
as expected (94 C and 209 C++ files, every one attributed; `design.md` §14).
rss was over one stale ceiling; the ceilings are re-derived.

**M5a** (commit `77a24448fa`; lean gate, fingerprint `b8fedd56`): all 16
steps pass (build, ctest, cir-suite 233/233, both corpora, modes, cpp-modes,
poison, writer-stress, cpp-writer-stress, symbols, mixed-tu 70/70,
opt-corpus, tools-test, both asm-diffs); 0 asm, status or opt-corpus
differences against M5p.

**M5** (this commit; tree `~/edg-cir-config/m5-int`, fingerprint
`52e3c1ace35157582571aa704e01e54f491f5e53dc3f79aa655768b7659f1984` over
1809 files, equal on the Mac and the box, evidence
`.tmp/checkpoint-20261020-102543`; `edg-cir-checkpoint --milestone --tree
m5-int --jobs 12`, fresh sync, build directories wiped). 38 steps: 33
passed on the first run; `opt-corpus` passed on its re-run after one triage
line (§9.4); 4 FAIL and are open.

### 9.1 Step table

| Step | Result | Detail |
| --- | --- | --- |
| build | PASS | `build/cir`, `build/gcc-cir`, `build/gcc-release-cir` (91 s) |
| identity | PASS | 720 files, 4 presets, 0 problems |
| isolation | PASS | 5 checks |
| smoke | PASS | 40/40 in both configurations |
| cir-suite | PASS | 259/259 in both configurations |
| corpus | PASS | 1661/2561; 0 CIR-specific crashes; 0 lines differ from the recorded expectations; 0 PASS→not PASS |
| modes | PASS | 2561 identical, 3 diagnostic-only, 0 different |
| stress | PASS | |
| writer-stress | PASS | 270 tests |
| rss | PASS | 24 measurements, the highest at 80% of its ceiling |
| day | PASS | statuses identical to the baseline; 45432 executed in each of `edg_x86_64` and `edg_x86_64_cp` |
| ctest | PASS | 4/4 presets |
| asm-diff | **FAIL** (expected) | 2545 identical, 13 other, 6 status changed (§9.3) |
| perf | **FAIL** (open) | largest change +2.60% over 43 rows; the S1 target missed on the generated C++ unit (§9.5) |
| cpp-smoke | PASS | 98/98 in both configurations |
| cpp-corpus | PASS | 1471/1823 in both configurations; 0 crashes (+1 FE crash the reference has too); 1 PASS→not PASS, triaged (`torture/pr101373`) |
| cpp-modes | PASS | 1778 identical, 1 diagnostic-only, 0 different |
| cpp-writer-stress | PASS | 289 tests |
| cpp-stress | PASS | 9 cases within 30 s and 400 MB |
| cpp-stress-asan | PASS | 9 cases within 30 s and 1200 MB |
| poison | PASS | 2564 + 397 + 1779 tests, 0 INFO |
| release-smoke | PASS | C 40/40, C++ 98/98 in both configurations, 0 only in release |
| cpp-asm-diff | **FAIL** (expected) | 1664 identical; 221 other, 45 symbols, 26 block layout only, 2 status changed (§9.3) |
| crash-sweep | PASS | C 11,301 cases (43 excused, 15 fatal errors); C++ 18,438 (42 excused, 203 fatal errors); extra (eh, rtti, cir) 452 (0, 0); 0 problems |
| abi-diff | PASS | 20 files, 450 identical (+11 normalized, 56 loose); 9 not compared (known tool limits) |
| symbols | PASS | 15 files, 8 known differences |
| mixed-tu | PASS | g++ 70/70, clang 70/70 |
| eh-probes | PASS | 31/31 |
| m4-stress | PASS | 10 cases, the slowest at 21% of its limit |
| opt-corpus | FAIL, then PASS | §0 and §9.4 |
| tools-test | PASS | 225 self-tests |
| lazy-identity | PASS | §9.6 |
| attr-diff | **FAIL** (open) | 539 unexplained in the gated attributes, residue 1913 (§4.1) |
| codequality | PASS (report) | §9.6 |
| chunk-identity | PASS | §9.6 |
| chunk-rss | PASS | §9.6 |
| slow-below-threshold | PASS | §9.6 |
| names-check | PASS | §9.6 |

### 9.2 Statuses against `m5p-base-status`

- C, 9 changes: 5 improvements (`20031102-1`, `loop-2`, `pr48837`,
  `tree-ssa/inline-2`, `tree-ssa/pr22591` now PASS: the inline-only
  definitions are kept as available_externally); 4 `torture/stackalign`
  cases from CATASTROPHE(COMPILE) to FAIL(COMPILE).
- C++, 1 change: `torture/pr101373` PASS→FAIL, triaged for both
  configurations (§5). `pr100382` is fixed and passes.

### 9.3 asm attribution against `m5p-base-*`

1515 of 1805 C++ streams and 294 of 2564 C streams differ from the M5p
streams (ignoring the header features word). S1 and S3 change no stream and
no assembly (`lazy-identity` and `chunk-identity` pass). Every differing asm
file has a changed stream. Categories are by the pattern of the diff, not
per cause.

C (13 other, 6 status):
- 10 frame or spill changes with fewer instructions: S4 purity;
- 3 calls through the PLT (`pr48837`, `pr78333`, `torture/inline-2`): the
  INLINE_ONLY drops;
- status: `torture/stackalign` `alloca-2`, `alloca-3`, `push-1`,
  `vararg-3` change their compile failure from rc 4 to rc 2, and
  `builtin-apply-4` now produces asm: S2 library diagnostics and support.

C++ (221 other, 45 symbols, 26 block layout only, 2 status):
- 138 frame or spill changes: S4 purity (spill and reload pairs removed);
- 20 static-invoker closure locals: the null `this` (`xorl %eax,%eax;
  movl %eax,%edi`) becomes the address of a local closure object (`leaq
  N(%rsp)`);
- 17 calls through the PLT: clang parity (§4.1, `dso_local`);
- 2 `nullptr_t` loads removed (`nullptr28`, `opt/cleanup1`);
- 44 other, 26 block layout only and 45 symbols: all in the call-site
  (LSDA) tables. Calls that are now nounwind lose exception-table ranges;
  functions that need no table any more lose `.Lcst_*`, `.Lttbase` and
  `DW.ref.__gxx_personality_v0`. This grew from 17 files at the S2
  checkpoint after S2 round 2 (nothrow facts at call sites);
- 2 status: `va-arg-pack-1` and `va-arg-pack-len-1` now produce asm.

### 9.4 opt-corpus

The first run had 1 untriaged worse case, `torture/pr100382`. The line added
to `cir-opt-triage.txt`:

`imported/gnu/cpp/torture/pr100382.sft.cpp | -DTEST_NUMBER=1 | dg-option |
needs -fno-delete-dead-exceptions (dg-additional-options, which the corpus
no longer carries): g++ -O0 passes and g++ -O2 aborts without it, clang -O0
and -O2 abort too`

The triage tool proposed `ub`; `dg-option` is the more exact cause. The test
passes at -O0 (the call of a pure function that can throw is kept) and fails
only at -O2. The re-run passed: C 13 worse (dg-option 8, upstream 5) and 69
FAIL→PASS; C++ 8 worse (dg-option 3, ref-fails 1, upstream 4) and 8
FAIL→PASS; `_2p` smoke at -O2 97/98.

### 9.5 Performance

Baseline `m5a-base-perf.json` (the M5a tree, asm identical to M5p);
cachegrind instructions.

S1 targets, library instructions as a share of the baseline's:

| unit | -O0 | -O2 | limit |
| --- | ---: | ---: | --- |
| libstdcxx/chrono | 29.4% | 28.1% | 70% |
| libstdcxx/functional | 49.9% | 52.1% | 70% |
| libstdcxx/iostream | 33.4% | 41.6% | 70% |
| boost/mp11 | 62.5% | 62.0% | 70% |
| boost/circular_buffer | 26.4% | 27.1% | 70% |
| boost/lexical_cast | 32.9% | 38.8% | 70% |
| generated 6000-function C++ unit | **102.6% (missed)** | 90.3% (reported only) | 90% at -O0 |

The generated unit was at 88.3% at the S1 checkpoint; the default chunking
added in S3 costs it +21% in lowering (§0.1).

Other rows (before → after):
- C big file, two-process library: 42,194.3 M → 37,313.3 M (-11.6%) at
  -O0; 163,039.0 M → 158,073.5 M (-3.1%) at -O2; FE -2.5%.
- Generated C++ unit, two-process library: 13,078.6 M → 13,418.4 M
  (+2.6%, the largest change, over the 2.0% limit) at -O0; 22,183.7 M →
  20,041.4 M (-9.7%) at -O2; FE -4.5%.
- Pinned units, FE plus library, -O0: circular_buffer 4,685.8 M → 3,304.2
  M (-29.5%); lexical_cast 4,138.4 M → 2,386.3 M (-42.3%); mp11 474.8 M →
  438.2 M (-7.7%); chrono 600.3 M → 395.7 M (-34.1%); functional 2,963.5 M
  → 2,078.0 M (-29.9%); iostream 2,415.2 M → 1,461.4 M (-39.5%). The -O2
  rows are within a few points.
- Stream `cxx_funcs6000` through `edg-cir`: 18,624.6 M → 14,553.7 M
  (-21.9%).
- C corpus (2564 cases): FE 36,263.5 M → 36,464.9 M (+0.56%); library
  98,172.9 M → 88,010.6 M (-10.35%).

### 9.6 S1, S3, names and codequality

- **chunk-rss** (target met): c12000 forced 408.3 MB, 52.3% of 781.0 MB
  (limit 55%). Curve in MB (default, unchunked, forced): c3000 150.8,
  224.6, 150.2; c6000 234.6, 414.7, 234.7; c12000 408.4, 783.3, 408.3;
  cxx6000 118.8, 157.3, 118.8. At the gate's threshold (2000) the default
  equals forced on these units; at 4000 the c3000 default column changes
  *(not re-run)*.
- **chunk-identity** (forced one function per chunk, against unchunked):
  pinned -O0 and -O2 6/6 identical and byte-identical, 6 lowered in 2
  chunks or more (largest 310); C++ 1805 identical (1467 byte-identical),
  1460 chunked (largest 482); C 2564 identical (1885 byte-identical), 1882
  chunked (largest 468).
- **slow-below-threshold** (`-j 2`): 126 streams below the threshold, 0
  above, 0 different; the largest excess of the default run -0.0001%.
- **lazy-identity**:
  - pinned: 6/6 identical and byte-identical; deferred 4833, replayed 669,
    dropped 4164; 614 replayed bodies survive in eager (the oracle),
    overshoot 55;
  - C++: 1805 identical (1467 byte-identical); deferred 32,864, replayed
    8192, dropped 24,672; 7306 survive, overshoot 886;
  - C: 2564 identical (1885 byte-identical); deferred 1091, replayed 695,
    dropped 396; 674 survive, overshoot 21;
  - 0 eager-only diagnostics, 0 different; 156 C++ and 222 C streams fail
    in both runs before the finish with the same status and diagnostics.
- **codequality** (a report, not gated): 10 kernels, 9 built (`sort` needs
  `__builtin_constant_p`); ours against clang, geometric mean:
  instructions ×1.00, `.text` ×1.01.
- **names-check** (`ECIL_CHECK_ALL_NAMES=1`): 8 units ok (chrono 725
  `DECL_FUNC`, functional 2438, iostream 2269, mp11 137, circular_buffer
  3179, lexical_cast 3727, generated c6000 12,002, cxx6000 13,807); every
  stream is identical to the one without the variable.
- **df**: 45 GB free (90% used) before and after; `m5-int` is 17 GB.
  Nothing was pruned. `m41-int` and `m5p-int` are pruned to their `.tmp`
  evidence and KEEP markers; `m3-final`, `m4-final`, `day-base` and the
  `m*-base-*` baselines are untouched.

### 9.7 Reviews (session 4)

Every review was read-only by cir-review (Opus) on a frozen snapshot under
`build/review-snap-*`.

| Review | Verdict | Notes |
| --- | --- | --- |
| Session plan | PROCEED WITH FIXES | "Replay at finish" conflicted with the library in several places; resolved in the M5a design (`m5_design.md` §2) |
| M4.1 part A | PROCEED WITH FIXES | BLOCK: the new `ZERO_BYTES` rule rejected a valid stream; fixed (DEREF-made glvalues may exceed by whole multiples) |
| M4.1 part B | PROCEED WITH FIXES | A cir-suite failure, a misattributing triage tool, vacuous gate steps; fixed |
| M4.1 additions (W6, W7, L6, L7) | APPROVE WITH FOLLOW-UPS | linkonce name collisions and spec 11.2; fixed, re-review APPROVE |
| M4.1 remainder | APPROVE WITH FOLLOW-UPS | How asm-diff must be read |
| M4.1 shared-symbol skip | FIX | The skip was too broad (it could drop a real definition); narrowed |
| M5p parts A, B, C | APPROVE WITH FOLLOW-UPS ×3 | asm-churn classifier fix; `stress` and `tools-test` run with `--only`; area derivation holes fixed before M5a |
| M5a | APPROVE WITH FOLLOW-UPS | Two contract items fixed before S2 consumed them |
| S1 | APPROVE WITH FOLLOW-UPS | A missed edge would fail silently (now an error); dead bodies not validated by the emitter (documented) |
| S2 library series | BLOCK on 0009 → APPROVE WITH FOLLOW-UPS | available_externally of self-recursive glibc inlines (`memchr`, `strchr`, …) was a silent -O2 miscompile; `calls_own_symbol` guard |
| S4/S5 writer | APPROVE WITH FOLLOW-UPS, one required fix | Volatile purity (a silent volatile-order regression); fixed in S6 |
| S3 | APPROVE WITH FOLLOW-UPS | No correctness defect; one latent fix, two runtime checks |
| Final pre-M5 | APPROVE WITH FOLLOW-UPS | FIX: `hot` with `cold` gave invalid IR (writer sends the first, library drops both); thunk result noundef under `strict_result` |
