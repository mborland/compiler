# M2 triage of the C corpus under `edg_x86_64_cir`

Owner: cir-config. A living document: the numbers are from the **first
baseline** (run `2026.10.09-20.13.29` on the box, `edg-cir-summary`), taken
before the B3 (C++ rejection), B1, B2 and the writer/lib fixes that landed
afterwards. "Now" lines say what a repro on the current tree (synced
2026-10-09 ~20:40 or later) does. Rerun with

    edg-docker-test --non-interactive --posture cir --only-configs edg_x86_64_cir \
        --types rp,lp -T 120 @tests/.edgy/filters/cir-c-corpus.txt
    python3 dev_tools/bin/edg-cir-summary tests/runs/<ts> --list <BUCKET>

(`edg-cir-summary` has `--compare <older run>` and `--json`.)

## Headline numbers (baseline)

2561 test cases (the `rp`/`lp` tests of `imported/gnu/c` top level, `torture/`,
`tree-ssa/`, `ipa/`).

| Bucket | Cases | Of which also fail in `edg_x86_64` (the reference) |
| --- | ---: | ---: |
| PASS | 1274 (49.7%) | - |
| UNSUPPORTED | 671 | 199 |
| LIB_ERROR | 179 | 26 |
| FE_DIAG | 161 | **161** |
| LINK_ERROR | 136 | 69 |
| RUN_FAIL | 89 | 64 |
| LIB_CRASH | 28 | 13 |
| FE_CRASH | 23 | 11 |

**543 of the 1287 failures also fail in `edg_x86_64`** (the recorded
expectations `tests/expectations/edg_x86_64/imported.log`; `edg-cir-summary`
reads them by default and reports the numbers). Against what the reference can
do, the pass rate is 1274/2018 (63.1%). The CIR-specific failures are 744:
UNSUPPORTED 472, LIB_ERROR 153, LINK_ERROR 67, RUN_FAIL 25, LIB_CRASH 15,
FE_CRASH 12. **The M2 gate should be judged against this set** (a test that
fails in `edg_x86_64` too is not a CIR defect); `edg-cir-summary` prints it.

The corpus files are already preprocessed (`# 0 "./x.c"`, GCC 15 system
headers expanded), so `EDG_USE_SYSTEM_HEADERS` does not matter for them.

## Buckets

Owner: **writer** = ECIL content wrong or missing, FE crash in `ecil_*.c`;
**lib** = a valid stream mis-emitted, pipeline/verifier failure; **config** =
harness, flags, headers.

### FE_CRASH (23)

| Count | Message | Tests | Owner | Evidence |
| ---: | --- | --- | --- | --- |
| 18 | `internal error: assertion failed at "ecil_write.c" ... in ecil_rec_end` | `torture/float128-div-underflow`, `torture/bitint-25`, `torture/fp-int-convert-float16`, `torture/arm-fp16-ops-1` | writer | `_Float128`, `_Float16` and `_BitInt` constants/types reach `ecil_rec_end` with an operand the record cannot hold. **Now:** the assertion is gone; the same tests give "the body of `main` uses a floating constant of this format, which the CIR back end does not support; the function is dropped" (an UNSUPPORTED diagnostic, correct) plus "Error: ... definition of `main` was dropped" |
| 4 | `Segmentation fault` | `torture/pr112281-1`, `torture/pr112281-2`, `torture/pr89111`, `tree-ssa/pr82078` | writer or lib (find which with a backtrace) | **Now:** the library catches the crash: "Error: CIR back end: internal crash while consuming the ECIL stream" (`pr112281-1`, `pr89111`, `pr82078`) - still a real crash, now reported cleanly. `pr82078` also has `e[c][45]` (a subscript out of range, in a multi-dim array of struct) |
| 1 | `Aborted (core dumped)` | `torture/bitint-66` | lib | **Now:** `error: failed to verify 'declared_type': integer or boolean type` from `mlir::StorageUniquerSupport.h` (`cir::BitFieldDeclAttr`) then an abort: a bit-field of `_BitInt` type reaches `BitFieldDeclAttr`, which only accepts integer/boolean. An UNSUPPORTED diagnostic is wanted instead |

