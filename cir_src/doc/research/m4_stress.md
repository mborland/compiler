# M4 stress measurements (cir-lib, session 3)

Author: cir-lib, 2026-10-12/13. Status: measurement record for gate M4.
Scope: how the ECIL 1.2 C++ features (virtual functions, virtual bases, RTTI,
exceptions) scale in the library, which stages are quadratic or worse, which
of those are ours (fixed) and which are upstream CIR/MLIR (watch).

## Method

- Generator: `cir_src/scripts/ecil-m4-stress-gen.py KIND N` writes an ECIL
  text stream; `ecil-asm` makes the binary; `edg-cir --stats --emit=obj`
  compiles it. Runner: `cir_src/scripts/box_m4_stress.sh kind:n ...`.
- Box: x86-64, cir-env image, release (RelWithDebInfo) build, one process,
  `ulimit -s 256` on the main thread (the worker threads carry the deep
  recursion, so this checks that nothing recurses on the main stack).
  Times are from `--stats`, in ms; RSS is peak resident (`time -f %M`).
  Stages: decode(+validate) / emit / finish (vtables, RTTI, layout) /
  verify / cir passes / lower (CIR to LLVM) / codegen.
- Kinds: `wide` (one class with N virtual functions), `vchain` (N-level
  chain, every level overrides the previous), `diamond` (2N classes, each
  layer virtually derived from both classes of the previous layer),
  `vbases` (N direct virtual bases), `tuple` (recursive base chain, a
  constructor per level), `tries` (N nested try blocks), `news` (N `new`
  expressions in one full expression, each with a deactivatable cleanup),
  `unwind` (a throw through N cleanups), `unwindcall` (same with a call in
  place of the throw).
- The EDG front end itself needs about 89 s for a 400-level vchain, so
  inputs past a few hundred levels are not realistic; the table goes
  further to find the shape of the curve.

## Results

Growth is the ratio for one doubling of N (2 = linear, 4 = quadratic,
8 = cubic).

| kind | N | total | dominant stage (ms) | RSS MB |
| --- | --- | --- | --- | --- |
| wide | 2500 / 5000 / 10000 | 107 / 361 / 1314 | lower 81 / 317 / 1232 | 59 / 68 / 85 |
| vchain | 500 / 1000 / 2000 | 494-585 / 3.7 s / 27.7 s | finish 479 / 3407 / 26594 | 82 / 168 / 509 |
| diamond | 20 / 40 / 80 / 160 | 66 / 460 / 3887 / 36079 | lower 46 / 373 / 3216 / 31504; finish 7 / 41 / 293 / 2274 | 56 / 113 / 590 / 4194 |
| vbases | 100 / 200 / 400 | 23 / 38 / 74 | linear in all stages | 49 / 51 / 57 |
| tuple | 500 / 1000 / 2000 | 153 / 432 / 1436 | passes 46 / 163 / 613; lower 57 / 161 / 564 | 58 / 67 / 84 |
| tries | 500 / 1000 / 2000 | 1.5 s / 13.8 s / 211 s | passes 1478 / 13586 / 207545 | 57 / 61 / - |
| news | 1250 / 2500 | 2.7 s / 19.8 s | lower 2112 / 16881; verify 292 / 1750 | 113 / 172 |
| unwind (throw) | 500 / 1000 / 2000 / 4000 | 0.30 / 1.06 / 4.19 / 21.8 s | lower 242 / 922 / 3699 / 19925 | 66 / 80 / 106 / 159 |
| unwindcall | 1000 / 2000 / 4000 | 1.08 / 4.18 / 21.97 s | lower 905 / 3616 / 19881 | 88 / 122 / 189 |

Growth per doubling, by stage:

| stage | growth | owner |
| --- | --- | --- |
| our decode, emit (all kinds) | about 2 (linear) | ours |
| our finish: wide, vbases, tuple, unwind | about 2 | ours |
| our finish: vchain | 7.2 / 7.6 (cubic) | ours, but inherent (below) |
| our finish: diamond | 5.9 / 7.1 / 7.8 | ours, inherent (below) |
| lower, wide | 3.9 / 3.9 (quadratic) | upstream |
| lower, diamond | 8.0 / 8.6 / 9.8 | upstream |
| lower, unwind/unwindcall | 3.9 / 4.0 / 5.4 | upstream |
| lower, news | 8.0 (cubic) | upstream |
| verify, news | 6.0 | upstream (CIRDialect.cpp verifier walks nested calls) |
| passes, tries | 9.2 / 15.3 | upstream (CIRCanonicalize greedy driver) |
| passes, tuple | 3.5 / 3.8 | upstream (type walks) |

## What was ours and is fixed

Before this session's fixes the vtable layout stage was exponential in
diamonds and cubic with a very large constant in override chains:

- vchain finish: 500 / 1000 = 1042 / 7928 ms and a stack overflow at 2000.
  Now 479 / 3407 / 26594 ms with no crash.
- diamond finish: 10 = 1.9 ms, 25 = 14.8 s, 50 and 100 timed out (>300 s).
  Now 20 / 40 / 80 / 160 = 7 / 41 / 293 / 2274 ms.
- Fixes (`lib/abi/abi_vtable.cpp`, `abi_rtti.cpp`): `find_nearest_overridden`
  uses a cached sorted owner index (`a_vtable_context::overridden_in`);
  `find_paths` visits a virtual base once per call (epoch marks, reused
  stack), as `CXXBasePaths` does; `add_vbase_offsets`,
  `layout_vtables_for_virtual_bases` and `vmi_class_flags` no longer
  re-walk a virtual base that was already explored.
- The rest of the vchain cost is inherent in clang's algorithm: every class
  places its k bases and each placement does a base-path search of O(k),
  so O(n^3) per chain. Upstream clang has the same shape (it is the
  Itanium vtable builder). At 400 levels (the largest the front end
  produces in a sensible time) finish is about 0.2 s.
- `consume` (the decode and emit loop) now runs on the big-stack worker
  thread: the tuple:2000 stream crashed under a 256 KiB main stack before.
- Lazy free-exception cleanup (see below) cut `unwind` (throw through N
  cleanups) from 59 s to 21.8 s at N = 4000, the same as `unwindcall`.
  The extra cost came from every cleanup in scope having the exception
  free-cleanup stacked on top of it.

## What is upstream (watch)

All of these are in the pinned llvm-project, not in `lib/`; each needs an
unrealistically large input to matter, but the curves are worth recording
for the next LLVM_REVISION bump.

- CIR to LLVM lowering is quadratic in the number of functions in a
  vtable (wide: 1.2 s at 10000 virtual functions) and cubic in the number
  of live cleanup/EH regions per function (news: 17 s at 2500 `new`s in one
  expression; clang -fclangir on equivalent C++ with N locals: 1007 / 4071
  / 18255 ms at N = 1000 / 2000 / 4000, which equals our `unwind` at the
  same N, 922 / 3699 / 19925 ms).
- Diamond lowering: 31.5 s and 4.2 GB at 160 layers (320 classes, each with
  a VTT and construction vtables). The class count is the multiplier.
- `tries`: CIRCanonicalize's greedy pattern driver (`findCommonAncestor`,
  `findAncestorOpInRegion`) takes 1.5 s / 13.6 s / 208 s for 500 / 1000 /
  2000 nested try blocks. Not reachable from real C++ (the FE limit on
  nesting is far lower) but a good first thing to look at on an upgrade.
- Verifier on `news`: `CIRDialect.cpp` verification walks nested calls
  (292 ms / 1750 ms).
- `tuple` passes and lower are mildly superlinear (3.5-3.8x per doubling),
  from recursive type walks (`isPureCIRType`-style); 4000 levels is 6.4 s.

## EH cost (exceptions on vs `--no_exceptions`)

132 corpus files (cpfe-cir via the writer tree, subset that the FE
accepts with `--no_exceptions`, so files with try/throw are excluded and
this measures only the cost of cleanups and unwind tables for ordinary code):

