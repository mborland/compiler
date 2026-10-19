# Upstream ClangIR drift since the LLVM pin (for M4.1 / M5p)

Written by cir-config (C5), 2026-10-19. Supersedes `m4_upstream_drift.md`
(which covered 2026-10-09 to 2026-10-12 and found nothing to take).

Status: the drift study is done. The candidate image has NOT been built; it
waits for the orchestrator's go-ahead (section 6). The recommendation is in
section 7.

## 1. Method

- Pin: `cir_src/LLVM_REVISION` = `894bfff5ca9fbe8ffc4171d5f5a4e68d11537f6d`
  (2026-10-09, `[AMDGPU] Add async and tensor waits to isWaitcnt`).
- Upstream `main` at the time of the study: `79b321b1238ceaf8261a5b177185a3ec5111918a`
  (2026-10-19 16:53 +0100). 1539 commits after the pin.
- History: the blobless bare repository `~/edg-cir-config/llvm-drift` on the
  box (now 21 MB), refreshed with
  `git fetch --shallow-since=2026-10-08 --filter=blob:none upstream
  main:refs/remotes/upstream/main`.
- Commands:
  - `git log 894bfff5..upstream/main -- clang/lib/CIR clang/include/clang/CIR`
    (75 commits touch those paths; 81 with `clang/test/CIR`);
  - `git diff --stat` and `git diff` of the library-relevant files (the
    dialect, `Dialect/Transforms`, `Lowering`, `CIRBaseBuilder.h`), read in
    full for `FlattenCFG`, `CXXABILowering`, `CallConvLoweringPass`,
    `HoistAllocas`, `LoweringPrepare`, `LowerToLLVM` (the hunks that matter
    to us) and the `.td` and `CIRDialect.cpp` diffs;
  - `git show --stat` of the commits of interest (section 3);
  - MLIR: the commits that touch `mlir/IR`, `Pass`, `Transforms`,
    `Target/LLVMIR`, `Dialect/LLVMIR`, `Interfaces` (27 commits; 115 touch
    `mlir/` at all).
