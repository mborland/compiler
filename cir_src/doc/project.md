# EDG ClangIR back end — project state (the living handoff)

This is the one document to read before working on the ClangIR (CIR) back
end. The binding requirements are in `brief.md`; this file is how the
project meets them. It replaces the session handoffs, which are archived
unchanged in `archive/` (historical, not required reading). Keep this file
current: each gate replaces §0 and §9 and updates §2 to §8. Do not append
history; git keeps it.

Tags: `H1`–`H4` cite the archived handoffs (`archive/handoff.md` …
`handoff_4.md`); "user" marks a user decision with its date.

## 0. State

**Branch** `cir`, local only, never pushed.

| Gate | Commit | What it added |
| --- | --- | --- |
| R, M0, M1, M2 | `c8f11f51f5` `fee14b0c21` `f4609f1cb2` `59a33cffa7` | Design and ECIL format; `BACK_END_TYPE`; cpfe-cir, writer and library skeleton; a C subset runs |
| M2.1, M3a, M3 | `501d0888e7` `908f7b680a` `3592b2b3f7` | Refactors; the ECIL 1.0 C++ contract; core C++ |
| M3.1, M4a, M4 | `511d7a76fc` `6a52c93316` `0ce8b90333` | Re-look items (ECIL 1.1); the ECIL 1.2 contract; virtual functions, RTTI, EH, member pointers |
| M4.1, M5p, M5a, M5 | `9d518c3178` `e6bdad03f4` `77a24448fa` `1a6d11c284` | -O plumbing; pin 2d6f180e; the ECIL 1.3 contract and C API 2; lazy emission, optimization facts, chunked lowering, writer speed |
| (answers) | `89013a6337` | The user's answers to H4 §8 |
| M5.1-docs, M5.1a, M5.1b, M5.1 | `991737c19a` `2eb008a3ad` `4902ad4e5e` (M5.1: the commit that adds this row) | This document set; clang4 and the re-derived baselines; attr-diff facts, the -O0 always_inline inliner, the mangler and `__has_builtin` (ungated), CWG2387 under CIR; the size-based chunk decision, the guard-worker exit fix, the milestone gate |

**Format and toolchain.** ECIL **1.3** (frozen at M5). C API **2**
(`EDG_CIR_API_VERSION`). llvm-project pin
`2d6f180e7dbad81db52f5163a34502c1b8058012` (`cir_src/LLVM_REVISION`), no
local patches.

**Host.** x86-64 work runs on `matt@clang4.cpp.al` (user, 2026-11-05).
The old box `192.168.1.167` is retired: its home directory was wiped on
2026-10-28, with the M5 trees, gate evidence and every baseline. M5.1
session 1 rebuilt the images and baselines on clang4.

**Numbers at M5.1** (clang4, milestone gate): C corpus 1663/2561; C++
corpus 1472/1823; smoke 40/40 (C) and 98/98 (C++); cir-suite 266/266;
crash-sweep 0 problems; attr-diff 0 unexplained (34 checked rows); perf at
most +0.48% against M5; S1 met: the generated C++ unit is 91.6% of M5a at
-O0 (decision 37: at most 92%); chunk-rss 51.8%; slow-below-threshold at
most +0.002%. `asm-diff` and `cpp-asm-diff` differ from M5 only by the
attributed M5.1b changes (§9).

**Now:** milestone **M5.1 is done**; waiting for the user's review and
the §8 answers before M6 is planned (§6). The baselines live on clang4
(`cookbook.md` §1).

## 1. Map

| Topic | Where |
| --- | --- |
| Requirements | `brief.md` |
| Architecture and decision history per milestone | `design.md` (§2 components; §10–§16 are milestone logs) |
| The ECIL wire format | `ecil_format.md` (normative; `include/edg_cir/ecil_format.h` wins) |
| The FE writer | `writer.md` (§9.12 is the authoritative list of C++ gaps) |
| Research and measurements | `research/README.md` (index, live or historical) |
| Library sources | `cir_src/lib/{format,cirgen,abi,driver}`, C API `cir_src/include/edg_cir/edg_cir.h` |
| Writer sources | `src/ecil_*.{c,h}`, `src/cir_be.{c,h}`, CIR-gated hooks (§7) |
| How to build, run, test and gate | `cookbook.md` |
| Gate tooling | `dev_tools/bin/edg-cir-*`, filters `tests/.edgy/filters/cir-*.txt` |

## 2. Decisions

Keep these unless there is a reason to change them; changing one is a user
decision.

**Scope and architecture**
1. Only non-lowered IL. The FE sends the facts it computes; the library
   computes the Itanium mechanics and ports clang's vtable and VTT builders
   (`lib/abi`) (H1 §2.1, H3 §2.2).
2. Standard C++ first; GNU and then MSVC extensions later (brief).
3. Build isolation: `cir_src` is outside the FE CMake tree, found through
   `EdgCirConfig.cmake` and linked with `LINK_DEPENDS_NO_SHARED`; `cir_src`
   never includes `src/` headers and the FE never includes LLVM or MLIR
   headers (H1 §2.6).
