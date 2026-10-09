# BACK_END_IS_* audit and M0 plan

Author: cir-config, Phase R, 2026-10-09. Base tree: `43e10b90fc` plus untracked
`cir_*`. No `src/` edits were made.

Counts were taken with

    grep -rIn "BACK_END_IS_C_GEN_BE\|BACK_END_IS_CP_GEN_BE" . \
        --exclude-dir=build --exclude-dir=runs --exclude-dir=.git

which gives 742 lines (the plan's 299 + 555 uses counted per macro token, not per
line). Every use is either a preprocessor condition or a bare macro operand
(see "Runtime uses" below); there are no `if (BACK_END_IS_*)` runtime tests.

## Bucket definitions used here

The plan names buckets A-E without defining them in the text I was given, so I
fixed these:

| Bucket | Meaning | M0 action |
| --- | --- | --- |
| A | IL shape and IL content recorded only for one generator (fields in `il_def.h`, their alloc/display/walk code, FE code that fills them) | none; CIR takes the "neither" arm |
| B | Back-end entry-point dispatch (`#include` of generator headers, `back_end()` declaration, cleanup call) | convert to `BACK_END_TYPE` comparisons |
| C | Generator or lowering internals compiled for one generator only | none |
| D | Configuration policy: defaults and cross-checks that assume "not CP implies C/lowering", or "neither implies external" | explicit CIR arm in M1; M0 only removes the defaults |
| E | Config inputs, docs, tests, harness text | update only where noted |

Counts: A 232, B 20, C 187, D 258, E 45 (total 742). Full per-file spans are in
the appendix.

## Verified include-order claims

* `basics.h:25` includes `defines.h` first. The `TRUE`/`FALSE` block is at
  `basics.h:212-222` (`USE_BOOL_FOR_BOOLEAN_IN_CPLUSPLUS` selects `(0==0)`/`(0!=0)`
  or `1`/`0`). `defines.h:20-26` itself warns that `TRUE`/`FALSE` are not yet
  defined there, so configs use `1`/`0`.
* `host_envir.h` includes `lang_feat.h` at line 22, **before** its own
  `BACK_END_IS_C_GEN_BE` default (516-518) and `BACK_END_IS_CP_GEN_BE` default
  (540-542). `lang_feat.h:194-198` tests `#ifdef BACK_END_IS_CP_GEN_BE` /
  `#if BACK_END_IS_CP_GEN_BE`. So today the result depends on whether the config
  (defines.h/cmakedef) defined the macro, not on host_envir's default. For
  `cpfe` (C gen, `-DBACK_END_IS_C_GEN_BE=1`, CP undefined) `ifdef` is false and
  `DEFAULT_EXCEPTIONS_ENABLED` is `FALSE`; defining CP as `FALSE` earlier gives the
  same value.
* Every translation unit reaches `basics.h` before any other EDG header: all
  `src/*.c` start with `fe_common.h` or `basic_hdrs.h`, and `basic_hdrs.h` reads
  `lint.h` (no includes) then `basics.h:25`. In `util/`, `edg_prelink.c`,
  `edg_munch.c`, `edg_decode.c`, `decode.c` and `mk_errinfo.c` include `basics.h`
  first. `util/dettarg.c` and `make_win_predef_macro.c` include no EDG headers.
  Therefore `back_end_type.h` included from `basics.h` just after the
  `TRUE`/`FALSE` block (after line 222) precedes `lang_feat.h`, `host_envir.h`,
  `targ_def.h`.
* **Legacy `defines.h` path** (`util/`): `util/CMakeLists.txt` builds
  `edg_munch edg_prelink edg_decode mk_errinfo dettarg` with only
  `-I src -std=gnu++14 -x c++`, no `-DUSE_CMAKE_DEFINES`, so `defines.h:40-3727`
  runs. There `BACK_END_IS_CP_GEN_BE` is defined as `0` (`defines.h:304-306`,
  `3586-3588`) or `1` under `CP_GEN_BE_VERSION` (`defines.h:283-284`, also
  `defines_win32.h:226-227`, both with `BACK_END_IS_C_GEN_BE 0`).
  `BACK_END_IS_C_GEN_BE` is **not** set, and defaults to `TRUE` in
  `host_envir.h:517`. So `util/` and `cdisp` (which sets only
  `IL_SHOULD_BE_WRITTEN_TO_FILE=1`, `STANDALONE_IL_DISPLAY`) are C-gen
  configurations by default. `util/edg_prelink.h:29` relies on that (`.int.o`).
  `back_end_type.h` must keep "C undefined => C gen".
* Compile commands (gcc-debug): `cpfe` gets `-DBACK_END_IS_C_GEN_BE=1`,
  `cpfe-cp` gets `-DBACK_END_IS_CP_GEN_BE=1 -DBACK_END_IS_C_GEN_BE=0`, `cdisp`
  and `util` get neither. 174 TUs per preset (75 cpfe, 75 cpfe-cp, 17 cdisp, 7
  util).