| metric | exceptions | no exceptions | delta |
| --- | --- | --- | --- |
| object bytes | 397144 | 382216 | +3.9% |
| edg-cir total (ms) | 957.0 | 921.1 | +4% |
| decode / emit | 300.8 / 54.9 | 294.1 / 53.0 | |
| verify / passes | 14.4 / 72.1 | 17.3 / 69.4 | |
| lower / codegen | 298.2 / 206.0 | 281.3 / 195.7 | |

The writer binary's `EDG_CIR_EXCEPTIONS=0` environment override has no
effect (reported to the coordinator).

## Lazy free-exception cleanup (a behaviour change made while measuring)

`throw expr;` used to open the deactivatable `__cxa_free_exception`
cleanup at THROW_BEGIN. CIRGen (clang) opens it only when something that
can throw runs between the allocation and the construction finishing;
`throw k;` of a trivially-copyable value has no cleanup at all.
`a_throw` now records the allocation slot and `materialize_throws()`
(eh.cpp) opens the cleanup at the first possibly-throwing record: a
non-nothrow call, delete, `new[]` with a throwing size check, typeid,
dynamic_cast, pointer-to-member call, a non-exit CLEANUP_BEGIN, conditional
and comma operators, array constructors/destructors, full-expression and
static-init begin; THROW_END materializes (except for the last operand)
and deactivates only if materialized; RETHROW materializes.
Validation: `gen_eh.py` hosts now wrap `__cxa_free_exception` and log it
(note -3); the lit tests compare the CIR log with g++'s, so a missing or
extra free would fail them (164/164 pass).

## Conclusions

- Nothing in `lib/` is worse than linear except the vtable layout stage,
  whose cubic term is inherent in the Itanium algorithm (about 0.2 s for a
  400-level chain, 2.3 s for 320 diamond classes).
- Everything that exceeds a few seconds for plausible input is in upstream
  lowering or passes. Realistic sizes (hundreds of virtual functions, tens
  of bases, tens of nested tries) cost milliseconds.
- Exceptions cost about 4% in size and in time on non-throwing code.

## Rerun after the review fixes (2026-10-13, current tree)

`box_m4_stress.sh` under `ulimit -s 256`, a loaded box (finish and total in
ms; the quiet-box numbers of the first table are lower):

| kind | N | total | finish |
| --- | --- | --- | --- |
| diamond | 14 / 16 / 20 / 24 / 28 | 71 / 112 / 184 / 309 / 510 | 5 / 8 / 13 / 21 / 41 |
| diamond | 40 / 80 / 160 | 1531 / 8058 / 42942 | 101 / 632 / 2544 |
| vchain | 500 / 1000 / 2000 | 596 / 3985 / 33117 | 478 / 3644 / 31939 |
| vbases | 100 / 400 | 26 / 95 | 3 / 11 |
| wide | 2500 / 5000 / 10000 | 136 / 403 / 1317 | 5 / 10 / 20 |
| tuple | 500 / 1000 / 2000 | 197 / 571 / 2132 | 0.4 / 0.9 / 1.8 |
| unwind | 1000 / 2000 / 4000 | 1304 / 5527 / 37554 | 0.7 / 1.3 / 2.4 |
| unwindcall | 1000 / 2000 / 4000 | 1310 / 9540 / 59133 | 0.6 / 1.1 / 3.9 |

The diamond "finish" stage is not exponential (an older tree, from before
the fixes of the first section, shows 20 layers at 422 ms and 24 at 7.2 s).
Regression guards: abi_tests `test_diamond_lattice` (40 layers: vtable
group, VTT and typeinfo of every class) and the lit test
`cxx-s4-stress-diamond.test` (diamond 40 under a 256 KiB stack and
`timeout 60`).  Putting back any of the three path-walk fixes
(`layout_vtables_for_virtual_bases` explored set, `add_vbase_offsets`
revisit skip, `vmi_class_flags` second visit) makes both fail.