4. The library never depends on FE macros; facts such as INLINE_HINT,
   EXTERN_C and NOTHROW travel in the stream (H4 §2.19).
5. `BACK_END_TYPE` with an enumeration, no new `BACK_END_IS_*` (brief, M0).
6. Every FE change is CIR-gated (`BACK_END_TYPE == BACK_END_TYPE_CIR`). The
   only approved exceptions are the M5.1 mangler fix and the
   `__has_builtin(__builtin_operator_new/delete)` values (user, 2026-11-05).
7. Concurrency: leave room, implement nothing (H4 §2.2).

**ECIL format**
8. Forward-only and pointer-free, post-order. Minor versions only add. The
   reader fails only on unknown opcodes, enum values, flags or required
   bits. Validation is strict; a writer contract violation is a format
   error (H1 §2.2).
9. Optional facts are supp keys, skippable, several per blob (1.3).
10. Dense G-ids and L-ids; the `ecil_id` prefix slot is reset by
    `clear_il_entry_prefix`; one G-id per symbol; a declaration is
    re-issued to change its type (H1 §2.4).

**Streaming and memory**
11. Each finished function is streamed from
    `check_for_done_with_memory_region` and freed only if the keep tests
    allow; a bitmap on `function_def_number` stops re-streaming;
    `MAINTAIN_NEEDED_FLAGS=0`, with GlobalDCE at every -O level (H1 §2.3).
12. Lazy emission: the library defers discardable bodies and replays the
    reachable ones at finish only. A missed edge is an error, never a
    silent undefined reference. `EAGER_BODIES` is the escape hatch and the
    mode for tools that compare every body (user; H4 §2.3).
13. Bounded memory: chunk design (a), output-identical (except the `.N`
    suffixes of LLVM-named local data at -O2, §4.6), behind a threshold; the CIR passes stay whole-module; a failed chunk is a
    diagnosed error; design (b) is rejected (user; H4 §2.4).
14. The default chunk decision is a size rule (M5.1): chunk when the
    emitted-body bytes (the reader offset minus the bodies lazy emission
    dropped) reach 1,250,000, which bounds the unchunked peak
    (`.tmp/m51/s1/cir-lib-la.md`). Explicit values keep their body-count
    meaning: N bodies, 1 forces, `UINT32_MAX` never (user; H4 §2.5, M5.1
    plan).

**Robustness**
15. Decoding on the caller's thread; a 256 MiB worker with an alternate
    signal stack; diagnostics queued; crash recovery under one mutex; a
    poisoned session leaks on purpose (H1 §2.7, design §13).
16. Unsupported constructs are diagnosed, never guessed. An unreferenced
    discardable dropped function is a warning, other drops are errors,
    dropping a root is a `cir_error`; the reason travels in the stream
    (user; H2 §2.9, H3 §2.8).
17. Verifiers run only under `VERIFY_EACH` (H4 §2.18).

**ABI and semantics**
18. x86-64 SysV only. Plain signatures with records by value;
    CallConvLowering does sret and byval; an unprototyped call is a
    variadic bitcast plus an indirect call; `agg.tmp` plus one `__retval`;
    allocas at function entry (H1 §2.5, H2 §2.7).
19. Itanium names everywhere. Internal names are deterministic (`_ZL`,
    `_GLOBAL__N_1`, no mtime or cwd module id); the TU-local predicate
    forces internal linkage; late names are nil names plus `NAME_BIND`
    with a CHECKING re-mangle (user; H2 §2.3).
20. Linkage: `linkonce_odr` plus COMDAT; `weak_odr` for explicit
    instantiations; explicit specializations external; vague linkage on
    definitions only; dropping an external non-discardable definition is
    an error (H2 §2.4, design §11, §13).
21. Structors: one body under C2/D2; C1 is an alias, a C5 comdat plus
    alias, or a finish-time substitution; arrays and `new` use C1/D1; no
    C1→C2 canonicalization (H2 §2.5, design §12, H4 §2.17).
22. Cleanups are pushed after construction; conditional cleanups are
    flagged; `__retval` while a cleanup is open; a guarded NRVO cleanup
    (H2 §2.6).
23. Static initialization: `cir.local_init`, the global ctor and dtor
    regions, `_ZGV` guards, `UseInitArray`, clang's module order (H2 §2.8).
24. ABI ties follow clang (user), with two exceptions: object layout is
    the FE's (`vbase13`), and typeinfo names of TU-local types get the
    `*` prefix (H3 §2.3).
25. Where clang and g++ behave differently, follow classic clang: throw
    temporaries are destroyed during unwinding, the array element whose
    constructor threw, CWG 2176 (user; H3 §2.4).
26. `uwtable` on every function, C included (user; H3 §2.6).
27. `__builtin_is_pointer_interconvertible_with_class` on a
    non-standard-layout union is true (H3 §2.9).
28. Mangling: M5.1 fixes the dependent variable-template defect for every
    back end and uses g++'s `sr` form in GNU mode; clang's as-written form
    under clang mode is M6 (user, 2026-11-05). The ABI guard is
    `ABI_COMPATIBILITY_VERSION >= 701`, the release under development.