* `--dump_configuration` (`cmd_line.c:6443-10447`) prints through
  `define_numeric_valued_macro(X)` => `#define X <value>` on stderr, after
  `BACK_END_IS_CP_GEN_BE`/`_C_GEN_BE` (6688-6697) and
  `BACK_END_SHOULD_BE_CALLED` (6698-6702). Both IS macros are always defined by
  then. The test harness matches `//require:` by plain **substring** of the dump
  (`edgtest/exec/__init__.py:977-982`), so `//require:BACK_END_TYPE 3` matches
  `#define BACK_END_TYPE 3` (and would also match a hypothetical `... 30`).

## Proposed `src/back_end_type.h`

> M0 as implemented differs from this listing: `BACK_END_TYPE_EXTERNAL` is 4 (0 is
> invalid and `#error`s), the header comment says it is included inside the
> `edg` namespace and must contain macros only, `host_envir.h` repeats the two
> IS/TYPE consistency checks, and a CIR-with-`DO_IL_LOWERING` `#error` follows
> the `DO_IL_LOWERING` default. The file `src/back_end_type.h` is authoritative.

```c
/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*

back_end_type.h -- Selects the back end and derives the BACK_END_IS_*
                   macros from it.

*/

/*
This header is included from basics.h immediately after TRUE and FALSE are
defined, so that it precedes every use of the BACK_END_* macros (including
the test in lang_feat.h).
*/

#ifndef BACK_END_TYPE_H
#define BACK_END_TYPE_H 1

/*
The values of BACK_END_TYPE.  These numbers are permanent: they appear in
--dump_configuration output and in //require: lines of tests.  0 is not a
valid value on purpose: an undefined identifier evaluates to 0 in #if, so a
misspelled BACK_END_TYPE_* name would otherwise silently select a back end.
*/
#define BACK_END_TYPE_EXTERNAL 4  /* A user-supplied back_end(). */
#define BACK_END_TYPE_C_GEN 1     /* The C-generating back end. */
#define BACK_END_TYPE_CP_GEN 2    /* The C++-generating back end. */
#define BACK_END_TYPE_CIR 3       /* The ClangIR (CIR) back end. */

#ifndef BACK_END_TYPE

/*
Legacy configurations select a back end with BACK_END_IS_C_GEN_BE and/or
BACK_END_IS_CP_GEN_BE.  A configuration that sets neither gets the C-
generating back end (this has always been the default).
*/
#ifndef BACK_END_IS_C_GEN_BE
#define BACK_END_IS_C_GEN_BE TRUE  /* You can change this. */
#endif /* ifndef BACK_END_IS_C_GEN_BE */
#ifndef BACK_END_IS_CP_GEN_BE
#define BACK_END_IS_CP_GEN_BE FALSE  /* You can change this. */
#endif /* ifndef BACK_END_IS_CP_GEN_BE */

#if BACK_END_IS_C_GEN_BE && BACK_END_IS_CP_GEN_BE
 #error -- BACK_END_IS_C_GEN_BE and BACK_END_IS_CP_GEN_BE cannot both be TRUE.
#endif /* BACK_END_IS_C_GEN_BE && BACK_END_IS_CP_GEN_BE */

#if BACK_END_IS_C_GEN_BE
#define BACK_END_TYPE BACK_END_TYPE_C_GEN
#elif BACK_END_IS_CP_GEN_BE
#define BACK_END_TYPE BACK_END_TYPE_CP_GEN
#else /* !BACK_END_IS_C_GEN_BE && !BACK_END_IS_CP_GEN_BE */
#define BACK_END_TYPE BACK_END_TYPE_EXTERNAL
#endif /* BACK_END_IS_C_GEN_BE */

#else /* defined(BACK_END_TYPE) */

/*
New-style configurations set only BACK_END_TYPE.  Derive the legacy macros
using the same TRUE/FALSE tokens the old defaults used.  If a configuration
also sets one of them, it must agree.
*/
#if BACK_END_TYPE == 0
 #error -- BACK_END_TYPE is 0; this usually means that it was set to a \
           misspelled or undefined BACK_END_TYPE_* name.
#endif /* BACK_END_TYPE == 0 */
#if BACK_END_TYPE != BACK_END_TYPE_EXTERNAL && \
    BACK_END_TYPE != BACK_END_TYPE_C_GEN && \
    BACK_END_TYPE != BACK_END_TYPE_CP_GEN && \
    BACK_END_TYPE != BACK_END_TYPE_CIR
 #error -- BACK_END_TYPE is not one of the BACK_END_TYPE_* values.
#endif /* BACK_END_TYPE != ... */

#ifndef BACK_END_IS_C_GEN_BE
#if BACK_END_TYPE == BACK_END_TYPE_C_GEN
#define BACK_END_IS_C_GEN_BE TRUE
#else /* BACK_END_TYPE != BACK_END_TYPE_C_GEN */
#define BACK_END_IS_C_GEN_BE FALSE
#endif /* BACK_END_TYPE == BACK_END_TYPE_C_GEN */
#endif /* ifndef BACK_END_IS_C_GEN_BE */
#ifndef BACK_END_IS_CP_GEN_BE
#if BACK_END_TYPE == BACK_END_TYPE_CP_GEN
#define BACK_END_IS_CP_GEN_BE TRUE
#else /* BACK_END_TYPE != BACK_END_TYPE_CP_GEN */
#define BACK_END_IS_CP_GEN_BE FALSE
#endif /* BACK_END_TYPE == BACK_END_TYPE_CP_GEN */
#endif /* ifndef BACK_END_IS_CP_GEN_BE */

#if (BACK_END_IS_C_GEN_BE != 0) != (BACK_END_TYPE == BACK_END_TYPE_C_GEN)
 #error -- BACK_END_IS_C_GEN_BE disagrees with BACK_END_TYPE.
#endif /* (BACK_END_IS_C_GEN_BE != 0) != ... */
#if (BACK_END_IS_CP_GEN_BE != 0) != (BACK_END_TYPE == BACK_END_TYPE_CP_GEN)
 #error -- BACK_END_IS_CP_GEN_BE disagrees with BACK_END_TYPE.
#endif /* (BACK_END_IS_CP_GEN_BE != 0) != ... */

#endif /* ifndef BACK_END_TYPE */

#endif /* ifndef BACK_END_TYPE_H */
```

