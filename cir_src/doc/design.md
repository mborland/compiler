# EDG ClangIR back end — design (Phase R synthesis)

Status: **v0.2** (gate R blockers R1–R8 addressed; see section 8). Owner: orchestrator. The binding
requirements are in `cir_design_guidance.md` (repo root), and the approved
plan is `~/.claude/plans/we-want-to-add-pure-snowflake.md`. The evidence
for this document is in `research/`:

| Document | Author | Contents |
| --- | --- | --- |
| `research/back_end_type_audit.md` | cir-config | `BACK_END_IS_*` use classification and the M0 edit list |
| `research/test_plumbing.md` | cir-config | edgy/eccp wiring, the C corpus and the smoke list |
| `research/writer_hooks.md` | cir-writer | Region lifecycle, hooks, mangling, `ecil_id`, needed flags, stack |
| `research/il_to_ecil_map.md` | cir-writer | The C-subset IL→ECIL mapping, unsupported-construct detection, writer work stack |
| `research/cir_api_inventory.md` | cir-lib | Public CIR API, module attributes, pipeline, link set, NYI list |
| `research/cirgen_map.md` | cir-lib | Clang CIRGen components and our frame-stack emitter |

The stream format is specified in `ecil_format.md`. Its normative contract
is `cir_src/include/edg_cir/ecil_format.h`; the library API is
`cir_src/include/edg_cir/edg_cir.h`.

## 1. Requirement traceability (cir_design_guidance.md)

| # | Requirement | How it is met |
| --- | --- | --- |
| 1 | Only non-lowered IL; keep C++ IL for C++ CIR | `cpfe-cir` sets `DO_IL_LOWERING=0` (with `#error`s). The writer reads the IL as the front end leaves it. Lowering-only facts (vtables, ctor variants, typeinfo, guards, cleanups) are built in `cir_src` from FE facts (section 5). |
| 2 | Works with a write/read so the back end can be a second process | The ECIL stream is identical in-process and on file or pipe. `edg-cir` consumes files and stdin. The test config `_2p` requires byte-identical output in both modes. |
| 3 | Fast builds; a CIR library behind a minimal shim; FE changes don't rebuild CIR | `cir_src` is a standalone CMake project. The FE includes only `cir_src/include/edg_cir/{ecil_format.h,edg_cir.h}` and links `libedg_cir.so` (dev) through an 8-function C API. `cir_src` includes nothing from `src/`; `edg-cir-check-isolation` enforces this. |
| 4 | Optimized CIR library with debug info, even for debug FE builds | `cir_src` presets are `RelWithDebInfo` (`-O2 -g`). LLVM is built `-O2 -gline-tables-only` with assertions. The shared library has no ASan, so a debug ASan FE loads it unchanged. |
| 5 | Own config header and source directory | `cir_src/config/cir_config.h`, with a tripwire against FE headers. Target and language facts arrive in the stream, never through FE macros. |
| 6 | New streamable IL write/read; no `il_def.h` edits; no pointers; skippable supplementary data with presence bits; non-recursive traversal; act on packed data in place; prefer speed | ECIL (`ecil_format.md`): length-prefixed frames; little-endian fixed-width tags like MessagePack; post-order and bracketed encoding; heap work stacks on both sides; a zero-copy cursor; `supp` blobs and `EXT` records; feature and frame presence bits; an ID→mangled-name table (`NAME_BIND`) for the ODR stretch goal. The mechanism is in v0, but supp data is only produced and consumed from M5. |
| 7 | Standard C++ first; GNU/MSVC later | v0 is C11 (M2). v1 is core C++ (M3). v2 adds virtuals, RTTI and EH (M4). GNU/MSVC extensions are deferred. Ignorable attributes and library builtins are still accepted in v0 (follow-up P2); other extensions produce an "unsupported in CIR back end" diagnostic (spec 8.11). |
| 8 | Stream functions out at region discard; leave room for concurrency | Hook at `check_for_done_with_memory_region` (`writer_hooks.md` 1.2/1.8): stream, then free if the keep tests pass. The sink API (acquire/submit/finish/abort, with the buffer owned by the sink after submit) admits double buffering, threads, sockets and io_uring later; none are built now. |
| 9 | `BACK_END_TYPE` enumeration, not a new `BACK_END_IS_*` | `src/back_end_type.h` (M0): `BACK_END_TYPE_{EXTERNAL,C_GEN,CP_GEN,CIR}`. `BACK_END_IS_*` are derived as `TRUE`/`FALSE`, because `il_def.h` still uses them. |