### LIB_CRASH (28)

| Count | Message | Tests | Owner | Evidence |
| ---: | --- | --- | --- | --- |
| 14 | `internal error in the CIR pipeline: The pass manager failed to lower CIR to LLVMIR dialect!` | `alias-5`, `alias-6`, `attr-ifunc-4`, `ipa/pure-const-3` | lib (lowering of the alias/ifunc symbol) with writer (the alias stream) | Minimal repro: `static inline int foo(void){return 0;} static int bar(void) __attribute__((alias("foo"))); int main(void){return bar();}`. **Now:** "definition of `main` was dropped" (alias or weakref attribute unsupported). Stream: after `DECL_FUNC 6 4 0 8 6` / `0 9 6` (`main`) there is `STR 7 "bar"` and then `DECL_FUNC 6 4 0 40 6` - the alias declaration reuses `main`'s id (6) and name sid (6) instead of its own: **probable writer bug** |
| 7 | `internal error: assertion failed ... ecil_rec_end` (4 plain, 3 via a header) | `torture/float16-complex`, `torture/float16-tg-2`, `torture/bfloat16-basic` | writer | same as FE_CRASH above |
| 4 | `Aborted` / `Aborted (core dumped)` | `torture/bitint-42`, `bitint-62`, `bitint-68`, `bitint-34` | lib | `_BitInt` bit-fields, as `bitint-66` |
| 1 | `the body of ... uses variable length array ... dropped` after an abort | `builtin-dynamic-object-size-0` | lib | VLA unsupported; the abort follows |

### LIB_ERROR (179)

| Count | Message | Tests | Owner | Evidence |
| ---: | --- | --- | --- | --- |
| 95 | `operand #N does not dominate this use` (then "the MLIR verifier rejected the module") | `bic-bitmask-13`..`16`, any `for (int i = ...; ...)` | **lib** (hoist allocas to the entry block, as clang does) or writer | Minimal: `void f(unsigned *x, int n){ for (int i = 0; i < n; i++) x[i] = 1; }` fails; the same with `int i;` declared before the loop passes. Stream: `FOR_BEGIN`, `BLOCK_BEGIN`, `LOCAL 3 1 8 0 0`, `LREF 3`, `C_INT 1 0`, `INIT_CONST`, `BLOCK_END`, **then** `FOR_COND`, `LREF 3` ... The `LOCAL` of the init declaration is declared inside a block that is closed before the condition, step and body use it, so its alloca is not visible there. Either the lib allocates every `LOCAL` at function entry, or the writer must keep the init declaration's scope open over the loop. By far the largest single LIB_ERROR group |
| 43 | `RETURN_STMT without a value in a non-void function` | `20020607-2`, `c23-labels-1`, `di-sync-multithread` | lib | `int f(double x){ if (x > 1.0) return 1; }` (falls off the end) and `int f(int x){ if (x) return; return 2; }` (C89-style `return;`) both give it. C allows both (behavior undefined only if the value is used), and `edg_x86_64` compiles them. The stream has `RETURN_STMT false`; the library must accept it (return poison/undef, or `cir.unreachable` at the end of the function), or the writer must synthesize a value |
| 26 | `C_AGG_BEGIN type is not a complete record or array` | `pr101384`, `pr107131`, `pr32912-2`, `torture/vector-1`, `torture/pr35771-1`, `stackalign/alloca-2` | writer/lib | all 26 are **vector types** (`__attribute__((vector_size))`, `__m128`). **Now:** `U y = (U){1,...,16} + u;` gives the UNSUPPORTED diagnostic "uses an aggregate ... dropped", which is the right class. The remaining step is a clear "vector types" diagnostic |
| 5 | `CONST_INT does not fit its type` | `c23-nullptr-1`, `c23-nullptr-5`, `c2x-nullptr-1` | writer | `nullptr_t` (C23): the constant `nullptr` is written as `CONST_INT` of a type that is not an integer |
| 3 | `AGG_SEL on a type that is neither record nor array` | `pr111683-1`, `pr63594-2`, `pr96239` | writer/lib | member/element selection on a vector or complex type |
| 2 | `REM: invalid operand types` | | writer | `%` on non-integer operands |
| 1 | `VAR_INIT constant type differs from the variable type` | | writer | |