Notes on the header:

* A configuration giving only `BACK_END_IS_CP_GEN_BE=1` still hits the "both
  TRUE" `#error` (C defaults TRUE), exactly as today.
* Configs that define the IS macros as `0`/`1` (cmakedefs, `defines.h`) keep their
  tokens; only absent ones get `TRUE`/`FALSE` as before. Preprocessed output is
  unchanged because every use expands to the same integer.
* A `-DBACK_END_TYPE=3` or `=BACK_END_TYPE_CIR` both work (the dump casts to a
  number).
* The external case cannot be inferred from absence (absence means C gen);
  legacy external configs set `BACK_END_IS_C_GEN_BE=0` and leave CP at 0, giving
  type 0. Same as today.
* Derived-macro idea for M1, defined in the same header: `BACK_END_GENERATES_SOURCE`
  (`C_GEN || CP_GEN`) and `BACK_END_HAS_OUTPUT_FILE` (`C_GEN || CP_GEN || CIR`),
  used for the `--gen_c_file_name` guards below.

## M0 edit list (no behavior change)

1. **New** `src/back_end_type.h` (above).
2. `basics.h` after line 222 (`#endif /* USE_BOOL_FOR_BOOLEAN_IN_CPLUSPLUS */`):
   `#include "back_end_type.h"` with a short comment.
3. `host_envir.h`:
   * lines 509-518 and 534-542: remove the two default blocks (keep a one-line
     comment pointing at `back_end_type.h`, keep the explanatory prose about
     "not necessarily in the current program");
   * 527-532: keep `ENSURE_LOWERED_TYPE_LIST_ORDERING` and its `#error`;
   * 544-546: remove the "both TRUE" `#error` (moved to `back_end_type.h`);
   * 548-551 (CP gen vs `COMPILE_MULTIPLE_TRANSLATION_UNITS`): keep.
4. `cmd_line.c` after line 6702 (after `BACK_END_SHOULD_BE_CALLED`, alphabetical):
   ```c
   #if defined(BACK_END_TYPE)
     define_numeric_valued_macro(BACK_END_TYPE);
   #else /* !defined(BACK_END_TYPE) */
     comment_undefined_macro_name(BACK_END_TYPE);
   #endif /* defined(BACK_END_TYPE) */
   ```
   Keep the existing IS lines (edgy `//require:` strings like
   `BACK_END_IS_CP_GEN_BE 1` match them: `edgtest/exec/__init__.py:1096`,
   `tests/tests/edg/ccs/gnats21635.sft.cpp`, `gnats21047.sft.cpp`).