## 2. Components and data flow

```
cpfe-cir (C++14 FE, any build type)            libedg_cir (C++17, -O2 -g)
  parse ─► non-lowered IL                         lib/format:  frame splitter, cursor,
  pop_scope ─► check_for_done_with_memory_region      validator, ID tables (LLVM-free)
     └► ecil_hand_off_region ─► ecil_write.c      lib/cirgen:  EDG-CIRGen frame machine
          heap work stack, decl+body buffers           (ModuleEmitter, TypeConverter,
          └► sink.submit(frame) ───────────────►       FunctionEmitter, Expr/Const/Agg,
  fe_wrapup: F_GLOBALS, F_NAMES, F_END                 BuiltinEmitter; ItaniumABI in M4)
  error path: F_ABORT                             lib/driver:  C API, big-stack worker,
  cir_be.c back_end(): finish session              runCIRToCIRPasses → lowerDirectly…
                                                   → GlobalDCE → TargetMachine → .s/.o
sinks: file | stdout/pipe | in-process (edg_cir_session_consume)
tools: edg-cir (secondary process), ecil-dump, ecil-asm (LLVM-free)
```

## 3. Decisions (Phase R)

| Topic | Decision | Evidence |
| --- | --- | --- |
| LLVM pin | `894bfff5ca9f…` (main, 2026-10-09), validated with `check-clang-cir` on the box | plan; cir-config |
| Name mangling | `MANGLE_ALL_NAMES=0`, `NEED_NAME_MANGLING=1` explicit. The non-mutating getters (already in the `cpfe-cp` binary) are used and copied at once. C uses source or `asm` names; static locals use `<fn>.<var>` | `writer_hooks.md` 2 |
| Needed flags | `MAINTAIN_NEEDED_FLAGS=0`: every region is freed at pop. Dead internal functions are removed by LLVM `GlobalDCE`, which the consumer always runs before codegen. REFS-driven lazy decode is an M5 optimization | `writer_hooks.md` 4 |
| Entity IDs | An `ecil_id` slot in the IL entry prefix (`mem_tables.h`, CIR-gated, `static_assert` on size). It is reset by `clear_il_entry_prefix`, so recycled blocks cannot carry stale IDs | `writer_hooks.md` 3 |
| Stream vs free | These are separate steps. Kept regions (constexpr, `do_not_free_memory_region`, debug keep) are streamed once; a bitmap indexed by `function_def_number` prevents re-streaming | `writer_hooks.md` 1 |
| Late bodies | Re-check regions from `finish_function_processing_for_function_def` and from closure completion, using the `pch.c:1782` `depth_in_scope_stack` pattern (C++, M3) | `writer_hooks.md` 1.5 |
| Abort | `ecil_abort()` is called from the `error.c:4973` `cancel_il_file` arm. cir-writer owns that hunk | `writer_hooks.md` 1.7 |
| Keep switch | `--ecil_keep_regions` (and `--il_display`) keep regions in CIR mode, the CIR analogue of `skip_il_read` | `writer_hooks.md` 1.9 |
| PCH, modules, multiple TUs | Rejected in v0 (`cmd_line` check, `#error` for `COMPILE_MULTIPLE_TRANSLATION_UNITS`) | `writer_hooks.md` 7 |
| Initializers | The writer normalizes designators and repeats itself (C11 6.7.9 current object, on a work stack). It emits `C_AGG_*` (constant) or `AGG_INIT_*` (dynamic) brackets | `il_to_ecil_map.md` 2.2 |
| Value categories | Explicit `LOAD`. Casts are classified by the writer. `TO_BOOL` handles raw conditions. Compound assignment is expanded by the writer with `DUP`. C comparisons get `CAST bool_to_int`, plus the writer peephole | `il_to_ecil_map.md` 0, 5 |
| K&R definitions | `PARAM(l, t_abi, t_decl)`: the consumer narrows | `il_to_ecil_map.md` 1 |
| ABI | Plain signatures with records by value. CIR `CallConvLowering` (x86-64 SysV only) does sret/byval/`va_arg`. Other triples give an "unsupported target" error | `cir_api_inventory.md` |
| Module attributes | Mandatory: `cir.triple`, `cir.target_abi` (`""`), `dlti.dl_spec` via `cir::setMLIRDataLayout`. Also `cir.lang`, `cir.size_type_width`, `cir.int_type_width`, and `cir.record_layouts` when alignment differs | `cir_api_inventory.md` |
| Records | `cir::StructType`/`UnionType` with explicit `Pad`/`BitField` members built from FE offsets. ECIL carries size, data size, align, offsets and the `empty_for_abi`/`unnamed` flags | `cir_api_inventory.md`, `cirgen_map.md` |
| Builder | `EcilBuilder : cir::CIRBaseBuilderTy`, re-implementing the few private `CIRGenBuilderTy` helpers. `cir.ternary` is built with `OperationState`. `CIRCompat.h` isolates churn | `cirgen_map.md` |
| Threading | MLIR threading disabled. The pipeline runs on a 256 MiB-stack worker thread. `report_fatal_error` is caught with a crash-recovery wrapper and becomes `EDG_CIR_ERROR_INTERNAL` | `cir_api_inventory.md` |
| Link set | `clangCIR`, `clangCIRLoweringDirectToLLVM`, `CIRRegisterAllDialects`, `MLIRCIR`, `MLIRCIRTransforms` (+ clangAST/Basic/Lex/CodeGenUtils, MLIR, LLVM CodeGen/target X86). Static archives inside `libedg_cir.so` | `cir_api_inventory.md` |
| Runtime library | CIR output links libstdc++/libsupc++/libgcc_s plus `edg_cir_rt`, never `edgrt` (which uses setjmp EH and its own `__dynamic_cast`) | plan; `test_plumbing.md` |
| Supp presence | Signalled by the stream feature bit `SUPP`, the frame flag `HAS_SUPP` and the tag check C7–C9. The plan's per-record "flags bit 7" was dropped, because per-record flags operands vary by opcode. | gate R |
| Unprototyped calls | `CALL`/`CALL_INDIRECT` carry the call-site function type. A non-matching or `no_proto` call becomes a bitcast to a zero-fixed-parameter variadic type, then an indirect call (clang x86-64 behavior; avoids CallConvLowering failures and asserts at the pin) | gate R C1 |
| Falling off the end | C: return the uninitialized `__retval`; C++: trap + unreachable; `main`: return 0 | gate R C2 |
| C99 inline | Every inline body is streamed. The final `inline_only` status is re-issued in `F_GLOBALS`, and the consumer drops the bodies of inline-only functions | gate R C3 |
| Unsupported constructs | Drop the body, keep the declaration section, re-issue `DECL_FUNC(unsupported)`, warn. An error only if a definition references it and it is internal. `TY_OPAQUE` for types that cannot be expressed | gate R C4 |
| Stack stress | Two-process: the FE runs with a large stack and writes `.ecil`; `edg-cir` reads it under a 256 KB limit. The writer is non-recursive by construction (call-graph cycle check plus a debug stack watermark) | `writer_hooks.md` 8 |