28a. CWG2387 (external linkage for const variable-template instances, as
    g++ 15 and clang give them) applies under CIR only, in GNU mode 15 and
    later; cpfe and cpfe-cp keep internal linkage (user, 2026-11-06; §5).

**Optimization and code quality**
29. -O: `--cir_opt_level=N` (0–3, CIR-gated) predefines `__OPTIMIZE__` and
    `__NO_INLINE__` by gcc's rules; eccp maps the -O forms (user; H4 §2.12,
    design §13).
30. Parity with clangir at the pin on the high-value attributes; where
    clangir lacks a fact we do not add it; the residue is reported, not
    gated (user; H4 §2.7).
31. attr-diff: checked RISK and MISSED rows are recorded as known entries
    so the step can pass (user; H4 §8 Q2).
32. Library function facts come from clang's `Builtins.inc` (unconditional
    letters U, c, r, n; glibc's cancellation points `fprintf`, `vprintf`,
    `vfprintf` are not nothrow), in a separate generated header; the FE's
    builtin tables are untouched (user; H2 §2.10, H4 §2.8).
33. Allocation elision follows clang: `builtin` on new-expression calls
    (user; H4 §2.9).
34. `used` → `llvm.compiler.used`; `used` static data members of class
    templates follow gcc (H4 §2.15).
35. Library builtins stay plain calls (`memcpy`, not `llvm.memcpy`); a
    possible M6 item (H4 §2.17).
36. The native macOS cpfe-cir sends no INLINE_HINT. Never record goldens
    on the Mac (H4 §2.16).
37. The generated C++ unit's S1 target applies at -O0 only (user; H4
    §2.10) and is 92% of M5a from M5.1 on: unchunked it measures 91.7%;
    the points over the old 90% are the cost of S2's facts (user,
    2026-11-06).

**Toolchain and configuration**
38. Pin an upstream llvm-project `main` SHA and bump early when it helps;
    no local llvm-project patches (work around in the library or
    diagnose); the G1–G8 workarounds stay (user; H2 §8 Q4, H3 §2.5, H4
    §2.6).
39. The toolchain is the Docker images `edgcpp/llvm-cir` → `edgcpp/cir-env`
    on the x86-64 host, where the runtime tests run (CallConvLowering is
    x86-64 SysV only) (user, 2026-10-09).
40. cpfe-cir defaults to `DEFAULT_INSTANTIATION_MODE=tim_used`; the test
    configs use the debug FE; `release-smoke` covers the shipping macros
    (user; H2 §2.12). The release FE macro pins come in three groups and
    are kept for the IL-semantic group (an `#error` guard enforces it) and
    decided one by one at M7 for the others (H3 §2.7).
41. Test runtime: libstdc++, libsupc++ and libgcc_s plus `edg_cir_rt`,
    never `edgrt`; gcc assembles and links; `DisableIntegratedAS` (H1
    §2.8).
42. Orphan killing and a 120 s per-program timeout are unconditional;
    cir-env uses EDG headers; Microsoft-mode variants are REQUIREMENTS NOT
    MET (user; H2 §2.11).
43. `--c` for C side files, in the CIR configurations only; edgdocker
    forwards `EDG_CPFE_DEFAULT_OPTIONS` only when set (user; H4 §2.11,
    §2.13).
44. The roughly 16 GNU builtins of v0 are accepted (user; H1 §8 Q1).

**Process** (details in §7)
45. An Opus orchestrator, three Sonnet implementers with exclusive file
    ownership, and a read-only Opus reviewer (user; H1 §2.9).
46. Only the orchestrator commits, once per reviewer-approved gate, on
    local `cir`; never push (user, confirmed each session; M5.1 plan).
47. Lean gates: per change the targeted checks; per gate the steps the
    change set can affect; the full gate only at milestones (user; H4
    §2.14).
48. Checkpoints run on a synced host tree; reviews read a frozen
    `build/review-snap-*` (H2 §2.13).
49. Prune anything not needed; anything we made may be deleted; never
    touch anyone else's files, trees or containers (user; H4 §8 Q4).
50. Each session fits one orchestrator context window; this file is the
    handoff (user, 2026-11-05).

### 2.1 Retired

| Was | Now |
| --- | --- |
| A no-exceptions v1 stream (H2 §2.2) | ECIL 1.2 exceptions (M4) |
| Prefer `release/24.x` (H1 §3) | Pin `main`, bump early (decision 38) |
| A libc flag in the FE builtin tables (H1 §8 Q3) | A separate generated header (decision 32) |
| Never commit `cir_design_guidance.md` (H1 §7) | Committed verbatim as `brief.md` (user, 2026-11-05) |
| Ask before deleting box trees that are not ours (H2 §8 Q1) | Decision 49; the old box is retired |
| The mangler issue is FE maintenance (H4 §4.2, §6) | CIR fixes it in M5.1 (decision 28) |
| Next: M5.1 or M6 (H4 §6) | M5.1 only (user, H4 §8 Q1) |
| attr-diff stays FAIL until the RISK rows are checked (H4 §4.1) | Decision 31 |
| REFS-driven lazy decode (design §3) | Library-side finish-only replay (decision 12); `REFS` stays specified and unused |
| The consumer drops C99 inline-only bodies (design §3) | available_externally at -O1 and up (H4 §1.2) |
| An unsupported construct is an error only if a definition references it and it is internal (design §3) | Decision 16 |
| A 60 s per-program timeout (H1 §3) | 120 s (decision 42) |
| Box `192.168.1.167`, the `cm-edgbox` socket | clang4 (§0, §3) |