5. Dispatch sites, preprocessor conditions only:
   * `cfe.c:44-49`: `#if BACK_END_TYPE == BACK_END_TYPE_C_GEN` /
     `... == BACK_END_TYPE_CP_GEN`.
   * `cfe.c:50-51,57`: condition becomes
     `BACK_END_SHOULD_BE_CALLED && (BACK_END_TYPE == BACK_END_TYPE_EXTERNAL || BACK_END_TYPE == BACK_END_TYPE_CIR)`.
     `cir_be.c` provides `back_end()` exactly like an external back end.
   * `cfe.c:99-106`: `BACK_END_TYPE == BACK_END_TYPE_CP_GEN`.
   * `fe_init.c:162-167`: as `cfe.c:44-49`.
   * `fe_wrapup.c:52-54`, `902-904`: `BACK_END_TYPE == BACK_END_TYPE_CP_GEN`
     (`cp_gen_be_cleanup`). `fe_wrapup.c` ownership passes to cir-writer after M0.
   * `c_gen_be.h:23`, `cp_gen_be.h:22` and `c_gen_be.c`/`cp_gen_be.c:32-43` keep
     the IS macros (they are generator-internal, bucket C).
   * The `src/Changes` entry (UTF-8).
6. Do **not** touch: `il_def.h`, `lang_feat.h:194` (works as is, see below), the
   `targ_def.h` conditions, and all bucket A/C sites.
7. Negative checks to run (each must fail to compile with the stated message):
   `-DBACK_END_TYPE=7`; `-DBACK_END_TYPE=1 -DBACK_END_IS_CP_GEN_BE=1`;
   `-DBACK_END_TYPE=3 -DBACK_END_IS_C_GEN_BE=1`;
   `-DBACK_END_IS_C_GEN_BE=1 -DBACK_END_IS_CP_GEN_BE=1`; and a positive
   `-DBACK_END_TYPE=3` configuration compiles `cfe.c` with `back_end()` declared.

`lang_feat.h:194-198`: after M0 `BACK_END_IS_CP_GEN_BE` is always defined at that
point, so `#ifdef` is always true and the `#if` decides; behavior is identical to
today. M1 adds a CIR arm there (see D table: exceptions default TRUE for CIR).

## Runtime uses (macro as an expression)

| Site | Expression | Bucket | CIR value |
| --- | --- | --- | --- |
| `preproc.c:4271` | `ignore_in_back_end = !BACK_END_IS_CP_GEN_BE` | D | TRUE (macro declarations are not passed on) |
| `pragma.c:1683-1684,1775,1780,2331` | `automatically_include_in_il`, `record_pragma_text`, `ignore_in_back_end`, `expand_macros` = CP / !CP | D | same as non-CP (pragmas ignored by the back end) |
| `trans_copy.c:3231` | `okay = !BACK_END_IS_CP_GEN_BE` (CHECKING assertion) | A | unchanged |
| `interpret.c:22765` | `note_routine_interpreted` only when CP | A | no-op |
| `targ_def.h:3275,4893,5127,5724` | `TARG_PAD_ALLOCATED_EMPTY_BASE`, `PROMOTE_LOCAL_ENTITIES_TO_FILE_SCOPE`, `FORCE_STORES_OF_VARS_MODIFIED_IN_TRY_BLOCKS` = C gen; `FAVOR_CONSTANT_RESULT_FOR_NONSTATIC_INIT = !CP` | D | FALSE, FALSE, FALSE, TRUE |
| `defines.h`/cmakedefs | `TARG_MICROSOFT_PTR_TO_MEMBER_SIZING_<target> = BACK_END_IS_CP_GEN_BE` | D | 0 (it is "preserve source dialect"; CIR targets the Itanium ABI) |

## Bucket D: CIR decisions

CIR is "neither C gen nor CP gen", with `DO_IL_LOWERING=0`. So CIR inherits the
"not CP" arm unless the "not CP" arm secretly means "C gen/lowering".
"Decision" says what M1 does. Items marked **explicit** need a new
`BACK_END_TYPE == BACK_END_TYPE_CIR` arm or an explicit cmakedef value.

