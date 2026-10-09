# Writer hooks: verification of the per-function streaming design

Author: cir-writer (Phase R). Scope: research only, no `src/` edits.
Everything below is checked against the tree at branch `cir`, HEAD `43e10b90fc`.
"V" marks a fact read in the code (file:line), "P" a fact observed with the
native debug `build/clang/bin/cpfe-cp --il_display` (non-lowered IL), "I" an
inference or proposal that still needs confirming by the first prototype.

## 0. Summary: what the plan gets right and what needs correcting

Right (V): the memory-region machinery is exactly the hook the brief asks for.
`pop_scope_full` -> `check_for_done_with_memory_region` is called once per
completed top-level function, the function IL is final at that point (for C),
and the region is then freed in the `IL_SHOULD_BE_WRITTEN_TO_FILE` +
`FREE_MEMORY_REGIONS_EARLY` configuration.

Corrections and additions:

1. **The early-free path is much better exercised than the plan assumes (V).**
   The *debug* build recipes already set `IL_SHOULD_BE_WRITTEN_TO_FILE=1`
   (`cmake/macro-conf/support/build-type/debug/base.cmakedef:36`),
   `MAINTAIN_NEEDED_FLAGS=1` (`:28`), `OVERWRITE_FREED_MEM_BLOCKS=1`, and the
   Linux platform sets `FREE_MEMORY_REGIONS_EARLY=1`
   (`support/platform/linux/base.cmakedef:65`). So every `edg_x86_64` /
   `edg_x86_64_cp` debug test run frees every function region right after it is
   written and re-reads it from the IL file for the back end. What is *not*
   exercised today is the other arm of the keep test (`MAINTAIN_NEEDED_FLAGS=0`
   with early free): only `windows-msvc-stl-testing` combines
   `IL_SHOULD_BE_WRITTEN_TO_FILE=1` with `MAINTAIN_NEEDED_FLAGS=0`. Release
   builds set `IL_SHOULD_BE_WRITTEN_TO_FILE=0`, so they never free early. The
   top risk "latent bugs in FREE_MEMORY_REGIONS_EARLY" is therefore smaller, but
   "early free with MAINTAIN_NEEDED_FLAGS=0" is a genuinely new combination.
2. **cmakedef precedence trap (V).** `debug/base.cmakedef` forces
   `IL_SHOULD_BE_WRITTEN_TO_FILE=1`, `MAINTAIN_NEEDED_FLAGS=1`,
   `LOWER_VARIABLE_LENGTH_ARRAYS=0`, `LOWER_DESIGNATED_INITIALIZERS=1`,
   `EXPENSIVE_CHECKING=1` for every target that imports it; macOS does *not* set
   `FREE_MEMORY_REGIONS_EARLY` at all (default FALSE, `host_envir.h:249-251`).
   `cpfe-cir.cmakedef` must override these *after* the base import, and the
   build must `#error` if `BACK_END_TYPE==CIR && IL_SHOULD_BE_WRITTEN_TO_FILE`,
   or `!FREE_MEMORY_REGIONS_EARLY` (the `!FREE_MEMORY_REGIONS_EARLY` arm at
   `mem_manage.c:1447-1453` keeps every function region until end of TU, which
   defeats streaming).
3. **`must_wait_for_mangling_reasons` has no IL-only tests (V).** It is merely
   *guarded* by the `#if DO_IL_LOWERING` that opens at `scope_stk.c:9029` and
   closes at `:9536`, together with `should_delay_lowering_on_function`
   (`:9388`) and the module-id queue (`:9066`). The logic is lowering-agnostic.
   A snag: `should_delay_lowering_on_function` reads/writes
   `a_routine::lowering_delayed_on_nested_function`, which `il_def.h:12958`
   declares only `#if DO_IL_LOWERING` (and `il_def.h` is frozen). So CIR cannot
   simply widen the guard; see section 1.4 for the proposal.
4. **`lower_name.c` getters already compile in a non-lowered config (P).**
   `nm build/clang/bin/cpfe-cp` shows `get_mangled_function_name_full`,
   `get_mangled_variable_name`, `mangled_vtbl_name`, `mangled_class_name`,
   `mangled_typeinfo_name`, `do_all_name_mangling`. That binary has
   `DO_IL_LOWERING=0, NEED_NAME_MANGLING=1, MANGLE_ALL_NAMES=0`. Reason:
   `NEED_NAME_MANGLING` is TRUE there because debug configs enable
   `MICROSOFT_EXTENSIONS_ALLOWED`. For a release-posture `cpfe-cir` (MS
   extensions off) the macro is *derived* FALSE
   (`host_envir.h:1824-1830`), so `cpfe-cir.cmakedef` must define
   `NEED_NAME_MANGLING=1` explicitly (the `#ifndef` and the `#error` at
   `:1826-1829` permit that).
5. **The ecil id field is free in release builds (V/I).** Without
   `EXPENSIVE_CHECKING` the prefix is four 1-bit fields in one `unsigned int`
   (4 bytes, `mem_tables.h:143-225`) but `SPACE_FOR_IL_ENTRY_PREFIX` rounds up
   to `HOST_IL_ENTRY_PREFIX_ALIGNMENT` = 8 (`mem_tables.h:346-349`,
   `targ_def.h:5571`). A `uint32_t` costs 0 bytes on 64-bit hosts. Recommend the
   prefix field (section 3).
6. **Stress targets need rethinking (P).** The FE itself recurses on these
   shapes. Debug `cpfe-cp` on macOS segfaults on a 20k-term `a+a+...` chain with
   an 8 MB stack; a 50k-term chain needs ~32 MB and a 20k-link `else if` ladder
   ~64 MB (`ulimit -s`; 16 MB fails, 32 MB / 64 MB pass respectively). So
   "50k + chains under `ulimit -s 256` through edg-cir" can only be run
   two-process (FE with a huge stack writing a file, `edg-cir` consuming it under
   the small limit). Writer recursion has to be proven by construction plus a
   stack-watermark assertion, not by an rlimit run. See section 8.
7. **`MAINTAIN_NEEDED_FLAGS`: recommend 0**, with the evidence in section 4,
   including the one thing it costs us (no free dead-static elimination in the
   FE; the consumer or an FE-side deferred-frame queue has to do it).