### UNSUPPORTED (671; this class is acceptable for the gate if no crash/assertion)

Top reasons (cases): "uses a builtin function" 205 (builtin ids the lib does
not map; list them with `edg-cir-summary --list UNSUPPORTED`), "definition of `X` was dropped: it uses a construct the CIR
back end cannot express" 119 (the same reason, from a definition; e.g. the glibc
`__bswap_32`, which uses a builtin), "uses inline assembly" 88, "integer type
of N bits is not supported" 53 (`__int128`, `_BitInt`), "conversion involving
an unsupported type" 44, "this operator" 36 (complex, `_Complex`), "variable
length array" 26, "this kind of expression" 22, "this kind of constant" 15.
199 of these also fail in the reference. Owner: writer/lib, in priority order of
count: builtins, then inline asm, then 128-bit integers.

### FE_DIAG (161) - no config cause

**All 161 also fail in `edg_x86_64`**, so there is nothing to fix in the config:
the FE diagnoses language it does not support, identically in both
configurations. Messages: `expected a ";"` 33 (C2y named loops, `_BitInt`
syntax), `identifier is undefined` 18 (decimal float types), `integer constant
is too large` 11 (`wb`/`uwb` suffix `_BitInt` literals), `complex integral
types are not supported` 7, `expected an expression` 6 (C2y `if` declarations),
... Not a CIR concern. (The C++ rejection from B3 is not involved: the corpus
runs with `--gcc`, i.e. C mode, and its streams have `HDR_LANG 0`.)

### LINK_ERROR (136)

| Count | Message | Cause |
| ---: | --- | --- |
| 65 | `undefined reference to link_error` | **Not a CIR defect**: these tests need the optimizer to remove a call to `link_error()`. `edg_x86_64` does not optimize either: the harness's `//options:` has no `-O` for them (the GCC `dg-options` lines are comments), so gcc compiles the generated C at `-O0`, and the recorded expectations for `edg_x86_64` list these tests as `lp ... FAIL` (e.g. `20030804-1`, `builtins-10`, `tree-ssa/pr14841`). 69 of the 136 LINK_ERRORs fail in the reference too |
| 6 | `undefined reference to link_failure` | same |
| 5 / 4 / 3 | `foo`, `main`, `magic` | `foo`/`main`: the definition was dropped by an UNSUPPORTED diagnostic (the compile then "succeeds" and the link fails: see "dropped function" note below); `magic`: `attr-ifunc-*` (ifunc unsupported) |
| 3 | `__sync_val_compare_and_swap_4` | `ia64-sync-*`: the call to the `__sync_*` builtin appears to be left as a call to a library function that does not exist (writer or lib; either lower it to an atomic operation or link a library that has it - not investigated) |

**Optimization level:** the effective level of `edg_x86_64` is **none** (gcc
default `-O0`; `eccp` forwards `-O`/`--optimize=N` to gcc only when the test's
`//options:` has it, which is rare: 7 tests in this corpus). The `link_error`
failures are therefore expected in both configurations and **no
`--cir_opt_level` option is needed** for the gate. If a test's options contain
`-O`, `eccp` does not pass it to the front end, so the CIR config would run at
the library's default level; if that proves to matter, the option would be:
`--cir_opt_level=N` in `cmd_line.c` (config) -> `ecil_opt_level` read by
`cir_be.c` (writer) into `an_edg_cir_options::opt_level` -> the pipeline's `-ON`
(lib), set by the cir-env base from the gcc `-O` it forwards. Not implemented:
no failure needs it.

