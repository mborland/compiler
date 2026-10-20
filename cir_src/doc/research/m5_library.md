# M5a library research (cir-lib): dead-body census, lazy emission prototype, candidate pin, chunked lowering, optimization facts, -O2 shake-down, profile

Author: cir-lib, 2026-10-19/20.  Status: measurement record for gate M5a (placed in the tree at the gate; the spike code stays out of it).
Nothing here ships behavior.  The prototype lives only in the box tree
`~/edg-cir-lib/lazy-proto` (a copy of `cir_src` with the changes below); the
candidate-pin build is in `~/edg-cir-lib/pin-candidate`.  Neither is in a commit.

## 0. Summary

- **L1 census.**  GlobalDCE erases **68.0%** of the function bodies that the
  library emits for the C++ corpus (24,449 of 35,970), and **63.3%** on eight
  header-heavy libstdc++ translation units (6,231 of 9,850; from 34% for a
  container TU to 97% for an iostream TU).  On the C corpus it is 3.3%.  Those
  bodies are 61% of the CIR operations and 57% of the time the stream spends in
  function records.  An A/B run that does not emit the dead set (an oracle)
  bounds what lazy emission can save: the library takes **73.0%** of its time
  on the C++ corpus and **61.4%** on the libstdc++ TUs.
- **L2 prototype.**  A working lazy emitter with the deferral rule of the
  plan, replay, reachability with roots and edges, in two flavors (finish-only
  and incremental replay).  Result: the assembly is **byte-identical to eager
  emission** for every stream set (C 2564, C++ 1805, writer goldens and probes
  319, eight libstdc++ TUs; also at -O2 on the TUs), with one exception that is
  only a different position of a library-created declaration; status and
  diagnostics are equal on all of them.  Library time: **76.8%** of eager on the
  C++ corpus and **63.0%** on the libstdc++ TUs (the oracle bound is 73.0% and
  61.4%): lazy emission gets 85% to 95% of the possible saving, for a
  reachability cost of 0.75% and a replay cost that is the price of the bodies
  that are needed anyway.  Peak RSS of the libstdc++ TUs drops 10.5%.
- **Incremental replay** (a deferred body is replayed when an emitted body
  first names it) gives the same set of bodies and no gain in total time
  (64.5% against 63.0% on the TUs), because FE and library run in series in the
  same thread: it only moves work earlier and costs 2% more.  Recommendation:
  ship finish-only replay (S1); keep the frame-end drain hook out of the
  product until the library runs concurrently with the FE.
- **Candidate pin** `2d6f180e` (section 6): one adaptation in `CIRCompat.h`,
  all tests pass, no stress watch item improves, one fix (`c/pr78468`), one
  regression (`opt/nrv5`), -O0 code shape churn for by-value records.

- **L3 chunked lowering** (section 7).  The CIR module is lowered in chunks
  into one LLVM module (a) or code-generated per chunk and merged as text (b).
  Peak RSS at 500 functions per chunk: c6000 425 -> 231 MB (a) / 218 MB (b),
  c12000 810 -> 395 / 366, cxx6000 157 -> 118 / 109, cxx12000 260 -> 177 / 163
  (floor = the CIR module after the CIR passes: 158, 278, 72, 105 MB), for +11%
  to +37% time (a) and +11% to +24% (b).  **(a) is output-identical** on every
  case tried (1,883 C and 1,464 C++ programs at one function per chunk, -O2 on
  339, three libstdc++ TUs: 0 failures, 0 differences; fixes needed: symbol walk
  over attribute dictionaries, aliases and comdats re-attached after the merge,
  internal linkage restored, catch-copy thunks cloned into the users).  (b) is
  not: 3.8% of the programs differ in instruction selection, dead `linkonce`
  functions are kept (601 of 1,464 programs, 3.3x the functions in a libstdc++
  TU), -O1+ loses the cross-chunk middle end, and it needs a text
  post-processor for assembler-generated symbols (personality stub, typeinfo
  stubs).  Bound: the CIR module is resident until its chunk is lowered (names
  are chosen at finish and the CIR passes run on the whole module), so the
  floor is the CIR module.  Recommendation: lazy emission first, then (a)
  behind a size threshold.
- **L4 optimization facts** (section 8).  Inventory of nine facts against
  `clang -fclangir` with the rule, the measured gap, whether the stream
  already has it, whether the FE must supply it, and whether upstream CIR
  expresses and lowers it.  One cannot be done with the pin as it is: branch
  weights (no CIR form; `cir.expect` exists).  The call attribute `builtin` of
  new-expression calls is lowered (settled by experiment, section 8.4), so
  `ALLOC_EXPR` is needed for parity.  Value experiment (real clangir IR,
  three libstdc++ TUs, attributes stripped, -O2): **`nounwind` is worth +12%
  to +23% of text**, `nonnull`/`dereferenceable`/`align` up to +5.5%,
  `inlinehint` +4.8% on one TU, noundef, mustprogress, norecurse, noreturn
  about 0.  Our gap is `nounwind` on declarations (8% against clangir's 56%;
  definitions 51% against 56%).  Six open questions (A to F) for the contract;
  three hazards named with their guards (static invoker `unused.capture`,
  noundef only for C++ non-`extern "C"` returns, mustprogress against trivial
  infinite loops).
- **L5 -O2 shake-down** (section 9).  C corpus 1,641 pass at both levels, 14
  regressions at -O2 and 63 improvements; C++ 1,354 / 3 / 7.  All 17
  regressions are attributed (lost `dg-options`, undefined behavior,
  LLVM/g++ failing the same way) except `builtin-ctype-1` (undiagnosed, S2)
  and `pr118224` (not attributable).  The four -O2 DIFFs of the writer
  probes are two probes with undefined behavior and two deviations of g++ -O2;
  none is a library defect.  Real-world units: 6 of 15 pass at -O2; the
  failures are `__builtin_constant_p`, the `__atomic_*` family, one placement
  form, and one stream rejection (Boost multiprecision, undiagnosed).
- **L6 profile** (section 10).  The lowering is 57% of the instructions, 17%
  of them the per-pass verifier; **lowering without the per-pass verifier
  gives -26% on the lowering stage and -13% to -16% on wall time with
  identical assembly** (ten lines in `pipeline.cpp`, the verifier stays under
  `VERIFY_EACH`).  Decode+emit is 8.6%, finish 4.8%; no further cheap win
  inside our own code.

Inputs.  C corpus: the 2,564 streams of `cir-c-corpus.txt` (types rp,lp);
C++ corpus: 1,805 streams of 1,958 cases of `cir-cpp-corpus.txt` (188 cases
write no stream: the FE rejects their options; 1,629 streams reach a module);
real-world TUs: eight programs (`containers`, `algo`, `strings`, `iostream`,
`memory`, `regexchrono`, `mixed`, `threadlike`: libstdc++ 15.3.1 headers, the
scraped gcc include path, `--c++17`) compiled by the box front end (an M4-era
FE; the debug build needs about 10 s per TU).  Tools added under
`cir_src/scripts/`: `box_collect_streams.py`, `box_collect_probes.sh`,
`keep-stream-fe.sh`, `box_stream_check.py`, `box_run_opt.py`.  Library time is
the sum of the per-stream minimum of 2 (corpus) or 3 (TUs) runs of the stage
times of `--stats` (decode, emit, finish, verify, CIR passes, lower, codegen);
Ir was not used (the box was shared, the stage times are CPU times of one
process).

## 1. L1: the dead-body census

Method.  At the end of `finish_entities` every defined function is listed with
its operation count, the time its records took (FUNC_BEGIN to FUNC_END, decode
and emit) and whether it is live by the symbol uses of the emitted module
(`entity_liveness`).  After lowering, the functions that GlobalDCE erased are
read from the LLVM module.  (`EDG_CIR_CENSUS=<file>` in the prototype.)

Per translation unit (-O0):

| TU | bodies | erased by GlobalDCE | dead ops share | dead consume-time share |
| --- | ---: | ---: | ---: | ---: |
| iostream | 784 | 759 (96.8%) | 97.5% | 97.0% |
| strings | 888 | 768 (86.5%) | 84.9% | 83.3% |
| threadlike | 1086 | 917 (84.4%) | 79.8% | 78.1% |
| algo | 1056 | 848 (80.3%) | 75.4% | 73.1% |
| memory | 363 | 262 (72.2%) | 67.3% | 64.1% |
| mixed | 1505 | 1002 (66.6%) | 64.4% | 60.4% |
| regexchrono | 3423 | 1421 (41.5%) | 30.0% | 27.5% |
| containers | 745 | 254 (34.1%) | 25.5% | 24.6% |
| total | 9850 | 6231 (63.3%) | | |

Corpora:

| | streams | bodies | erased | dead ops | dead consume time |
| --- | ---: | ---: | ---: | ---: | ---: |
| C++ corpus | 1629 | 35,970 | 24,449 (68.0%) | 586,496 of 960,845 (61.0%) | 1.01 s of 1.77 s (57.2%) |
| C corpus | 2314 | 10,103 | 338 (3.3%) | 7,364 of 763,913 (1.0%) | 23 ms of 1.66 s (1.4%) |