## 3. Cookbook

Commands, gate steps, baselines, the lean-gate map and host hygiene are in
`cookbook.md`; read it on demand.

## 4. Known gaps

Items are one line each with their source; details live in the code and the
cited notes. "M5.1" marks an item the current milestone fixes (§6).

### 4.1 Diagnosed as unsupported (a named reason; the body or variable is dropped)

- The C list: VLAs; `_Complex`, vector types, `_Atomic`, `_BitInt`
  bit-fields; inline asm, statement expressions, computed goto; the alias,
  ifunc, weakref, cleanup, constructor, destructor and init_priority
  attributes; builtins without a mapping (`__sync_*`, `__atomic_*`, the
  setjmp family); `_FloatN` and other float formats; a wide string followed
  by elements, flexible-array-member initializers, a GNU range designator
  followed by more initializers (H1 §4; `ecil_type.c`, `ecil_code.c`,
  `ecil_const.c`).
- C++: `writer.md` §9.12 is the authoritative list (H2 §4); the M4-scope
  list is `research/il_to_ecil_map_m4.md` §13.2; library-side diagnoses
  and their tests are `research/m4_library_status.md` §2.
- How often (H3 §4, 528 C++ drop notes): vector 143, multiversioning 64,
  inline asm 56, float formats 37, vector builtins 22, alias/ifunc/weakref
  18, cleanup 17, address constants 11, init_priority 9, source_location 8,
  VLA 7, flexible-array init 7.
- Microsoft mode is skipped for cpfe-cir; native Windows edgy is
  unsupported (H1 §4, H2 §5; M7).

### 4.2 Silent (not diagnosed)

- `section`, `transparent_union` and `ms_abi` are ignored on supported
  entities (H1–H3 §4; verified 2026-11-05). The fix is a declaration fact
  in a 1.x minor. (`used` was closed in M5: supp key 11.)

### 4.3 Approximations

- `__builtin_object_size` and `__builtin_dynamic_object_size` fold to the
  "unknown" value (`ecil_code.c:3220-3240`; needs an object-size builtin
  and inlining) (H1 §4).
- `-fwrapv` is not plumbed: only the `EDG_CIR_WRAPV` environment variable
  (`ecil_write.c:249-258`) (H1 §4).
- No debug info; with chunking it will need one `DICompileUnit` per TU
  (`chunk.cpp` `copy_attributes`) (H1 §4, H4 §4.2; M6).
- `_Float16` and `_Float128` reach the library as half and quad; support
  or diagnose is undecided (H1 §4–5).
- `gnu_inline` in GNU C++ is always inline-only: the IL cannot say whether
  the definition is `extern` (H2 §4).
- The writer streams every header inline function, referenced or not; lazy
  emission removes the library cost (H2 §4, H4 §1.1).

### 4.4 Builtins and library facts

- No mapping for `__builtin_constant_p` (blocks libstdc++ algorithms and
  containers, Boost algorithm and range, the codequality `sort` kernel),
  `__atomic_*` (memory, regex, Boost format), or x86
  `__builtin_ia32_addcarryx_u64`/`_sbb_u64` (boost/multiprecision)
  (H4 §4.2; M6).