8. **Streaming is independent of keeping (I).** The plan says constexpr
   functions "are streamed but kept". The existing keep tests only compute a
   boolean "free or not"; the proposal below splits `stream` from `free`, so a
   kept region (constexpr, `do_not_free_memory_region`, PCH pending, debug keep)
   can still be streamed once, and the end-of-TU sweep skips already-streamed
   regions via a dense bitmap indexed by `function_def_number`.

## 1. Per-function streaming hook

### 1.1 Memory regions in outline (V)

* A function body lives in its own region created by `new_function_scope`
  (`il.c:3743-3790`, called from `scope_stk.c` for `sck_function` push,
  `:2955-2965`). Lambdas and (for C++) nested functions share the region of the
  enclosing function (`get_enclosing_memory_region`, `scope_stk.c:2700-2735`).
  `il_header.region_scope_entry[n]` heads a list (`sp->next`) of the function
  scopes in region `n`; for C this list always has exactly one entry.
* The file-scope region (`FILE_SCOPE_REGION_NUMBER`) holds all types, routine
  declarations, file-scope variables, local *static* variables, string constants,
  named constants, and anything referenced from more than one region
  (`doc/source/il.rst:49-135`). Function regions hold statements, expression
  nodes, non-static local variables (`a_scope::nonstatic_variables`),
  `a_dynamic_init`, `a_local_static_variable_init`, block scopes, labels.
* `free_memory_region` (`mem_manage.c:1185-1223`) frees the blocks and nulls
  `mem_region_table[n]` and `region_scope_entry[n]`. It does **not** clear
  `il_header.function_def_table[fdn].scope`, which therefore dangles.
  `finish_function_processing_for_function_def` (`scope_stk.c:8881-8905`) guards
  with `mem_region_table[...] == NULL`; any new reader of
  `scope_for_function_def` / `scope_for_routine` after an early free must do the
  same (the file path avoids this because `il.c:11401-11412` reloads the
  region; that arm is `IL_SHOULD_BE_WRITTEN_TO_FILE && BACK_END_SHOULD_BE_CALLED`
  and `STANDALONE_*`, so it does not exist for CIR).
* `OVERWRITE_FREED_MEM_BLOCKS=1` in debug poisons freed blocks, so a premature
  read of a freed function region fails loudly in debug (V:
  `debug/base.cmakedef`, last line).

### 1.2 `check_for_done_with_memory_region` (`mem_manage.c:1404-1518`)

Current decision tree when `IL_SHOULD_BE_WRITTEN_TO_FILE` (V):

```
keep_memory = FALSE
#if !FREE_MEMORY_REGIONS_EARLY: function regions -> keep   (1447-1453)
if may_be_building_new_pch()          keep   (header_stop_position_pending, 1455)
else if in_secondary_trans_unit(scope) keep   (1462)
else if scope->do_not_free_memory_region keep (1469; generic lambdas)
else if skip_il_read                  keep   (1473; --il_display sets it, cmd_line.c:10671)
if (!keep && rout != NULL)            for each scope in the region's list:
    memory_region_should_be_kept_for_routine()  (1353-1398)
        keep_function_body_for_possible_inlining(rout)  (scope_stk.c:8721)
            -> rout->is_constexpr always; is_inline only with MINIMAL_INLINING
               (needs DO_IL_LOWERING) or ONE_INSTANTIATION_PER_OBJECT
        else rout->contains_local_class_type
        else (DO_IL_LOWERING) lambda with conversion function
        else !scope->function_body_processing_finished
        else (MAINTAIN_NEEDED_FLAGS) !keep_definition_in_il || !definition_needed
if (!keep) write_memory_region(n)  (1503)
then:  keep -> trim_memory_region, else free_memory_region
```

When `!IL_SHOULD_BE_WRITTEN_TO_FILE` the routine unconditionally keeps
(`:1436-1438`) and trims. `check_for_done_with_all_function_memory_regions`
(`:1522-1574`) is the end-of-TU sweep: for every still-live region
> `FILE_SCOPE_REGION_NUMBER` it writes (unless secondary-TU) and frees (unless
`skip_il_read`); its whole body is `#if IL_SHOULD_BE_WRITTEN_TO_FILE`.

Config matrix for the function-region decision (V from the code above):

| config | writes | frees early |
| --- | --- | --- |
| release `cpfe`/`cpfe-cp` (file=0) | never | never; freed after back end in `fe_wrapup_part_2` (`fe_wrapup.c:885`) |
| debug Linux (file=1, early=1, needed=1) | when keep tests pass | yes |
| debug macOS (file=1, early=0) | when keep tests pass | no; at end |
| **cpfe-cir (proposed)** | at the same point | yes, unless keep tests |

### 1.3 Where a C function becomes final (V)

`pop_scope_full` (`scope_stk.c:9875`), `sck_function` branch:

1. `scope_stk.c:10333-10349`: `should_delay_finishing_of_function_body` (always
   compiled, `:9538`; TRUE only for generic lambdas, members of incomplete
   classes, structured-binding lambdas: all C++) else
   `finish_function_body_processing` (`:8912`). With `DO_IL_LOWERING=0` this
   function does only bookkeeping (orphan lists, `empty_func_shareable_constants_table`)
   and sets `scope->function_body_processing_finished = TRUE` at `:8994`.
   The `#if DO_IL_LOWERING` arm at `:10331` (`should_delay_lowering_on_function`)
   does not exist.
2. `:10367` `set_routine_defined` (definition `il.c:27984`): sets
   `rout->defined`; with `MAINTAIN_NEEDED_FLAGS` also `remark_routine_definition_needed`.
3. `:10375-10378` the IL scope is marked off the stack
   (`depth_in_scope_stack = NO_SCOPE_DEPTH`), explicitly "before
   check_for_done_with_memory_region".
4. `:10381-10397` unless the body is discarded or the region is still on the
   stack (`old_region_still_needed`, computed `:10272-10285`): `set_block_scope_parents`
   then `check_for_done_with_memory_region`.