## 4. Configuration of `cpfe-cir` (M1)

The file is `cmake/macro-conf/default/cpfe-cir.cmakedef`. It must override
the debug base after importing it.

| Macro | Value | Why |
| --- | --- | --- |
| `BACK_END_TYPE` | `BACK_END_TYPE_CIR` | |
| `DO_IL_LOWERING` / `DOING_SOURCE_ANALYSIS` | 0 / 0 | |
| `IL_SHOULD_BE_WRITTEN_TO_FILE` | 0 | Overrides the debug base (1). `#error` if 1 together with CIR. |
| `FREE_MEMORY_REGIONS_EARLY` | 1 | Must be explicit on macOS. `#error` if 0. |
| `MAINTAIN_NEEDED_FLAGS` | 0 | Overrides the debug base. |
| `NEED_NAME_MANGLING` / `MANGLE_ALL_NAMES` | 1 / 0 | |
| `AUTOMATIC_TEMPLATE_INSTANTIATION` / `INSTANTIATE_TEMPLATES_EVERYWHERE_USED` | 0 / 1 | |
| `ONE_INSTANTIATION_PER_OBJECT` | 0 | |
| `COMPILE_MULTIPLE_TRANSLATION_UNITS` | 0 | |
| `LOWER_DESIGNATED_INITIALIZERS` | 0 | |
| `OVERWRITE_FREED_MEM_BLOCKS` | 1 (debug) | Poisons freed regions. |
| Microsoft extensions | off | |
| `IA64_ABI` | 1 | |
| `COROUTINE_ENABLING_POSSIBLE` | 0 | Until coroutines are designed. |
| `SCOPE_ORPHANED_LIST_PROCESSING_NEEDED` | TRUE (derived) | `ORPHAN_PROCESSING_NEEDED` forces it on, because `NEED_NAME_MANGLING=1`; `back_end_type_audit.md:276` is wrong here. |