- Not reviewed: the LLVM optimizer: 172 files changed under
  `llvm/lib/Transforms` and others under `llvm/lib/Passes`, `Analysis` and
  `IR` (the -O2 pipeline and GlobalDCE are LLVM's).  Differences at -O2 after
  a bump are therefore attributed by category (the triage categories of
  `m5_baseline.md`), not predicted here.  Not reviewed either:
  `clang/lib/CIR/CodeGen` (our library has its own CIRGen; a
  CodeGen-only commit cannot change our output), the target back ends of LLVM
  (438 files under `llvm/lib/Target`), and the non-CIR parts of clang.
  CIR commits that only touch AMDGPU, HIP, CUDA, SPIR-V, SYCL, OpenACC,
  coroutines, matrices and x86 builtins are listed in the appendix as "not
  ours".
- Box disk before the fetches: 85 GB free.

## 2. What changed in the parts we link

Of the 70 files that differ under `clang/lib/CIR` and `clang/include/clang/CIR`
(4731 insertions, 1833 deletions), the ones in our path:

| Area | Files | Size |
| --- | --- | --- |
| Dialect definition | `CIROps.td` (+292), `CIRAttrs.td` (+95), `CIRDialect.td`, `CIRDialectBytecode.td` (new), `CIRDialect.cpp` (181), `CIRTypes.cpp`, `CIRAttrs.cpp` | additive except the region-branch changes below |
| Builder | `CIRBaseBuilder.h` (88): fast-math flags member, `createFPBinOp`, `createVecCompare` overloads, complex add/sub take the `fenv` attribute | the entry points we call (`createFAdd` ...) keep their signatures |
| Passes | `FlattenCFG` (8), `CXXABILowering` (42), `CallConvLoweringPass` (26), `HoistAllocas` (10), `LoweringPrepare` (246), `CIRABIRewriteContext` (678) | see section 3 |
| Lowering | `LowerToLLVM.cpp` (470), `LoweringHelpers.cpp` (142), `LowerToLLVMIR.cpp` | see section 3 |

Passes that did not change at all: `CIRSimplify`, `CIRCanonicalize`,
`EHABILowering`, and the CIR pass pipeline in `Lowering/CIRPasses.cpp`.  (The
library's `GlobalDCE` is LLVM's pass, not a CIR one: see "not reviewed".)

## 3. The watch list against the commits

| Watch item (plan, `m4_library_status.md` section 3) | After the pin | Verdict |
| --- | --- | --- |
| Quadratic lowering (wide vtables, `new` chains, unwind chains, diamonds) | no change in `LowerToLLVM` patterns that matter for vtables or cleanups. `08b8038d` (CXXABI lowering no longer rebuilds records that no member pointer touches) is a constant-factor gain: upstream measured 1-3% on its slowest self-hosting TUs | not fixed; a small gain for C++ TUs with many records |
| `CIRCanonicalize` on nested try (2000 = 208 s, `findCommonAncestor`) | `CIRCanonicalize.cpp` unchanged | not fixed |
| `FlattenCFG` array-element EH gap | `FlattenCFG.cpp`: 8 lines, an NFC use of the cleanup-kind attribute predicates (`9abbf8e2`) | not fixed |
| `cir.cleanup.scope` region-branch semantics | `16b2fbe1` rewrites `CleanupScopeOp::getSuccessorRegions`: the parent enters the body, a body exit enters the cleanup only if the cleanup kind covers it (`cir.resume` for EH, other terminators for normal), the cleanup exits to the parent. Before, the cleanup could run without the body and no edge led from the body to it | real correctness change for any region-based analysis (canonicalizer, DCE, dataflow) at -O1+. It matters for -O2 on EH-heavy code. Risk: our "cleanup region ends without a yield" workarounds see different behavior; needs the `cxx-s4-eh-*` tests |
| G1 `__cxa_free_exception` | no occurrence added | not fixed |
| G2 `#cir.method` this-adjustment | `MethodAttr` unchanged (`CIRAttrs.td` diff: `DataMemberAttr` builder, `ConstRecordAttr`, frame-pointer, fast-math) | not fixed |
| G3, G4, G5, G6, G7, G8 | CIRGen-only or untouched | not fixed (G5 text printer: untouched) |
| CXXABI lowering of an extern member-pointer declaration | `lowerInitialValue` still does `cast_if_present<DataMemberAttr>` and passes the (null) attribute on; `CXXABILowering.cpp` has `typeMayChange` (perf), undef/poison initializers of any type (`5f24abfb`, CUDA), a same-type-cast legality rule | not fixed: the library's null-initializer guard stays |
| CIRSimplify / -O2 fixes | `CIRSimplify.cpp` and `CIRCanonicalize.cpp` unchanged. `HoistAllocas` and `LoweringPrepare` changed (below) | nothing to gain for -O2 from the CIR-level passes; the LLVM pipeline is the only -O2 optimizer |
| Attribute support | see the table below | several of the L4 facts become expressible |
| `CIRCompat.h` churn | see section 4 | low |

Attribute and codegen-parity commits (the L4 inventory is about these):

| Commit | What | Effect for us |
| --- | --- | --- |
| `1af48d87` | `LowerToLLVM`: the function-level `nothrow` attribute lowers to LLVM `nounwind`. At the pin CIR dropped it. | closes an attr-diff item for `noexcept` functions that the library marks `nothrow` (`cirgen/types.cpp`); changes -O0 and -O2 output |
| `a0feb022` | `optnone` (`CIRDialect::getOptNoneAttrName`) lowers to `optnone`; `naked`, `minsize`, `cold`, `hot`, `noduplicate` added to the dialect and CIRGen | `optnone` at -O0 and the L4 `noinline` question get an upstream spelling |
| `6544686b` | `noalias` on `malloc`-like returns | CIRGen only; the library must set the call/return attribute itself (the dialect already had `noalias` result attributes) |
| `2df2d3dd` | `#cir.frame_pointer` function attribute and module flag | not needed now; a clean spelling if `-fno-omit-frame-pointer` is ever wanted |
| `61a6a2c3` | `cir.libc.memmove`/`memset` with alignments, `no_builtin` on callees | `MemMoveOp` gains optional `dst_alignment`/`src_alignment` attributes (the `MemSetOp::create` call in `CIRCompat.h:474` must be re-checked) |
| `2772f92d` | `HoistAllocas` puts hoisted allocas after the leading allocas of the entry block; CIRGen creates `__retval` first | changes the stack-slot order of every function that hoists: expect many -O0 asm differences (frame offsets only) |
| `21e441d9` | `-ffp-contract=fast` as a per-op flag; fast-math flags on FP binary ops | needed for `-Ofast`/`-ffast-math` later |
| MLIR `a0957bb8`, `2d97a34c` | `noundef` and `range` metadata on `llvm.load` | CIR's load lowering does not use them yet; available for L4 |

Correctness and robustness commits on the lowering side:

| Commit | What | Effect for us |
| --- | --- | --- |
| `036efe53` | x86_64 call lowering moves every record through memory at every coercion site (a union lowered to its storage type, such as one with an x87 `long double` member, dropped bytes as an SSA value); removes the union check of `765392a6` from `CallConvLoweringPass.cpp` | the *classification* is the `llvm::abi` library's (`llvm/lib/ABI`, unchanged in the range), so which registers or stack slots an argument takes is unchanged; the *coercion mechanism* changes for every record argument and return (a memory round trip instead of SSA aggregates), and signatures with such unions, which the removed check had left unlowered, now go through the classification. The calling convention should be identical, but this is the one change in the list that can show in `mixed-tu`, `symbols` and C-corpus assembly: a risk to test at M5p, not a benefit |
| `765392a6` | x87 `long double` vector arguments on x86_64 | no effect (vector types are a later item) |
| `408db6b4` | `switch.flat` successor operands converted in lowering | fixes a verifier failure that needs forwarded successor operands; none of our output |
| `2d6f180e` | arrays with many trailing zeros lower as packed structs (`T x[1000000] = {1}`); upstream: the compile time went from "not completing" to 0.1 s | removes a scaling cliff for large zero-padded globals. Output changes (`<{ ... }>` types) |
| `lowerDirectlyFromCIRToLLVMIR` | returns null on failure instead of `report_fatal_error` (MLIR diagnostic already issued) | the library (`lib/driver/pipeline.cpp:213`) already treats null as a lowering failure; better than an abort |
| `155ae7bf` | references to constant-initialized `const` variables fold as constants | CIRGen-only; the lowering hunk fixes `cir.const` forms we may build |
| `f3c60c3e` | non-power-of-two vector layout and attributes | later (vectors) |

## 4. Where churn would hit `CIRCompat.h` and the library

- Direct `Op::create` calls in `CIRCompat.h` (`MemSetOp::create`, case/switch,
  ternary, cleanup scope builders): the changed operation definitions in
  `CIROps.td` that we name are `MemMoveOp` (two optional attributes: source
  compatible), the FP binary ops (`fenv` + `fastmath_flags`; we call the
  builder's `createFAdd` ... which keep their signatures), `ComplexFAdd/FSub`
  (take `fenv`; we do not build complex FP), `CoroSuspendPoint`,
  `CleanupScopeOp` description (kind `none` text dropped; we use `Normal` and
  `All`). I expect `CIRCompat.h` to need a handful of lines at most, not more
  than the M4-pin adaptation.
- Behavior churn matters more than API churn:
  - `HoistAllocas` order and `__retval`: -O0 asm frame offsets;
  - `nounwind` on `nothrow` functions: asm and LLVM IR attributes;
  - the call-conv rewrite of records: ABI output;
  - the cleanup-scope region branch: -O2 only.
- MLIR/LLVM API: `d7316136` (per-`LLVMContext` library options) changes how
  `cl::` options are consumed; the library calls `ParseCommandLineOptions`
  once in `lib/driver/capi.cpp:49`. The LLVM legacy of `Global` options still
  works (fallback), so no change is expected; the build will tell.

## 5. Risks of bumping

- The whole-tree rebuild (section 6): header changes in `APInt.h`,
  `APFloat.h`, `DenseMap.h`, `SmallVector.h` touch nearly every object.
- Pin-bump asm churn (all of it explainable: alloca order, `nounwind`,
  record ABI, zero-array globals) will move both asm baselines at M5p; the
  M5p gate attributes each difference to a commit above.
- `main` is not release-quality: 1539 commits in 10 days. `2d6f180e` is the
  newest CIR commit and was merged through upstream review; its CI status was
  not verified (the box has no access to the CI results), so the build and
  `check-clang-cir` of the candidate image are the only evidence: it built,
  and the CIR lit suite had 1265 passes, 1 expected failure and 2 failures
  (`CIR/CodeGen/goto-indirect-cleanup-nyi.c`, `CIR/CodeGenHLSL/
  matrix-element-expr-load.hlsl`) of 1321 tests.
- The CIR lit suite at the candidate has not been run (the image build runs
  `check-clang-cir`; at the pin it was 1207 passed, 0 failed, 1 expected
  failure, 43 unsupported out of 1251).

## 6. Candidate image: time and disk estimate

Candidate: **`2d6f180e7dbad81db52f5163a34502c1b8058012`** (2026-10-19,
`[CIR] Lower array globals with many trailing zeros as packed structs
(#232835)`), the newest upstream CIR commit; every CIR change listed above is
in it and nothing newer touches CIR. Tags to create: `edgcpp/llvm-cir:2d6f180e7dbad`
and `edgcpp/cir-env:2d6f180e7dbad` (`:latest` stays at the pin until M5p).

State prepared on the box (no build started):

- `~/edg-cir-config/llvm-project` is now checked out at the candidate
  (shallow fetch of that one commit; `.git` 695 MB). To go back to the pin:
  `git fetch --depth 1 origin 894bfff5...; git checkout FETCH_HEAD`.
- `cmake .` was re-run in `~/edg-cir-config/llvm-build` (a container named
  `cir-config-llvm-dryrun`, `--rm`; 12 s) and `ninja -n` written to
  `llvm-build/dryrun.txt`.

**Can it reuse `llvm-build` incrementally? Barely.** `ninja -n` reports
7256 steps for the default target, against 7613 for the original full build
(including the `check-clang-cir` run). The cause is header churn in
`llvm/ADT` (`APInt.h`, `APFloat.h`, `DenseMap.h`, `SmallVector.h`) and
`llvm/IR`, so every object that includes them is stale; 103 of the steps
are the CIR objects and 235 the target back ends (X86, AArch64). Keeping
`llvm-build` is still right: it saves the CMake cache, the native TableGen
and the unchanged ~350 objects, and the original full build took 27 minutes
(2026-10-09 17:43 to 18:10 box time, check included).

| Step | Estimate |
| --- | --- |
| `ninja install` (7256 steps) inside `edgcpp/dev-env`, 16 threads, assertions on | 30-45 min wall at the box's full CPU (27 min was the idle-box full build); a loaded box (other jobs ran at load 8) pushes it to about an hour. This is the only heavy CPU part |
| `check-clang-cir` | 1-2 min |
| Stage and strip the prefix (`prebuild.py`: `copytree` 16 GB, then `strip`) | 5-10 min, I/O bound |
| `docker build` of `llvm-cir` and `cir-env` (a `COPY` of 10 GB twice) | 10-15 min |
| Total | about 1 to 1.25 hours, mostly CPU; I will run it detached with an exit-code file and polling, with `--jobs 12` to leave four threads for the other agents unless told otherwise |

Disk (free: 85 GB now):

| Item | Size |
| --- | --- |
| `llvm-build` (reused; objects replaced in place) | 20 GB now, +0 to 2 GB |
| `llvm-cir-install` (overwritten in place by `install`) | 16 GB, +0 |
| staged prefix for the image build (deleted afterwards) | +10 to 16 GB transient |
| `llvm-cir:<sha>` image | +10 GB |
| `cir-env:<sha>` image | +14 GB |
| BuildKit context and cache during the two builds | +10 to 20 GB transient |
| Peak need | about 45 to 60 GB; steady state about 24 GB more than today |

There is **no shortfall**, but the headroom falls below the checkpoint's
40 GB warning during the build and settles near 55 to 60 GB afterwards. The
state after adoption can regain 24 GB by removing the pin images
(`docker rmi` by ID, after the user accepts the bump). If more room is
needed, the candidates in my trees (not touched), by size: `m3-final` 14 GB,
`day-base` 5.5 GB, the pin `llvm-cir-install` 16 GB (after adoption).

Caveats:
- `cir-env/Dockerfile` hard-codes `edgcpp/llvm-cir:latest` and `edg-exec`
  resolves `:latest`. To run cir-lib against the candidate without
  retagging `:latest` (which every agent uses), the Dockerfile needs a
  global `ARG LLVM_CIR_TAG=latest` and `FROM edgcpp/llvm-cir:${LLVM_CIR_TAG}
  AS llvm`, and the candidate tree must be run with `docker run` on the
  tagged image directly. That is a change to a shared Dockerfile
  (neutral at the default); I have not made it. Alternative: retag `latest`
  only at adoption (M5p), and let cir-lib run `docker run
  edgcpp/cir-env:<sha> ...` for its own candidate runs.

## 7. Recommendation

**Bump at M5p, to `2d6f180e7dbad81db52f5163a34502c1b8058012`**, if the image
builds and the library's lit and ctest suites pass against it. Reasons, in
order of weight:

1. The pin has two real gaps that the bump closes and that matter at -O2 and
   for attribute parity: `nothrow` -> `nounwind` on functions (`1af48d87`)
   and the `cir.cleanup.scope` region-branch fix (`16b2fbe1`), which makes
   canonicalization and DCE of cleanup regions correct; plus `optnone`
   (`a0feb022`) for the L4 `noinline`/`optnone` question.  (`036efe53`, the
   by-memory x86_64 record coercion, is a risk to test, not a reason to bump.)
2. Two small compile-time gains (CXXABI lowering, packed large arrays).
3. The cost is bounded: no CIR-dialect operation that the library builds
   changes shape in a way I can find, `CIRCompat.h` should need at most a
   handful of lines, and the asm differences are explained by named commits.

It does **not** fix: the quadratic lowering, the nested-try canonicalize cost,
FlattenCFG's array-element EH gap, G1-G8, the extern member-pointer crash, or
any CIRSimplify/-O2 CIR pass behavior. So the library workarounds stay, and
the stress watch list is expected to be unchanged at the candidate (to be
measured at M5p).

If the user prefers no bump now, nothing in M4.1 depends on one. The
-O2 baseline (`research/m5_baseline.md`) is taken at the pin, and the M5p
gate would then show every -O2 change attributed to the pin.

Pin-dependent items that should wait for the decision: L3 chunk spikes, L4
facts inventory, L5 -O2 shake-down (as the plan says).

## Appendix: the CIR commits after the pin that are not ours

AMDGPU/HIP/CUDA/SPIR-V/SYCL/OpenACC/OpenMP/matrix/coroutine/bytecode/x86
builtin work (about 50 of the 75): `0baa8d99`, `bb69a27f`, `533bab2e`,
`71e68dd7`, `3261d9d9`, `efef068f`, `37c927a7`, `fa70420c`, `6bcb0a8b`,
`56f73431`, `e633c7da`, `f432af68`, `a5219941`, `46c5ec8d`, `d15cc895`,
`d8219b47`, `34aba97c`, `0c55c0b5`, `ef8a56a2`, `faf60e71`, `187d6ddf`,
`09fa829c`, `313665c8`, `285b36e5`, `56aa8ae4`, `95c78f20`, `73dd1890`,
`bf602d3e`, `55ce745e`, `5bb07b4e`, `79bb6504`, `015c205e`, `e4c84f5c`,
`061d4ecd`, `a77ed9c2`, `ba87a6bf`, plus build, docs and frontend plumbing
(`c9ab7840`, `0173f46b`, `481f2e92`, `3b90e2db`, `b6b54b57`, `7e31fab0`,
`4df53f6b`, `b92247a6`, `bd901a2f`, `aa4e0db8`, `892b5b2c`, `cf56268a`,
`34c44270`, `d3ad7fe9`).

The ones that touch EH in CIRGen only (not our CIRGen): `17c1704a`
(deactivation of EH-only cleanups with exceptions off), `0e7848d1`, `4b7c1006`.

## Re-running

```
ssh mborland@192.168.1.167
cd ~/edg-cir-config/llvm-drift
git fetch -q --shallow-since=2026-10-08 --filter=blob:none upstream \
    main:refs/remotes/upstream/main
git log --format='%h %ad %s' --date=short \
    894bfff5ca9fbe8ffc4171d5f5a4e68d11537f6d..upstream/main -- \
    clang/lib/CIR clang/include/clang/CIR
```
