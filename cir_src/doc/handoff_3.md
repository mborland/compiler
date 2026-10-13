# EDG ClangIR back end — handoff after M4 (session 3, 2026-10-13)

This document is the design guidance for the next session. It follows the
spirit of `cir_design_guidance.md` (repo root), which remains the binding
brief, and of `cir_src/doc/handoff.md` and `handoff_2.md`, the earlier
handoffs. Read it first, then:

- `cir_src/doc/design.md` (§11 has the M3.1 decisions, §12 the M4 ones);
- `cir_src/doc/ecil_format.md` (ECIL **1.2**; §11 is core C++, §12 is M4);
- `cir_src/doc/writer.md` (§9 is the C++ writer, §10 the ECIL versions);
- `cir_src/doc/research/m4_library_status.md` (the library's M4 status);
- `cir_src/doc/research/il_to_ecil_map_m4.md` §13 (the writer's M4 status);
- the other research notes cited below.

The approved session-3 plan is
`~/.claude/plans/in-handoff-2-md-we-have-declarative-badger.md`.

## 0. Where things stand

**Commits** (branch `cir`; never pushed):

| Commit | Gate | Contents |
| --- | --- | --- |
| `c8f11f51f5` | R | Design, ECIL spec, contract headers, toolchain spike |
| `fee14b0c21` | M0 | `BACK_END_TYPE` configuration macro |
| `f4609f1cb2` | M1 | `cpfe-cir`, ECIL writer, library skeleton |
| `59a33cffa7` | M2 | A C subset through ClangIR on x86-64 |
| `501d0888e7` | M2.1 | Refactor and efficiency ahead of C++ |
| `908f7b680a` | M3a | The ECIL v1 C++ contract and research |
| `3592b2b3f7` | M3 | Core C++ through ClangIR; ECIL 1.0 |
| `511d7a76fc` | M3.1 | The M3 re-look items; ECIL 1.1 |
| `6a52c93316` | M4a | The ECIL 1.2 contract, `lib/abi`, M4 research and harnesses |
| *(this commit)* | M4 | Virtual functions, virtual bases, RTTI, exceptions and member pointers; this document |

**Format and toolchain**
- **ECIL 1.2** (`MINOR 2`) is frozen at this gate. 1.2 added class facts,
  ABI data, virtual-base structors, vptr initialization, RTTI, member
  pointers, exceptions, deactivatable cleanups, and (during M4b) the
  additions listed in `writer.md` §10: `NEW_ARRAY_SIZE_THROW`,
  `INIT_ZERO_BASE`, `CLASS_VFUNCS REF_RESULT`, `DELETE GLOBAL`/`DESTROYING`,
  `STRUCTOR_DELETING DESTROYING`, `DECL_ABI_DATA TU_LOCAL`. Flags newer
  than a stream's minor are rejected (§9 of the spec).
- The llvm-project pin is unchanged: `894bfff5ca9f…` (main, 2026-10-09).
  No local llvm-project patch was needed (user decision): every upstream gap
  is worked around in the library or diagnosed (§5, M4 watch list).
- Toolchain image unchanged (`edgcpp/cir-env:latest`, x86-64 box).

**M4 results** (tree `~/edg-cir-config/m4-final`, fingerprint `269edfa4…`,
evidence `.tmp/checkpoint-20261013-141713`; 27 of 29 gate steps PASS, and
the two that FAIL are expected and attributed; details in §9):
- **C++ corpus** (now including `gnu/cpp/eh` and `gnu/cpp/rtti`):
  **1461/1823** in both configurations, 0 crashes (M3.1 1001/1732 without
  eh/rtti; M4a 1008/1823). That is about **91%** of what the reference
  passes (1603). Without eh/rtti: 1393/1732. `eh/` 57/80 (reference 60),
  `rtti/` 11/11 (reference 11).
- **Status changes against M4a** (`edg-cir-rto-diff --auto`): 455 to
  PASS (S1 140, S4–S5 exceptions 104, S2 virtual bases 73, S3 17, the
  rest other unsupported reasons), 14 to a failure. 13 of those also fail
  in the reference. The one PASS→FAIL where the reference passes
  (`rma/init33` case 2) is the diagnosed gap of an array element whose
  destructor may throw. It passed at M4a only because exceptions were off.
  The run-time failures `rma/eh40`, `eh/ref-temp1` and `eh/ref-temp2`
  are the documented clang-consistent differences (§2 item 4).
- **Silent-miscompile hunt.** Every corpus case that compiles under CIR
  and fails at run time where the reference passes was triaged. The
  CIR-specific ones were fixed, and the rest are clang-consistent
  behaviour, optimization-dependent `link_error` tests, or reference
  failures. The tests that now compile but fail at link or with a
  library error are triaged in §4; none of those triaged failures can
  produce wrong code silently. The silent gaps that remain are listed in
  §4.
- **ABI evidence** (new gate steps, §3):
  - `abi-diff`: 20 files, 517 vtable/VTT blocks: 450 identical, 11 after
    normalization, 56 in the loose form; 0 differences of ours or of the
    FE; 9 blocks not compared (known tool limits);
  - `symbols`: 15 files, identical to clang apart from 8 documented
    structor-variant and size differences;
  - `mixed-tu`: 36 programs with g++ and 36 with clang on the other side,
    all pass;
  - `eh-probes`: 31 probes of the EH execution differential against g++,
    all pass;
  - `m4-stress`: 10 cases, the slowest at 20% of its limit.
- **C++ smoke**: 98 tests, 100% in both configurations and through the
  release FE.
- **C corpus**: 1656/2561, unchanged from M3.1; C assembly identical to
  M3.1 (2564 files; uwtable on every function changed no C output).
- **Release and debug streams** are byte-identical (poison over 2564 C,
  350 smoke and cir, 1779 C++ tests, no exclusions).
- **Cost** against M3.1:
  - The 6000-function C++ TU: library instructions +13.6% (two-process)
    and +13.1% (in-process), library CPU time +23%, peak RSS +2.9%. The
    cause is exception handling: landing pads and cleanup regions. The
    fixed stream of the same TU is +0.03%.
  - The C++ corpus: FE +0.14%, library +0.29%.
  - The C big TU: FE +0.44%, library +0.1–0.9%, peak RSS 3.5–5.7% lower.

## 1. What exists (architecture as implemented)

New in session 3:

| Area | Files | Notes |
| --- | --- | --- |
| ABI builders | `cir_src/lib/abi` (LLVM-free; `edg_abi`) | Port of clang's ItaniumVTableBuilder, VTTBuilder, RTTI layout and computeOffsetHint on explicit stacks; `validate_deep`, `check_all_slots` |
| Library, M4 | `lib/cirgen/{classes,eh,members,rtti}.cpp`, `lib/format/{ecil_classes,ecil_abi_decode}.cpp` | Vtables, typeinfo, thunks, D0, VTT/construction vtables, cir.try/catch/throw, deactivatable cleanups, member pointers, the 12.7 null constants |
| Writer, M4 | `src/ecil_{type,code,const}.c` | Class facts (layout right away, vtable facts at first need), key function and linkage, VPTR_INIT, devirtualization, double structor walk and synthesized D1, VTT, RTTI, member pointers, EH (`writer.md` §9.16–§9.19) |
| Runtime | `cir_src/runtime/edg_cir_rt.cpp` | `std::type_info::operator==`/`!=`/`before`/`name` with libstdc++ semantics |
| Tooling | `dev_tools/bin/edg-cir-{abi-diff,symbols,mixed-tu,fe-facts,run-diff,rto-diff}`, `edg-cir-gate-evidence` (crash-sweep, cpp-asm-diff, M4 steps), cases in `dev_tools/cir-{abi,mixed-tu,fe-facts}-cases` | §3 |
| Tests | `tests/tests/cir/writer` (193 goldens), `cir_src/test/{cirgen,lit,runtime}` (175 lit tests), `cir_src/test/format/fixtures` (49 good, 412 bad, the bad_v12 ones generated), writer probes `cir_src/test/writer/probes/m4` (90 files) | |

## 2. Decisions taken

Keep these unless there is a reason to change them. They add to
`handoff_2.md` §2.

1. **Gate M3.1 first** (user request): every handoff_2 §5 item fixed,
   measured and moved, or decided (`design.md` §11,
   `research/m31_reassessment.md`).
2. **FE facts, library mechanics.** The writer sends slots, final overriders
   per subobject, the primary base, nvsize, manglings and emission/linkage
   decisions; the library ports clang's Itanium builders.
3. **ABI tiebreak: follow clang** (user). Exceptions (user decisions):
   object layout is the FE's (`vbase13`); typeinfo names of TU-local types
   carry g++'s `*` prefix.
4. **Behaviour: follow classic clang** where clang and g++ differ (user
   decision for throw temporaries, applied consistently):
   - throw-operand temporaries are destroyed during unwinding;
   - an array element whose constructor's full-expression threw is not
     destroyed (eh/array2), the returned object is not destroyed when a
     later destructor throws (CWG 2176), eh/ref-temp1/2, rma/eh40.
   Each is checked against clang 21 and g++ and documented (`writer.md`
   §9.19, `design.md` §12).
5. **No local llvm-project patches** (user). Gaps worked around: G1–G8,
   the CXXABI extern member-pointer crash, FlattenCFG's cleanup rules
   (`m4_library_status.md` §3).
6. **`uwtable` on every function, C included** (user).
7. **Release FE macros pinned for CIR** (`cmake/macro-conf/default/
   cpfe-cir.cmakedef`). A release cpfe-cir took the defaults of
   `targ_def.h`, and with `DO_IL_LOWERING=0` the default
   `NEW_CAN_BE_FOLDED_INTO_CTOR` is TRUE: the release FE dropped the
   free-on-throw cleanup of a class `new` (the debug FE wrote it). The
   pins are in three groups:
   - **IL-semantic, kept**: `NEW_CAN_BE_FOLDED_INTO_CTOR=0`, the delete and
     array folding macros at 1 (an `#error` guard in `ecil_write.h`
     enforces these three), `ASSIGNMENT_TO_THIS_ALLOWED=0`,
     `SAME_REPR_INTS_INTERCHANGEABLE_IN_IL=0`;
   - **language behavior, decide one by one before a release** (M7):
     `PRAGMA_WEAK_ALLOWED`, the two Microsoft macros,
     `INCLUDE_COMMENTS_IN_ASM_FUNC_BODY`, and the constexpr call depth
     (256, where the release default is 512);
   - **test and diagnostic, restore before a release** (M7):
     `ENABLE_TRANS_UNIT_TEST_MODE`, `SEQUENCING_DIAGNOSTICS_ENABLED`,
     `DEFAULT_DISPLAY_ERROR_CONTEXT_ON_CATASTROPHE` (they cost 0.02% or
     less). The five EDG test-feature macros (test pragmas, attributes,
     named address spaces and registers, checking pragmas) were pinned at
     first and are restored already: they cost 5.7% of the release FE's
     instructions on the 6000-function C file.

   The poison step checks that debug and release streams are
   byte-identical.
8. **Writer diagnoses rather than guesses.** Remaining M4-scope diagnoses:
   arrays whose element destructor may throw (upstream gap), a throw of a
   class conditional, a destroying delete for `new`, an anonymous member
   initializer with a destructor (`il_to_ecil_map_m4.md` §13.2).
9. **`__builtin_is_pointer_interconvertible_with_class`** on a union that is
   not standard-layout returns true, as g++ and the FE's folding do (the
   standard says false).

## 3. How to build, run and test (cookbook)

Unchanged from `handoff_2.md` §3, plus:

- **Gate evidence** gains steps: `crash-sweep` (every test type, not only
  the corpus types; allowlist `tests/.edgy/filters/cir-excused-crashes.txt`,
  entries pinned to an assertion or a signal plus equal output),
  `cpp-asm-diff`, and the M4 steps (`--with-m4`): `abi-diff`, `symbols`,
  `mixed-tu`, `eh-probes` (`edg-cir-run-diff`), `m4-stress`. Each M4 step
  fails when it compared nothing.
- **Time bounds**: every command of every gate step runs with a limit
  (default 7200 s, `--command-timeout`; per-step limits, overridden with
  `--step-timeout STEP=SECONDS`). On a timeout the gate kills the process
  group, stops the step's containers by their `edg.job` label and fails
  the step. The step's containers are named `cir-config-<tree>-<step>-…`
  through the opt-in `EDG_DOCKER_NAME_PREFIX` of `edgdocker`.
  `box_stress.sh` takes `EDG_CIR_STRESS_TIMEOUT` and
  `EDG_CIR_STRESS_STACK_KB` (256 KB for `cpp-stress`, 8 MB for
  `cpp-stress-asan`, whose own leak reporting needs the stack).
- **Attribution**: `edg-cir-rto-diff --auto` attributes status changes and
  checks the reference expectation before calling a run failure a CIR bug.
  Known, documented differences are listed with their reason in
  `tests/.edgy/filters/cir-documented-gaps.txt` (`--documented`); at this
  gate `--auto` leaves 0 changes unattributed.
- **Checkpoints** record a tree fingerprint; `edg-cir-checkpoint --prune`;
  baselines `~/edg-cir-config/m31-base-*` (M4 used them), and the M4 gate
  evidence becomes the next baseline.
- **Harnesses**: `edg-cir-abi-diff batch [--vbases]` (vtable layouts vs
  clang), `edg-cir-symbols` (readelf vs clang, `--also g++`),
  `edg-cir-mixed-tu` (link and run with g++/clang TUs, xfail/expected.rc),
  `edg-cir-fe-facts` (FE layout facts vs clang), `edg-cir-run-diff` (g++
  execution differential, `-O<N>` through `edg-cir -O<N>`).
- **Library**: `cir_src/scripts/ecil-m4-stress-gen.py` + `box_m4_stress.sh`;
  `cir-op-compare.py` / `box_diff_m4.sh` (clang CIR differential);
  `gen_eh.py` (generated EH run tests); `gen_v12_fixtures.py` (format fixtures).
- **Writer**: `stream_sweep.py --reasons` (drop-reason histogram).

## 4. Known gaps

Everything below is diagnosed unless marked silent.

**M4 scope, diagnosed** (`il_to_ecil_map_m4.md` §13.2): array elements with
a potentially-throwing destructor (the pin skips earlier elements); a throw
of a class conditional; a destroying delete as `new`'s free function; an
anonymous-member initializer with a destructor; `typeid(float _Complex)`;
`.*` yielding a function address (GNU).

**Later (counts from the C++ corpus drop table, 528 notes)**: vector types
143, multiversioning 64, inline asm 56, float formats 37, vector builtins
22, alias/ifunc/weakref 18, the cleanup attribute 17, address constants 11,
init_priority 9, source_location 8, VLAs 7, flexible array initializers 7, …

**Library** (`m4_library_status.md` §2): see there, with the test per gap.

**Clang-consistent differences from g++**: §2 item 4.

**FE issues found** (`il_to_ecil_map_m4.md` §13.4): GNU-15-mode lifetime
extension (§5.10), NRV only in the outermost scope (nrv23/26), friend72,
addr-const1.

**Optimization level**: the `-O` level does not reach the library under the
cir test configurations (everything is -O0). The fix needs a new FE option
in `src/cmd_line.c`; the permission system denied that edit this session,
so it is deferred (M5; design in §6).

**Found by the final re-record** (all were failing at M4a; triaged
read-only at the gate, not fixed, so that the gate tree stayed frozen):

| Test | Reference | Cause | Fix |
| --- | --- | --- | --- |
| `init/ref16` | passes | The writer emits a stray `DROP` after a `?:` arm that copy-constructs in place under an `eok_class_rvalue_adjust`: `value_is_pushed()` (`ecil_code.c` ~554) does not look through that adjust. The library rejects it at `TERN_ELSE`. | Small, writer (about 7 lines). Changes exactly 1 corpus stream and no golden. |
| `rma/lifetime18` | passes | `__debug_level` is a variable of EDG's C++ runtime, which the reference links; CIR links libstdc++ | Small: define it in `edg_cir_rt`, or exclude the test |
| `abi/empty25` (and `empty12`–`26`) | fails | The harness compiles the `.c` side file with the C++ options, so cpfe-cir compiles it as C++ (`fun` is mangled) | Small, cir-config: pass `--c` for C sources |
| `eh/comdat1` | fails | Needs `-O2` to remove a call; CIR runs at -O0 (g++ -O0 fails the same way) | The -O plumbing (M5) |
| `eh/return1` | fails | The FE applies NRV only to variables of the function's outermost scope (as `opt/nrv23`, `nrv26`); g++ applies NRVO in nested blocks. The deliberately undefined copy constructor is then called. | Medium, writer: NRVO for nested scopes, or accept as an FE limitation |
| `ipa/pr64049-2` | fails | The test's second source file (`pr64049-1.C`) is missing; g++ fails the same way | Test environment |

All five link errors are strong undefined references, so none can bind to
a wrong definition.

**Silent** (carried over): attributes `used`/`section`/`transparent_union`/
`ms_abi` are ignored on otherwise supported entities.

## 5. Needs re-look

- **Unenforced writer assumptions** (`m4_library_status.md` §5): NOTHROW
  truthfulness, NONTRIVIAL_ABI/DTOR, class facts vs source, linkage flags
  cannot be checked from the stream; review them when the writer changes.
- **Upstream watch** (retest on an LLVM bump; numbers in
  `research/m4_stress.md`): quadratic/cubic lowering (wide vtables, diamond
  160 layers 4.2 GB, `new` chains, unwind chains, nested try 2000 = 208 s
  in CIRCanonicalize), FlattenCFG's array-element EH gap.
- **The structor-variant symbol differences** (no C2 for implicit inline
  constructors; an extra linkonce vtable/D0 in no-key) are harmless but
  differ from clang.
- **Typeinfo of a class completed after a file-scope initializer's first
  need** is linkonce_odr where clang has internal (documented).
- **Stack discipline in the library** (found with `init/ref16`). `DROP`
  and the pops refuse only an empty stack, and `FULLEXPR_END` rejects only
  a stack above its base. A stray pop from the writer is therefore caught
  late, at the next frame end or underflow, and far from its cause.
  - It is never silent alone. Every statement starts with an empty stack
    and every expression leaves the stack at the same height, so it ends
    as an imbalance.
  - Two compensating writer bugs in one function would be silent.
  - Hardening (cir-lib, small): refuse a pop below the innermost frame's
    value base, and make `FULLEXPR_END` reject a stack below its base.
    This applies this session's lesson (§7) that stated assumptions
    become validator rules.
- **Gate tooling, from the post-review of the gate delta.** Fixed before
  the commit:
  - `edg-cir-rto-diff --documented` attributes only the exact documented
    status and variant;
  - an `edg-cir-fe-facts` timeout fails;
  - gate container labels are unique per run.

  Keep these properties when the tools change.
- **Follow-ups from the final M4 review** (none blocks; the first is the
  one to fix first):
  - The guard worker: `exit()` called on the worker inside a guarded
    section while a static destructor or an atexit handler takes
    `guard_mutex` can deadlock, and the capi tests do not cover it.
    Proposed fix: skip the mutex when `on_guard_worker`, make
    `session_close` from the worker skip guarded sections, and add a test
    mode `EDG_CIR_TEST_CONSUME_CRASH=exit`.
  - The guard worker has no `sigaltstack`, so a stack overflow on it is
    not reported as a crash of the guarded stage.
  - `ZERO_BYTES` accepts whole multiples of the element size for every
    target; it should accept them only for `new[]` targets.
  - Gate steps: `eh-probes` should require at least one expected case,
    and `mixed-tu` should check rows = cases × directions × 2 hosts.
  - M7: the constexpr depth (256 against 512) and the remaining test and
    diagnostic pins (§2 item 7).

## 6. Future milestones (guidance)

**Items re-assessed at M3.1 and moved** (`research/m31_reassessment.md`,
with the tables):

| Item | Numbers | Moves to |
| --- | --- | --- |
| In-process MLIR residency | about 20 KB of MLIR per C function; the peak is at lowering (CIR and LLVM alive together): 427 MB at 6000 C functions, 822 MB at 12000 | M5 (a per-chunk split of the module is the only option without local LLVM patches) |
| Region keep #2 (prototype instantiations) | still needed (`gnats-26823`) | M5, with the memory work |
| C++17 purity rule | spills counted; the member-selection finding; no correctness benefit | M5 |
| The "unsequenced" diagnostic | — | M6 |
| Upstream cost growth (quadratic nesting, cubic nested `cir.array.ctor`) | unchanged since M3 (pin unchanged); M4 numbers in `research/m4_stress.md` | watch upstream |
| Microsoft-mode test variants | 49 cases skipped (46 C++, 3 C); 45 pass in the reference | M7 |
| Native Windows | unsupported | M7 (with upstream Windows CallConvLowering) |
| `cpp1z/elide2` (DR 1697) | the FE's IL; the reference fails too | none (front end) |
| Builtin generator drift (C5) | deferred by the user as FE maintenance | none (FE maintenance) |

### M5 — performance and streaming
- The `-O` plumbing: `--cir_opt_level=N` in `cmd_line.c`, `cir_be.c` uses
  it, `cpfe-cir-2p` passes `-O` (design by cir-config, blocked this session
  on a file permission).
- Lazy decode (REFS-driven), supp keys for optimization facts, bounded
  library memory (per-chunk module split: the MLIR module costs about
  20 KB per function and the peak is at lowering, 69–77 KB per function),
  the C++17 purity spills (`expression_is_pure` member selection).
- Upstream cost growth (§5).

### M6 — remaining gaps and documentation
- The diagnosed lists of §4, then user and developer documentation.

### M7 — release and platforms
- As before; Microsoft mode and native Windows (re-assessed at M3.1:
  45 skipped C++ cases pass in the reference; upstream MSVC CIR ABI is a
  stub).
- The release FE macro pins (§2 item 7): decide each language-behavior pin
  (including the constexpr depth, 256 against the release 512), and restore
  the test and diagnostic pins; re-record the corpus and goldens with
  them.

## 7. Process notes for the next session

- **What worked**: the contract gate with writer and library checks plus
  reviewer rounds before code; spikes at the pin; incremental reviews on
  frozen snapshots for every slice and hardening round; independent
  differentials (clang layout and symbols, mixed-TU, g++ execution, clang
  CIR shape); the crash sweep over every test type.
- **Lessons**:
  - Ordering rules in a stream contract need writer-shaped good fixtures
    for real hierarchies (iostream, Boost exceptions, interface diamonds):
    three review rounds each found a "first need" defect.
  - "Library assumptions on the writer" written as notes became silent
    miscompiles; make each a validator rule or remove it.
  - Check the reference expectation before calling a run failure a
    miscompile (`rto-diff --auto` does it now).
  - Compare release and debug streams, not only statuses (the folding
    macro bug).
  - Record a tree fingerprint in every checkpoint (a stale box tree looked
    like a performance regression).
  - Performance fixes need a time-bounded regression test in ctest.
  - Keep the shared tree buildable while slices overlap; a checkpoint sync
    waits for each implementer's "tree green".
  - Never use a global flag in nested walkers (save/restore per item).
  - Run the ASan preset (`linux-cir-asan` ctest) for every library change,
    not only at the gate: the guard-worker leak surfaced only there.
  - Bound the time of every gate step that runs a compiler or a program.
    The first M4 gate run hung for over two hours: a leak at exit made
    the ASan runtime's own symbolizer overflow the 256 KB stress stack and
    deadlock in its signal handler, and the stress step checked the time
    only after a case returned.
  - Measure the release FE, not only the debug one: the test-feature pins
    cost 5.7% of FE instructions and no debug-build measurement showed it.
- **Team**: the same four agent types; update `~/.claude/agents/cir-*.md`
  to point at this document.

## 8. Open questions for the user

1. **Next milestone**: M5 (performance, lazy decode, the -O plumbing) or M6
   (remaining gaps: vectors, multiversioning, inline asm, …)?
2. **`src/cmd_line.c`**: may the next session add the CIR-gated
   `--cir_opt_level` option there (the edit was refused by the permission
   system this session)?
3. **Box trees**: may the next session prune `~/edg-cir-config/m31-int`
   (the M3.1/M4 working tree) after taking the M4 baselines?
4. **A small M4.1 first?** Fix the small items below as a short gate
   before the next milestone, as M3.1 did for M3:
   - the `value_is_pushed` fix (`init/ref16`) and the library stack-floor
     check (§5);
   - the final-review follow-ups: the guard-worker `exit()` deadlock,
     `sigaltstack`, the `ZERO_BYTES` rule, and the two gate-step checks
     (§5);
   - `--c` for C side files, and `__debug_level` (§4).

## 9. Gate evidence

**M3.1** (commit `511d7a76fc`; tree `~/edg-cir-config/m31-int`, evidence
`.tmp/checkpoint-20261012-151454`, crash-sweep re-run `-160044`):
- identity IDENTICAL (720 files); isolation 5/5; smoke 40/40 both configs;
  C++ smoke 57/57 both configs and release;
- C corpus 1656/2561 (asm: 114 files changed, all the constant-`if`
  folding, identical with it switched off); C++ corpus 1001/1732 (980 at
  M3): 28 status changes, all FAIL→PASS and attributed, 0 PASS→FAIL;
- crash-sweep (new, every test type): 0 problems after the allowlist;
- modes, poison, release-smoke, stress, writer-stress, day, ctest pass;
  perf within +0.7%.

**M4a** (commit `6a52c93316`; evidence `checkpoint-20261012-182845` and the
re-run `-185436`): C and C++ assembly identical to the M3.1 baselines (2564
and 1958 files), statuses unchanged, crash-sweep 0 problems; box ctest
9/9, lit 102/102, format_tests 48 good / 387 bad.

**M4b checkpoints** (`~/edg-cir-config/m31-int`):

| Checkpoint | Evidence | C side | C++ smoke | C++ corpus (with eh/rtti) | Notes |
| --- | --- | --- | --- | --- | --- |
| S1 only | `-20261012-200938` | all PASS, asm identical | 56/57 (aligned-new3, fixed) | 1085/1823 | 3 crash-sweep problems, explained later |
| S1+S2 (+part of S3) | `-20261012-211603` | all PASS, asm identical | 57/57 | 1228/1823 | 26 crash problems (library, writer CHECKING, FE assertion moved), all fixed or allowlisted |
| S3+S4 | `-20261012-231737` | all PASS; 1 C asm change (subscript order leaked into C; reverted) | 63/63 | 1387/1823 | perf: C++ TU +9.18% library instructions (EH +6.6%) |
| M4 gate dry run | `-20261013-090301` | — | — | — | new M4 steps; the diamond "regression" was a stale box tree |

**M4 gate** (tree `~/edg-cir-config/m4-final`, fingerprint `269edfa4…`
over 1674 files, evidence `.tmp/checkpoint-20261013-141713`; one full run
of the 29 steps, `--with-cpp --with-m4 --smoke-all-pass`): 26 PASS, and
crash-sweep PASS on its re-run. Two steps FAIL, both expected and
attributed: `cir-suite`, by its 6 recorded goldens, and `cpp-asm-diff`,
against the M3.1 baseline. A first run
on an earlier fingerprint (`checkpoint-20261013-102556`) hung in
`cpp-stress-asan` (§7) and is kept as a record only.

| Step | Result |
| --- | --- |
| build, identity, isolation | PASS; IDENTICAL over 720 files; 5/5 |
| smoke | 40/40 in `_cir` and `_2p` |
| cir-suite | **FAIL** (expected): 211/211 pass in both configurations; the 6 writer goldens whose streams changed in S5 (class-cond-discarded, tu-local, spaceship, empty-assign, records-dsize, virtual-destroying-delete) re-recorded |
| corpus | 1656/2561, 0 status changes, 0 CIR crashes |
| modes, cpp-modes | 0 different (2557 and 1778 identical) |
| stress, writer-stress, cpp-writer-stress, rss | PASS (233 and 259 writer-stress tests) |
| day | statuses identical to the baseline (45432 per configuration) |
| ctest | 4/4 presets, `linux-cir-asan` included |
| asm-diff (C) | 2564 identical to M3.1 |
| perf | largest change +13.58% (the C++ TU, exception handling; §0) |
| cpp-smoke | 98/98 in both configurations |
| cpp-corpus | 1461/1823 in both configurations, 0 crashes |
| cpp-stress, cpp-stress-asan | 9 cases each within their limits |
| poison | PASS over 2564 + 350 + 1779 tests; release and debug streams identical |
| release-smoke | C 40/40, C++ 98/98 in both configurations |
| cpp-asm-diff | **FAIL** (expected): differences against M3.1, attributed: 1226 identical; 470 status changes (462 now compile; 7 exit 4 both; `builtin-arith-overflow-2` fails in both, now diagnosed); 262 asm differences (114 exceptions on, 60 control-flow shape, 1 virtual, 87 S3–S5 evaluation-order and temporary changes by test family) |
| crash-sweep | 0 crashes over 30,142 cases. One test edgy cancelled for time under box load (`builtin-atomic-overloads5`, 105 s alone in both configurations). The step now re-runs a cancelled test alone once, and the re-run (with cpp-smoke at 98/98) passes: 0 problems, fingerprint `0502e1a8…`, evidence `.tmp/checkpoint-20261013-161054`. That fingerprint differs from `269edfa4…` only in the gate tool, `edg-cir-rto-diff --documented`, the smoke filter, the new `cir-documented-gaps.txt` and this document |
| abi-diff, symbols, mixed-tu, eh-probes, m4-stress | PASS (§0) |

**Reviews (session 3):**

| Gate | Verdict | Notes |
| --- | --- | --- |
| M3.1 R1–R3 | PROCEED WITH FIXES ×3 | Truncated reasons, C++17 class-assignment value spill (a silent M3 miscompile), W12 partial static-init global flag (BLOCK), crash-excuse holes |
| M3.1 final delta | APPROVE WITH FOLLOW-UPS | |
| M4a contract | PROCEED WITH FIXES → ×2 → APPROVE WITH FOLLOW-UPS | Layout facts at first need broke iostream/Boost shapes; devirtualization to the final overrider; INIT_ZERO_BASE; typeinfo masks and TU_LOCAL; slot cross-check |
| S1 | PROCEED WITH FIXES → PROCEED WITH FIXES → closed in S2 | Use-after-free in the sig table; thunks missing for overrider-only TUs; silent covariant-return adjustment loss |
| S2 | APPROVE WITH FOLLOW-UPS | |
| S3 | PROCEED WITH FIXES | `type_info::before` not a strict weak order |
| S4 | APPROVE WITH FOLLOW-UPS | Terminate on throwing catch copy and on throwing dtors during unwinding (both conform at the pin) |
| M4c | PROCEED WITH FIXES | Empty-class writes for empty-derived classes; throw-flag deactivation; vacuous gate steps |
| Final delta | APPROVE WITH FOLLOW-UPS | Guard worker reviewed; CWG 2176 claim withdrawn after clang evidence |
| Leak fix | APPROVE | |