| Where | What | Today (C gen / CP gen / neither) | CIR decision |
| --- | --- | --- | --- |
| `host_envir.h:527` | `ENSURE_LOWERED_TYPE_LIST_ORDERING` | C / - | FALSE (default follows C) |
| `host_envir.h:558-567` | `GEN_C_FILE_SUFFIX` | defined for C or CP only | **explicit** `".int.s"` for CIR (also `host_util.h:74-78`, which sizes names against `GEN_C_FILE_SUFFIX` or `OBJECT_FILE_SUFFIX`) |
| `host_envir.h:578-582` | `LOWER_STRING_LITERALS_TO_NON_CONST` | TRUE for C | FALSE (default) |
| `host_envir.h:681-693` | `DO_IL_LOWERING` | CP FALSE, else TRUE | **explicit** FALSE for CIR; `#error` if CIR and `DO_IL_LOWERING` |
| `host_envir.h:784-788` | `FUNCTION_PROTOTYPE_INSTANTIATION_DEFERRAL_ALLOWED` | CP FALSE, else TRUE | TRUE (default; writer filters prototype instantiations) |
| `host_envir.h:951-961` | `AUTOMATIC_TEMPLATE_INSTANTIATION` | CP FALSE else follows `INSTANTIATE_TEMPLATES_EVERYWHERE_USED` | **explicit** FALSE (cmakedef sets `INSTANTIATE_TEMPLATES_EVERYWHERE_USED=1`; also add an `#error` for CIR + automatic) |
| `host_envir.h:1100-1104` | `PRAGMA_DEFINE_TYPE_INFO_IS_REQUIRED` | CP FALSE, else TRUE | **explicit** FALSE (CIR uses libstdc++ `<typeinfo>`, which has no EDG pragma); matters at M3 |
| `host_envir.h:1113-1117` | `MAINTAIN_NEEDED_FLAGS` | C TRUE else FALSE | **explicit** 0 in the cmakedef (cir-writer's Phase R decision) |
| `host_envir.h:1130-1134` | `DEFAULT_REMOVE_UNNEEDED_ENTITIES` | follows needed flags | FALSE (derived) |
| `host_envir.h:1158-1162` | `ONE_INSTANTIATION_PER_OBJECT` | C TRUE | FALSE (default) |
| `host_envir.h:1293-1297` | `DEFAULT_RECORD_FORM_OF_NAME_REFERENCE` | CP TRUE | FALSE (default) |
| `host_envir.h:1527-1539` | `BACKING_EXPR_FOR_NONTYPE_TEMPL_ARG` | CP TRUE | FALSE (default) |
| `host_envir.h:1555` | `GENERATE_SOURCE_SEQUENCE_LISTS` | CP TRUE | **explicit** FALSE in cmakedef |
| `host_envir.h:1635-1646` | SSI `#error`s | - | n/a (SSI off) |
| `host_envir.h:1854-1872` | `SCOPE_ORPHANED_LIST_PROCESSING_NEEDED` | `ORPHAN_PROCESSING_NEEDED \|\| C gen` => TRUE | TRUE (derived through `ORPHAN_PROCESSING_NEEDED`; corrected per design.md v0.2, the earlier "FALSE" was wrong) |
| `host_envir.h:1886-1946` | `MINIMAL_INLINING` (C TRUE), `INCLUDE_UNRECOGNIZED_PRAGMAS_IN_IL`, `RECORD_HIDDEN_NAMES_IN_IL`, `RECORD_TEMPLATE_STRINGS`, `RECORD_UNRECOGNIZED_ATTRIBUTES` (all CP TRUE) | FALSE (defaults; source-fidelity features) |
| `host_envir.h:3905` | `MODULE_ID_NEEDED` | standalone C-gen utility only | no change (`NEED_NAME_MANGLING=1` already makes it TRUE) |
| `targ_def.h:526-528,581-583` | `LOWER_VARIABLE_LENGTH_ARRAYS`, `LOWER_CLASS_RVALUE_ADJUST` | TRUE for C | FALSE (default) |
| `targ_def.h:631-633` | thread-local lazy-init `#error` | C | n/a |
| `targ_def.h:3474-3560,3630-3643` | `*_IS_GENERATED_CODE_TARGET`, `LOWER_IFUNC`, default target by host compiler | C or CP arm only | **explicit** arm for CIR: define all `*_IS_GENERATED_CODE_TARGET` as FALSE, no host-compiler probing (macros only; runtime variables `gcc_is_generated_code_target` etc. at `target.h:1131-1160` and `target.c:993-1025` stay C/CP only; check each non-guarded use, e.g. `symbol_ref.c:651,997` and `lexical.c:28737` are already CP/C guarded) |
| `targ_def.h:3554-3600,3799-3850` | `MSVC_TARGET_VERSION_NUMBER`, `SPECIFIC_TARGET_DIALECT_SET` checks | C or CP | skip for CIR |
| `targ_def.h:3658-3664` `DEFAULT_ALWAYS_FOLD_CALLS_TO_BUILTIN_CONSTANT_P` | folds `__builtin_constant_p` | FALSE only for gcc/clang gen targets | TRUE (default; correct for CIR) |
| `targ_def.h:3888` | `C_GEN_BE_GENERATES_ANSI_C` | C | skip |
| `targ_def.h:4251-4262` | `DEFAULT_PASS_STDARG_REFERENCES_TO_GENERATED_CODE` | C/CP TRUE | FALSE (default) |
| `targ_def.h:4309` | `SAME_REPR_INTS_INTERCHANGEABLE_IN_IL` | C non-ANSI | FALSE |
| `targ_def.h:4785-4797,4855` | `VLA_DEALLOCATIONS_IN_IL` | TRUE unless CP | derived TRUE; decide at VLA support (VLAs excluded from M2; the writer may prefer FALSE and emit stacksave/stackrestore) |
| `targ_def.h:5823-5832` | `REPLACE_SPECIAL_CHARACTERS_IN_MANGLED_NAMES` | C TRUE | FALSE (Itanium names are already `[A-Za-z0-9_]`) |
| `targ_def.h:5908` | `PRESERVE_EMBED_DIRECTIVE_WHEN_OPTIMIZED` | CP | FALSE |
| `lang_feat.h:194` | `DEFAULT_EXCEPTIONS_ENABLED` | CP TRUE else FALSE | **explicit** TRUE for CIR in C++ (matches gcc; the consumer lowers EH); M1 edit; irrelevant to C |
| `cmd_line.{c,h}` `optk_old_line_dirs`, `gen_old_style_line_dirs` | `--old_line_commands` | C or CP | **explicit**: must be accepted for CIR, since `eccp.sh:378` adds it whenever the suffix does not end in `.c` (".int.s"); accept and ignore |
| `cmd_line.{c,h}` `optk_gen_c_file_name`, `gen_c_file_name` | see design below | C or CP | **explicit** |
| `cmd_line.{c,h}` `optk_msvc/gnu/clang_target_version` | `--gen_c_*_version` | C or CP | not available for CIR (eccp only passes them if the user does) |
| `cmd_line.c:12899, 13094` | CP-only fixups | CP | n/a |
| `expr.c:22067-22345` | `dynamic_cast` known-at-compile-time path | non-CP folds via `cast_operand_full` | non-CP behavior is right for CIR (static upcast folded) |
| `il.c/il.h/overload.*/expr.c` CP blocks | `found_through_adl`, `keep_as_cast_for_cp_gen_be`, name-reference special kind, class-template-arg-deduction info | CP | not needed (A) |

### Notes the other agents need

* IL layout for CIR is the "neither" layout. Fields guarded `BACK_END_IS_C_GEN_BE`
  in `il_def.h` are absent: `elide_aggregate_braces` (4920), the prototype-scope and
  typedef-pending bits (9845), **`bit_field_alignment_type` (12093)**, and
  `has_deferred_ampersand` (15294). The CP-only fields (source sequence lists,
  `evaluated_in_interpreter`, etc.) are absent as well. The writer sends offsets
  and sizes computed by the FE, so `bit_field_alignment_type` should not be
  required, but if it is, its guard must be widened (shared file, cir-writer to
  propose).
* `MAINTAIN_NEEDED_FLAGS` is needed-flag *maintenance*; with 0 the writer
  cannot trust `needed` bits (see cir-writer's note).

## `--gen_c_file_name` design for CIR (M1)

Facts:

* `eccp.sh:2790-2793` always passes `--gen_c_file_name=<tmpdir>/<base><suffix>`;
  line 247 sets `suffix=${EDG_GEN_C_SUFFIX-".int.c"}`; line 378 adds
  `--old_line_commands` for a non-`.c` suffix; `compile_int_c` (754-835) runs
  `$cc -o X.o -c <file>` (a `.int.s` file is assembled by gcc).
* Registration and storage are guarded `BACK_END_IS_C_GEN_BE ||
  BACK_END_IS_CP_GEN_BE` at `cmd_line.h:111-116` (enum), `1368-1374`
  (`gen_c_file_name`), `cmd_line.c:486-504` (registration, together with the three
  `--gen_c_*_version` options), `11095-11116` (handlers), `13422-13425` (init),
  `261-264/10628/13240` (`--old_line_commands`). The generators read
  `gen_c_file_name` in `c_gen_be.c:12096-12114` / `cp_gen_be.c:26622-26638`
  (each has a local variable of the same name, shadowing the global).

Design:

1. Add derived macros in `back_end_type.h`:
   `BACK_END_GENERATES_SOURCE` = C gen or CP gen;
   `BACK_END_HAS_OUTPUT_FILE` = C gen, CP gen or CIR.
2. Split the guards: `optk_gen_c_file_name`, `gen_c_file_name`, its handler and
   its init, plus `optk_old_line_dirs` and `gen_old_style_line_dirs`, use
   `BACK_END_HAS_OUTPUT_FILE`. The three `--gen_c_*_version` options and
   `msvc_is_generated_code_target` handling stay under
   `BACK_END_GENERATES_SOURCE`.
3. For CIR `GEN_C_FILE_SUFFIX` is `".int.s"` (`host_envir.h:558-567`) so the FE's
   own derivation (when `--gen_c_file_name` is absent) agrees with the eccp config
   (`EDG_GEN_C_SUFFIX=".int.s"`). `host_util.h:74-78` takes the same arm.
4. `cir_be.c` (cir-writer) resolves the output name as
   `gen_c_file_name` if non-NULL, else `derived_name(primary_source_file_name,
   GEN_C_FILE_SUFFIX)`, and passes it to the library as the output path; the
   output kind (asm default) comes from a new CIR-only option
   (`--cir_output_kind`), so the file extension does not decide the format.
   `--old_line_commands` is accepted and ignored.
5. `--gen_c_file_name` with `--ecil_output=` (the stream file) are independent:
   the stream goes to the sink, the codegen result to the generated-file path.
6. Instantiation files (`.ti`/`.ii`, `edg_prelink`) are off for CIR
   (`AUTOMATIC_TEMPLATE_INSTANTIATION=0`), so nothing else uses the suffix.

## Appendix: all hit lines by bucket

Spans merge hits at most 3 lines apart; the list is reproducible with the grep
above. `il_to_str.c`, the IL files, `src/Changes` and the tests are bulk.

### A - IL shape and generator-only IL content

| File | Count | Lines |
| --- | ---: | --- |
| `src/il_def.h` | 44 | 123,128,3444,3469,3758,3764,4225,4242,4920,4925,7023,7028,7765,7770,7832,7841,8921,8927,9845,9866,9875,9933,10426,10432,10703,10730,10779,10794,11653,11663,12093,12101,12794,12848,15043,15067,15224,15234,15294,15309,15327,15333,17322,17343 |
| `src/il_alloc.c` | 42 | 963-965,1333-1335,1357-1359,1742-1744,2023-2026,2075,2080,2163,2167,2283,2287-2290,2305,2450-2453,2678-2681,2806-2808,3050,3066,3456-3459,3471-3473,3477-3479,3740-3743,4370-4372,4562-4564,5081-5084,5964,5968 |
| `src/il_display.c` | 25 | 2297,2301,2594,2599,2739,2749,3458,3463,4064,4084,4770,4777,4849,4856,4894,4898,5854,5858,6119,6124,7191,7359,7363,7744,7750 |
| `src/expr.c` | 23 | 8988,8995,22067-22069,22304-22306,22320,22341,22378,30849-30851,30919-30921,31549,31556,42266,42275,42374,42381,42534,42542,53667,53671 |
| `src/templates.c` | 18 | 5564,5569,11064-11066,11073-11075,11082-11084,27803,27815,33732-33735,34216,34226,37288,37300-37302,37312 |
| `src/class_decl.c` | 14 | 6004-6007,10759,10797,12139,12144,16596,16604,17072,17076,33490,33501,36175-36178 |
| `src/overload.c` | 12 | 6950,6956,10146,10165,10201-10204,11695,11700,11947,11951,20612-20614 |
| `src/symbol_ref.c` | 11 | 391-393,438,447,651,656,997,1010,2153-2158 |
| `src/exprutil.c` | 6 | 9973,9977,17089,17105,19565-19568 |
| `src/il.c` | 4 | 23125,23130,31992,32006 |
| `src/decls.c` | 4 | 10519,10537,12907,12912 |
| `src/layout.c` | 4 | 649,657,1206-1208 |
| `src/walk_entry.h` | 4 | 1428-1430,1516,1539 |
| `src/src_seq.c` | 3 | 2140-2145 |
| `src/interpret.c` | 3 | 22765-22771 |
| `src/il.h` | 2 | 4366,4390 |
| `src/il_alloc.h` | 2 | 509,514 |
| `src/overload.h` | 2 | 529,533 |
| `src/decl_inits.c` | 2 | 6043,6052 |
| `src/decl_spec.c` | 2 | 2409-2412 |
| `src/attribute.c` | 2 | 9388-9390 |
| `src/cmd_line.c` | 2 | 12899,13094 |
| `src/trans_copy.c` | 1 | 3231 |

### B - back-end dispatch

| File | Count | Lines |
| --- | ---: | --- |
| `src/cfe.c` | 8 | 44-51,57,99,106 |
| `src/fe_init.c` | 4 | 162-167 |
| `src/fe_wrapup.c` | 4 | 52-54,902-904 |
| `src/cp_gen_be.h` | 2 | 22,41 |
| `src/c_gen_be.h` | 2 | 23,48 |

### C - generator and lowering internals

| File | Count | Lines |
| --- | ---: | --- |
| `src/il_to_str.c` | 127 | 18-20,32,36-38,44,48-50,56,60-62,123-128,197,214,451,486-488,576-578,585,607,807,824,841-844,875-878,1396,1407,1421,1427,1441,1447,1681,1687,1699,1715-1717,1722,1769-1772,1783,1788,1796,1802,1923,1929,1967-1969,1994,2181,2248-2250,2369,2377,2422,2430,2437,2445,3308,3313,3791,3819,3898-3900,4380,4384,4639,4648,5637-5639,5650,5658,5677,5696,5897-5900,5990,5998,6006,6010-6013,6017-6020,6026-6027,6031-6034,6038-6041,6045-6048,6052,6107,6174,6827,6842,7026,7031,7142,7153,7304,7311,7619,7630,7738,8402-8403,8441,8445-8446,8467-8469,8539-8540,8563-8565,8642,8651-8654,8660,8683-8686 |
| `src/il_to_str.h` | 19 | 74,107-111,207,214-215,223,590,610,631,663-665,672,676-677,681-684,695 |
| `src/c_gen_be.c` | 7 | 12,32,36-38,42,12264,12276 |
| `src/cp_gen_be.c` | 7 | 16,33,37-39,43,26943,26955 |
| `src/lexical.c` | 6 | 28737,28742,28767,28798,29518,29523 |
| `src/lower_c99.c` | 6 | 4295,4351,4388-4390,4409,4421 |
| `src/inline.c` | 4 | 453,471,823,836 |
| `src/lower_il.c` | 4 | 1471,1477,10023,10034 |
| `src/cmd_line.c` | 3 | 34,424,11028 |
| `src/lower_eh.c` | 2 | 5494,5519 |
| `src/cmd_line.h` | 2 | 94-96 |

### D - configuration policy (assumes "not CP implies C/lowering")

| File | Count | Lines |
| --- | ---: | --- |
| `src/host_envir.h` | 78 | 516-518,527-532,540-551,559,566,578-582,591-593,681-683,689-693,784-788,951,955,961,1100-1104,1113-1117,1130-1134,1158-1162,1293-1297,1527,1532-1539,1555-1559,1635,1641,1646,1854,1868-1872,1886-1890,1896-1903,1918,1922-1926,1942-1946,3905 |
| `src/targ_def.h` | 67 | 526-530,581,585,631-632,3275,3474,3480,3492-3494,3541-3542,3550-3554,3570,3630-3634,3640-3644,3660,3667,3678,3750,3799,3821,3836,3842-3844,3886-3888,4078,4124,4128,4251,4259-4263,4267-4268,4309-4313,4785-4789,4795-4798,4852-4855,4859,4893,5127,5724,5823-5832,5908-5912 |
| `src/cmd_line.c` | 27 | 40,261,265,428,486,504,6688-6697,10628,10635,11040,11095,11116,12903,13098,13240-13242,13422-13424 |
| `src/defines.h` | 24 | 283-284,304-306,695,866,1036,1206,1376,1546,1890,2068,2243,2406,2569,2732,2895,3058,3221,3586-3588,3593 |
| `src/target.h` | 14 | 1083,1090,1131,1192,1199,1207,1241,1248,1458,1480,1677,1702,1716-1718 |
| `src/target.c` | 13 | 731,784,982-985,993,1008,1012-1014,1018-1025 |
| `src/pragma.c` | 10 | 118,133,1535,1544,1549,1683-1684,1775,1780,2331 |
| `src/cmd_line.h` | 8 | 45-47,111,116,464,470,1368,1374 |
| `src/lang_feat.h` | 4 | 194-198 |
| `src/float_pt.c` | 4 | 3225-3227,3254-3256 |
| `util/edg_prelink.h` | 3 | 29,37,45 |
| `src/host_util.h` | 3 | 74-78 |
| `src/defines_win32.h` | 2 | 226-227 |
| `src/preproc.c` | 1 | 4271 |

### E - inputs, docs, tests, harness

| File | Count | Lines |
| --- | ---: | --- |
| `src/Changes` | 28 | 4479,10502,22804,33117-33118,62197,63649,102773,109480-109483,111721,111993,112629,129229,130140,132043,133200,133453,134685,138871,150460,150982-150985,157519,175120-175122 |
| `cmake/macro-conf/default/cpfe-cp.cmakedef` | 2 | 1-2 |
| `doc/source/build.rst` | 2 | 339,345 |
| `cmake/macro-conf/support/target/linux_riscv32/base.cmakedef` | 1 | 103 |
| `cmake/macro-conf/support/target/linux_aarch64/base.cmakedef` | 1 | 103 |
| `cmake/macro-conf/support/target/linux_riscv64/base.cmakedef` | 1 | 103 |
| `cmake/macro-conf/support/target/linux_armv7/base.cmakedef` | 1 | 103 |
| `cmake/macro-conf/support/target/win32/base.cmakedef` | 1 | 103 |
| `cmake/macro-conf/default/cpfe.cmakedef` | 1 | 2 |
| `cmake/macro-conf/support/target/win64/base.cmakedef` | 1 | 103 |
| `tests/tests/edg/ccs/gnats21635.sft.cpp` | 1 | 4 |
| `tests/tests/edg/ccs/gnats21047.sft.cpp` | 1 | 4 |
| `tests/tests/edg/ccs/coroutines/gnats20948-paramcopy.sft.cpp` | 1 | 4 |
| `tests/tests/edg/ccs/coroutines/gnats15431_21/test.mft.cpp` | 1 | 3 |
| `dev_tools/pylibs/edgtest/exec/__init__.py` | 1 | 1096 |
| `src/msinfo.txt` | 1 | 68 |