Per stream the erased fraction of the C++ corpus is bimodal: of 1,629
streams 907 erase less than 10% and 75 erase 90% or more.  The symbol-use
liveness predicts GlobalDCE closely: 24,116 dead against 24,449 erased on the
corpus (the 333 others are things LLVM sees as dead that the CIR symbol uses
still reach, for example through a dead vtable's constants), and 848 against
848 on `algo`.

Upper bound (A/B).  `EDG_CIR_ORACLE=<census>` makes the emitter skip the
body of every function that the census says GlobalDCE erased (the records are
still decoded and validated; the function stays a declaration).  The assembly
is byte-identical to eager emission for all eight TUs.  Library time, summed
(ms; eager / oracle):

| | decode | emit | finish | verify | passes | lower | codegen | total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| C++ corpus, eager | 2921 | 970 | 420 | 331 | 1664 | 8875 | 2983 | 18164 |
| C++ corpus, oracle | 2973 | 633 | 293 | 172 | 908 | 5259 | 3021 | 13259 (73.0%) |
| TUs, eager | 113 | 188 | 88 | 56 | 398 | 1426 | 410 | 2679 |
| TUs, oracle | 105 | 119 | 55 | 28 | 230 | 698 | 409 | 1645 (61.4%) |

Mean peak RSS of the TUs: 80.1 MB eager, 71.3 MB oracle (-11%); the C++ corpus
streams are small (mean 51 MB, process baseline 22 MB; the largest drops from
338 to 312 MB).  Lowering is the stage that shrinks most (the bodies are never
lowered), then the CIR passes; decode is unchanged.  So the dead set costs 39%
of the library on header-heavy code, and 27% on the corpus.

## 2. L2: the lazy-emission prototype

Implemented in `lazy-proto` (about 600 lines: `lib/cirgen/lazy.cpp` and hooks;
the plan's section "Lazy emission design" is the specification).  What it does:

- **Deferral.**  At FUNC_BEGIN of a function in a function-region frame whose
  linkage is internal, private or linkonce_odr, that is not C99 inline-only and
  has no dropped definition, the emitter ignores the body's records and
  remembers the function: its bytes (FUNC_BEGIN .. FUNC_END, copied: 83 bytes
  per body on average, 819 KB for 9,883 bodies), the frame's kind and flags,
  its stream offset, the G-ids the validator looked up while reading it, the
  class of `this`, and whether it holds VPTR_INIT, a VTT use or class
  aggregates.  The unit of deferral is the function, not the frame (a frame
  holds one function in practice, and the replay needs nothing of its frame).
  The function's entity gets `has_body = true` right away, so `finish` sees what
  it always saw (D0, C1 aliases, comdats, thunks); a separate `body_arrived`
  bit was not needed in the prototype.  90.5% of the C++ corpus bodies
  (32,562 of 35,970) and 100% of the libstdc++ ones are deferred.
- **Collecting references while validating.**  `a_id_tables::find_gid` appends
  every G-id it resolves to a sink that the emitter sets for the span of a
  deferred body.  No other change to the validator: it already looks every id
  up.  Edges also come from three kinds of record the lookups do not cover (a
  vptr inside a constant, VPTR_INIT and VTT uses: section 3).
- **Reachability at finish** (`lazy_finish`, before `finish_classes`): the
  existing `entity_liveness` closure over the emitted module's symbol uses and
  the roots of the plan (non-discardable definitions, dynamic initialization,
  uses from ops that belong to no entity), plus extra edges: deferred function
  to every entity it names; to the vtable and VTT of its `this` class when it
  holds VPTR_INIT or a VTT use, and to the vtables of the classes of its
  constant aggregates; a vtable or VTT entity to every function of its class's
  group (computed from the CLASS_VFUNCS/CLASS_OVERRIDES facts of the class and
  its bases, with the deleting destructors and `operator delete` of
  `deleting_of`: no layout is built); D0 to D1 and its delete function; the
  vptr placeholders of emitted code (`a_vptr_use`, `a_late_use`) to the vtables
  and VTTs they will name; the method-pointer constants of emitted code to the
  functions they hold.  C1/C2 pairs live together as before.
- **Replay** (`replay_function`): `a_stream_reader::replay_body` decodes the
  saved bytes and runs the bracket tracker and the reference checker with
  `refs_.replaying` set, which lifts exactly two once-only rules (a function is
  defined once; a STATIC_INIT is seen once) and keeps the L-id tables, the
  bracket depths and everything else; the handler is the emitter's own
  `on_record`.  `func_begin` accepts the already `has_body` entity while
  replaying.  The session's last file, line and column are saved and restored
  around the replay.  Bodies are replayed in stream order, so the module's
  function order is the eager one (the `cir.func` is created by its DECL_FUNC
  and filled in).
- **What stays dead.**  A function that is not replayed becomes an external
  declaration in `finish_entities` (the code that makes C99 inline-only bodies
  declarations): GlobalDCE then erases what refers to it, as before.
- **Incremental replay** (`EDG_CIR_LAZY=inc`): at FUNC_END of any emitted
  function the symbol uses inside it are looked up in a name table of the
  deferred functions; those are queued and replayed in `on_frame_end` (a safe
  point: the reader has finished the frame, so its tracker and checker are
  idle; replay inside the record callback would clobber the state that the
  reader's `commit` still needs).  The finish closure then handles the rest.

### 2.1 Result: per-symbol output against eager

`lazy_cmp.py` compiles each stream eagerly and lazily (`--emit=asm
--keep-going`) and compares exit status, the whole assembly (md5) and the
diagnostics.

| set | streams | identical (finish-only) | identical (incremental) |
| --- | ---: | ---: | ---: |
| C corpus | 2564 | 2564 | 2564 |
| C++ corpus | 1805 | 1805 | 1805 |
| writer goldens (C++ mode, C mode) | 159 + 34 | 158 + 34 | 158 + 34 |
| writer probes | 160 | 160 | 160 |
| libstdc++ TUs, -O0 and -O2 | 8 | 8 | 8 |

The one difference (`writer-cxx-static-init`) is the position of the line
`.hidden __dso_handle`: the eager run creates that declaration while it emits a
dead function that registers a destructor, the lazy run when the first live one
does.  Per symbol the output is equal.  Status and diagnostics are equal on
every stream: in particular no stream of any set has an error that only the
emitter finds inside a dead body (the "accepted loss" of the plan: none showed
up in 4,500 streams).

How the prototype got there.  The first run over the C++ corpus differed on 13
of 1,805 streams, from two missing edges, found by comparing the assembly (a
function a program needs is silently missing from the assembly otherwise: an
undefined reference at link time, never a wrong binding):

1. a vptr inside a constant (`C_AGG_BEGIN` of a dynamic class in a deferred
   body): eager emission creates a placeholder that names the vtable at
   finish; the replay is later, so nothing named the vtable (fix: the classes
   of `C_AGG_BEGIN` are edges to their vtables);
2. a method-pointer constant in an emitted function (`#cir.method<@f>`): the
   pin's `SymbolTable::getSymbolUses` does not report the symbol held by
   that attribute, so a function whose address is taken only that way looked
   dead (fix: the emitter records (user, function) when it makes such a
   constant; for the lowered `{i64, i64}` form too).

The oracle is the soundness test for any later edge change: the set of
functions replayed must contain every function that survives GlobalDCE, and the
assembly must be equal.  The `lazy-identity` gate step of the plan checks
exactly that.

### 2.2 Result: time, memory and the share moved to finish

Summed library time (ms; the deferral rule as above):

| | emit | finish | passes | lower | total | share of eager |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| C++ corpus, eager | 970 | 420 | 1664 | 8875 | 18164 | 100% |
| C++ corpus, lazy | 532 | 566 | 1002 | 5668 | 13942 | 76.8% |
| C++ corpus, incremental | 587 | 567 | 1000 | 5653 | 13957 | 76.8% |
| C++ corpus, oracle | 633 | 293 | 908 | 5259 | 13259 | 73.0% |
| TUs, eager | 188 | 88 | 398 | 1426 | 2679 | 100% |
| TUs, lazy | 64 | 131 | 237 | 723 | 1687 | 63.0% |
| TUs, incremental | 70 | 159 | 241 | 725 | 1729 | 64.5% |
| TUs, oracle | 119 | 55 | 230 | 698 | 1645 | 61.4% |

- Counts.  Corpus: 32,562 deferred, 8,189 replayed (25%), 24,373 dropped; the
  eager survivors among the deferrable functions number 8,008, so the closure
  overshoots GlobalDCE by 181 bodies (2.3%).  TUs: 9,883 deferred, 3,682
  replayed, 6,201 dropped, 3,609 survivors (overshoot 73, 2.0%).
- Costs of laziness.  Reachability (class-fact function sets, vtable and VTT
  edges, the symbol-use closure over the module): 134 ms on the corpus (0.74%
  of the eager library), 20 ms on the TUs (0.75%).  It builds the ABI facts a
  second time, as `finish_classes` does (the two should share one).  Replay:
  149 ms on the corpus, 57 ms on the TUs, 15.6 us per body against 19 us per
  body eager (the replay is as costly as the emission it replaces, so the
  saving is in the bodies not replayed).  The records of dead bodies are still
  decoded and validated (that is the "decode" column, unchanged): the
  validator's work, not the emitter's, is what remains of a dead body.
- Share of emit time moved to finish.  TUs: the stream-time emit falls from 188
  ms to 64 ms and 57 ms of replay come at finish: **47%** of the lazy emit time
  is spent at finish (corpus 22%).  Finish itself goes from 88 to 131 ms.
- Incremental replay.  1,682 of the corpus's 8,300 replays (20%) and 195 of
  the TUs' 3,702 (5%) happen before finish; the other bodies are reached
  through vtables, constructors and chains of deferred functions that start
  at finish roots.  It replays 20 more bodies on the TUs than finish-only (a
  reference from an emitted function that later dies), pays the symbol-use
  scan of every emitted function (+6 ms) and ends 1.5 points slower.  In
  the in-process mode FE and library are serial, so replaying earlier does not
  overlap anything: the only effect is that the CIR module of the replayed
  bodies exists earlier.  It becomes interesting only when the library has a
  thread of its own (the reader state at the safe point would then be the
  hand-over point).
- Memory.  TUs mean peak RSS: eager 80.1 MB, lazy 71.7 MB, incremental 71.9
  MB, oracle 71.3 MB.  The saving is in the lowering peak (the dead functions
  are never in the LLVM module at the same time as the CIR).

## 3. What the prototype says about the design

- The plan's phase 1 (class facts and vtable layouts "without emission") needs
  only the facts, not the layouts: the functions a vtable group can name are
  the virtual functions of the class and its bases, their final overriders and
  the deleting destructors.  That over-approximates the vtable, is cheap and was
  sound on all streams.
- Soundness needs an explicit edge for every implicit use that eager emission
  turns into a symbol use only at finish: VPTR_INIT and VTT uses (the vtable
  and VTT of the class of `this`), vptr in a constant aggregate, `D0` to `D1`
  and the delete function, thunks (targets are in the group), method-pointer
  constants.  The two bugs of section 2.1 were of this kind.  S1 should list
  them as the roots and edges of its design document and give each a
  `lazy-identity` case; the oracle comparison finds the rest.
- The reader needs three small things: a reference sink on `find_gid`, a replay
  entry point, and two exemptions in `replaying` mode (defined twice, STATIC_INIT
  twice).  The bracket tracker and the L-id table must really run in replay (the
  emitter reads the L-id payloads and the bracket depth); the plan's finding 2
  is right.
- A replay must not run inside a record callback of the same reader; at the end
  of a frame, and at finish, it is safe.
- `has_body` set at deferral keeps `finish_classes` and `finish_entities`
  unchanged; the checks the plan lists for `body_arrived` (type changed after
  the body, `has_vbases` after the body, a library-owned name) then behave as in
  eager mode.  The "defined twice" check of `func_begin` must be skipped while
  replaying.
- The method-pointer attribute `#cir.method<@f>` is invisible to
  `SymbolTable::getSymbolUses` at the pin (also to any reachability that relies
  on it).  The existing use of `entity_liveness` for dropped definitions has the
  same blind spot.
- The module order of library-created entities (`__dso_handle`, `__cxa_*`
  declarations) follows the first emitted use, so it differs between eager and
  lazy emission.  The gate step has to compare per symbol, not the file.
- Unit independence (concurrency room): a deferred body is a byte range plus
  a list of G-ids; replays are independent of each other except through the
  entity table and the module, and run in stream order only to keep creation
  order.  Nothing in the prototype needs the FE to wait.

Open points for S1: sharing the ABI facts between reachability and
`finish_classes`; per-frame deferral for frames with several functions (the
prototype defers per function, which is simpler and finer); the `used` roots
(key 11) and the guarded static local owner (the prototype needed no special
case: the owner's references include the variable, and a variable has no other
user); comdat groups beyond C1/C2 (the prototype relies on symbol uses).

## 4. Findings on the way (not part of the research question)

- Two library bugs in the guard worker (exit from the worker thread, M4.1 L2)
  and `ZERO_BYTES`: see the M4.1 delta.
- `ecil-stress-gen.py` init_deep/agg_init had failed since M3.1 with the
  reader's id-density rule (fixed in M4.1).

## 5. Stress at -O2 (watch items)

See section 6.

## 6. The candidate pin 2d6f180e (C5)

Image `edgcpp/cir-env:2d6f180e` (clang 24.0.0git, llvm-project `2d6f180e`),
against the pin `894bfff5`.

- Adaptation: one, in `CIRCompat.h`: `cir::LoweringLangOptionsAttr::get` has an
  `hlsl` bool after `hip`.  Nothing else in the library, tools or tests.
- Tests: ctest 10/10 (RelWithDebInfo), 10/10 (release), 9/9 (ASan, with the
  overflow and exit hooks); lit 186/186 (189 with L6/L7 on RelWithDebInfo).
- Stress watch list at -O0 and -O2 (every case rc=0 on both pins; four jobs
  ran concurrently, so absolute times are noisy): no growth changes.  diamond:160
  99 s / 100 s and 4.2 GB, vchain:2000 31 s / 31 s, tries:1000 19 s / 20 s,
  news:2500 39 s / 33 s, unwind:4000 110 s / 76 s at -O0.  No watch item is
  fixed (CIRCanonicalize on nested try, the quadratic vtable and diamond
  lowering, `new` and unwind chains, vchain finish).
- New -O2 watch items (the same on both pins): the stage "CIR passes" at -O2
  takes 207 s (pin) / 265 s (candidate) on `ternary:5000` and about 200 s on
  `land:5000`, against 0.3 s at -O0 (LLVM `lower` 5.7 s / 11.2 s); `elseif:20000`
  takes 288 s / 303 s at -O2 (163 s / 178 s at -O0: verify 15.7 s, passes
  5.3 s, lower 31.6 s).
- 036efe53 (x86_64 records through memory), checked three ways: the mixed-TU
  cases (19 cases, 2 directions, g++ and clang++ as the other compiler) give
  36/36 PASS on both libraries; every lit host-TU differential passes; a
  run-differential over the corpus streams at -O0 (C 2564, C++ 1770 cases;
  link, run, exit status) differs in 13 cases: 11 print addresses (the pin
  differs from itself), `c/pr78468` (the pin aborts, the candidate passes: a
  fix) and `opt/nrv5`, a regression: the candidate's CallConvLowering rejects a
  record value that leaves a region through `cir.yield` ("use of a record
  argument or result other than by a store, call, return or member read not
  yet implemented").  The -O0 assembly differs in 99 of 2564 C and 222 of 1805
  C++ streams, in the form of the copies (stack layout, extra copies), not in
  the registers.  No classification flip was found; the candidate adds one
  unsupported shape and changes the code shape of by-value records.

### 6.1 -O2 run-differential, pin library against candidate library

Same corpus streams as the -O0 run (C 2564 cases, C++ 1770 cases), compiled at
-O2 with each library, linked, run (20 s limit) and compared by exit status:

| | cases | same | differ | classification of the differences |
| --- | ---: | ---: | ---: | --- |
| C | 2564 | 2558 | 6 | `builtins-41` and `torture/builtin-round-roundeven`: the pin does not link (`link_error` not folded away), the candidate links and passes (2 LLVM optimizer fixes); `ipa-sra-1`, `matrix-2`, `matrix-6`: programs that print addresses (noise: the pin differs from itself); `torture/pr66793`: see below |
| C++ | 1770 | 1762 | 8 | `opt/nrv5`: the candidate's library error (fixed by 6.2); `rma/init24`, `rma/init26`, `cpp0x/constexpr-tuple`, `init/elide3`, `opt/alias2`, `opt/tmp1`, `tc1/dr20`: address-printing noise |

No difference involves EH or cleanup code: every case of `eh/` and every
cleanup-heavy case gives the same result with both libraries, so the
`cir.cleanup.scope` region-branch change (16b2fbe1) is not visible in the
corpora at -O2.

`torture/pr66793` (the pin exits with status 40, the candidate hangs) is not a
miscompile.  The test is a link-only test (`dg-do link`, type `lp`) whose
program dereferences a null pointer (`fn1 (**f)` with `d = 0`) and then loops
for ever (`for (; c;);` with `c` set to 1): running it is undefined and never
terminates once the undefined access is optimized away.  At -O2 gcc, clang and
`clang -fclangir` (candidate image) all hang, at -O0 all four crash with
SIGSEGV.  The pin's status 40 is an artifact of its older code generator; our
run harness runs link-only tests, the gate does not.

### 6.2 `opt/nrv5`: a record leaving a region by `cir.yield`

The candidate's CallConvLowering (after 036efe53) accepts a record SSA value
only in a store, a call, a return or a member read.  The one place where this
library lets a record value flow out of a region is the conditional operator
with an aggregate result: `tern_record` yields the record value of each arm
(`TERN_ELSE`, `TERN_END`).  Every other `cir.yield` we emit carries a pointer
(thunk bodies: `classes.cpp`), a bool (`LAND`/`LOR`), a scalar, or nothing.  The
pin accepted a yielded record and lowered it (so M4 produced and compiled the
shape); in the corpora it occurs in two C++ streams (`opt/nrv5` and one other)
and in none of the C ones.  clang's own CIRGen writes an aggregate conditional
into its destination slot.  The fix, applied to the candidate tree: for a
result of an aggregate type the arms store into a `tern.tmp` temporary
(`alloca_here`) and yield nothing; the value is loaded after the ternary.  The
`-O0` output of every other result type is unchanged (the change is inside
`result_type && is_aggregate_type`), the two streams with aggregate ternaries
change; `nrv5` compiles and passes at -O0 and -O2; lit `cxx-tern-record`.

### 6.3 Attribution of the -O0 assembly churn (`asm_churn_classify.py`)

`cir_src/scripts/asm_churn_classify.py` classifies the differences between two
assembly sets (a pair of directories of `.s` files, as the gate's asm-diff
produces, or stream by stream with two `edg-cir` binaries, in which case the
LLVM IR of both is compared too).  Per function: identical; `frame` (equal after
normalizing stack offsets, the frame size and CFI offsets); `copies` (equal after
dropping data-movement instructions: mov, lea, push, pop, extensions);
`calls` (same calls and control flow, other instructions differ); `other`
(calls or control flow differ).  A file takes its worst function's class;
`structure` is a different function set or different data; `backend-only` is a
file whose LLVM IR is identical while the assembly differs (upstream code
generator).  Notes count memcpy/memmove/memset calls added or removed.

Pin library against candidate library at -O0 on the corpus streams:

| | files | identical | frame | copies | calls | other | structure | backend-only |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| C | 2564 | 2465 (96.1%) | 5 | 52 | 17 | 0 | 1 | 24 |
| C++ | 1805 | 1582 (87.6%) | 40 | 75 | 9 | 21 | 15 | 63 |

So the record-copy rewrite is the `frame` and `copies` classes (C 2.2%, C++
6.4%); `calls` and `other` need a look (C 0.7%, C++ 1.7%): sampled, they are
the same copies with a different instruction order and frame size (`calls`),
a constant store that became a `memset` call (5 C++ files: the lowering of a
zeroing now emits the call at -O0), or a different alloca order that changes
which temporary a value lives in (`other`); `structure` (C++) is the EH
call-site table (`.gcc_except_table` entries of empty ranges dropped), and
`backend-only` is LLVM code generation (IR identical).  The classifier is a
triage aid, not a proof: the claim "no classification flip" rests on the
mixed-TU and run differentials (6, above).

### 6.4 M5p follow-ups (in the tree, uncommitted)

The three follow-ups of the M5p review are done in the Mac tree:
`asm_churn_classify.py` reports IR changes in every class (a file whose LLVM IR
differs is never `backend-only` any more); `test/cirgen/cxx-tern-record.test`
covers the aggregate conditional in a namespace-scope dynamic initializer, a
static local and a nested conditional (not only the function-local case of
`opt/nrv5`); the `lowering_lang_options` call in `CIRCompat.h` names every
argument in a comment (`/*exceptions=*/` ... `/*hlsl=*/`), so the next
positional change of the upstream attribute is visible in review.

### 6.5 Attribution with the body/module split (cir-config's M5p finding)

In the first form of the classifier (stream mode), the IR classes `ir-other`
and `ir-module` lumped three different things: a change of an attribute only
(a call that gained or lost `nounwind`, a declaration that gained `noundef`),
a shift in the numbering of unnamed basic blocks (`5:` for `1:`), and a real
change of an instruction or a declaration.  cir-config found the first two
dominating the "other" and "module" counts of the M5p attribution and
therefore reading as lowering changes.  `asm_churn_classify.py` now ignores the
block-label numbering and splits the IR difference by where it is and what it
is: `ir-attrs` (only attributes differ, in bodies on calls or at module level
on functions, parameters, returns and declarations), `ir-copies` (the record
copies, attributes aside), `ir-module` (the module level differs beyond
attributes), `ir-other` (an instruction of a body, or the set of functions,
differs); the worst class of a file wins.  `--self-test` checks the seven
cases (identical, renumbered labels, call attribute, declaration attribute,
extra alloca, extra declaration, changed terminator).  Attribution of an
asm change should read: `backend-only` = LLVM code generation; `ir-attrs` =
attribute plumbing (the nounwind/noundef kind of change of section 8);
`ir-copies` = record copies; `ir-module` and `ir-other` = lowering or library
changes to look at.

## 7. L3: chunked lowering spikes (the memory bound)

Question: the finish step builds one CIR module, then lowers all of it to one
LLVM module; the peak RSS of a large translation unit is the lowering, not the
decode.  Can the module be lowered in pieces?  Two spikes, in the scratch tree
`~/edg-cir-lib/m5a` (source: `cir_src/lib/driver/chunk.cpp`, a copy is in
`/tmp/m5a-spike/` on the Mac; nothing is in the repository):

- **(a) chunked CIR, one LLVM module.**  The function bodies are cut into
  chunks of N; every chunk becomes a CIR module of its own that holds the
  bodies plus declarations of every symbol they use; it is lowered to an
  `llvm::Module`, linked into the result with `llvm::Linker::linkModules`, and
  its CIR is erased.  The globals (everything that is not a function body) are
  one more chunk, lowered first.  The merged module then goes through the
  existing GlobalDCE/optimizer/code generator unchanged.
- **(b) per-chunk code generation.**  The same chunks, but each `llvm::Module`
  is code-generated to assembly text at once and freed; the texts are
  concatenated.

Both use the lowering without the per-pass verifiers (section 10), so the
reference column is the whole-module lowering with the verifiers off
(`EDG_CIR_LOWER_NOVERIFY`); the default whole-module path is slower than
that (c6000: 4.79 s against 4.04 s).  The CIR-to-CIR passes still run on the whole module before
the chunking (the floor below).

### 7.1 Numbers

Input: the generated streams `c6000`, `c12000` (C: 6,000 / 12,000 functions,
882 k / 1.76 M records) and `cxx6000`, `cxx12000` (C++: templates, classes,
namespaces), -O0, `--emit=asm`, one process on the box (x86-64, 16 cores,
RelWithDebInfo with LLVM assertions on).  Peak is `ru_maxrss`.  "Floor" is the
peak RSS after the CIR passes, i.e. what the CIR module alone costs (it is
resident at finish).  "End RSS / malloc" are `/proc/self/statm` and
`mallinfo2().uordblks + hblkhd` after the last chunk, before code generation.

| stream | reference peak | floor | (a) N | (a) peak | (a) end RSS / malloc | (b) peak | (b) end RSS / malloc |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| c6000 | 425 MB | 158 MB | 200 / 500 / 1000 | 221 / 231 / 246 MB | 183 / 199 / 208 MB; 111 MB | 208 / 218 / 241 MB | 208 / 218 / 224 MB; 41-49 MB |
| c12000 | 810 MB | 278 MB | 500 | 395 MB | n/a | 366 MB | n/a |
| cxx6000 | 157 MB | 72 MB | 100 / 500 / 2000 | 115 / 118 / 131 MB | 89 / 92 / 106 MB; 41 MB | 104 / 109 / 132 MB | 104 / 107 / 126 MB; 26-28 MB |
| cxx12000 | 260 MB | 105 MB | 500 | 177 MB | 135 MB; 81 MB | 163 MB | 156 MB; 57 MB |

Peak reduction at N=500: c6000 -46% (a) / -49% (b); c12000 -51% / -55%;
cxx6000 -25% / -31%; cxx12000 -32% / -37%.  Two observations from `mallinfo2`:

- In (b) the resident size after the last chunk is 3 to 5 times the memory in
  use (c6000: 218 MB against 42 MB): the glibc heap keeps what the freed
  chunks held.  `malloc_trim(0)` after each chunk (one line, env switch in the
  spike) brings the (b) peak from 218 to 198 MB (N=500) and 208 to 186 MB
  (N=200), and the end RSS to 151 MB, for no measurable time.  In (a) it does
  not change the peak, because (a)'s peak is the code generation of the merged
  module (the end-of-link RSS of c6000 is 199 MB, the peak 231 MB).
- (a) keeps the whole LLVM module (malloc in use 111 MB at the end of c6000)
  and so never gets below about the LLVM-IR size plus the code generator's
  working set; (b) keeps nothing but the text (the assembly of c6000 is 11 MB).

Time (wall, total process, N=500 unless marked; reference = verifiers off):

| stream | reference | (a) | (b) |
| --- | ---: | ---: | ---: |
| c6000 | 4.04 s | 4.96 s (+23%) | 4.60 s (+14%) |
| c12000 | 8.06 s | 11.04 s (+37%); N=1000 10.57 s; N=3000 10.43 s | 9.99 s (+24%) |
| cxx6000 | 1.46 s | 1.62-1.70 s (+11% to +16%) | 1.62-1.67 s (+11% to +14%) |
| cxx12000 | 3.10 s | 3.71 s (+20%) | 3.66 s (+18%) |

Where the time goes (c12000, N=500, 24 chunks, per chunk): lowering 172-213 ms
(rising slowly with the chunk index; the globals chunk, 12,000 declarations,
248 ms); `Linker::linkModules` 4 ms for the second chunk, rising linearly to
81 ms for the last (1.3 s in total: the link cost grows with the size of the
result); creating the next chunk (moving the bodies, scanning for the symbols
they use) 15 to 26 ms; translation to LLVM and erasing the CIR chunk 4 to 22
ms.  With N=3000 (four chunks) the link cost is small and the total is still
2.4 s above the reference, so the link is not all of it; the remainder was not
profiled.  The overhead is lower for C++ in absolute terms and about the same
in percent.  A per-chunk fixed cost of 40 to 60 ms would explain the C numbers;
bigger chunks lower it and raise the peak (c6000: N=200 221 MB, N=1000 246 MB).

Output at -O2: (a) gives the same assembly as the whole module (cxx6000
1,560,241 bytes both, 1,560,242 with the module name; c6000 10,700,514 against
10,700,512), the same time and a peak of 130 MB against 166 MB (cxx6000) and
247 MB against 431 MB (c6000), because the middle end runs on the merged
module.  (b) cannot do this: without a middle end the -O2 assembly is 4.07 MB
for cxx6000 (the whole module gives 1.56 MB: nothing inlined, nothing
dead-stripped); with the per-chunk pipeline (`EDG_CIR_CHUNK_OPT`) it is 1.38 MB
(smaller than the whole-module result, because no cross-function inlining
grows it) in 2.83 s against 3.10 s, and 10.68 MB against 10.70 MB for c6000 in
the same time: a valid but different optimization result.

### 7.2 Output identity

At N=1 (every function in a chunk of its own: the worst case for every
cross-chunk problem) over the corpus streams at -O0, with the sweep
`m5_sweep.py` (assemble both outputs with `as`; compare, function by function,
the disassembly with relocation operands, jump targets, nop padding and
addresses masked, plus the section sizes without `.text`):

| set | compared (the rest do not compile at the reference) | (a) | (b) |
| --- | ---: | --- | --- |
| C (2564 streams) | 1883 | same text 1800, same object 48, same functions with different data padding 35; **0 failures** | not run |
| C++ (1806 streams) | 1464 | same text 1290, same object 159, same functions with different data padding 15; **0 failures** | same object 768, same functions with padding 39, extra functions only 601, **different instruction selection 56**, 0 assembler failures after the fixes below |
| C++ -O2, first 400 | 339 | same text 299, same object 38, padding 2: **0 failures** | not run (see above) |
| three libstdc++ TUs (the other five do not compile) | 3 | same (N=20, 200; -O0 and -O2) | extra functions only 1, different 2 (below) |

Padding means the same functions with a different order of the data objects
(the alignment padding between them differs, a few bytes of `.data`/`.rodata`
or of `.text` alignment).  "Extra functions" means the merged text has every
function of the reference and more: dead `linkonce_odr` functions that
GlobalDCE removes in the whole module; libstdc++ `mixed` has 1,506 functions
in (b) against 504, `strings` 889 against 121.  "Different instruction
selection" is 56 of the 1,465 programs: one or more functions whose
instructions differ, with the same behavior (examples: `movl $5, x` against
`mov y(%rip), %eax; mov %eax, x` where the reference folded a load from a
constant global defined in the module; `movups` against two `mov`s of a copy
from a global whose alignment was known in the module).  An external
declaration does not carry what the definition's initializer and alignment
tell the code generator.

### 7.3 Correctness issues, with verdict

Each item was found by a failing case; the fix is in the spike.

1. **Symbol uses.**  `SymbolTable::getSymbolUses` on the chunk module misses
   calls inside `cir.func` bodies and the symbols in nested attributes
   (`#cir.method`, `global_view`); the first lowering of a chunk crashed in
   `rewriteCallOrInvoke` (a callee that does not exist in the module).  The
   declarations of a chunk must come from a walk over every operation's
   attribute dictionary (`Attribute::walk<SymbolRefAttr>`), the same blind spot
   found for lazy emission (section 2).  Verdict: fixed; required for both.
2. **Internal names.**  A chunk cannot see another chunk's `internal` or
   `private` symbol.  Both are made external while the chunks are lowered and
   restored in the merged module ((a)) or before code generation ((b)); the
   declarations of them get `dso_local` (else an access to a `private` constant
   goes through the GOT).  (b) uses `internal` for `private` too: a `private`
   symbol is named `.L<x>` where defined and `<x>` where only declared, which
   gives an undefined reference.  Name uniquing is not a problem because CIR
   already gives every symbol a unique name at finish (`__cxx_global_var_init.N`,
   renamed internals); nothing is renamed by the linker.  Verdict: fixed;
   (b) differs from the reference in the symbol table (`.str` against `.L.str`:
   more local symbols in the object file).
3. **Aliases.**  A `cir.func ... alias(@target)` cannot cross a link boundary:
   the linker asserts when another chunk declares the alias name.  (a): the
   aliasee attribute is stripped before the chunks are lowered and the
   `GlobalAlias` is created in the merged module (replace uses, take the
   name).  (b): the alias and its target are forced into the globals chunk;
   the other chunks' references are resolved by the assembler in the one file.
   Verdict: fixed for functions; aliases of globals were not exercised.
4. **Comdats.**  The linker keeps the first definition of a comdat group and
   drops the others, so members of a group that are split over chunks (the
   initialization function of an inline variable, which belongs to the
   variable's group; the complete and base structor variants) disappear.  (a):
   the comdat attribute of the function bodies is stripped and re-attached in
   the merged module (`getOrInsertComdat`).  (b): the comdat sections of the
   chunks coexist in one file (a group with members in several sections is
   legal), but every chunk emits the synthesized weak helpers again:
   `__clang_call_terminate` (removed after the first chunk by turning later
   `linkonce` definitions into declarations), `DW.ref.__gxx_personality_v0`
   (assembler-generated; later copies are cut from the text) and the
   typeinfo stubs `.L<name>.DW.stub` (renamed per chunk).  Verdict: (a) fixed,
   cheap; (b) needs a text post-processor that knows these three, and any new
   AsmPrinter-generated symbol would break it: fragile.
5. **Global constructors and destructors.**  The module attributes
   (`cir.global_ctors`/`dtors`) go to the globals chunk only; the
   `@llvm.global_ctors` array of the result is complete (checked, including
   the entries with associated data of inline variables).  Verdict: fixed.
6. **Finish-time vtables and thunks.**  They are ordinary operations of the
   module by the time of lowering (vtables are globals, thunks are functions),
   so they chunk like any other.  Covariant and `this`-adjusting thunks pass
   in the abi tests at N=1.  One exception: the lowering of
   `cir.construct_catch_param` inlines the body of its catch-copy thunk
   (`cir.eh.catch_copy_thunk`), which has to be in the same module as the
   user ("catch-copy thunk has no body to inline"): such callees are cloned
   with their body into every chunk that names them, with the transitive
   declarations of the clone.  Verdict: fixed; a general rule is needed: any
   lowering that reads a callee's body must be told to the chunker (this is
   the one case found).
7. **CallConvLowering module state.**  In the spike every CIR-to-CIR pass
   (`runCIRToCIRPasses`, which includes `CallConvLowering`, `LoweringPrepare`
   and `FlattenCFG`) runs before the chunking, so the state they keep per
   module is untouched, and this is also why the bound below holds.  Chunking
   the passes themselves was not tried; `LoweringPrepare` creates the global
   constructor function and the guard variables, which are module-level
   decisions.  Verdict: not an issue for the lowering chunker; a blocker for
   anything that wants to free the CIR before the passes.
8. **Order of the chunks.**  The globals chunk is lowered and linked first.
   Tried last (to give the aliases a defined target in the destination): the
   final link turned the definitions of `linkonce_odr` comdat functions that
   the body chunks had defined into declarations (the globals chunk holds a
   declaration of every body function); the cause was not isolated, the
   order was changed back and the alias problem solved differently (item 3).
9. **Dead code.**  (b) has no whole-module GlobalDCE (at -O0 the whole-module
   path runs it to drop dead internal and `linkonce_odr` functions), so the
   result has more functions: 601 of 1,465 C++ programs, 3.3 times more
   functions in a libstdc++ TU.  Unless lazy emission (section 2) has already
   dropped them, (b) is a code-size and assembler-time regression for exactly
   the header-heavy programs that are large.
10. **-O1 and up.**  (b) has no whole-module middle end; per-chunk pipelines
    give a valid, different result (no cross-chunk inlining or IPO, no
    cross-chunk dead-function removal) and are not output-identical.  (a) is.
11. **One object file.**  (b) produces text; an object file needs the text to
    be parsed again (GNU `as` takes 0.3 s for the 11 MB of c6000; the in-process
    MC assembler should be of the same order, not measured) or one object per chunk and a
    relocatable link (`ld -r`) outside the process; LLVM has no in-process
    merge of object files.  (a) goes through the unchanged object writer.
12. **Module identity.**  The merged module has the name `chunk` (visible as
    `.file`); trivial to set from the first module.

Fallback: the whole-module lowering (status quo) stays the path below a size
threshold.  A failed chunked lowering cannot fall back to the whole module in
the same process: each chunk's CIR is erased once it is lowered, so the whole
module no longer exists.  The fallback would have to run the pipeline again
from the stream (the stream is a file or a memory buffer in the callers that
matter, so a second pass is possible, but it is a second pass).

### 7.4 The bound

The CIR module is resident at finish and stays resident through the CIR
passes, because names are chosen there (internal-name uniquing, structor and
thunk selection, comdat decisions, the lazily-created declarations) and
because the passes run on the whole module.  The chunked lowering only
removes the lowering's own cost, which is 1.7 to 2.7 times the CIR module for
these streams (c6000: floor 158 MB, whole-module peak 425 MB).  The best case
is therefore floor plus the in-flight chunk plus what has already been merged
or emitted: (b) 208 MB (186 MB with the trim) for c6000 against the floor of
158 MB; (a) 221 MB.  Below the floor needs either the passes to run per chunk
(not tried) or a CIR module that is smaller (lazy emission, section 2, removes
the dead 63% to 68% of the bodies before any of this: the libstdc++ peaks
already drop 10.5%, and the chunked path then applies to what is left).

### 7.5 Recommendation (for the user's choice at the gate)

- (a) is output-identical on everything tried, cheap to maintain (the
  linker does the work; the three adaptations are strip-and-restore of
  linkage, aliases and comdats, plus the symbol walk and the inline-required
  callee list), and gives -25% to -51% peak RSS for +11% to +37% time in
  the stress streams, and the same time at -O2 where the middle end dominates.
  It does not help small translation units (cxx6000 saves 40 MB).
- (b) saves 3 to 7 points more memory and 10% less time than (a) but is not
  output-identical (different instruction selection in 3.8% of programs, dead
  functions kept, not usable at -O1+ without giving up cross-chunk
  optimization) and depends on a text post-processor for assembler-generated
  symbols.  Not recommended except as a -O0-only memory mode.
- Suggested order: lazy emission first (S1: it removes 63% to 68% of the
  bodies for header-heavy C++ and costs no time), then (a) behind a size
  threshold (functions with bodies after finish above about 2,000, or a CIR
  floor above about 100 MB), as a separate gate item; measure again on a real
  header-heavy translation unit at that point (the generated streams are
  call-light: 2 to 3 calls per function, which is why a chunk of 500 functions
  declares only a few more).

## 8. L4: optimization facts against `clang -fclangir`

Method.  `edg-cir-attr-diff` (cir-config's tool) compares the LLVM IR attributes
of ours (`cpfe-cir` + `edg-cir`, scratch `--no-llvm-passes`, which stands for
the `EDG_CIR_FLAG_NO_LLVM_PASSES` that is not in the contract yet) with
`clang -fclangir -Xclang -disable-llvm-passes`, per symbol: function, return
and parameter attributes, the union of call-site attributes per callee, the
kinds of instruction metadata.  Sets: `cir-c-smoke` (40 TUs), `cir-cpp-smoke`
(99; clang could not compile 12, we failed on 7, mostly `main()` without a type
and a `0.0bf16` literal) and the seven libstdc++ TUs (we fail on 4 without
`--keep-going`).  The counts below are the most frequent differences (a
difference counts once per function, parameter or callee; same counts at -O0
and -O2 because no LLVM pass ran on either side).  The rules are read from
`clang/lib/CIR/CodeGen/CIRGenCall.cpp`, `CIRGenFunction.cpp`, `CIRGenModule.cpp`
and `CIRGenExprCXX.cpp` at `2d6f180e` and `Lowering/DirectToLLVM/LowerToLLVM.cpp`
at the same revision; "measured" means attr-diff or an experiment of this
section, "read" means source reading only.

What the stream carries today (`ecil_format.h`, minor 1.3): `DECL_FUNC` flags
`NORETURN`, `INLINE`, `INLINE_ONLY`, `IS_MAIN`, `NOTHROW` (1.2); `CALL` flags
`NORETURN`, `NOTHROW`; `PARAM` flag `THIS`; the language and `EXCEPTIONS`
flags in the header; supp keys `nothrow` (hint), `branch_hint`, `pure`,
`const`, `align_assumed`, `noinline` (14).  The library uses `NORETURN`
(function and call), `INLINE`, `INLINE_ONLY`, `IS_MAIN`'s consequences for
the entry, `noinline`, and `NOTHROW` (only to leave a call without an unwind
edge, never as a function attribute: `apply_function_attributes` sets only
noreturn and the inline kind).  Not used: supp `nothrow`, `branch_hint`,
`pure`, `const`, `align_assumed`.

### 8.1 Inventory

| fact | clang -fclangir rule | gap measured | from the stream? | FE fact needed? | upstream CIR expresses and lowers it? | -O1+ recovery by LLVM |
| --- | --- | --- | --- | --- | --- | --- |
| **noreturn on declarations and calls** | the declaration's `[[noreturn]]`/`_Noreturn`/attribute, **and** library builtins (abort, exit, `_Exit`, `quick_exit`, ... by builtin id: the declaration must match the builtin's type); the call site gets it from the callee (`info.isNoReturn`, `FunctionDecl::isNoReturn`) | c-smoke: 38 functions (`abort`, `exit` declared without the attribute), 33 calls of `abort`, 6 of `exit` | calls: yes (the callee's `NORETURN` flag); declarations of the library builtins: only with a table keyed on name, C linkage and exact type (library knowledge in the library) | **yes** for builtins: whether EDG's own builtin knowledge marks `abort`/`exit` noreturn (open question A) | yes: unit attribute `noreturn` on `cir.func` and `cir.call`, both lowered | `InferFunctionAttrs` sets `noreturn` on declarations of the library functions it knows (not under `nobuiltin`); `FunctionAttrs` infers it for definitions; the call-site attribute follows | 
| **noundef** on parameters, arguments, returns | `determineNoUndef`: every scalar, pointer, enum, bool, complex/vector element; not records, member pointers, or a type whose store size differs from its size; return only if `hasStrictReturn` = C++ and not `extern "C"` and not a type that may be dropped | cpp-smoke: param0 425, param1 100, return 138; libstdc++: 398 / 193 / 201; c-smoke: 29 / 10 / 4 parameters, 11 call arguments | yes: the type kinds are in the stream; `extern "C"` for the return rule is the one thing to check (the language and the linkage of the entity: derivable if the mangled name equals the source name and the TU is C++, otherwise FE) | `extern "C"` (open question B) | yes: `llvm.noundef` in `arg_attrs`/`res_attrs`/call `arg_attrs` dictionaries, lowered | none for parameters (LLVM cannot infer it); `FunctionAttrs` can infer it for returns |
| **nounwind / nothrow** | C++: `noexcept`, `throw()`, implicit-`noexcept` destructors, library nothrow; C: every function and call when `-fno-exceptions` (always for C); also from `const`/`pure`/`noalias` function attributes. Lowered: either `nothrow` or `nounwind` sets LLVM `nounwind` (1af48d87) | libstdc++ and C++ smoke: function-level `nounwind` only on clang (cpp-smoke 76, libstdc++ many); C: function-level on clang, call-level on ours (13 calls of `foo`, 6 of `exit`: we set `nothrow` on calls whose callee has the flag or when the TU has no exceptions, never on the function); definitions with `nounwind` (containers, IR before the LLVM passes): 381 of 746 (ours, 51%) against 258 of 463 (clangir, 56%); declarations 4 of 49 (8%) against 14 of 25 (56%); calls 682 of 1187 (57%) against 515 of 1155 (45%) (eager emission makes the counts of ours larger, the ratios are comparable) | the language/exception flag: yes (every C function); `noexcept` and dtors: the `NOTHROW` flag exists, the **coverage of the FE predicate** is unknown (open question C) | audit of which functions get `DECL_FUNC.NOTHROW` | yes: `nothrow`/`nounwind` unit attributes on `cir.func` and `cir.call`, lowered | `FunctionAttrs` infers `nounwind` for definitions bottom-up; **declarations and indirect calls are not recovered** |
| **this / reference nonnull, dereferenceable, align** | `this`: `dereferenceable(sizeof non-virtual part)`, `nonnull`, `align` (natural alignment of the class) unless null is valid; references (parameters and returns): `dereferenceable(size)` if the type is complete and constant-size, `nonnull`, `align`; `restrict` parameters get `noalias` | cpp-smoke: param0 `nonnull` 312, `align 8` 228, `dereferenceable(8)` 150, `align 1`/`dereferenceable(1)` 63; libstdc++: 344 / 255 / 79 | `this`: yes (`PARAM` flag `THIS`; size and alignment from `CLASS_INFO`, minimum object size is the non-virtual size); references and `restrict`: the stream types are pointers, so the reference-ness is lost | **yes** for reference parameters and returns and for `restrict` (open question D) | yes: `llvm.nonnull`, `llvm.dereferenceable`, `llvm.align`, `llvm.noalias` in the argument/result dictionaries, lowered | partly (`align`, `nonnull` from dereferences; `dereferenceable` from loads at the start of a function) |
| **`builtin` on the calls of a new-expression** | an elidable call to a replaceable global allocation function (declared `nobuiltin`) gets the call attribute `builtin` (C++1y [expr.new]p10); a direct `::operator new(n)` call does not | **settled by experiment** (section 8.4): clangir at `2d6f180e` emits `call ... @_Znwm(...) #N` with `attributes #N = { builtin allocsize(0) }` for `new`, `builtin nounwind` for the matching `operator delete`, and only `allocsize(0)` for an explicit `::operator new(8)`; ours has `nobuiltin` on the declaration (M4.1) and nothing on the call | the call and its callee are in the stream; whether the call comes from a *new-expression* (not a direct `operator new(n)` call) is not | **yes**: a flag on the `CALL` (`ALLOC_EXPR`, in 1.3 now) | yes: CIRGen sets the unit attribute `builtin` on the `cir.call`, and the lowering carries it to LLVM's `builtin` call attribute (the experiment shows it in the LLVM IR; `LowerToLLVM.cpp` has no explicit `setBuiltin`, the attribute travels with the call's attribute copy) | none (it is the call-site attribute that licenses the elision: without it new/delete pairs are not removed) |
| **`available_externally`** for C99 `inline`/`gnu_inline` definitions with no external definition | the body is emitted with `available_externally` linkage at -O1+ (and omitted at -O0) so that LLVM can inline it | in this library such a function is dropped to a declaration (`globals.cpp`, `INLINE_ONLY`): every call is an external call; effect at -O2: no inlining of the inline-only function (not measured) | the body is in the stream (flag `INLINE_ONLY`); the optimization level is known | none | the linkage exists (`AvailableExternallyLinkage`, used by CIRGen for thunks and variables) and lowers | none (the body is gone) |
| **mustprogress, norecurse** | `mustprogress` on every function in C++11 and later (`checkIfFunctionMustProgress`: C++11, unless `-ffinite-loops=never`), removed by a trivial infinite loop (`while (true) {}` and the like, `checkIfLoopMustProgress` with the C++26 rule); `norecurse` on C++ `main` and on OpenCL/HLSL/SYCL/CUDA kernels. CIR has no per-loop `llvm.loop mustprogress` | cpp-smoke: `mustprogress` 423, `norecurse` 80 (the `main` functions); libstdc++: 376 | yes for C++ >= 11 (language and standard in the header); `norecurse`: `IS_MAIN` flag and C++; the loop condition: constants are folded by the FE, a literal true condition is in the stream | none, if the standard version is in the header facts (it is: `ECIL_LANG_*` and the language; the standard level is not: open question E) | yes: unit attributes `mustprogress`, `norecurse` on `cir.func`, lowered | none (LLVM does not add `mustprogress`; its absence blocks removal of side-effect-free loops) |
| **branch weights, `cir.expect`** | `__builtin_expect` becomes `cir.expect` (CIRGenBuiltin); `[[likely]]`/`[[unlikely]]` and profile data do not exist in CIR | not measurable by attribute (metadata `prof` and `llvm.expect`): the stream's supp `branch_hint` is not consumed | the FE's `branch_hint` supp key (if the FE writes it) | the FE must write `branch_hint` and say for which expression | `cir.expect` exists and lowers to `llvm.intr.expect`; **branch weights have no CIR form** (`cir.brcond` and `cir.if` have no weights; CIRGen emits none for `[[likely]]`) | `llvm.expect` is lowered by `LowerExpectIntrinsic` at -O1+, so `__builtin_expect` is recovered if we emit `cir.expect`; `[[likely]]` is not recoverable |
| **inline kind** | `-O0`: `noinline` on everything that is not `always_inline`; otherwise `inlinehint` for functions declared `inline` (any redeclaration or the template pattern; not implicitly inline member functions), `noinline` for the rest when only hints are honored | cpp-smoke: only ours `inlinehint`: 294 at -O0, 160 at -O2; only clang `inlinehint`: 46 at -O2 | `INLINE` flag; the optimization level | whether the keyword was written (EDG's `INLINE` flag may include implicit inline: open question F) | yes (`InlineKind`, lowered) | none |

(`noinline` and `optnone` are in the tool's ignored set
(`IGNORED_FUNCTION_ATTRS`), so the -O0 `noinline` of clang is not compared;
the one visible effect is the extra `inlinehint` of ours.  Whether we should
mark every function `noinline` at -O0 as clang does is a matter of IR
equality, not of generated code: `noinline` on every function at -O0 changes
nothing the code generator uses.)

### 8.2 What the facts are worth at -O2

Experiment (`m5_attrvalue.py`): take `clang -fclangir -O2` IR (`clang`
24.0.0git at `2d6f180e`, from `/edg/llvm-cir/bin`; **not** the distribution
`clang` that is first on the box's `PATH`: it is version 21.1.8, ignores
`-fclangir` and gives classic IR, which I used for a first run of this
experiment and discarded) before the LLVM passes for three libstdc++
translation units, strip one group of attributes (noundef; nonnull,
dereferenceable and align on `this` and references; `mustprogress`;
`norecurse`; `nounwind` on functions and calls; `noreturn`; `inlinehint`) from
the IR text, and compile it with `clang -O2 -x ir`.  Change of `.text` against
the unstripped IR:

| stripped | containers | algo | mixed |
| --- | ---: | ---: | ---: |
| noundef | 0.0% | 0.0% | 0.0% |
| nonnull / dereferenceable / align | +5.5% | +0.4% | +1.6% |
| mustprogress | 0.0% | +0.2% | 0.0% |
| norecurse | 0.0% | 0.0% | 0.0% |
| **nounwind** (functions and calls) | **+17.7%** | **+11.8%** | **+22.6%** |
| noreturn | +0.3% | 0.0% | +0.2% |
| inlinehint | 0.0% | +4.8% | +0.2% |
| all of them | +23.3% | +17.6% | +24.9% |

For comparison, `.text` of the object from our stream at -O2 against
`clang -fclangir -O2` is 9,104 against 7,944 bytes (containers), 9,384 against
8,079 (algo), 9,582 against 7,723 (mixed): +15%, +16%, +24% (front-end
differences are in this number as well, so it is an upper bound for what the
attributes explain; the dead inline functions of our stream are removed by
GlobalDCE and do not count).  Reading: **`nounwind` is the one fact with a
large effect at -O2** (landing pads and cleanup code stay when a call may
unwind); `nonnull`/`dereferenceable`/`align` on `this` and references is
second (up to +5.5%), `inlinehint` can matter (+4.8% on one TU); the others do
not change the size of these programs and may still matter for speed, which
this experiment does not measure.  Where we are against clangir (IR before the
LLVM passes, containers): `nounwind` on definitions 51% against 56%, on
**declarations 8% against 56%**, on calls 57% against 45%.  So the
`nounwind` gap is on declarations (library and other-TU functions: `__cxa_*`,
`operator new`/`delete`, `memcpy`-like), which no LLVM pass can recover: a
caller must keep the landing pad of a call to a declaration that may unwind.

Recommended order of work (S2): `nothrow`/`nounwind` on declarations and
definitions from the FE predicate and from the language (every function in C;
the entity's own `NOTHROW` flag), with an audit of what `DECL_FUNC.NOTHROW`
covers (open question C); then `this`/reference `nonnull`, `dereferenceable`,
`align` (key 15 of 1.3), `inlinehint` (question F), noreturn for builtins (A),
`noundef` (cheap, from types), `mustprogress`/`norecurse`, and the call-site
`builtin` (`ALLOC_EXPR`).

### 8.3 Hazards (what must be done before an attribute is safe)

- **Static invoker and a null `this`.**  The static invoker of a captureless
  lambda calls `operator()`; the front end passes a null pointer as the object.
  With `nonnull dereferenceable align` on `this` that call is undefined.
  CIRGen avoids it with a temporary: `createMemTemp(lambdaType, ...,
  "unused.capture")` (`CIRGenClass.cpp:942` at the pin).  The library must do
  the same for any call whose `this` argument is the literal null constant (or
  the FE must say "invoker").  The same applies to any other place the FE
  synthesizes a call with a null `this` (none other seen in the corpora).
- **noundef against GNU `return;`.**  `hasStrictReturn` is false for C (a
  function that falls off the end or `return;`s in a non-void function is not
  undefined until the value is used) and for `extern "C"` functions in C++.
  Marking the return `noundef` there would turn gcc-torture programs that
  rely on this into undefined behavior; the rule above is exactly clang's, so
  it is safe; the parameters are `noundef` in C as well (clang does).
- **mustprogress on constant loops.**  A function with a trivial infinite loop
  must not be `mustprogress` (the optimizer would delete the loop); the rule
  is clang's `checkIfLoopMustProgress`: the loop's condition is a constant
  true (or absent) and the body has no side effects; the attribute is added
  speculatively at the start of the function and removed when such a loop
  is seen.  With the FE folding constants, the emitter sees a literal true
  condition; the body-effect check needs "empty body" (the FE's `FOR`/`WHILE`
  with an empty statement).
- **noreturn and the code after the call.**  Marking a call `noreturn` is a
  promise: a library that is not marked `noreturn` by the FE but is by name
  (abort, exit) must match the exact prototype, or a user function called
  `exit` would be treated as a builtin.
- **`nonnull`/`dereferenceable` on references** are only safe where the FE
  emitted the reference as a reference (never for a pointer that the FE
  converted): the FE fact (D) is what prevents an UB-introducing mistake.

Open questions for the orchestrator: (A) does the front end flag
`abort`/`exit`/`_Exit`/`quick_exit` and the other noreturn library builtins as
noreturn in `DECL_FUNC`, and is a name table in the library acceptable if it
does not; (B) is `extern "C"` (language linkage) visible in the stream (it is
for the mangled-name rule, not as a flag); (C) which functions get
`DECL_FUNC.NOTHROW` today (noexcept, `throw()`, implicit-noexcept destructors
and special members, nothrow library functions); (D) a parameter flag
`REFERENCE` (and `RESTRICT`) on `PARAM` and a return flag on `DECL_FUNC`;
(E) the C++ standard level in the header facts; (F) whether `INLINE` means
`inline` as written or inline linkage (member functions defined in the class).
These are contract questions: nothing is proposed as a change here.

### 8.4 The `builtin` call attribute (experiment)

Command (box, cir-env image; script `cir_src/scripts/box_builtin_attr.sh`):
`/edg/llvm-cir/bin/clang++ -fclangir -std=c++17 -O2 -S -emit-llvm -Xclang
-disable-llvm-passes a.cpp` with `new int(3)`, `new int[4]` / `delete[]`,
`new S` / `delete s` and `::operator new(8)` / `::operator delete`
(`/edg/llvm-cir/bin/clang --version`: 24.0.0git at `2d6f180e`).  Result: the
three new-expression calls to `_Znwm`/`_Znam` carry attribute group
`{ builtin allocsize(0) }` and the delete calls `{ builtin nounwind }`; the
explicit `::operator new(8)` carries `{ allocsize(0) }` only; the declarations
carry `nobuiltin allocsize(0)`.  Classic clang (same version, no `-fclangir`)
gives the same `builtin` placement, plus `memory(inaccessiblemem: readwrite,
errnomem: write)` on the `new` call, which clangir does not emit.  So
parity needs the call flag: the stream must say which calls come from a new-
or delete-expression, and 1.3 has `ECIL_CALL_ALLOC_EXPR` for exactly that.
(My first reading of `LowerToLLVM.cpp`, that the attribute is not lowered, was
wrong: it has no `setBuiltin`, but the attribute reaches the LLVM call.  The
cir-writer probe that reported `builtin allocsize(0)` was right; it ran real
clangir or classic clang with the same result.)

## 9. L5: the -O2 shake-down

Every case (the streams of a test case, linked together) is compiled at -O0
from the streams of the -O0 front-end run and at -O2 from the streams of a
front-end run with `--cir_opt_level=2`, linked with g++, run and compared to
the expected result (rp: exit status 0; lp: a successful link).  Harness:
`box_shake.py`; classification of the cases that pass at -O0 and fail at -O2
with `edg-cir-opt-triage` (which also builds each variant with g++, clang and
`clang -fclangir`).

| set | cases | PASS both | fail both | O0 pass, O2 fail | O0 fail, O2 pass |
| --- | ---: | ---: | ---: | ---: | ---: |
| C corpus | 2564 | 1641 | 846 (490 link-fail, 249 library error, 106 run failures, 1 library crash) | 14 | 63 |
| C++ corpus | 1770 | 1354 | 406 (193 link-fail, 171 library error, 42 run failures) | 3 | 7 |

("fail both" cases are the M4 gaps and the tests that do not link by design
(`lp` tests that expect a link error, tests that need other translation units);
O0 fail and O2 pass are mostly link-only tests whose undefined reference the
optimizer removes: 61 of the 63 C and 6 of the 7 C++.  The one library crash is
`c/pr114907`, at both levels.)

The 14 C and 3 C++ regressions, all attributed except two:

| cases | cause |
| --- | --- |
| `fold-abs-2`, `fwrapv-1`, `pr101254`, `pr114090`, `pr55236`, `negneg-4` (C) | the corpus lost the `dg-options` (`-fwrapv`): g++ and clang -O2 fail too without the flag; not ours |
| `fp-uint64-convert-double-1`, `-2` (C) | undefined behavior: g++ fails at -O2 |
| `pr95115`, `builtin-ctype-2`, `builtin-wctype-1`, `builtin-sprintf` (C) | LLVM: clang -O2 and `clang -fclangir -O2` fail the same way (the `link_failure` call is not folded away) |
| `eh/sighandle`, `torture/pr85334` (C++), `torture/except-1` | the corpus lost `-fnon-call-exceptions`; g++ -O2 fails too; `except-1` fails the reference |
| `pr118224` (C) | clang cannot compile the test (`__builtin_aligned_alloc` unknown), so no attribution; ours aborts at -O2 only |
| `builtin-ctype-1` (C) | **undiagnosed (S2)**: our -O2 IR leaves a call to `link_failure_toupper` that gcc and clang -O2 fold away; clang -fclangir -O2 passes |

Writer probes (143, `box_collect_probes.sh`): -O0: 121 PASS, 20 REF-FAIL (the
multi-file probes and two probes the reference does not build), 1 RUN, 1
UNSUPPORTED; -O2: 119 PASS, 19 REF-FAIL, 4 DIFF, 1 UNSUPPORTED.  The four -O2
DIFFs and the one -O0 RUN were looked at (`m5_probe.sh`: ours at -O0 and -O2,
g++ at -O0 and -O2):

| probe | difference | attribution |
| --- | --- | --- |
| `cxx_arrays_and_conditions` | output differs from g++ -O2 | the probe has undefined behavior: `trace = trace * 10 + x` overflows `int`; ours at -O0 and -O2 equals g++ -O0, g++ -O2 differs from g++ -O0 |
| `cxx_new_multidim` | `z 0` against g++ -O2's `z 13` | `new Z[3]()` value-initializes `Z` (zeroes it before the non-trivial `B()`): ours at -O0 and -O2 and g++ -O0 print 0, g++ -O2 prints 13: g++ -O2 deviates from the standard; ours is right |
| `m4_s5_delete_throw` | g++ -O2 prints 1 where ours and g++ -O0 print 0 | g++ -O2 elides the pair around the throwing destructor (the test is gcc's own `eh/delete1`, which fails with g++ -O2 and clang -O2); ours behaves like -O0 |
| `m4_s1_template` | exit status 88 against 7 (-O2), 224 against 248 (-O0) | the probe reads an uninitialized member (`Tm<char> tc;` then `tc.get()`); undefined |

No library defect among them; the three probes with undefined behavior or a
reference-compiler deviation are `tests/tests/cir/writer` files (cir-writer's):
`cxx_arrays_and_conditions` should use an unsigned `trace`, `m4_s1_template`
should initialize `v`.

Real-world units at -O2 (`dev_tools/cir-realworld`: seven libstdc++ and eight
Boost TUs; `edg-cir-run-diff`, the reference needs `CPLUS_INCLUDE_PATH` for the
Boost include directory, the harness does not pass `-I` to the reference):

| unit | result | reason |
| --- | --- | --- |
| libstdc++ chrono, functional, iostream | PASS | |
| libstdc++ algorithms, containers | UNSUPPORTED | `__builtin_constant_p` (the `std::__advance` family) |
| libstdc++ memory | OURS-FAIL | `__atomic_load_8` (`_Sp_counted_base::_M_release`) |
| libstdc++ regex | OURS-FAIL | `__builtin_constant_p` |
| Boost circular_buffer, lexical_cast, mp11 | PASS | |
| Boost algorithm, range | OURS-FAIL | `__builtin_constant_p` |
| Boost format | OURS-FAIL | `__atomic_fetch_sub_4` |
| Boost container | UNSUPPORTED | a placement operand of a class type with an operator delete |
| Boost multiprecision | OURS-FAIL | `error [1001] DROP of a NONTRIVIAL_ABI record prvalue that has a destructor (materialize it and destroy it)`: the stream is rejected as malformed; this one is not a missing builtin and may be a front-end or reader rule: to be looked at |

Result: 6 of 15 pass at -O2; every failure is a construct the library reports
(three missing builtins, `__builtin_constant_p` and the `__atomic_*` family,
decide eleven units' fate: S2 should start with them), plus the one stream
rejection.  `--keep-going` crashes the library on `c/pr114907` (`main` is
dropped for a floating-constant format, then another function crashes in
`CastInst::Create` ("Invalid cast!") on a call whose callee type no longer
matches the declaration): with `--keep-going` a dropped definition changes the
callee's type seen by its callers, which the library should treat as the same
class of error as an undefined reference.

## 10. L6: profile of decode, emit and finish

Callgrind (`Ir`), the libstdc++ `mixed` TU (377 KB stream, 48,448 records,
new pin, RelWithDebInfo), `--emit=obj`: 2,777 M instructions.

| stage | Ir | share |
| --- | ---: | ---: |
| decode, validate and emit (`consume`) | 238 M | 8.6% (the emitter's `on_record` 170 M = 6.2%; decode + validate 2.4%) |
| finish (`finish_entities` 114.7 M, of which `entity_liveness` 90.6 M) | 133 M | 4.8% (liveness 3.3%) |
| verify of the CIR module | 82 M | 3.0% |
| CIR-to-CIR passes (`runCIRToCIRPasses`) | 298 M | 10.7% |
| lowering (`lowerDirectlyFromCIRToLLVMIR`) | 1,591 M | 57.3% |
| LLVM code generation | 368 M | 13.3% |

Inside the lowering: the conversion pass 576 M (20.8%), `translateModuleToLLVMIR`
340 M (12.3%), and **the verifier after each pass: about 470 M (16.9% of the
whole process)**.  `mlir::verify` is 553 M (19.9%) in all, of which 82 M is our
own verify stage.

Cheap wins, in order of value:

1. **Lowering without the per-pass verifier** (measured, shipped nowhere):
   `lowerDirectlyFromCIRToLLVMIR` runs `populateCIRToLLVMPasses` in a pass
   manager with the verifier on (after each of the five passes) and verifies
   the LLVM module.  The module was verified before and after the CIR passes
   already.  An own driver (`pm.enableVerifier(false)`, `translateModuleToLLVMIR`
   with `disableVerification=true`; `EDG_CIR_LOWER_NOVERIFY` in the scratch
   tree) gives, on the three libstdc++ TUs, `lower` 190.8 -> 138.3 ms (mixed),
   527.8 -> 384.7 (regexchrono), 145.0 -> 106.6 (algo): **-26% to -27% of the
   lowering stage and -13% to -16% of wall time**; the assembly is identical
   (`cmp`) on all three, and on every chunked-lowering run of section 7.  Keep
   the verifier under `EDG_CIR_FLAG_VERIFY_EACH`.  Ten lines in `pipeline.cpp`.
2. **Allocator.**  malloc/free/`_int_malloc` are 6% of the Ir on their own
   (`_int_malloc` 2.9%, `free` 1.1%, `malloc` 1.0%, `_int_free_chunk` 0.8%);
   MLIR and LLVM allocate in small pieces.  A bump or per-thread allocator for
   the worker (jemalloc/mimalloc as a link option) typically gives a few
   percent; not measured.  Not recommended until the verifier is out.
3. **`entity_liveness`** (3.3%): the dead-definition closure at finish; with
   lazy emission (section 2) it is replaced by the reachability closure that
   measured 0.75% of the library time; no separate work.
4. `StorageUniquer` and `xxh3_64bits` (1.3% and 0.7% self) are MLIR type and
   attribute uniquing; the emitter builds the same types and attributes again
   and again (a cache of the common `cir.ptr`/int types is possible; not
   measured, small).

Stage times of the other TUs (ms, `--stats`, mixed / regexchrono / algo):
decode+validate 16.7 / 37.1 / 11.9, emit 27.9 / 67.1 / 19.4, finish 12.4 /
32.1 / 8.4, verify 7.9 / 21.6 / 6.0, CIR passes 26.2 / 86.5 / 20.4, lower
190.8 / 527.8 / 145.0 (138.3 / 384.7 / 106.6 without the verifiers), codegen
46.5 / 213.7 / 23.6.  The lowering is 55% to 60% of the library time and the
decode/emit/finish part 15% to 20%: the profile does not show a cheap win
inside our own code after the verifier.

## 11. Findings on the way (new)

- `--keep-going` can crash the library when a dropped definition is called
  (section 9; stream `c/pr114907`).
- The two builtin families `__builtin_constant_p` and `__atomic_*` decide most
  real-world units (section 9); `__builtin_constant_p` in CIRGen is the
  constant 0 for a non-constant argument and a fold for a constant one: the FE
  can fold it, the library can emit 0 or 1.  Not looked at in upstream for the
  atomics (`cir.atomic.*` ops exist).
- Boost multiprecision: the stream is rejected by the library with
  `DROP of a NONTRIVIAL_ABI record prvalue that has a destructor`; not
  diagnosed (writer or library rule).
- In the chunked-lowering spike the symbol discovery pitfalls of section 2
  (nested attribute references, `cir.func` bodies) came up again: a
  library-wide "symbols used by an operation" helper that walks the attribute
  dictionaries is worth having once (lazy emission, chunking, the finish-time
  liveness closure all need it).
- Scratch artifacts: `/tmp/m5a-spike/` (Mac) and `~/edg-cir-lib/m5a` (box):
  `chunk.cpp` and `pipeline.cpp` of the spikes and the `m5_*` scripts
  (`m5_sweep.py`, `m5_cmp.py`, `m5_chunk_tab.sh`, `m5_attrvalue.py`,
  `m5_nounwind.py`, `m5_probe.sh`, `m5_rwu.sh`); none of them is in the
  repository.


## 12. M6 residue (S2 round 2 and S3, cir-lib)

What the S2 round 2 attr-diff run left, and the facts behind the answers
given at the S2+S4 checkpoint.  Counts are `edg-cir-attr-diff` at the S2+S4
checkpoint (probe 2456, pinned 2437 unexplained) and after round 2 (probe
1129, pinned 2261).

**Answers**

- **-O0 and a deleted call (`torture/pr101373`).** The CIR and the LLVM IR
  keep the call of a `const` function whose result is unused. The -O0 code
  generator (FastISel) drops it: a call that is `nounwind willreturn
  memory(none)` and unused is dead. clang -O0, `clang -fclangir -O0` and
  g++ -O0 all segfault on this test (the test is an xfail for gcc,
  PR100409); it is not a regression of ours, and the call stays at -O0 in
  our IR. `torture/pr100382` was a real defect (item 10 used `nothrow`
  where CIRGen uses `nounwind` on const and pure calls, so the call in a try
  body was not an invoke); fixed, with `s2-pure-in-try.test`.
- **`callq f@PLT` for external functions.** Not a change: the relocation
  model has been PIC since M3, the library and the M5a binary give the same
  `callq _Z3exti@PLT` for an external declaration, and clang (classic and
  `-fclangir`, at -O0, with `-fPIE` as the default) gives the same line.
  `-fno-pic` gives a direct call. Intended parity.
- **Allocation elision.** `builtin` stays on the allocation-expression calls
  (user decision).

**Not closed: classified for M6**

- *Needs the writer:* NORETURN and NOTHROW for the library builtins (`abort`,
  `exit`, `memcmp`, ...: noreturn 76, nounwind about 90 on the probe),
  `INLINE_HINT` for the implicitly declared special members (the
  C2/D2 `inlinehint` only in clangir: 178 at -O2 on the probe, 37 on the
  pinned units; clangir hints them, the writer excludes them), keys 16-18
  (the library maps them: `alwaysinline`, `cold` with `optsize` at -O1 and
  up on a definition, `hot`; clang adds them to the calls as well).
- *clangir is incomplete:* the deleting destructor of a class with virtual
  bases is `llvm.trap` in clangir (8 `_ZdlPvm` calls only on our side).
- *Structors:* we call the complete variant (C1/D1, an alias of C2/D2, as
  classic clang does), clangir calls the base variant (28 pinned, 90 probe,
  distinct calls only on one side).
- *Explicit instantiation declarations* (`extern template class
  std::allocator<char>`): clangir defines `allocator<char>::allocator(const
  allocator&)` and `~allocator()`, we declare them (6 `mustprogress`/`uwtable`
  entries on the pinned units).
- *Mangling* (EDG against clang): constructor templates with a defaulted
  `enable_if` template parameter (`TnNSt9enable_if...`, 25 distinct calls on
  the pinned units) and lambdas in `main` (`$_0` against `UlvE_`).
- *Other call-set differences* (calls only one side has: 190 distinct calls
  on the pinned units at -O0, 301 on the probe): inlining of
  `__is_constant_evaluated`, memcpy intrinsics, sized against unsized
  `operator delete` were not traced beyond the D0 case above (after round 2
  the `_ZdlPvm` entries are the clangir-trap ones).
- *norecurse* of C++ `main` is done (one attribute); `terminate()` is
  nounwind in ours and not in clangir (4).

**S3 notes**

- **Default threshold: 4000 bodies** (`k_default_chunk_threshold` in
  `lib/driver/chunk.h`), raised from 2000 after the M5 gate on the user's
  decision: at 2000 the generated C++ TU chunked (13 chunks), paid +21%
  lowering time and its library Ir was 102.6% of M5a at -O0 against the S1
  target of 90%. **4000 does not close that miss (open):** the TU has about
  6000 emitted bodies (3600 of them deferred and replayed), so it still lowers
  in about 13 chunks at 4000 and the S1 target miss stays open. 4000 only
  spares translation units of 2000 to 4000 bodies, such as the generated
  c3000. The next session should consider a threshold on the estimated CIR
  module size or on the expected peak instead of a body count, then re-run
  perf, rss and slow-below-threshold. All S3 evidence in this document and in the gate was measured
  at 2000 (the threshold curve, the pinned-unit and corpus identity, rss,
  slow-below-threshold). Perf, rss and slow-below-threshold were not re-run at
  4000; the next session should re-run them. No test depends on the default:
  `chunk-lowering`, `chunk-failure`, `chunk-identity`, `no-llvm-passes` and
  cir-config's chunk tools pass `--chunk-threshold` explicitly, and
  `capi_tests`/`chunk-lowering` only expect 0 chunks for a module of a few
  bodies.

- At -O2 the assembly of a chunked module equals the whole-module assembly
  except for the numeric suffix of local data symbols that an LLVM pass names
  (`.L.crctable.1` against `.7`): the linker's declaration-to-definition
  replacement counts name collisions in the symbol table (about 1.7% of the
  corpus streams, forced to one function per chunk).
- With debug information every chunk would make its own `DICompileUnit`.
- Scripts: `box_chunk_identity.sh`, `box_chunk_o2.py`,
  `chunk_identity_tests.py`; the measured curve is in the S3 report.