At that moment, for C: the body IL is complete, `innermost_function_scope` is
still the popped function (the state restore is in `pop_scope_stack_entry`,
later), block-extern type fixups have already been undone (`:10290-10312`, so
use *expression* types, not `variable->type`, for accesses), and no further FE
code writes this region.

**Proposed hand-off point:** exactly where `write_memory_region(region_number)`
sits today (`mem_manage.c:1503`), but guarded so that "stream" and "free" are
separate decisions (section 1.8).

### 1.4 Delayed finishing and the mangling gate (V/I)

* `should_delay_finishing_of_function_body` (`:9538`) is compiled in every
  configuration and already keeps those regions (via
  `!scope->function_body_processing_finished` in the keep test). It also sets
  `function_body_processing_delayed_on_some_func_in_primary_il`.
* `should_delay_lowering_on_function` (`:9388-9533`) enumerates the reasons a
  function may not be processed yet: secondary-TU present, member of an
  incomplete class, constructor needing an unprocessed field initializer,
  generic lambda, lambda-closure member, local-in-pending-closure, constexpr,
  `must_wait_for_mangling_reasons`, weak with multi-TU, nested delayed. Plus
  the module-id queue (`:9029-9100`, `:9477`). All are C++-only; none fires in
  C.
* `must_wait_for_mangling_reasons` (`:9148-9223`) tests: promoted entities from
  an unnamed class whose `discriminator == 0`; lambdas in structured-binding
  initializers; parentless lambdas in default arguments. This is precisely the
  "mangled name not final yet" predicate the plan's
  `ecil_mangled_name_is_final()` wants.
* Proposal for M3 (not needed for C): widen the outer guard to
  `#if DO_IL_LOWERING || BACK_END_TYPE == BACK_END_TYPE_CIR`, rename the entry
  to a neutral `should_delay_finishing_for_back_end` macro, and replace the two
  `lowering_delayed_on_nested_function` accesses by a writer-owned bitmap
  indexed by `function_def_number` (dense, `highest_used_function_def_number`;
  `il_header.function_def_table`, sized in `mem_manage.c:895-910`). `il_def.h` stays
  untouched. Delayed functions flow into `finish_processing_for_function_bodies`
  (`fe_wrapup.c:725`, defined `:645-668`), which calls
  `finish_function_processing_for_function_def(n, inline_pass)`; the region is
  then swept by `check_for_done_with_all_function_memory_regions`
  (`fe_wrapup.c:593`).

### 1.5 Late finishing (lambdas, local classes) (V/I)

