# The -O2 baseline of the CIR back end (gate M4.1)

Written by cir-config (C4), 2026-10-19. The numbers are from the first
`opt-corpus` run on the box, tree `~/edg-cir-config/m41-int` (the M4.1 delta
of 2026-10-19, library pinned at llvm-project `894bfff5`), evidence directory
`.tmp/o2-baseline` of that tree.

## 1. What was run

- `--cir_opt_level=2` reached the runs through `EDG_CPFE_DEFAULT_OPTIONS`
  (`edgdocker` forwards it into the container when it is set; the gate step
  sets it for its own runs only). The front end then predefines
  `__OPTIMIZE__` instead of `__NO_INLINE__`, and the library compiles at -O2
  (the CIRSimplify and the O2 pipeline, LLVM -O2, `llc` at -O2).
- The C corpus (2561 cases) and the C++ corpus (1823 cases) in
  `edg_x86_64_cir`; both smoke lists (C 40, C++ 98) in `edg_x86_64_cir_2p`;
  `edg-cir-compare-modes --opt-level 0` and `--opt-level 2` over both smoke
  lists (the in-process and two-process assembly must agree at each level).
- -O0 statuses: the `corpus` and `cpp-corpus` steps of the same evidence
  directory (the tree after M4.1's C1/W1/L5 changes), so that every change is
  caused by the level.
- **Proof that -O2 reached the edgy runs** (review item): every run carries a
  canary test, `cir/config/opt-level-canary.sft.c` (`...-cpp.sft.cpp` for the
  C++ runs), that passes at every level and prints whether `__OPTIMIZE__` is
  defined; its output is recorded at -O0, so at -O2 it reports
  `PASS--OUTPUT MISMATCH`. The step fails if a run's canary does not. Also
  checked by hand: with the variable set, `cir/config/opt-level-macros-0`
  fails, and a test's own `-O0` still wins.
- **Non-vacuity**: the assembly of all 89 smoke tests that compile (the
  other 9 are compile-failure tests) differs between -O0 and -O2 (C 40/40, C++
  89/89 of those with assembly), in both modes (the two modes were identical at
  each level).

## 2. Numbers

Two runs. The first, on the tree before the M4.1 fixes of the our-IR items
below (W6 linkage of local statics, W7 `noinline`, L6 `nobuiltin`), had 26
PASS -> FAIL cases (section 4 lists the 8 that were ours). The second, after
them, is the baseline: tree `m41-int`, evidence `.tmp/g2`.

| | -O0 | -O2 |
| --- | --- | --- |
| C corpus, PASS | 1656 / 2561 | 1704 / 2561 |
| C: PASS -> FAIL | | 14 (10 RUN_FAIL, 4 LINK_ERROR) |
| C: FAIL -> PASS | | 62 (59 LINK_ERROR, 3 RUN_FAIL) |
| C++ corpus, PASS | 1472 / 1823 | 1476 / 1823 |
| C++: PASS -> FAIL | | 3 (RUN_FAIL) |
| C++: FAIL -> PASS | | 7 (6 LINK_ERROR, 1 RUN_FAIL) |
| CIR crashes (FE_CRASH, LIB_CRASH, TIMEOUT, KILLED) | 0 (+1 FE crash that the reference has too) | 0 (the same FE crash: `DRs/dr2867-2`) |
| C smoke, `_2p` | | 40/40 |
| C++ smoke, `_2p` | | 98/98 (97/98 in the first run: `cpp1z/aligned-new3`, fixed by L6) |

The FAIL -> PASS cases are the optimization-dependent tests: 38 are in
`gnu/c/tree-ssa`, and they link only if the optimizer removes a call to an
undefined function (`link_failure`, `link_error`); `eh/comdat1` is one (the
case the M4 handoff named). Three that passed at -O2 in the first run (`c/ipa/
ipa-bit-cp`, `ipa-bit-cp-1`, `ipa-pta-13`: they fail to link at -O0) fail at
-O2 now: they have `noinline` functions, which LLVM no longer inlines now that
W7 carries the attribute, and then the `link_error` calls stay (clang -O2 does
not link `ipa-pta-13` either). That is the expected effect of the fix, not a
regression against -O0.

## 3. Triage of every PASS -> FAIL (17 cases of the baseline)

Method: `edg-cir-opt-triage` builds each case with ours first, then with
gcc/g++ and clang at -O0 and -O2 and `clang -fclangir`, and applies the rules
in its header; `--matrix` shows the case built at front end level 0/2 x
library level 0/1/2/3 (and without the CIR passes). The `manual` ones of the
first run were decided by hand (IR experiments). The result is
`tests/.edgy/filters/cir-opt-triage.txt`; the gate step fails on any worse
case, or new crash, without a line there, and on a line for a case that did
not get worse.

| Category | Cases | Which |
| --- | --- | --- |
| dg-option | 10 | `c/fold-abs-2`, `c/fwrapv-1`, `c/pr101254`, `c/pr114090`, `c/pr55236`, `c/tree-ssa/negneg-4` (-fwrapv); `c/torture/fp-uint64-convert-double-1`, `-2` (-frounding-math: the tests call `fesetround`); `cpp/eh/sighandle`, `cpp/torture/pr85334` (-fnon-call-exceptions). The preprocessed corpus has lost the dg-options of its sources, so the tool probes the flags: gcc fails at -O2 and passes with the flag |
| ref-fails | 1 | `cpp/torture/except-1` (g++ fails at -O0 and -O2) |
| upstream | 5 | `c/pr95115` (clang -O2 and clang -fclangir -O2 fail too); `c/pr118224` (LLVM folds `malloc(huge) != null` to true; clang and clang -fclangir -O2 abort too); `c/torture/builtin-ctype-2`, `builtin-wctype-1`, `c/tree-ssa/builtin-sprintf` (their link_failure checks are compiled only under `__OPTIMIZE__` and need GCC's folding of the builtins; clang -O2 does not link them either) |
| our-ir | 1 | `c/torture/builtin-ctype-1`, section 4 |

Matrix finding for the 26 cases of the first run (`matrix.txt`): the front
end's predefine alone (front end level 2, library level 0) changes the
outcome of exactly four cases, `builtin-ctype-1`, `builtin-ctype-2`,
`builtin-wctype-1` and `builtin-sprintf`. These tests include no system
header: they compile their `link_failure` checks only `#ifdef __OPTIMIZE__`
(`builtin-ctype-1.sft.c` line 23, `-ctype-2` 44, `-wctype-1` 38,
`builtin-sprintf` 92), so they test nothing at -O0 and, at -O2, need the
optimizer to fold calls such as `isalnum(i) != isalnum(i)` away. Every other
case depended on the library's level only (and `--disable-cir-passes` did not
change it, except one that cannot lower without them): LLVM's -O2 on our
IR, not the CIR passes, is where the differences arose.

## 4. Failures attributed to our IR

At the first run eight cases, each cause checked by editing the unoptimized
LLVM IR that the library writes and running LLVM -O2 on it. Seven are fixed
in M4.1 and pass at -O2 now (the stale-line rule of the gate removed their
triage lines):

| Cases | Cause | Fixed by |
| --- | --- | --- |
| `cpp/template/new11`, `cpp/cpp1y/sized-dealloc1`, `cpp/cpp1z/aligned-new3`, `cpp/cpp0x/defaulted19` | Calls of the replaceable `operator new`/`operator delete` carried no `nobuiltin`: LLVM recognizes `_Znwm` by name and treated the result of the TU's own `operator new` as a removable allocation | L6 (`nobuiltin`); `init/new41` and `eh/delete1` passed with it too (they had been put down as upstream: permitted allocation elision and an unestablished cause) |
| `c/20070507-1` | The GNU `noinline` attribute of `bar`, `foo` and `loop_next` was not carried: LLVM inlined `bar`, read the uninitialized buffer as undef and reduced `main` to `unreachable` | W7/L7 (key 14) |
| `cpp/abi/pr39188-1a`, `-2a` | The static local of an inline function (an anonymous union, or the template instance) was emitted internal (`@f.var`), not `linkonce_odr` in the function's comdat: each TU kept its own copy once `f` was inlined (latent at -O0, where the linker merges `f` itself) | W6 (and the mixed-TU cases `s3-*`, at -O0 and -O2) |

One remains: `c/torture/builtin-ctype-1` is UNDIAGNOSED (for S2): probably our
declarations of the ctype functions or their call attributes. Its checks exist
only at -O2 and need `isalnum(i) != isalnum(i)` and the like folded (the
functions treated as pure); clang -O2 links it, ours leaves the
`link_failure_*` calls. With front end level 0 and the library at -O2 (no
checks compiled) it passes.

## 5. Notes

- **-Os** maps to level 2 without `__OPTIMIZE_SIZE__`: with `__OPTIMIZE__`
  defined glibc's headers use their `gnu_inline` extern-inline bodies
  (`__USE_EXTERN_INLINES`), which gcc -Os would not. No case of this
  baseline is caused by that (the four cases that the predefine changes are
  the tests' own `#ifdef __OPTIMIZE__` checks, section 3); the approximation
  is documented in the eccp comment.
- **C99 and `gnu_inline` inline-only definitions are never inlined** (for the
  S2 list). The library makes a function that the front end marks
  `ECIL_DECL_FUNC_INLINE_ONLY` an external declaration, without its body
  (`globals.cpp`, the loop after the incomplete-record handling, about
  lines 1902-1915): its definition is supposed to live in another translation
  unit, and with no body LLVM has nothing to inline at any level. clang emits
  such a function `available_externally`, which keeps the body for the
  optimizer and drops it from the object. A test that calls a plain `inline`
  function of its own and has no external definition (the GNU C tests
  that were written for gcc -O, which inlines it) therefore compiles but does
  not link: at -O0 the same is true of clang itself, so the gap shows only for
  the tests whose options carry a real `-O`. These are the four C tests
  that were re-recorded in M4.1 and had no cause in the first attribution:
  `gnu/c/20031102-1` (`__inline__ char InlineFunc`), `gnu/c/loop-2`
  (`__inline__ int tag`), `gnu/c/pr48837` (`inline ... bar`) and
  `gnu/c/tree-ssa/pr22591` (`inline void`); each is `-O` in its options and
  `FAIL(COMPILE)` in `edg_x86_64_cir`. The fix is `available_externally` for
  these functions (and for the glibc `gnu_inline` extern-inline bodies, which
  the same path reaches), after which -O1 and above inline them as clang does.
- `-Og` is 1, `-Ofast` is 3 without fast-math flags (the CIR dialect can carry
  them since the candidate pin; nothing sets them).
- The hazards of the plan (the static invoker's null `this`, `noundef` on a
  GNU `return;`, `mustprogress` on constant loops) belong to optimization
  facts that nothing emits yet, so this baseline cannot show them.
- Every PASS->FAIL is also a gate item from here on: a case that passes at
  -O0 and fails at -O2 needs a line in the triage file, and so does every new
  crash, kill or timeout.

## 6. Reproduce

```
edg-cir-checkpoint --steps corpus,cpp-corpus,opt-corpus    # or with the gate
edg-cir-gate-evidence --tree . --only corpus,cpp-corpus,opt-corpus
edg-exec --img cir-env -- python3 dev_tools/bin/edg-cir-opt-triage \
    <evidence>/opt-corpus/unattributed.txt --json <evidence>/opt-corpus/c.json
... edg-cir-opt-triage --matrix --case 'path | variant'
```