## 5. What the FE sends and what `cir_src` computes

The split follows the plan ("FE sends vs consumer computes"). For v0 (C),
the FE sends:
- record layout (size, data size, alignment, field byte/bit offsets and
  widths);
- type sizes and alignments;
- linkage, names, and constant values.

`cir_src` builds CIR record types with padding, the bit-field storage units,
`cir.record_layouts`, string-literal globals with unique names, and
`main`'s implicit `return 0`.

## 6. Milestones M0–M2 (owners)

**M0 (cir-config):** the `BACK_END_TYPE` refactor, following
`research/back_end_type_audit.md`. It is proved with
`edg-macro-identity-check` (byte-identical preprocessed output and an
identical `--dump_configuration` apart from the new line) and a box day
suite against a baseline.

**M1:**
- **cir-config:** the `cpfe-cir` target and cmakedefs, CIR arms and
  `#error`s, `--gen_c_file_name`, `EdgCir` linking, the `cir-env` base and
  config skeleton, and the isolation check.
- **cir-writer:** `ecil_write`/`ecil_sink`/`cir_be` emitting the preamble,
  `F_HEADER`, an empty `F_GLOBALS` and `F_END`, plus `F_ABORT`; the
  `--ecil_output` option.
- **cir-lib:** the project, `lib/format`, `ecil-dump`, `ecil-asm`, the C
  API, `edg-cir`, and an empty module emitted to `.o`.

**M2:**
- **cir-writer:** the full v0 writer and per-function streaming.
- **cir-lib:** EDG-CIRGen v0 and the pipeline.
- **cir-config:** `edg_x86_64_cir` and `_2p`, plus expectations.
- **Gates:** the curated smoke list passes, the C corpus has no crashes, the
  stress and RSS reports are done, and the default configs are unchanged.

## 7. Follow-ups from gate R (owners; due by the M2 gate unless noted)

| # | Item | Owner |
| --- | --- | --- |
| P1 | Threading and reentrancy rules are now in `edg_cir.h`. MLIR verify/erase runs at finish on the worker, never per function on the caller's thread. Diagnostics are queued to the calling thread. The FE callback must not reach `error()` → `ecil_abort()` → consume. | cir-lib, cir-writer |
| P2 | GNU baseline for the corpus: (a) a list of ignorable attributes (`noinline`, `noipa`, `noclone`, `used`, `unused`, `cold`, `hot`, `always_inline`, …); (b) a generic library-builtin rule (`__builtin_X` → call `X` when the FE marks X as a library builtin) instead of a hand-written list; report the pass rate both with and without GNU-flagged tests | cir-writer (writer), cir-config (corpus split) |
| P3 | `long double`: a single encoding, `FBITS`. The writer diagnoses constants it cannot convert exactly on its host; Linux x86-64 goldens are authoritative | cir-writer |
| P4 | The `F_GLOBALS` walker skips static locals (already defined in `F_REGION`) and never calls `get_variable_initializer` on them; orphan lists include them | cir-writer |
| P5 | Clear `function_def_table[n].scope`/`.memory_region` in a CIR arm of `free_memory_region`; matters from M3 | cir-writer |
| P6 | Deterministic emission order (spec rule 7); `_2p` byte-identity enforces it | cir-writer |
| P7 | Alignment is derived by the consumer (spec 8 preamble); packed-record tests in M2 | cir-lib |
| P8 | `HDR_TYPE_ROLE(va_list)` is allowed in any declaration section; the `__va_list_tag` record has exactly 4 fields | cir-writer, cir-lib |
| P9 | Lazy decode (M5): emit shared definitions (string literals, …) in `F_DECLS` so `F_REGION` frames stay skippable | M5 |
| P10 | C regions with local struct types are kept (`contains_local_class_type`) but still streamed. The RSS report says the in-process MLIR module stays resident. GlobalDCE is added explicitly at -O0 | cir-writer, cir-lib |
| F7 | `ecil_abort()` is idempotent and a no-op after `F_END`/finish. The `cir_be.c` diagnostic callback queues messages and reports them through EDG `error()` only after the API call returns | cir-writer |
| C9 | The `referenced`-based skip from `writer_hooks.md` §4 mitigation (2) applies to variables at `F_GLOBALS` only, never to functions | cir-writer |