`finish_function_processing_for_function_def` (`scope_stk.c:8881`) is called
from `fe_wrapup.c:660` (end of TU), `scope_stk.c:9081` (module-id queue drain,
mid-TU), `finish_local_function_body_processing` (`:8858`, local class
members before the enclosing function) and `class_decl.c:35405-35418`
(`finish_lambda_routine_processing`, when the closure completes; mid-TU and
usually while the *enclosing* function's region is still on the scope stack).
It finishes the body but never calls `check_for_done_with_memory_region`.

Proposal: at its end, `#if IL_REGIONS_DISPOSED_INCREMENTALLY`, if the function's
region is not on the stack (reuse the exact pattern of
`header_stop_no_longer_pending`, `pch.c:1782-1789`:
`sp->depth_in_scope_stack != NO_SCOPE_DEPTH` means still active) call
`check_for_done_with_memory_region(region)`. For a lambda inside an active
function the call is skipped and the outer `pop_scope` finishes the region
together with its lambdas (one `F_REGION` frame = the function plus its lambdas,
as in the plan). End-of-TU calls need nothing extra because the sweep follows.

### 1.6 `remove_unneeded_instantiations` / `clear_function_body` (V)

`templates.c:20999-21060` runs from `wrap_up_file_scopes` (`fe_wrapup.c:728-731`,
only `instantiation_mode != tim_all`). It calls `clear_variable_definition` or
`clear_function_body` (`il.c:30245-30330`) for instantiations made only to learn
a size or a deduced return type. `clear_function_body` resets the routine to
undefined, nulls `function_def_table[fdn]`, unlinks the scope and frees the
region if it was the last function in it. C++ only; in C mode
`remove_unneeded_instantiations` is a no-op in practice. For C++ (M3) a
function already streamed needs `RETRACT(gid)`; the cheap way is a hook at the
top of `clear_function_body`: `if (ecil_function_was_streamed(rp)) ecil_retract(rp)`
(`rp->function_def_number` indexes the bitmap).

### 1.7 End-of-TU order (V: `fe_wrapup.c`)

```
fe_wrapup() 749
  template_and_inline_entity_wrapup()            768   C++ only
  wrap_up_file_scopes() 672 (called 796)
    part_1 (secondary TUs, then primary)         names, wrapup_scope, clean_up_interpreter 386
    part_2                                       needed flags (MAINTAIN only)
    part_3 for secondary TUs
    copy_secondary_trans_unit_IL_to_primary      only multi-TU
    finish_processing_for_function_bodies        725
    remove_unneeded_instantiations               730
    part_3 for primary (545-640):
        pop_scope() of file scope
        needed flags / do_final_name_mangling   569-577 (MANGLE_ALL_NAMES)
        file_scope_il_wrapup_remove_unneeded_il  581 (MAINTAIN only)
        check_for_done_with_all_function_memory_regions()   593
        check_expected_errors()                  (CHECKING)
        check_for_done_with_memory_region(file_scope_region_number) 634
  do_il_display                                  804
  finish_il_file                                 810  (file mode)
  free_all_memory_regions or FRONT_END only      843-855
fe_wrapup_part_2 865: close_il_output_file, write_signoff, free_all_memory_regions 885
```

Proposed CIR placements:

* `:593` stream every remaining unstreamed function region (the unchanged sweep,
  with `hand_off_memory_region` in place of `write_memory_region` and a skip for
  already-streamed regions).
* `:634` hand-off of the file-scope region emits `F_GLOBALS`; the region is then
  freed (or kept for `--ecil_keep_regions` / `--il_display`).
* `:810` emits `F_END` (or `F_ABORT` if errors).
* `:843-855`: in CIR mode take the file-mode arm (`free_all_memory_regions()` after
  the stream is complete), not the "free front-end storage only" arm.
* `back_end()` in `cir_be.c` finishes the sink (`edg_cir_session_finish` for the
  in-process consumer); `cfe.c:170` only calls it when no errors
  (`suppress_back_end`, `cfe.c:153`), so the error path must send
  `F_ABORT` itself (below).

### 1.8 Proposed macro and `hand_off_memory_region` design

```c
/* host_envir.h (cir-config owns), after IL_SHOULD_BE_WRITTEN_TO_FILE: */
#if IL_SHOULD_BE_WRITTEN_TO_FILE || BACK_END_TYPE == BACK_END_TYPE_CIR
#define IL_REGIONS_DISPOSED_INCREMENTALLY TRUE
#else
#define IL_REGIONS_DISPOSED_INCREMENTALLY FALSE
#endif
#if IL_SHOULD_BE_WRITTEN_TO_FILE && BACK_END_TYPE == BACK_END_TYPE_CIR
 #error -- CIR streams ECIL, not the IL file
#endif
#if BACK_END_TYPE == BACK_END_TYPE_CIR && !FREE_MEMORY_REGIONS_EARLY
 #error -- CIR needs FREE_MEMORY_REGIONS_EARLY
#endif
```

```c
/* mem_manage.c, replaces the two write_memory_region call sites: */
#if IL_SHOULD_BE_WRITTEN_TO_FILE
#define hand_off_memory_region(n)  write_memory_region(n)
#else  /* CIR */
#define hand_off_memory_region(n)  ecil_hand_off_region(n)
#endif
```

`ecil_hand_off_region(n)` (in `ecil_write.c`):

1. returns immediately if the stream is in the aborted state, or
   `is_at_least_one_error()`;
2. `n == FILE_SCOPE_REGION_NUMBER` -> emit pending `TY_RECORD_BODY`s that became
   complete, then `F_GLOBALS` (variable definitions incl. tentative -> zero,
   file-scope asm/ctors diagnosed unsupported in v0);
3. else for each scope `sp` in `region_scope_entry[n]` whose routine is defined
   and not already streamed (bitmap by `function_def_number`) and not
   filtered: set `innermost_function_scope = sp`, `curr_il_region_number = n`
   (as `il_walk.c:664-690` does; `get_variable_initializer`,
   `il.c:17279`, requires one of them), build the frame (decl section + body
   section), `sink->submit`, set the streamed bit;
4. restore the saved globals. It must not call anything that marks entities
   referenced/needed or instantiates templates (the FE does not expect IL
   mutation during hand-off).

`check_for_done_with_memory_region` becomes:

```
compute keep_memory exactly as today (+ keep if ecil_keep_regions)
if (ready_to_stream(scope-list) && !keep_for_unfinished) hand_off_memory_region(n)  /* stream even if kept */
if (!keep_memory) free  else trim
```

where "ready" = `function_body_processing_finished` and `rout->defined`. For C
that is true whenever the function is popped normally. Constexpr (C++) streams
immediately and stays resident; the sweep skips it by the bitmap.

* `--ecil_keep_regions` (new global in `ecil_write.c`, option in `cmd_line.c`
  owned by cir-config) plus `--il_display`: `skip_il_read` only exists under
  `IL_SHOULD_BE_WRITTEN_TO_FILE` (`cmd_line.h:527-539`); introduce
  `#define ecil_keep_regions_flag` and OR it into `mem_manage.c:1473`,
  `:1567`, `fe_wrapup.c:844`; `cmd_line.c:10671` sets it for `--il_display`.
  `do_il_display` (`il_display.c:8838-8842`) already walks only regions with
  `mem_region_table[n] != 0` in the non-file arm, so it degrades gracefully.
* Error path: `error.c:4970-4974` `cancel_il_file()` is the model. Add a CIR arm
  calling `ecil_abort()` once when `total_all_errors > 0`; also on
  `term_compilation` for catastrophic errors. After abort the writer is a no-op.
  Rationale: functions streamed before the first error are already with the
  consumer, so the stream must be explicitly invalidated.
* `COMPILE_MULTIPLE_SOURCE_FILES` loop (`cfe.c:116-119`): one stream per compilation;
  `ecil_start_translation_unit()` where `start_il_file()` is called
  (`fe_init.c:2055-2066`), `ecil_cleanup()` in `fe_cleanup`
  (`fe_wrapup.c:905-907`).

### 1.9 Every `#if IL_SHOULD_BE_WRITTEN_TO_FILE` and what to do with it (V)

Change to `IL_REGIONS_DISPOSED_INCREMENTALLY` (region lifecycle only):

* `mem_manage.c:1351-1401` (`memory_region_should_be_kept_for_routine`),
  `:1417-1420`, `:1436-1505` (`check_for_done_with_memory_region`),
  `:1533-1574` (`check_for_done_with_all_function_memory_regions`).
* `fe_wrapup.c:843-855` (final free vs front-end-only free).

Add a CIR arm next to the file arm:

* `fe_init.c:2055-2066` (`start_il_file` -> `ecil_start_translation_unit`),
  `fe_wrapup.c:808-811` (`finish_il_file` -> `ecil_finish`),
  `fe_wrapup.c:874-877` (`close_il_output_file` -> sink close),
  `fe_wrapup.c:905-907` (cleanup), `error.c:4970-4974` (abort),
  `cmd_line.c:10671` (`--il_display` keep flag), `cmd_line.c` option table
  and `--dump_configuration` lines for the new macro
  (`cmd_line.c:8312-8316` pattern).

Stay as is (file-format specific): `mem_manage.c:23-28` (includes),
`:980-990`, `:2044-2046`, `:2172-2180` (`index_for_il_file`), `:1934-1937`
(`entry_number` debug print), `mem_tables.h:177-206`, `:266-281`
(`entry_written`/`entry_number`), `cmd_line.c` IL-file options (`:31`, `:2544`,
`:6605`, `:10703`, `:12685`, `:12986-13015`, `:13192`, `:13261`),
`cmd_line.h:527`, `exprutil.c:10057-10064` and `lexical.c:24387-24394` ("IL
reading cannot deal with shared template argument lists"), `il_read.*`,
`il_write.*`, `il.c:31`, `il.c:11401`, `il_walk.c:2264-2276`, `:2381-2483`
(file-write walkers), `fe_init.c:48-51`, `:155-161`, `:1680-1728`, `:1775-1777`,
`c_gen_be.c`, `cp_gen_be.c`, `il_display.c:47-50` (includes).

### 1.10 Other code that assumes regions are never freed when the IL is not on a file

* **Back ends** (`c_gen_be.c`, `cp_gen_be.c`): not in the CIR config.
* **`do_il_display`**: handled above (`skip_il_read` is set by `--il_display`).
* **PCH.** `may_be_building_new_pch()` already postpones free; regions finish
  through `header_stop_no_longer_pending` (`pch.c:1761-1795`, called from
  `lexical.c:7092`, `preproc.c:5317`, `pch.c:2481`) in region order, so streaming
  from the hook works for *creating* a PCH. Streaming does not need to wait for
  that (it does not mutate IL). *Using* a PCH loads regions that never pass
  through `check_for_done_with_memory_region`; v0 should reject `--pch`/`--use_pch`
  with a clean diagnostic, and a later change must sweep loaded function regions.
* **Modules** (`create_module_unit`, `modules_write_out`, `cfe.c:132`): reads
  the whole IL before `fe_wrapup`. C has no modules; CIR must refuse module
  creation (or force keep-regions) for C++20 (M3+).
* **Constexpr interpreter / `keep_function_body_for_possible_inlining`**: handled
  by the keep tests (constexpr bodies kept); `clean_up_interpreter`
  (`fe_wrapup.c:386`) runs in part 1, before the sweep.
* **Inlining**: `MINIMAL_INLINING` requires `DO_IL_LOWERING`
  (`host_envir.h:1874-1876`): not applicable.
* **Template instantiation from a body**: templates are instantiated from token
  caches (FE storage), not from IL bodies; except prototype instantiations
  (`function_body_should_be_discarded`, `scope_stk.c:8765-8770`) and generic
  lambdas (`do_not_free_memory_region`, `:9725`), which keep their regions.
* **Multiple translation units** (`COMPILE_MULTIPLE_TRANSLATION_UNITS`): off in
  `cpfe-cp` (P: `--dump_configuration`), on for the Linux `cpfe` target only
  (`support/platform/linux/cpfe.cmakedef`). CIR must set 0; then
  `in_secondary_trans_unit` is always FALSE.
* **`check_expected_errors`** (`fe_wrapup.c:628`) needs the file-scope region at
  `:634`; the hand-off of the file-scope region must follow it. It does.
* **`il_header.function_def_table[].scope` dangling** after early free: see 1.1.
* **`ODR` / correspondence** (`trans_corresp.c`) and `eliminate_*` passes:
  `MAINTAIN_NEEDED_FLAGS` or multi-TU only.

### 1.11 Writer-visible facts that can still change after a function is streamed

For C (all P): variable *definitions* complete at end of TU (tentative
`int x;`, a later `int x = 5;`, `int a[]` completed by `int a[10]`); routine
declarations can gain a body or a different prototype; incomplete struct types
can be completed later. The design handles these by (a) on-demand `G` decls using
the type known when first referenced, (b) completing `TY_RECORD_BODY` as soon as a
pending record becomes complete (cheap scan of a pending list at each hand-off)
or at `F_GLOBALS`, (c) `F_GLOBALS` carrying the final variable definitions.
The consumer must therefore accept a `G` var/fn whose type at definition differs
from its first-use type (clang does the same: replace the global). For calls the
writer uses the *callee expression's* function type (as typed in the call), not
the final declaration.

## 2. Mangling

### 2.1 Which getters exist and are safe mid-TU (V/P)

Declared in `lower_name.h:68-79` under
`TEMPLATE_LOOKUP_NEEDED || MICROSOFT_EXTENSIONS_ALLOWED || MODULE_ID_NEEDED`;
`MODULE_ID_NEEDED` is TRUE whenever `NEED_NAME_MANGLING` is (`host_envir.h:3903-3910`),
so with the explicit `NEED_NAME_MANGLING=1` they all compile. They are in the
existing `cpfe-cp` binary (P).

| getter | file:line | mutates entity? |
| --- | --- | --- |
| `get_mangled_function_name_full` | `lower_name.c:13571` | no; returns existing `scp->name` if `name_has_been_mangled`, else a *temporary buffer* (`end_mangling`) |
| `get_mangled_function_name` | `:13668` | wrapper (primary ctor/dtor) |
| `get_mangled_variable_name` | `:13980` | no (same rule) |
| `mangled_vtbl_name`, `mangled_typeinfo_name`, `mangled_class_name`, `mangled_virtual_table_table_name` | `lower_name.h:88-111` | computed on demand |

Notes:

* The returned pointer is into a mangling text buffer (`push_mangling_text_buffer`,
  `lower_name.c:1140`): the writer must copy it into its string table at once.
* Everything involving externalization (`needs_to_be_externalized`,
  `start_externalized_name`, `module_id_for_source_corresp`,
  `mangle_promoted_entity_name`, `externalize_mangled_name`) is under
  `DO_IL_LOWERING` and compiles away; with DO_IL_LOWERING=0 statics are never
  promoted/renamed by the FE.
* `calculate_implicit_abi_tags` (GNU abi_tag) can set flags while mangling
  (`variable_name_mangling_needed`, `:14419-14456`); low risk, checked by the
  verification pass below.

### 2.2 What C needs

C needs no getter at all (V):

* `name_mangling_needed()` is `(!C_mode())` without lowering
  (`lower_name.h:31-35`), and `do_all_name_mangling` runs only under
  `MANGLE_ALL_NAMES`.
* External C entities have `name_linkage == nlk_external`
  (`il_def.h:511-519`) and `function_name_mangling_needed` returns FALSE for them
  (`lower_name.c:13314-13340`); `main` is explicitly excluded.
* Use `source_corresp.name`, except `asm_name_is_valid` (GNU `__asm__("x")`,
  `a_variable`/`a_routine` `asm_name_or_reg.name`) which replaces it.
* `static` file-scope entities keep `nlk_internal` and the plain name; LLVM
  `internal` linkage resolves duplicates across TUs.
* Local static variables need a unique symbol: the FE never renames them
  without lowering. Writer rule: `<function name>.<variable name>` plus a per-TU
  counter on collision (deterministic: first-introduction order). Same for
  compound-literal statics and static `__func__`/string temporaries
  (`.str.N`, private).
* Names are final at first reference in C; there is no deferred `NAME_BIND` in v0.

### 2.3 C++ later: when is a name final?

Final iff none of the `must_wait_for_mangling_reasons` conditions hold for the
entity or any parent (unnamed-class discriminator assigned, lambda parent known,
structured-binding names known), which in practice is "when its declaration and
the enclosing class are complete". The same predicate gates a variable's name.
Deduced-return-type functions only need the body for the *type*, not for
Itanium encoding of non-template names. Plan unchanged: sid in the decl when
final, else `NAME_BIND` at `F_NAMES`. Add a CHECKING pass in the writer
(`ecil_verify_names`): at wrap-up recompute each streamed entity's name through
the same getter and assert equality with what was streamed. That catches any
premature name.

### 2.4 `MANGLE_ALL_NAMES` 0 versus 1

`MANGLE_ALL_NAMES=1` is legal without lowering (the `#error` after
`host_envir.h:1787` is one-directional). Then `fe_wrapup.c:358-366` runs
`do_all_name_mangling` at the start of wrap-up and `:569-577`
`do_final_name_mangling`. These mutate `scp->name` in place at end of TU, after
the functions were streamed, and `do_final_name_mangling` may *compress or
truncate* names (only when `DEFAULT_COMPRESS_MANGLED_NAMES` / a max length is
configured; both are off in `cpfe-cp`, P). Cost: a second full mangling pass,
plus a hazard that names change after use. **Recommend 0**: names are computed
once per entity by the non-mutating getters when first introduced and cached in
the writer's id table. `MANGLE_ALL_NAMES=1` has no benefit for C and only the
"cached lookup" benefit for C++.

## 3. `ecil_id` in the IL entry prefix versus a pointer hash table

Layout (V, `mem_tables.h`):

```
an_il_entry_prefix { file_scope:1; secondary_trans_unit:1; il_walk_flag:1;
  il_lowering_flag:1; [keep_in_il:1 if MAINTAIN]; [entry_written, entry_number
  if file && ALTERNATE]; [alloc_seq_number if MAINTAIN_ALLOCATION_SEQUENCE_NUMBER];
  [file_scope_region_number, region_number, magic_number if EXPENSIVE_CHECKING] }
SPACE_FOR_IL_ENTRY_PREFIX = round_up(sizeof, HOST_IL_ENTRY_PREFIX_ALIGNMENT (8))
```

* `clear_il_entry_prefix` (`mem_tables.h:333-342`, called from
  `il_alloc.c:200`, `:278`) is the single init point; every IL allocation passes
  through it. Adding `clear_ecil_id(epp)` there resets the id of a reused block,
  so there is no stale-id problem when freed blocks are recycled (blocks are
  reused: `reusable_blocks_list`).
* Cost on 64-bit hosts without `EXPENSIVE_CHECKING`: 4-byte prefix padded to 8, so a
  `uint32_t ecil_id` is free. With `EXPENSIVE_CHECKING` (debug) the struct is
  larger anyway.
* Ids: `file_scope` bit in the same prefix distinguishes G-ids (dense per TU)
  from L-ids (dense per frame); both fit 32 bits; 0 = unassigned.
  Types (including typeref entries, caching the canonical id), routines,
  variables, fields (`idx+1` inside the record), labels get ids; constants,
  expression nodes and statements do not.
* A pointer hash table needs explicit erase on region free, hits a random cache
  line per type/variable reference (the id sits next to the entry the walker
  already reads), and cannot detect block recycling without epochs.

**Recommendation: prefix field, gated** (`#if BACK_END_TYPE == BACK_END_TYPE_CIR`
via a derived `ECIL_ID_IN_IL_PREFIX` macro in `mem_tables.h`; `mem_tables.h` is
writer-owned). In other configs the struct is unchanged, so the cpfe/cpfe-cp
preprocessed identity is preserved. Add `static_assert(sizeof(an_il_entry_prefix)
<= SPACE_FOR_IL_ENTRY_PREFIX)` and, for release CIR builds,
`static_assert(SPACE_FOR_IL_ENTRY_PREFIX == 8)` to catch accidental growth.
Fallback if a host has 4-byte pointer alignment: use the hash table.

## 4. `MAINTAIN_NEEDED_FLAGS` 0 versus 1

What the flag does (V):

* Adds `needed`/`definition_needed`/`keep_definition_in_il` bits to entities
  (`il_def.h:3370`, `:10576`, `:12778`), `keep_in_il` to the prefix
  (`mem_tables.h:165`), `okay_to_walk_subtree_of_local_entity`.
* Wrap-up: `set_needed_flags_at_end_of_file_scope`, `mark_to_keep_in_il`, and (if
  `DEFAULT_REMOVE_UNNEEDED_ENTITIES`, which defaults TRUE for every non-C++-gen
  back end when needed flags are on, `host_envir.h:1129-1137`)
  `eliminate_bodies_of_unneeded_functions` + `eliminate_unneeded_il_entries`
  (`fe_wrapup.c:497-519`). That prunes unreferenced static functions/variables
  and unneeded types before the back end sees them.
* Region retention: a function region is kept after pop unless
  `keep_definition_in_il && definition_needed` (`mem_manage.c:1383-1390`).
  External definitions become needed at `finish_function_body_processing`
  (`routine_needed_even_if_unreferenced`, `scope_stk.c:8998-9012`), so they are
  freed at pop. A `static`/`inline` function not yet referenced stays resident until some needed function
  references it (the sweep then calls `check_for_done_with_memory_region`,
  `il_walk.c:699-711`) or until the end of TU (`remove_unneeded_il`, then the
  `:593` sweep). Hence "static helper defined above its user" pins one region
  per helper until the user is finished, and unreferenced statics pin theirs to
  the end.
* Other dependents (V): `ONE_INSTANTIATION_PER_OBJECT` requires it
  (`host_envir.h:1168-1171`); `ORPHAN_PROCESSING_NEEDED`
  (`host_envir.h:1839`), which is TRUE anyway with `NEED_NAME_MANGLING=1`;
  `DEFAULT_REMOVE_UNNEEDED_ENTITIES` must be 0 when it is 0 (`:1140`); ~20 files
  have `#if MAINTAIN_NEEDED_FLAGS` arms (lower_il.c, trans_copy.c, templates.c,
  src_seq.c, ...), all lowering/IL-surgery.

Evidence for the recommendation:

| | 0 | 1 |
| --- | --- | --- |
| region pinning | none after pop (all function regions free at pop, apart from constexpr / PCH / debug keep) | static/inline pinned until referenced or end |
| FE work | none | per-body sweep (`mark_as_needed`/il_walk) of every function + end-of-TU passes |
| tested today with unlowered IL | release `cpfe-cp` default (`host_envir.h:1112-1118`: TRUE only for C-gen) | debug `cpfe-cp` (needed=1 via debug base) |
| early-free with it | only `windows-msvc-stl-testing` | day posture |
| dead static removal | consumer (`REFS`/LLVM GlobalDCE) or deferred-frame queue | FE (free) |
| IL pruning side effects on what the writer can see | none: all types/vars remain on lists | types/entities may be removed (writer must tolerate) |

**Recommendation: `MAINTAIN_NEEDED_FLAGS=0`.** It maximises memory release
(the whole point of streaming), removes a pass over every body, and keeps the
writer's view of the IL complete. The one thing it forfeits is FE-side dead
static elimination. Mitigations, in order of preference: (1) consumer
reachability using `REFS` (LLVM `GlobalDCE` also does it at `-O1+`); (2) cheap
writer-side filter: skip `static` entities whose `source_corresp.referenced`
bit is FALSE at the time of streaming (`referenced` is maintained regardless of
the flag, `il_def.h:3361`, conservative: it is TRUE if *any* reference exists,
even from unneeded code); (3) if frame bytes matter, an FE-side *deferred
frame queue* for internal-linkage functions: serialize immediately (frees the
IL region) but hold the small frame until a later `FN_REF` makes it live; drop
the rest at `F_END`. (3) is a v1+ option.

Fallback if (0) shows bugs: `MAINTAIN_NEEDED_FLAGS=1` works with the hook
unchanged, because the keep test already handles it; it is the debug-tested
combination.

C (M2) emission rules (V/P):

| entity | emit |
| --- | --- |
| external function definition (`nlk_external`, `defined`) | external strong |
| `static` function | internal (consumer drops if unreferenced) |
| C99 `inline` without `extern` (`definition_for_inlining_only`, `suppress_inline_body`, probe `cinl`) | declaration only in v0 (an external definition must exist elsewhere) |
| `extern inline` in C99, `static inline` | external / internal definition |
| GNU `extern inline` / `gnu_inline` | same flag handles both (`il_def.h:12849-12873`) |
| tentative definition (`storage_class` unspecified/static, `init_kind == initk_none`, not `sc_extern`) | definition with zero initializer at `F_GLOBALS`; v0 ignores `-fcommon` (`il_header.default_nocommon`, `a_variable::is_common`) and emits regular zero-init (clang `-fno-common` default) |
| `extern` declaration (`sc_extern`, `initk_none`) | `G` decl only if referenced |
| unreferenced `static` variable | writer filter (2) above |
| weak/alias/ifunc/section/visibility | linkage+supp keys; alias and ifunc unsupported in v0 |

## 5. Writer-side filters replacing `ignore_*_in_back_end` (`il.h:873-916`)

With `DO_IL_LOWERING=0` all four macros expand to `FALSE`
(`il.h:911-915`): the C++-gen back end sees prototype instantiations and
consteval functions. The writer must reimplement the lowering-mode set:

| entity | skip when |
| --- | --- |
| routine | `is_prototype_instantiation`, `special_kind_is(sfk_deduction_guide)`, `is_consteval`; plus: not `defined`, `is_deleted`, `is_ifunc`, `definition_for_inlining_only`/`suppress_inline_body` (body only), `is_constexpr_intrinsic`, `explicit_do_not_instantiate` without body |
| variable | `is_prototype_instantiation`, `is_nonreal`, `init_kind == initk_binding`, `suppress_inline_definition`, `is_template_param_object` (v0 diag) |
| constant | `kind == ck_template_param` |
| type | `type_is_nonreal(type)` (`il.h:855`), class `is_nonreal_class`/`is_prototype_instantiation`, **`is_tag_redefinition`** (C23; `il_def.h:9843`: "a back end that emits a definition for each type it is given should skip this one"), the `is_builtin_va_list` record if the consumer implements va_list natively |

All except `is_tag_redefinition` and the routine `is_defined/is_deleted` tests
are C++-only; they cost one predicate call `ecil_skip_*(entity)` and stay a
single place to extend. `il_header.prototype_instantiations_in_il`
(`host_envir.h:760`, always TRUE) shows the IL contains the prototypes.

## 6. DO_IL_LOWERING=0 and C-mode constructs (new FE combination)

Observed with the non-lowered IL (P, `--c11`, `cpfe-cp --il_display`; the same
`cpfe-cp` already compiles C with these settings today, so C + non-lowered is
not new for the *front end*; what is new is a *consumer* of it):

* **Config macros that become inert (V).** `lower_c99.c` and `lower_il.c` are
  entirely `#if DO_IL_LOWERING` (`lower_c99.c:25`), as is `lower_c99.h:17`.
  `LOWER_DESIGNATED_INITIALIZERS` (default TRUE, `targ_def.h:5239`) is read
  only by lowering and `c_gen_be.c`, so it is harmless but should be set to 0
  in `cpfe-cir.cmakedef` so `--dump_configuration` is honest. `LOWER_COMPLEX`,
  `LOWER_VARIABLE_LENGTH_ARRAYS`, `LOWER_CLASS_RVALUE_ADJUST` are already
  derived FALSE or `#error` if TRUE without lowering. `MINIMAL_INLINING` must be
  FALSE (`#error`, `host_envir.h:1874`). `COROUTINE_ENABLING_POSSIBLE` defaults
  TRUE without lowering (`host_envir.h:3884-3886`); the plan sets it 0.
  `REPRESENT_C11_GENERIC_CONSTRUCT_IN_IL` becomes TRUE (`:1465-1469`), so
  `enk_c11_generic` nodes appear; the writer emits only `.result`.
* **Designated initializers** stay as `ck_designator` in `ck_aggregate` lists
  (nested: `.i.b[1] = 5` becomes `{.b = {[1] = 5}}`), plus `ck_init_repeat` for
  `[a ... b] = x`; brace elision and conversions are already normalised by the FE
  (`{1,2,3,4}` for `int[2][2]` arrives as two inner aggregates; constants are
  converted to the field type: `(char)3`); missing trailing elements are
  implicit zero (`partial_aggr_value`). The writer needs its own normaliser
  (`lower_c99.c`'s is unavailable).
* **Compound literals**: `enk_temp_init` (lvalue) with `dik_constant` or
  `dik_nonconstant_aggregate`; at file scope a `sc_static` variable
  (`is_compound_literal`) plus an address constant.
* **`_Bool`**: integer type with `bool_type` set, `int_kind` unsigned char,
  size 1; conversions to it are `eok_bool_cast` (compiler generated).
* **Boolean controlling expressions are not normalised** (no
  `lowering_normalizes_boolean_controlling_expressions`): `if (p)`, `while (d)`,
  `!i`, `&&`, `||`, `?:`, `for` conditions arrive with raw int/pointer/float type;
  comparisons yield `int` in C. The writer inserts the to-bool conversion.
* **VLAs**: `array.is_vla`, `stmk_set_vla_size`, `stmk_vla_decl`,
  `scope->vla_dimensions`, `a_variable::is_vla`: unsupported v0.
* **Complex**: `tk_complex`, `eok_xconj`, `ck_complex`: unsupported v0
  (`LOWER_COMPLEX` is 0).
* **Block-scope externs and K&R functions** appear as described in
  `il_to_ecil_map.md`.

## 7. Corrections to the "cpfe-cir configuration" list

| plan item | status |
| --- | --- |
| `DO_IL_LOWERING=0`, `DOING_SOURCE_ANALYSIS=0` | OK (V: `host_envir.h:676-687`; `DOING_SOURCE_ANALYSIS && DO_IL_LOWERING` is an error, not the reverse) |
| `IL_SHOULD_BE_WRITTEN_TO_FILE=0` | must override the debug base (=1) |
| `FREE_MEMORY_REGIONS_EARLY=1` | must be set explicitly on macOS/Windows; `#error` if unset |
| `NEED_NAME_MANGLING=1`, `MANGLE_ALL_NAMES=0` | OK; `NEED_NAME_MANGLING` must be explicit |
| `MAINTAIN_NEEDED_FLAGS` | recommend 0 (and must override the debug base) |
| `GENERATE_SOURCE_SEQUENCE_LISTS=0` | already the default without `BACK_END_IS_CP_GEN_BE` (`host_envir.h:1554-1559`) |
| `AUTOMATIC_TEMPLATE_INSTANTIATION=0` | OK; `ONE_INSTANTIATION_PER_OBJECT` then must be 0 (default FALSE when not C-gen) |
| `COMPILE_MULTIPLE_TRANSLATION_UNITS` | add: must be 0; EXPORT can stay (inert) |
| `LOWER_DESIGNATED_INITIALIZERS` | add: set 0 for honesty |
| `ORPHAN_PROCESSING_NEEDED` / `SCOPE_ORPHANED_LIST_PROCESSING_NEEDED` | derived TRUE from `NEED_NAME_MANGLING` (`host_envir.h:1839-1860`); fine |
| `PCH` | add: reject `--pch` flags in v0 |
| `OVERWRITE_FREED_MEM_BLOCKS` | keep 1 in debug CIR builds (poisoning) |

## 8. Stack safety and the stress plan

Measured (P, debug `cpfe-cp`, macOS, `ulimit -s`): a function with 10,000 terms
`a+a+...` compiles with 8 MB; 20,000 segfaults; 50,000 needs between 16 MB and
32 MB; a 20,000-link `else if` ladder needs between 32 MB and 64 MB; both
crash with the 8 MB default. The recursion is in the FE (expression passes and
statement parsing), not in the planned writer. Consequences:

1. The "run under `ulimit -s 256`" stress must be two-process: generate the
   `.ecil` once from a FE run with a huge stack (or `ulimit -s unlimited`), then
   run `edg-cir` on the file under 256 KB. That validates the consumer only.
2. For the writer: guarantee by construction (no mutual recursion in
   `ecil_write.c`; verify with `-fcallgraph-info`/`cflow` in CI) and a debug
   watermark: `ecil_hand_off_region` stores the address of a local; every
   non-leaf writer function asserts `base - &local < 64 KiB` in CHECKING builds.
   Because the hook runs from `pop_scope` (shallow stack), the writer starts
   from a nearly empty stack even when the FE needed more while parsing.
3. Keep stress sizes at or below what the FE survives in CI (10k terms at 8 MB)
   for the in-process path, and use larger sizes only with a larger FE stack.
4. A cheap way to test writer depth independent of the FE: unit tests in
   `cir_src/test/writer` that build the IL for a 1M-deep chain through the FE's
   own IL constructors in a thread with a 256 KB stack (no parser involved).
   Only worth doing if the FE allocation helpers can be driven standalone; if
   not, rely on (2).

## 9. Open items for the orchestrator

* Decide whether `--pch` is rejected in v0 (recommended) or handled by a load-time sweep.
* `cir-config` owns `host_envir.h` (macro), `cmd_line.{c,h}` (options),
  `fe_init.c`; I need an `ecil_*` declarations header included from those three
  and from `mem_manage.c`, `scope_stk.c`, `fe_wrapup.c`, `error.c`. `error.c` is
  not in the writer's file list: the abort hook (`error.c:4970`) needs an owner
  (propose cir-config adds the 3-line CIR arm; I provide `ecil_abort()`).
* `mem_tables.h` field `ecil_id` plus the derived gate macro: writer-owned, will
  be added with the first M2 patch.
* `class_decl.c` is untouched in v0; the late-finish re-check lives in
  `scope_stk.c:8881` (writer-owned hook region).