- `isascii`/`toascii` get attributes from LLVM inference only (not in
  clang's table) (H4 §4.2).
- Sized delete: the FE picks `_ZdlPvm` or `_ZdlPv` differently from
  clangir in 27 probe cases; not traced beyond D0 (H4 §4.2). The array
  new-expression cleanup calls unsized `_ZdaPv` where clang calls
  `_ZdaPvm` (`ecil_code.c` around 6495-6540, L-c).
- An extern `thread_local` access is treated as possibly dynamically
  initialized (impure); a local one is not (H4 §4.2).
- Without exceptions the writer marks NOTHROW as clang does (M5.1b,
  `ecil_format.md` 12.6) except destructor declarations under C++
  `--no_exceptions`, which clang marks (the safe direction; M6).
- GNU `nonnull` attributes do not reach the nonnull mask, and `memcmp` is
  not in the generated table: 16 + 4 checked attr-diff rows (M6).
- `__has_builtin(__builtin_operator_new/delete)` is `201802L` in every
  clang mode (M5.1b); clang 18 to 22 confirmed, clang 6 and earlier
  returned 1, the first version is unconfirmed.

### 4.5 attr-diff after M5.1b

attr-diff passes with 0 unexplained gated differences (M5: 539). Fixed:
`this`/reference dereferenceable (L1), alwaysinline (W2, L2), nounwind on
C declarations (W1), `builtin` on `_Znwm`/`_ZdlPvm` (W4, with the harness
at `--gnu-version 150000` for the image's g++ 15.3.1), the `use_facet`
result `noundef` (B2). `tests/.edgy/filters/cir-attr-known.txt` holds the
checked rows (decision 31), each with its reason:

| Rows | Disposition |
| --- | --- |
| nounwind and noreturn only ours on `abort`, `exit`, `std::terminate`, `__cxa_bad_cast`, `_exit` (declarations and C++ call sites) | True facts clangir's declarations lack |
| Call-site nounwind and noreturn only ours, in C | C streams are nothrow everywhere (`ecil_format.md` 12.6) |
| `align 8` vs 16 on `llvm.memset`, `llvm.memcpy` alignment, `memcmp` nounwind, GNU `nonnull` on `_Rb_tree_insert_and_rebalance`, `dereferenceable(0)` on references to functions | We claim less (M6 for `nonnull` and `memcmp`, §4.4) |
| `builtin` and `noundef` on new42's `delete[]` | Structural: for a constant `new T[0]` we emit the constructor loop and its cleanup |
| dereferenceable only clangir (`_Base_manager` helpers) | Likely `final` classes; a `FINAL` flag is an M6 option |
| Residue (not gated; 1913 at M5) | callset 1805; sret spelling 58; noalias on `_Znwm` 34; cold 10; uwtable 6. L2 inlines always_inline bodies away at -O0, so the pinned units' only-clangir symbols rose from 114 to 181 |

### 4.6 Library

- `.N` suffixes under chunking: at -O2 about 1.7% of corpus streams (forced
  to one function per chunk) differ only in the numeric suffix of local
  data an LLVM pass names; the pinned units are byte-identical (H4 §4.2).
- clangir's D0 trap: the deleting destructor of a class with virtual bases
  is `llvm.trap` in clangir; ours is a real body (H4 §4.2).
- Library builtins stay plain calls (30 pair rows), decision 35.
- Structor variants: C1/D1 where clangir calls C2/D2 (542 rows); no C2 for
  implicit inline constructors; extra linkonce vtable or D0 for classes
  without a key function (H3 §5, H4 §4.1).
- Entry-block allocas lose slot reuse and scoped lifetime markers; `agg.tmp`
  plus one `__retval` costs a copy per aggregate call or return (waits on
  upstream CallConvLowering NRVO) (H1 §5).
- The quadratic MLIR verifier at nesting depth (20k nested ifs about 49 s;
  not re-measured since the pin moved) (H1 §5).

### 4.7 Documented differences from g++ (clang-consistent)

Falling off a non-void C++ function traps; `return;` in a non-void
function returns an undefined value; init order of inline and template
statics follows clang; GNU mode does not extend the lifetime of a scalar
member of a temporary (`init/ref19`); C++17 assignment order follows the
standard; typeinfo of a class completed after a file-scope initializer's
first need is `linkonce_odr` where clang has internal; `link_error` tests
that need -O0 folding fail on the reference too (H2 §4, H3 §5).

### 4.8 FE issues found

- NRV only for variables in the function's outermost scope (`opt/nrv23`,
  `nrv26`, `eh/return1`) (H3 §4).
- The FE does not fold an address constant through a cast
  (`init/addr-const1`); a lambda captures `this` where gcc does not
  (`cpp1y/pr61636-2`); `friend72`; ilp32 `abi/bitfield9`
  (`research/il_to_ecil_map_m4.md` §13.4).
- `cpp1z/elide2` (DR1697): the FE marks the member temporary as an
  expression temporary; the reference fails too (H2 §5).
- `ipa/pr64049-2`: the test's second source is missing; environment (H3 §4).
- Found by W-b (2026-11-05): EDG rejects a dependent `tref<vt<T>,T>`
  reference template argument that g++ and clang accept; an internal
  error at `folding.c:6477` (`binary_operation`) on `(&vtg<T> != nullptr)`.
  It also folds `vt<int>` to `Lb1E` inside dependent conditions and adds a
  spurious `ad` on `CT<T>::template cv<T>` (`.tmp/m51/s1/cir-writer-wb.md`).
  Also, in every mode: `C<T>::template tf<T>(x)` loses its qualifier in
  the mangled name, and a pseudo-destructor is `dn` where g++ writes `co`
  (M5.1b, `.tmp/m51/s2/cir-writer-c1/w3-table.txt`).

## 5. Needs re-look

**On every LLVM bump**
- Regenerate the library data and diff the NOTHROW set (H4 §5).
- Re-probe the INLINE_ONLY drop rule and the INLINE_HINT rules (CodeGen
  behavior probed at the pin) (H4 §5).
- `lowering-drift.test`: on failure read upstream's diff of
  `lowerDirectlyFromCIRToLLVMIR`, then update `lower_to_llvm_ir` and the
  chunk assumptions (H4 §5).
- Re-test the G1–G8 workarounds (`research/m4_library_status.md` §3) and
  the upstream watch: vtable 10000 virtuals 1.2 s; diamond 160 layers
  31.5 s and 4.2 GB; `new` chains 2500 17 s; unwind 4000 20 s; nested try
  500/1000/2000 = 1.5/13.8/208 s (CIRCanonicalize); override chain 2000
  26 s; FlattenCFG's array-element EH gap (`research/m4_stress.md`) (H3 §5).
- Route new upstream ops through `CIRCompat.h` (H1 §5).

**Facts the library cannot check** (`research/m4_library_status.md` §5)
- NOTHROW truthfulness (two sources: the FE predicates and the 231-name
  table; a wrong NOTHROW silently skips cleanups), NONTRIVIAL_ABI and
  NONTRIVIAL_DTOR, class facts against the source, linkage flags. Review
  whenever the writer changes them (H3 §5, H4 §5).
- User `pure`/`const` functions give `nounwind willreturn`; in C++ they can
  throw (clang does the same). `torture/pr101373` fails at -O0 for every
  compiler (H4 §5).

**Writer**
- The sampled name check (the first 2000 functions in full, then 1 in 16;
  `ECIL_CHECK_ALL_NAMES=1` for all) sees a mangler bug that only large TUs
  have late (H4 §5); the full check over the C++ corpus and two
  `bits/stdc++.h` units was clean after the M5.1b mangler change.
- The unit harness `unit/run_unit.sh` does not build on the Mac (headers
  lack `ec_cir_error`, BSD `od`); run it on clang4 with
  `UNIT_BUILD=build/gcc-cir CXX=g++`. `run_cases.sh` and `cases/*.dump`
  are stale. `asm_cmp.sh` and `perf_ir.sh` default to 12 jobs.
- The prefix bit `ecil_inline_specifier`: if the local-class keep is ever
  narrowed, check `template_pattern_has_inline_specifier` (H4 §5).
- `loop_condition` drops a comma's left operands only when
  `expression_is_pure`; revisit if that changes (H4 §5).
- Region keeps cost about 17% of FE peak on the template TU and no single
  reason is removable (`research/m5_writer.md` §6). The C++17 purity rule
  spills more than needed (a cost, not a defect) (H2 §5).
- The `builtin_defs.h` generator does not regenerate from checked-in data
  (`research/builtin_table_regen.md`; FE maintenance) (H2 §5).
- A C initializer differential fuzz against gcc (C11 6.7.9) was never done
  (H1 §5).

**Linkage**
- CWG2387, settled in M5.1b: under CIR, GNU mode 15 and later gives const
  variable-template instances external linkage, as g++ 15 and clang do
  (`GNU_CONST_VAR_TEMPL_INTERNAL`, `templates.c`); other configurations
  and older GNU modes keep internal linkage, and CIR writes no `L` for an
  internal instance in GNU mode. It stays CIR-only (decision 28a).

**Library**
- L2's call-site sort (`order_always_inline_uses`) is exercised only by
  the pinned chunk-identity runs; small lit streams already have use
  lists in program order (M5.1b).
- A new lowering that reads a callee's body must be told to the chunker
  (`inline_bodies_`, `chunk.cpp`; the catch-copy thunk is the one case)
  (H4 §5).
- `a_entity::op` points at erased operations after chunking; nothing
  dereferences it (H4 §5).
- Strict validation: the validator does not track operand types, so some
  contract checks live only in the emitter; dropped lazy bodies get only
  the reader's checks (H1 §5, H4 §1.1).

**Tests and gates**
- Writer goldens check structure only; semantic coverage comes from
  `cmp_run.sh`, probes and the corpus (H1 §5).
- Recordings over 100 KB have no output coverage; `_2p` expectations exist
  for the smoke list only (H1 §5).
- The excused-crash heuristic excuses an FE assertion the writer triggers
  inside shared FE code if the reference aborts there too (H2 §5).
- The `-j` flake of `slow-below-threshold` (random streams about +0.3%,
  about 68K instructions) was the detached guard worker's thread exit
  racing process exit under valgrind; fixed in M5.1 (the worker is never
  woken or destroyed at exit, `pipeline.cpp`). Any new detached thread
  needs the same care (`.tmp/m51/s3/cir-lib-slow.md`).
- LeakSanitizer noise: "Unable to get registers from thread" under
  `linux-cir-asan` in containers is ptrace noise (H4 §5).
- Keep these gate-tool properties: `rto-diff --documented` attributes only
  the exact documented status and variant; an `edg-cir-fe-facts` timeout
  fails; container labels are unique per run (H3 §5).

**Spec leftover** (H1 §5): the `va_list` role is never emitted.

## 6. Roadmap

### M5.1: done (2026-11-06)

Three sessions, plan `~/.claude/plans/using-the-cir-planning-lexical-panda.md`
(sessions 1–2) and `~/.claude/plans/using-plan-md-and-the-sunny-puffin.md`
(session 3). Session 1: this document set, clang4, the re-derived baselines.
Session 2 (M5.1b): attr-diff facts (L1, L2, B2, W1, W2), the ungated mangler
and `__has_builtin` (W3, W4), CWG2387 under CIR. Session 3 (M5.1): the
size-based default chunk decision (L3, decision 14); the test hook
`EDG_CIR_TEST_NO_ALWAYS_INLINE`, which attributed every M5.1b asm
difference (C: 14 files L2; C++: 57 tests = 43 L2 + 14 W3/CWG2387, the
changed names equal g++ 15's); the guard-worker exit fix (§5); identity
moved to `identity-m51b`, the allow-list emptied; day-base re-taken at
`4902ad4e5e`; the R2/R3 tooling follow-ups; S1 met (§0). Reviews:
`.tmp/m51/s3/review-m51-{1,2}.md`; evidence §9.

### M6: remaining gaps and documentation

M6 is every gap that does not fit an earlier milestone, plus user and
developer documentation (user, H2 §6). Planned after the user's review of
M5.1 and the §8 answers. First items:
- switch the tool defaults from `m5-base-*` to `m51-base-*` (taken at the
  M5.1 commit, `cookbook.md` §1) and `identity-m51`;
- the M5.1 review follow-ups (`.tmp/m51/s3/review-m51-2.md`): the
  guard-worker comment in `pipeline.cpp` (its second reason never applied),
  `-z nodelete` or a "never dlclose" note for `libedg_cir.so`, the two
  `edg-cir-gate-evidence` comments (RSS run, `SLOW_JOBS`), and a GH tag
  for the pending Changes entries at the rebase.

It includes:
- the builtins: `__builtin_constant_p` first (it blocks libstdc++
  algorithms), `__atomic_*`, the x86 add-with-carry builtins;
- the attr-diff residue; library builtins as `llvm.mem*` intrinsics;
- debug info, with one compile unit per TU when chunking;
- clang's `sr` mangling under clang mode: clang uses the first spelling
  of the qualifier seen in the TU, so EDG can match it only best effort;
  document it or do best effort;
- the diagnosed lists (§4) and the "unsequenced" diagnostic.

### M7: release and platforms

Microsoft mode, native Windows, the release FE macro pins (decision 40),
macOS universal and `-static` builds; AArch64 and other targets once
upstream CallConvLowering supports them (H1 §6, H2 §6).

### Extensions

After standard C++: GNU extensions, then MSVC extensions (brief). Each
needs its own format addition and review (H1 §6, H2 §6).

## 7. Process

### 7.1 Team and ownership

Agent types: `cir-config`, `cir-writer`, `cir-lib` (Sonnet implementers)
and `cir-review` (read-only Opus reviewer), defined in
`~/.claude/agents/cir-*.md`. Continue an agent with `SendMessage`.

| Owner | Files (exclusive write) |
| --- | --- |
| orchestrator | `cir_src/doc/{brief,project,design,ecil_format}.md`, `cir_src/doc/archive/**`, `cir_src/doc/{research/README.md,cookbook.md}`, the contracts `cir_src/include/edg_cir/{ecil_format.h,edg_cir.h}`, `cir_src/LLVM_REVISION`; outside the repo `~/.claude/agents/cir-*.md` and the memory notes |
| cir-config | `dev_tools/bin/{cpfe-cir-2p,edg-cir-*}`, the CIR files in `tests/.edgy/**`, `tests/expectations/edg_x86_64_cir*/**` and any non-CIR `tests/expectations/edg_x86_*` re-recording an approved ungated FE change needs, `bases/docker/cir-env/**`, `dev_tools/docker/{llvm-cir,cir-env}/**`, the CIR helpers in `dev_tools/pylibs/edgdocker/__init__.py`, the CIR hunks of `util/eccp.sh`, the CIR options in `src/cmd_line.{c,h}`, the clang4 trees and images |
| cir-writer | `src/ecil_*`, `src/cir_be.*`, the CIR-gated FE hooks (`mem_manage.c`, `scope_stk.c`, `fe_wrapup.c`, `error.c`, `class_decl.c`, `decl_spec.c`, `decls.c`, `symbol_tbl.{h,c}`, `mem_tables.h`, the CIR helpers in `lower_name.c`, the triple call in `fe_init.c`); in M5.1 also `attribute.c`, the `lower_name.c` mangler, the `macro.c` `__has_builtin` values (ungated) and the CWG2387 macro in `templates.c`; `dev_tools/builtins/generate-libc-builtins.py` and `src/builtin_libc_attrs.h`; `cir_src/test/writer/**`, `tests/tests/cir/writer/**`, the new mangling test under `tests/tests/edg/`, `cir_src/doc/writer.md` |
| cir-lib | `cir_src/**` except the orchestrator's files and `cir_src/test/writer/**`; `tests/tests/cir/cirgen/**` |

Changes entries: since M5.1 they follow main's workflow (user,
2026-11-06): one file per entry in `src/pending_changes/*.txt` (main's
`CONTRIBUTING.md` "Changes Entries"; checked with main's
`edg-collect-changes --check`), never a direct edit of `src/Changes`; one
CIR entry per gate, written by cir-config (cir-writer adds to it).
Ungated FE changes get their own ordinary entries written as release
notes. A CIR entry has no GitHub issue yet, so its tag block is `[]`,
which main's checker rejects until it names a `GH #N` (§8 Q1). Every
`Changes` file and entry stays valid UTF-8.

### 7.2 Rules

- AGENTS.md and `doc/source/code_style.rst` apply (`cir_src` uses the EDG
  style adapted to C++17; MLIR calls stay camelCase).
- Implementers stop and report any question that changes the contract,
  changes FE behavior outside CIR gating, touches shared tooling, changes
  scope, or has more than one reasonable answer. The orchestrator batches
  these for the user.
- Prototypes and spikes stay out of every commit.
- Never touch the git stash or files another session left in the working
  tree; stage explicit paths only.

### 7.3 Reviews

- Every review reads a frozen snapshot under `build/review-snap-*`.
- Every slice and every hardening round gets an incremental review; fixes
  are re-reviewed; the final review covers only the delta since the last
  one.
- Run the ASan preset for every library change; measure the release FE.

### 7.4 Context budget (every session)

- The orchestrator reads `brief.md` and this file (§6 holds the session's
  items and gate criteria); `cookbook.md` and anything else on demand, in
  line ranges.
- Assignments point at files and sections instead of pasting them.
- Agent reports are at most 40 lines (outcome, files changed, commands
  with one-line results, open issues); details go to
  `.tmp/m51/s<N>/<agent>-<item>.md` (not committed).
- cir-review's report is at most one page; pass its path on, do not
  restate it.
- The orchestrator reads only the step table and summary of gate
  evidence; the owning agent triages its own failures.
- Long jobs run detached on clang4 with exit files; wait with a monitor,
  never by polling in the conversation.
- Per session: one gate commit (session 1 of M5.1: two), at most three
  reviews, at most two fix rounds per assignment before asking the user.
- When context runs low: write `.tmp/m51/s<N>/resume.md` (state, running
  jobs, next steps) and an "in progress" line in §0 first.

## 8. Open questions for the user

1. `src/Changes`: main now takes entries as `src/pending_changes/*.txt`,
   collected nightly (user, 2026-11-06). M5.1's entry is
   `src/pending_changes/cir-m51-chunk-size-threshold.txt` with an empty
   `[]` tag, which main's checker rejects until it names a `GH #N` (user:
   leave it for now, fix at the rebase). Should the CIR entries already
   committed in `src/Changes` (gates M0 to M5.1b) also move into pending
   files before the rebase, and which GitHub issue should tag them?