## 8. Errata to the research documents (the spec wins)

`ecil_format.md` and `ecil_format.h` are normative. Corrections:

| Ref | Research text | Correction |
| --- | --- | --- |
| a | `cirgen_map.md:313-335`: bracketed `CASE_BEGIN/CASE_END` | Inline `CASE_*` markers. The consumer emits empty-region marker `cir.case` ops and statements follow in the enclosing region (spec 8.4) |
| b | `cirgen_map.md:246`: `LAND_END`/`LOR_END` push `bool_to_int` | They push **bool**; the writer emits the cast (spec 8.9) |
| c | `il_to_ecil_map.md:302`: no record for `&x` | `<x> ADDR` |
| d | `il_to_ecil_map.md:316`: `VA_START` pops ap and the last parameter | `VA_START` pops only `ap` |
| e | `il_to_ecil_map.md:131,278`: `LOCAL_VAR` in the declaration section | `LOCAL` sits in the body at the point of declaration and may appear between expression records (spec 8.3) |
| f | `ecil_format.md` v0.1: "parameter types are the ABI (promoted) types" | Declared types for prototyped functions; promoted types only for a K&R `no_proto` definition (spec 8.2) |
| g | `ecil_format.md` v0.1: `VAR_INIT` only in declaration sections | `C_*` records may feed `VAR_INIT` in declaration sections and `INIT_CONST` in bodies (spec 8.10) |
| h | `cir_api_inventory.md:142`: put `static_local` on `get_global` for static locals | Not for unguarded C statics: the verifier requires a guard. The flag is informational (spec 8.2) |
| i | `cirgen_map.md:186`, `cir_api_inventory.md:140,476`: "K&R unprototyped calls: NYI → diagnose" | Supported through the variadic-bitcast rule (spec 8.8) |
| j | `il_to_ecil_map.md:210` / `ecil_format.md` v0.1: falling off a non-void C function is unreachable | See spec 8.3: C returns the uninitialized value |
| k | `il_to_ecil_map.md:208`: C99 inline-only definitions are declaration only, decided at pop | Always streamed, with the final status in `F_GLOBALS` (spec 8.2) |
| l | `il_to_ecil_map.md:368-371`: on an unsupported construct, discard the frame buffer | Discard only the function's body records (spec 8.11) |
| m | `il_to_ecil_map.md:151`: x87/quad constants as hex text | `FBITS` only (spec 8.6) |
| n | Opcode names in the research docs (`DISCARD`, `TOBOOL`, `PTR_STRIDE`, `BFIELD`, `COND_END`, `CALL G n`, `K_AGGR`, `STR_LIT`, …) | Use the names and arities in `ecil_format.h`: `DROP`, `TO_BOOL`, `PTR_STRIDE`; `FIELD` covers bit-fields; `WHILE_BODY`/`DO_END`/`FOR_STEP`; `CALL(g,t_call,n,flags)`; `C_AGG_*`; string literals are `DECL_VAR(string_literal)` + `C_STRING` + `VAR_INIT` |

## 9. Open items carried into M1–M2

- The worked example in `ecil_format.md` section 10 is regenerated with
  `ecil-asm` in M1.