**Dropped functions:** when the library drops a function it issues a
*warning* (or an error "definition of ... was dropped") and the assembly then
lacks that symbol; tests that only warn compile and fail at link/run. These are
bucketed UNSUPPORTED by `edg-cir-summary` (the root cause), not LINK_ERROR.

### RUN_FAIL (89; 64 also fail in the reference)

The 25 CIR-specific ones, reproduced on the current tree (compile, run, and a
plain `gcc` build of the same file as the control):

- `alias-7`, `ssa-fre-42`: "uses an alias or weakref attribute" (UNSUPPORTED,
  compile fails with exit 2 now).
- `cleanup-2`, `cleanup-4`: "uses a cleanup attribute" (UNSUPPORTED).
- `initpri1`, `initpri3`, `ipa/pr70306`: constructor/destructor attributes are
  not run (the program exits 1 / aborts): `__attribute__((constructor))`
  functions must go in `.init_array` (writer/lib).
- 18 tests compile with no diagnostic and **abort at run time** (gcc build
  passes): `anon-struct-10`, `c99-func-1`, `c99-init-1`, `fwrapv-2`,
  `gnu99-init-1`, `pr56078`, `pr67410`, `store_merging_24`, `store_merging_25`,
  `torture/pr61346`, `stackalign/fastcall-1`, `global-1`, `inline-1`,
  `ret-struct-1`, `ucnid-9`, `ucnid-9-utf8`. Real miscompiles, to be
  diagnosed one by one. Hypotheses from the sources:
  - `c99-func-1`: `strcmp(__func__, "main") || sizeof(__func__) != 5`: the
    `__func__` array or its size.
  - `pr56078`, `pr67410`, `c99-init-1`, `gnu99-init-1`, `anon-struct-10`:
    designated/overlapping initializers, flexible array members, `char16_t`
    string initializers (writer's initializer lowering or lib).
  - `fwrapv-2`: `(2*x)/2` with `x = INT_MAX` must give `x` (the test is
    `-fwrapv`): signed arithmetic must not be emitted `nsw` or the pipeline must
    not fold under that assumption. The FE option `-fwrapv`/`--wrapv`, if the FE
    has one, is not passed to the library.
  - `ucnid-9*`: UCN identifiers in symbol names (mangling/escapes).
  - `nrv1`: passes now.

## Data: memory and time (not a triage item)

Generated TU: 20000 functions, 260004 lines, 6350 KB, **debug ASan front end**
(measured with `edg-cir-rss`, a not-quiet box, the ASan quarantine at its
256 MB default):

| Mode | wall s | peak RSS MB |
| --- | ---: | ---: |
| in-process streaming | 126.8 | 2553 |
| `--ecil_keep_regions` | 77.4 | 3016 |
| two-process, front end only | 100.0 | 522 |
| two-process, `edg-cir` | 14.0 | 1321 |
| plain `cpfe` (C-generating) | 83.4 | 782 |

The in-process streaming wall time (127 s) exceeds `--ecil_keep_regions`
(77 s): the freeing path (ASan's quarantine, `OVERWRITE_FREED_MEM_BLOCKS`
poisoning every freed block) is a suspect; cir-writer found that the ASan
quarantine inflates streaming peaks (with `quarantine_size_mb=0`, the streaming
front-end peak is ~45% below `--ecil_keep_regions` on 6000 functions). To be
re-measured on a quiet box with (a) `ASAN_OPTIONS=quarantine_size_mb=0` and (b)
a non-ASan build (a release-based `cpfe-cir` with the library linked, not
`-static`), with and without `OVERWRITE_FREED_MEM_BLOCKS`. Stack stress (two
process, front end under a 2 GB stack, `edg-cir` under 256 KB): 50000-term
`+` chain, 20000-rung `else if`, 5000-deep nested initializer and 2000 nested
blocks all pass.