2. RSS ceilings: the C rows of the `rss` step sit at 33–46% of their
   ceilings (1.25 times the M5p values), so a 2x regression would pass.
   Tighten them to 1.25 times the M5.1 values in M6?
3. clang4 clean-up: deleting trees there is denied to the agents by the
   permission settings. Run or allow the listed `rm -rf` (the M5.1 report
   names the trees we made), or keep them.
4. M6 scope and order (§6): start with `__builtin_constant_p`, or with
   another item?

## 9. Latest gate evidence

**M5.1** (milestone, session 3, clang4, tree `m51-int`, fingerprint
`0827453be16d`; evidence `m51-int/.tmp/checkpoint-20261106-180208`, step
table `.tmp/m51/s3/cir-config-gate-table.txt`). `edg-cir-checkpoint
--milestone` against `m5-base-*`, identity against `identity-m51b`, day
against the day-base re-taken at `4902ad4e5e`. Every step passed except
the two attributed asm steps: build, identity (720 files, 0 problems, empty
allow-list), isolation, smoke 40/40, cir-suite 266/266, corpus 1663/2561
(0 PASS→not PASS), modes, stress, writer-stress, rss (24 rows, highest 79%
of its ceiling; no ceiling raised), day and day32 (identical to the
baseline), ctest (4 presets, ASan), perf (+0.48% largest change over 44
rows; S1: the generated C++ unit 91.6% of M5a at -O0, 83.5% at -O2),
cpp-smoke 98/98, cpp-corpus 1472/1823, cpp-modes, cpp-writer-stress,
cpp-stress, cpp-stress-asan, poison, release-smoke, crash-sweep (0
problems), abi-diff, symbols, mixed-tu, eh-probes, m4-stress, opt-corpus,
tools-test (313), lazy-identity, chunk-identity (pinned 6/6 at -O0 and -O2;
cpp 1805/1805), attr-diff (0 unexplained; 1395 explained by 34 of 34 known
rows), codequality, chunk-rss (51.8%), slow-below-threshold (126 streams,
largest +0.0020%), names-check. Attributed:
- `asm-diff` (14 C files) and `cpp-asm-diff` (58 entries, 57 tests): the
  reports are byte-identical to M5.1b's; with
  `EDG_CIR_TEST_NO_ALWAYS_INLINE` the C side has 0 differences and the C++
  side 14 tests, all W3/CWG2387 names equal to g++ 15's
  (`.tmp/m51/s3/cir-writer-e1.md`).
- attr-diff's explained count fell 1399→1395 at M5.1b because R2 narrowed
  the memset row: 4 pinned-unit `llvm.memset` `align 8` call rows moved to
  the ungated callset residue (we claim less; M6 with §4.4).
Before the gate: lit 231/231 and ctest in 3 presets with ASan/LSan;
pinned chunk-identity; the guard-worker fix showed 0 outliers in 96 runs
under load (3 in 48 before). Reviews: R1 APPROVE WITH FOLLOW-UPS (all
fixed), R2 on the final delta (`.tmp/m51/s3/review-m51-2.md`). M5.1b's
evidence is in git (`4902ad4e5e`, this section); M5's in
`archive/handoff_4.md` §9.