- `type_info::name` and other `edg_cir_rt` gaps only matter for C++ (M3).
- Function attributes (`nounwind` for C, `noinline` at -O0, `uwtable`,
  frame pointer) are absent from the spike's LLVM IR. They are not needed
  for correctness. cir-lib adds `nounwind` for C functions in M2, and the
  rest later.

## 10. M3: core C++ (session 2)

The contract is `ecil_format.md` §11 (ECIL 0.8 declares every v1 record; 1.0
at the M3 gate). The research behind it:
- `research/m3_ecil_v1_design.md` (the design, with the spike verdicts in §8);
- `research/cxx_cir_shapes.md` (CIR shapes at the pin);
- `research/il_to_ecil_map_cxx.md` (IL facts and writer mapping).

| Topic | Decision | Evidence |
| --- | --- | --- |
| Exceptions | The FE keeps exceptions on. The writer sends a no-exceptions stream (`HDR_LANG.exceptions` = 0): emitted functions with try/throw are dropped and diagnosed, and EH-only cleanups are sent as `eh` and ignored until M4 (user decision) | ~450 of 1,728 preprocessed gnu/cpp tests contain try/throw in expanded headers |
| Instantiation | `DEFAULT_INSTANTIATION_MODE=tim_used` in the cpfe-cir cmakedef (user decision) | `il_to_ecil_map_cxx.md` O2 |
| Internal names | Itanium internal manglings (`_ZL…`, `_ZN12_GLOBAL__N_1…`); no module id from mtime or cwd (user decision) | The module id made names nondeterministic (`il_to_ecil_map_cxx.md` 2.1) |
| Local statics | `_ZZ<fn>E<name>[_n]` from a CIR-gated `lower_name.c` helper | `get_mangled_variable_name` gives `_Z3cnt` (2.2) |
| C2/D2/D0 names | A CIR-gated helper overriding `ctor_dtor_kind` | 2.2 |
| C1 strategy | Alias of C2 (external), C5 comdat + alias (weak_odr), substitution (discardable) | Spike 4 |
| Returns | Through `__retval` while any cleanup is open | Spike 2: a silent miscompile otherwise |
| Copy elision | Exact store/load shapes for NONTRIVIAL_ABI records; `DROP` only with a trivial dtor | Spike 1 |
| Conditional cleanups | Flag-guarded scope opened at the outermost conditional record | Spike 3 |
| Late names | NAME_BIND resolved at the file-scope hand-off, with a CHECKING re-mangle | 3 of 1809 names change after hand-off (2.1) |
| RETRACT | Evaluated by the writer at the file-scope hand-off; the consumer marks the definition discardable | `remove_unneeded_instantiations` skips freed regions (2.3) |
| Vague linkage | `routine_might_exist_in_multiple_copies` and not static → linkonce_odr + COMDAT; explicit instantiation → weak_odr | `use_comdat` is lowering-only (2.5) |
| Unsupported discardables | An unreferenced dropped *function* with internal or `linkonce_odr` linkage is erased with a warning (user decision, 2026-10-11). Dropped variables, and referenced or external functions, stay errors | Header inline functions with M4 constructs no longer fail TUs that don't use them; +55 C++ cases. It applies to C too, but the C pass count did not change: the +50 C cases at the M3 gate are all `__int128` tests |
| Roots | Dropping a function or variable with `constructor`, `destructor`, `used` or `init_priority` is a `cir_error` | Otherwise the erase rule would lose them silently |
| Release FE configuration | `linux-gcc-release` differs from `linux-gcc` in FE macros (`NEW_CAN_BE_FOLDED_INTO_CTOR`, `PRAGMA_WEAK_ALLOWED`, …). The writer handles folded `new` (class `operator new`). The gate's `release-smoke` step runs the shipping configuration | A release-only wrong-operator-new bug was found |
| Region keeps (CIR) | Prototype-instantiation regions and regions with `expr_node_refs` are kept to the end of the TU; lambda call operators were already kept | FE use-after-free found by ASan region poisoning (`gnats-27775-run`, `pr69355`). FE code assumes these regions persist |
| ASan region poisoning | `free_mem_block`/`alloc_mem_block` poison and unpoison pooled blocks under ASan (CIR only). The `poison` gate step compares poisoned and unpoisoned builds | Pooled regions are otherwise invisible to ASan |
| `__int128` | Supported (types, full-width constants, arithmetic, calls). A switch with negative or wider-than-64-bit labels is diagnosed (upstream lowering limit) | — |
| Noreturn calls | `cir.unreachable` follows them, except inside an open cleanup scope (FlattenCFG requires the body to exit) | Keeping `unreachable` preserves dead-code removal; dropping it everywhere broke three C link tests |
| ECIL 1.0 | Frozen at the M3 gate; the minor-7 rules are unconditional; 0.x streams are rejected | — |

## 11. M3.1: the re-look items (session 3)

Gate M3.1 settles each "Needs re-look" item of `handoff_2.md` §5 in one of
three ways. It is fixed with a test, or it is measured and moved to a later
milestone with numbers (`research/m31_reassessment.md`), or it is decided. The
writer-side rules are in `writer.md`. The contract additions are ECIL 1.1
(`ecil_format.md` §6 and §11).

| Topic | Decision | Evidence |
| --- | --- | --- |
| ECIL 1.1 | Minor additions only: the `unsupported_reason` supp key, `CMP3WAY` (0x9D) and the `STATIC_INIT` flag `PARTIAL`. The §9 validation tightenings (strict data size, the 11.12 constant rules) apply to every minor | Older 1.0 consumers skip the supp key; streams that use a new opcode or flag are rejected by a 1.0 reader (`bad_newer_minor_*` fixtures) |
| Unsupported reasons | Reasons are bounded by `ECIL_REASON_MAX`, not `%.100s`. Each unsupported declaration carries its reason in the stream, and the library appends it to the finish-time error. FE diagnostics in system headers stay suppressed | A dropped header function now reports why it was dropped; 437 C and 1053 C++ expectation files changed in text only |
| Target triple | The writer captures the OS right after `init_predefined_macros` (CIR-gated call in `fe_init.c`), not at the first hand-off | `#undef __linux__` in a source file no longer changes the triple. `-U__linux__` on the command line still gives an unknown OS (documented) |
| Pending records | A CIR-gated hook at class completion (`class_decl.c`) sets a dirty flag. `ecil_complete_pending_records` rescans only when it is set | Streams are byte-identical on both corpora; Ir is −11.7% on a TU with many pending records and about 0 elsewhere |
| Symbol clashes | An `extern "C"` function named with a mangled structor symbol is diagnosed (shared-symbol rule). A dropped external non-discardable definition is an error, not a warning | `pr77550` (the test defines the C1 of `_Alloc_hider` by hand) |
| C++17 class assignment | When the order could be observed, the right operand is kept by address and read after the target is designated: the object (a cv-adjusted glvalue included), or the full-expression temporary of a prvalue. A call returning a trivially copyable class value, and a class constant that is not a glvalue, are fixed values and are kept as values. Any other right operand (a conditional, a comma expression, the result of an assignment), and a class assignment nested in the right operand of another, are diagnosed | M3 spilled the value before the target, a silent miscompile (R1-2). Goldens `writer-cxx-class-assign-*` |
| `--old_for_init` | The for-init declaration, its initialization and its cleanup are hoisted before `FOR_BEGIN`, so the destructor runs at the end of the enclosing scope | `edg/rma/lifetime25` is back in the C++ smoke list |
| `<=>` | FE-synthesized locals without a declaration statement are declared. The built-in `<=>` maps to `CMP3WAY` → `cir.cmp3way`, and the category object is built from library constants | 14 corpus cases now pass (the `spaceship-synth*` family) |
| Nested lambdas | A capture of a variable the enclosing closure captured reads the enclosing closure's member through its `this` | `gnats20538-lambda-3` and one more case |
| `cpp1z/elide2` | Not a writer bug: the FE IL marks the `S&&` member temporary as an expression temporary (DR1697). Documented; the reference fails too | `writer.md` |
| Constant `if` (user approved) | Only the live arm is sent when the FE folded the condition to an integer constant and the dead arm holds no reachable label or unknown statement structure. `if constexpr` is unchanged | 7 `link_error` tests go FAIL→PASS; the C asm changes are all in these files |
| Partial static initialization | A partly constant aggregate of static storage duration is `VAR_INIT` (constant elements, zero for dynamic ones) plus `STATIC_INIT PARTIAL` (dynamic elements only). Skipping is per initializer level. Excluded: `thread_local` variables, vague-linkage variables, and local statics of functions that may exist in several TUs; these keep zero data and the full (guarded) initialization, as clang does. A repeat of an aggregate with a dynamic part is diagnosed | `cpp0x/constexpr-static4`. R2 found a blocker: a global skip flag also dropped constants in nested temporaries. Fixed per level |
| Inheriting constructors | The inherited constructor gets the original by-value arguments, not a copy | P1 found a real miscompile (`forwarded_parameter`) |
| Members of nested unnamed classes | They get the `Ut_` name and the enclosing class's linkage (vague when inline), and so do their local statics, as g++ and clang do | P4 two-TU probe; `writer-cxx-names-unnamed` re-recorded |
| Region keeps | Lambda call operators are kept only when they can be evaluated as constants later (`is_constexpr`, or the closure has a conversion function). A function declared constexpr that is not one (`is_declared_constexpr`) is kept too: the constant evaluator reads its scope (`constexpr-fno-elide-ctors1`). The pack-reference keep stays (freeing it breaks `gnats-26823`), but `find_binding_for_pack` no longer reads the region: `scope_stk.c` keeps the pack facts in the pack reference | The ASan poison runs are clean. In-process and keep-regions are equal in a release build |
| `tu_walk` | Each class is queued once | `gnats-26324` used more than 30 GB in CIR (exponential walk). It now takes 0.02 s and 45 MB |
| Strict data size | An inconsistent `dsize` is a format error in the validator (unions exempt) | Made visible a writer bug with zero-width bit-fields (R1-1) |
| P3 classification | No change needed: 27 pairings × 4 probes (tail padding, empty classes, `[[no_unique_address]]`, sub-object copy) match g++ and clang | `cxx-abi-{tailpad,empty,nua,subcopy}` run tests |
| C++17 purity spills | Moved to M5. 1450 spills in 178 C++ cases; treating member selection as a leaf in `expression_is_pure` would halve them | W15 |
| Cost growth | Still "watch upstream". Nested ifs, loops and cleanups are quadratic in the upstream verifier, passes and lowering; nested `cir.array.ctor` is cubic in the passes. Unchanged since M3 at the same pin | `m31_reassessment.md` §a |
| In-process residency | Moved to M5 (per-chunk module split). The MLIR module is about 20 KB per C function, and the peak, at CIR→LLVM lowering, is 69–77 KB per function (427 MB at 6000 functions) | `m31_reassessment.md` §b |
| Microsoft mode, native Windows | Moved to M7. 49 cases are skipped (45 of them pass in the reference, 2.6% of the C++ corpus); upstream's MSVC CIR ABI is a stub | `m31_reassessment.md` |
| Builtin generator drift (C5) | Deferred as FE maintenance (user). The checked-in `builtin_defs.h` stays authoritative | The generator inputs for gcc 16.2 and the Lx tables cannot reproduce it without a table change |
| Crash gating | An FE crash is excused (`edg-cir-summary` `crash_excuse`) only when the reference crashes the same way and neither the message nor any backtrace frame names writer code. Then one of these must hold: the compile output equals the reference's recording without the CIR notes *and* the crash has an identity (an assertion with file:line, an "Internal error" line, or a symbolized frame); or a symbolized backtrace has no writer frame (and the assertion, if the reference's crash is one, is the same); or the assertion text, file:line, function and source position equal the reference's. A bare signal is never excused by these rules. A new gate step, `crash-sweep`, runs every test type, not only the corpus types. A crash that the rules cannot excuse but that was reviewed as the reference's own is pinned in `tests/.edgy/filters/cir-excused-crashes.txt` by its assertion text or, for a bare signal, by the signal plus equal output | The sweep found six CIR-only FE crashes in `fp`/`fn` tests that the corpus gate could not see; all six are fixed. Seven reference crashes are pinned: three assertions reported at a different position, and four bare `SIGSEGV`s that the reference also has |
