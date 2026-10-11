# IL to ECIL map for core C++ (M3a, cir-writer research)

Author: cir-writer, session 2, 2026-10-10. Status: input to gate M3a.
Subject: what the EDG IL actually contains for the C++ constructs of
`m3_ecil_v1_design.md` sections 1 to 5, which of that note's IL claims hold,
and what it omits. Evidence is `file:line` in the tree at the M2.1 working
state (commit 59a33cffa7 plus the uncommitted M2.1 changes); line numbers of
`src/ecil_*.c` are the current ones (the design note quotes pre-M2.1 numbers,
see 1.5). `src/` was **not** changed for this research.

## 0. Method and evidence base

- **Scratch tree.** `~/edg-cir-writer/.tmp-sweep/m3tree` on the box: a copy of
  `src/` with a patch script (`build/w/m3/hack.py` on the Mac, applied by
  `.tmp-sweep/rebuild.sh`) that
  - removes the "C++ is not supported yet" error (`cmd_line.c:12952`),
  - logs, at hand-off time, the name that `get_mangled_function_name_full` /
    `get_mangled_variable_name` return for every routine whose body is sent
    (`M3 def`), every function-local variable (`M3 vlocal`), the region
    lifecycle (`CHECKDONE`, `HANDOFF`, `FREE`), the `remove_unneeded_instantiations`
    decisions (`REMOVE`) and the late-finish hook (`LATEFINISH`),
  - re-computes every logged name at the file-scope hand-off, when the FE
    is still in the front end, and prints `M3FINAL same|DIFF old -> new`.
    This is the "name finality" experiment of item 1.
  - adds small probes (ctor/dtor kind override, local-static naming helper,
    lambda entry point, NSDMI, parameter-passing flags, linkage flags).
- **Corpus.** 921 C++ tests of `tests/tests/imported/gnu/cpp` selected by
  `build/w/m3/cpp_list.py` (no `#include`, no line markers, none of
  virtual/throw/try/catch/typeid/dynamic_cast/co_*/noexcept(). Run with
  `--instantiate=used` (see O2). 414 compile without an error (rc 0), 495 stop
  on an FE error (unsupported GNU or library constructs), 11 are module tests
  (rc 4), one aborts in a probe (see 2.1). The 414 clean tests hand off 1809
  function bodies and 54 function-local variables.
- **Unlowered IL.** `--il_display` needs a DEBUG FE, so IL dumps come from the Mac
  `build/clang/bin/cpfe-cp` (same IL as cpfe-cir for these constructs;
  `DO_IL_LOWERING=1` there but the dump is taken before lowering), viewed with
  `build/w/m3/ilview.py`. Snippets are in `build/w/m3/snips/`.
- Box usage stayed at single-file FE runs, except one 921-file name sweep run
  before the orchestrator's request for light use (two runs, `-j` 1 each).

## 1. Verification of the design note (sections 1 to 5)

Legend: OK = confirmed in the IL; CHANGE = true but with a necessary change;
WRONG = contradicted; NEW = a fact the note does not mention.

### 1.1 "Findings at the pin" that are statements about the IL

| Claim | Verdict | Evidence |
|---|---|---|
| Lambda static invokers have no body in the IL (`sfk_lambda_entry_point`, func_def.c:2626-2721) | OK, with detail | The entry point is the routine after the conversion function in the closure's routine list, declared non-inline and non-definition (class_decl.c:35617-35636), named `_FUN` (class_decl.c:35445), `function_def_number == 0`, `defined == 0`, `storage_class == sc_static`, `variant.lambda_call_operator` set (probe: `get_mangled_function_name_full` gives `_ZZ3lamvENUliE_4_FUNEi`, `fdn=0 defined=0 storage=2 static=1 call_op=set`). The conversion function itself has a body `return <enk_routine entry point>` (probe on `lam`). For generic lambdas the entry point is an instance found through the template list (func_def.c:2653-2722). See 2.6 |
| `remove_unneeded_instantiations` only affects linkonce candidates (templates.c:20999-21060) | OK for routines, but the hook the note assumes does not see the routine | See 2.3: in the CIR configuration the region of a streamed function is already freed, `scope_for_routine_or_null` returns NULL (templates.c:21056) and nothing is cleared or reported. Variables are cleared before the file-scope hand-off, so no RETRACT is needed for them |
| `use_comdat` / linkonce_odr is available | WRONG | `use_comdat` is set only by lowering (lower_il.c:10065 `put_routine_into_comdat_group`; il_alloc.c:3090 initial FALSE); the writer must compute it, see 2.5 and O6 |

### 1.2 Section 1a (opcodes) and 1b (flags and enums)

| Claim | Verdict | Evidence |
|---|---|---|
| `STRUCTOR`: "the C1/C2 names differ in exactly one character" | OK | Probe (ctor/dtor kind override in `mangled_function_name`): `_ZN1AC1Ev`/`C2Ev`, `_ZN1AD1Ev`/`D2Ev`/`D0Ev`, inheriting ctor `_ZN1DCI11BEi`/`CI21BEi` (names6 probe). EDG's `cdk_delegation` gives a non-Itanium `C9`/`D9`: never ask for it. The routine for C1 is the only routine EDG has in this configuration (the alternate entry points are created by lowering, lower_name.c:13123 `#if IA64_ABI && DO_IL_LOWERING`), so the C2/D2/D0 names need the helper of 2.2 |
| MEMINIT kind order "vbase=0/base=1/field=2/delegation=3 (EDG `cik` order)" | OK | il_def.h:16866-16872 (`cik_virtual_base_class`, `cik_direct_base_class`, `cik_field`, `cik_delegation`) |
| `MEMINIT ... flags{compiler_generated, default_member_init}` | OK, but the default-member-init case needs more than a flag | `use_field_initializer` entries have `initializer == NULL`; the initializer is `field->initializer` (decl_inits.c:9700-9715; used so by lower_init.c:9888, 16173, interpret.c:23738) and is written in terms of `enk_param_ref` nodes where `param_num == 0` means `this` (il_def.h:15610-15616), which the writer must map to the ctor's `this` PARAM. Some ctors carry their own copy (`init_is_ctor_dependent`, il_def.h:12015) |
| `NONTRIVIAL_ABI = !construction_by_bitwise_copy_allowed \|\| has_deleted_or_nontrivial_destructor` (il.c:10778-10785) | CHANGE | Matches the per-parameter flag `passed_via_copy_constructor` (il.c:10779) and agreed with Itanium in 11 of 12 probed classes (T, user copy ctor, user move ctor, dtor, defaulted-copy-with-dtor, defaulted trivial, ...). It differs for a class whose copy and move constructors are all deleted (`struct DEL { DEL(const DEL&) = delete; }`): EDG 0, Itanium "non-trivial for the purposes of calls" 1. Such a class is only passable as a C++17 prvalue, so the case is rare but real (O5) |
| `data_size` = Itanium dsize (`data_size_of_type`, types.h:824) | OK (not probed) | declaration present; the layout comparison is cir-lib spike 7 |
| `REC_FIELD BASE` / POTENTIALLY_OVERLAPPING | OK | `base_class` list and `is_optimized_empty_class` (il_def.h:12045); constructor-init entries reference `a_base_class` (il_def.h:16907) |
| References are `TY_PTR`, `eok_ref_indirect` is `LOAD DEREF` | OK | t4/t6 probes: reference variables have pointer types (`eok_reference_to` / `eok_ref_indirect` with `tk_pointer` operations) |
| `this` is `PARAM(THIS)`; `this_param_variable` | OK | `scope.variant.routine.this_param_variable`, `is_this_parameter=TRUE` in every member probe. Note field initializers use `enk_param_ref(0)` instead (above) |

### 1.3 Section 1c (normative semantics) as IL facts

| Claim | Verdict | Evidence |
|---|---|---|
| Cleanups: emit `CLEANUP_BEGIN ... CLEANUP_BODY` right after complete construction | OK | `stmk_init` follows `stmk_decl`; the destruction is the `dynamic-init` on the variable with `destructor` and `lifetime` (il_def.h:4007, 4020). Order of IL `destructions` lists is reverse order of the tree, which is also reverse construction order for the L-to-R evaluation of arguments and operands (probe u1/u2: both temporaries are `unordered=TRUE` but the list is `A(2)` then `A(1)`) |
| `unordered` entries need a pre-scan (fallback of spike 3) | NEW: not needed | With `CLEANUP_BEGIN` after each construction the library gets reverse construction order for free and `unordered` (il_def.h:4078) is only a statement that the IL list is not authoritative. The writer must emit the temporaries in clang's evaluation order, which is the IL's `eval_left_to_right` order (every call/operation printed `eval_left_to_right=TRUE`) |
| Conditional cleanups: ternary arms and `&&`/`||` are conditional regions | OK | `inside_conditional_expression=TRUE` is set on the `dynamic_init` of temporaries under `?:` (t2, u3 probes; il_def.h:4071-4074) |
| Loop conditions: writer rewrites condition variables | OK, see 2.8 | `enk_condition` at the top of `stmk_if/while/for/switch` (il_def.h:13588) |
| NRVO "with a writer-explicit nrvo bool and a `!flag` guard in the cleanup" | CHANGE (simpler) | `return_value_variable` is set only if every return statement returns the same nonstatic variable of the function's top scope (expr.c:52277-52321); in M3 (no EH) that variable is never destroyed by the callee, so no flag or guard is needed. See 2.8 |
| RETRACT only on internal or linkonce_odr | OK for routines | Only template instances are removed (templates.c:21049-21057); see 2.3 |
| NAME_BIND predicate "mirroring `must_wait_for_mangling_reasons`" (scope_stk.c:9166-9239) with four reasons | CHANGE | The four listed reasons are exactly that function, but it answers "can *lowering* wait", it is not "will the name change": it only fires for routines that have local scopes/types/variables or are ctors/dtors (scope_stk.c:9177-9187) because in lowering configurations all other names are computed at the end. The writer computes every name at hand-off, so the predicate must be broader, and there is a fifth reason (module id). See 2.1 |

### 1.4 Sections 2 (worked encodings) that depend on IL shapes

- (1) loop with local and break, (2) full-expression temporary, (3) conditional
  temporary: consistent with t2, t3, t5 (IL `enk_object_lifetime` wraps the
  full expression, `child_lifetime` of kind `olk_expr_temporary` holds the
  temporaries; a block lifetime holds block variables).
- (4) ctor with base, member, NSDMI: consistent, plus the NSDMI source and
  `compiler_generated`/`use_field_initializer` details above. In the IL the
  list order is base first, then fields in declaration order (ctor1 probe:
  `B(1)`, `mm`, `k` (NSDMI), `a` (NSDMI), `arr`). Trivially-constructible
  members without NSDMI have no entry.
- (5) destructor body: the cleanups the note wants come from the
  `constructor_inits` of the dtor scope, which lists the members in reverse
  declaration order and then the direct bases, each as `dynamic-init
  kind=dik_none destructor=<~T>` (`~D` probe). Fields of array type appear
  once with the element destructor (the writer needs `ARRAY_DTOR`).
- (6) guarded local static: IL gives `local_static_variable_inits`
  (il_def.h:18542) with a `dynamic-init` per static local; the variable's
  `stmk_decl` has `decl.has_static_or_thread_variable`; its `stmk_init`
  statement carries the same dynamic init (st1 probe). Name: see 2.2.
- (7) namespace scope: **one** list. `scope.dynamic_inits` (il_def.h:18533) is
  set only on the file scope; variables of all namespaces and static data
  members appear on it in source order (ns1 probe: `a1, g1, a2, b, c, g2` for
  `N1::a1, ::g1, N1::a2, N2::b, N2::In::c, ::g2`). Template static data
  members and inline variables are on the same list, in instantiation order,
  with `unordered=FALSE` (st1 probe): the unordered/guarded decision must be
  made from `is_template_variable` / `is_inline`, not from the flag.
- (8) `new A[n]`, `delete[]`: see 2.7, with a NEW fact about `routine`.
- (9) capturing lambda: not probed beyond name checks.
- (10) templates twice: confirmed that an instantiation is handed off in its own
  region before the end of the TU, with its body (rm probe).

### 1.5 Section 5 line references

`src/ecil_*.c` numbers in section 5 are stale (M2.1 moved them). Current:
`emit_function` ecil_code.c:2698, `do_stmt` 2379 (`stmk_init` 2413, `stmk_for`
2491, `stmk_return` 2592), `do_expr` 1919, `do_call` 1229, `do_init` 2142,
`emit_record_fields` ecil_type.c:786, `finish_function_type` 675,
`ecil_function_id` 1656, `ecil_emit_globals` ecil_const.c:1532,
`ecil_hand_off_region` ecil_write.c:1979, `ecil_stream_finish` 2012. The
`il_def.h` numbers quoted (4007, 4020, 4078, 4088, 16738, 18433, 18533,
18542) are correct.

## 2. The ten items

### 2.1 Name finality predicate

**Question.** After a function is handed off (and its region freed), can the
name that `get_mangled_function_name_full` returned change?

**Experiment.** Name recorded at hand-off vs recomputed at the file-scope
hand-off, 1809 bodies in 414 clean tests: **3 definitions change (0.17%),
all in three shapes**; the 11 other `DIFF` lines are variables named `__V<n>`
(the `__func__` storage, in tests using `__func__`) which are not an EDG
rename (the writer already names these itself; probe artifact: the second
call returns the entity's own name `__V1` after the first call set
`name_has_been_mangled`, not investigated further because the writer never
uses `get_mangled_variable_name` for them).

| Shape | Example | Hand-off name | Final name | Why |
|---|---|---|---|---|
| Unnamed class given a name for linkage by a typedef (any member, not only ctors/dtors) | `typedef struct { int get() const; } UnnamedT;` (names3 probe) | `_ZNK32_INTERNAL_10_names3_cpp_39c664d4Ut_3getEv` | `_ZNK8UnnamedT3getEv` | inline member bodies are finished at the end of the class body, before the declarator supplies the name |
| Lambda in a default argument, parent unknown | `int g(int (*f)(int) = [](int x){ return x; })`, lambda-ns-scope and names2 probes | cannot be computed: `lambda_parent.routine == NULL`, the lambda has `lambda_immediately_inside_default_arg_expression` | `_ZZ11lam_defaultPFiiEEd_NKUliE_cvS0_Ev` | the parent is set when the declaration completes (func_def.c:2604 `set_parent_routine_for_closure_types_in_default_args`). Computing the name earlier asserts (lower_name.c:7713 `type_is_lambda_in_default_argument` when the parent pointer is missing) |
| Anonymous union inside an anonymous union (compiler-generated ctor) | cpp0x/nsdmi-union5 | `..._2597d59dUt_C1Ev` | `..._2597d59d4__C1C1Ev` | `give_unnamed_class_or_enum_a_name` (lower_name.c:7905) fabricates `__C<n>` from a running counter at the first mangling; the first mangling happens later. Root cause beyond that not traced |

A name that is *wrong but valid* is the dangerous outcome (first row), the
second shape can also **abort the FE** if the name is requested too early.
Structured-binding containers are the fourth reason of
`must_wait_for_mangling_reasons` (lower_name.c `struct_binding_container_needs_mangling`
13750-13765); not present in the corpus subset (no hit).

**What is NOT a reason (verified stable):** names of ordinary, template,
member, local-class, lambda (non-default-arg) functions, ctors/dtors of
named classes, inline functions, entities in anonymous namespaces, `extern
"C"`, members of local classes, template instances (`names1`, `names5`,
`link1`, `loc2`, `tmpl`, `cookie` probes and 1806 of 1809 corpus bodies).

**Proposed predicate** (CIR-gated, evaluated when a function is first named):
walk the parent chain exactly as `must_wait_for_mangling_reasons`
(scope_stk.c:9166-9239) but *without* its `requires_early_mangling ||
has_base_classes` guard, i.e. pending if any enclosing class is
`originally_unnamed` with `discriminator == 0` and no linkage name, or any
enclosing closure has `has_parentless_lambda_in_default_argument` or defined in a
structured-binding container initializer, plus a class that is an anonymous
union with `unnamed_type_has_no_discriminator` and no `name_has_been_mangled`.
Resolve by `NAME_BIND` at the file-scope hand-off, where all of these were
final in every probe. The CHECKING re-mangle of the design (compare at end
of TU) is exactly the experiment of this section and is cheap; it would
have found all three shapes.

**Fifth reason in lowering: the module id** (`must_wait_for_module_id`,
scope_stk.c:9268-9322). Internal-linkage entities and entities in anonymous
namespaces get a module-id qualified name `_ZN32_INTERNAL_<len>_<file>_<id>...`.
`module_id_for_source_corresp` (lower_name.c:7973) uses the module id of the
translation unit and, when none exists yet, calls `make_module_id(NULL)`
(host_envir.c:3564), which builds it from the **source file's mtime and the
current directory** when no externally visible definition has been seen yet.
`names4` probe: same source, mtime changed with `touch -d`, internal names
changed (`..._80afd623...` vs `..._90ce6cfd...`). When an external definition
was seen first (`link1`: `ext` precedes `stat`) the id is derived from its
name and is stable (`9_link1_cpp__Z3exti`). So **output is not
deterministic for internal-linkage names unless the id is pinned** (O4).
Internal names only need to be unique in the TU, so the writer could instead
use Itanium internal-linkage manglings (`_ZL4stati`) or a pinned id.

### 2.2 CIR-gated `lower_name.c` helper for C2/D2/D0 (and what else it needs)

Facts:
- `mangled_function_name` sets `ctor_dtor_kind` only under `IA64_ABI &&
  DO_IL_LOWERING` (lower_name.c:13123-13136); otherwise `cdk_none`, which
  mangled_function_base_name turns into `C1`/`D1` (lower_name.c:12795-12816,
  `MANGLING_STRING_FOR_*` at 179-181). Inheriting ctors give `CI1` for
  `cdk_none` and `CI2` for `cdk_subobject` (12780-12791).
- Experiment: a file-static override read just after that block gave all
  expected names (C1/C2, D1/D2/D0, CI1/CI2), with an assertion for the
  invalid pairs (`D0` on a ctor, `C9`/`D9` for inheriting).
- Suggested shape: `char *get_mangled_ctor_dtor_name(a_routine_ptr routine,
  a_ctor_or_dtor_kind kind)` (CIR-gated, `#if BACK_END_TYPE == BACK_END_TYPE_CIR`)
  setting that override around `get_mangled_function_name_full` (lower_name.c:13571),
  restricted to `cdk_complete`, `cdk_subobject`, `cdk_deleting` (destructors
  only). `lower_name.c` is not in the writer's ownership list; this needs
  the orchestrator.

**Local static variables need a second helper (NEW, not in the note).**
`get_mangled_variable_name` returns `_Z3cnt` for a function-local static
`cnt` (names1/names5 probes): it adds no `Z<function>E` prefix. That prefix
is produced by `mangle_promoted_entity_name` (lower_name.c:15228), which is
inside `#if DO_IL_LOWERING` (15194) and is only called from lowering
(lower_il.c:21349, 21214, 21246). Experiment with a helper that does what
the IA-64 branch of that function does (`start_mangling`, `add_mangled_name_prefix`,
`add_prefix_for_local_entity(routine)`, `mangled_name_with_length(name)`,
`add_discriminator_if_necessary`, `end_mangling_full`), all static helpers in
`lower_name.c`:

| Source | Name produced |
|---|---|
| `int f(){ static int a; {static int a;} }` | `_ZZ1fvE1a`, `_ZZ1fvE1a_0` |
| `A::m` inline member, `cnt` | `_ZZN1A1mEiE3cnt` |
| `tf<int>`, `tf<double>` `c` | `_ZZ2tfIiET_S0_E1c`, `_ZZ2tfIdET_S0_E1c` |
| lambda `operator()`, `ls` | `_ZZZ3lamvENKUliE_clEiE2ls` |
| ctor `B::B()`, `ctorstatic` | `_ZZN1BC1EvE10ctorstatic` |
| `extern "C" cf`, `cfs` | `_ZZ2cfE3cfs` |
| local class method `get`, `ms` | `_ZZZ2g1vEN1L3getEvE2ms` |

These are the g++ names. The helper must not mutate the variable (the
promoted-entity function writes the name into the entity and sets
`name_has_been_mangled`, which the probe's `end_mangling_full` call did as
well; use a buffer). The guard variable (`_ZGV...`), `_ZGR<n>_` lifetime
extension names and `__cxa_atexit` registration are library or writer
constructions on top of this name; the discriminator is already available
(`variable discriminator`, lower_name.c:13783-13810 uses it for the plain
path).

### 2.3 `remove_unneeded_instantiations` safety and the RETRACT hook

Routine bodies (templates.c:21047-21058):
- Condition: `mip->already_instantiated && !automatically_instantiated &&
  !tip->explicit_instantiation && (instance_required_count == 0 ||
  explicit_do_not_instantiate)`. The first case is routines instantiated only
  to know a deduced return type (`sizeof(g1(2))`); the second is
  `extern template`.
- rm probe (default mode, regions freed): `g1<int>` was handed off with its
  body (region 4: `M3 def _Z2g1IiEDaT_`, then `FREE region 4`) long before
  the end of the TU. At the end `M3 REMOVE routine g1 fdn=3 scope=NULL`:
  `scope_for_routine_or_null` returns NULL because the region was freed
  (mem_manage.c:1228-1250 clears `function_def_table[].scope`), so
  `if (rp_scope != NULL) clear_function_body(rp_scope)` (templates.c:21057)
  is skipped. **The writer is never told.** With `--ecil_keep_regions` the
  scope is still set and the body is cleared.
- A removed instantiation is never referenced from evaluated code (that
  would have raised `instance_required_count`), so retracting is safe. It
  can already be referenced by streamed *types* only through
  decltype/sizeof. All removals are template instances with
  `routine_might_exist_in_multiple_copies == TRUE` (see 2.5), so
  "internal or linkonce_odr only" holds.
- **Hook, without editing templates.c.** At the file-scope hand-off
  (after `wrap_up_file_scopes`, fe_wrapup.c:741 onwards; `remove_unneeded_instantiations`
  has run) the writer can evaluate the same predicate for every
  streamed template-instance routine: take the instance record from the
  routine's symbol (`instance_ptr`, symbol_tbl.h:3088 family) and test
  `master_instance_of(tip)` (symbol_tbl.h:6766) fields. This needs no
  `templates.c` change and no scope; it needs the writer to remember streamed
  template-instance routines (a list, like the function-state map). If the
  orchestrator prefers a templates.c hook instead, it must be placed
  **before** `if (rp_scope != NULL)` (templates.c:21057), not inside it.
- Variables (templates.c:21036-21042): `clear_variable_definition` only for
  `storage_class == sc_unspecified` instances and runs before the file-scope
  hand-off (rm probe: `REMOVEVAR arr` before `HANDOFF region 1`), so
  `ecil_emit_globals` simply sees a declaration: no RETRACT.

### 2.4 Late bodies and growing regions (scope_stk.c:8907 guard)

- Regions are not always finished when first checked. Probes: `late1`
  (members of a class whose bodies are deferred to the end of the class, a
  friend defined in class, local classes, lambdas including generic ones,
  class templates) and `loc2`.
  - Member functions defined in the class body are finished at the end of
    the class, after their regions were popped, and appear as additional
    `check_for_done_with_memory_region` calls (the 4x `CHECKDONE region 10`
    for `lm` and its three generic-lambda `operator()` instances).
  - A region can hold several function scopes (`HANDOFF region 9: loc ~L L
    m`; `region 2: f L ~L L`): local classes and lambdas share the region of
    their enclosing function; it is handed off once, with functions
    `(done)` when all are finished.
  - `LATEFINISH` (my hook at scope_stk.c:8907-8923 in
    `finish_function_processing_for_function_def`) fires for exactly these;
    the guard (not on the scope stack, not the innermost function) kept
    every region alive until the last body was done in `late1`, `loc2` and
    `names5`; no "freed before finished" case occurred.
- `ctor_needs_unprocessed_field_initializer` (scope_stk.c:9328) is a further
  delay reason in lowering: a ctor whose `use_field_initializer` entry still
  has `initializer == NULL`. In the CIR flow the NSDMI is scanned when the
  ctor body is processed (`scan_field_initializer_if_needed`,
  decl_inits.c:9700), so by hand-off `field->initializer` is set (ctor1
  probe, both `use_field_initializer` entries had a dynamic init). Not seen
  to fail; add a CHECKING assertion in the writer.
- Still open (not exercised): late-defined functions whose region was already
  freed when the *name* is requested (the `scope == NULL` branch,
  scope_stk.c:8887-8893, silently returns), and `do_not_free_memory_region`
  regions (mem_manage.c:1513).

### 2.5 Template filters, linkage and COMDAT (NEW details)

- **Filters** `ecil_routine_is_filtered` (ecil_type.c:1261-1265) and
  `variable_is_filtered` (ecil_const.c:1218-1222) already mirror
  `ignore_routine_in_back_end` / `ignore_variable_in_back_end` (il.h:880-915,
  which are FALSE without lowering). Missing: `ignore_type_in_back_end` =
  `type_is_nonreal` (nonreal classes, i.e. dependent/prototype classes) and
  `ignore_constant_in_back_end` = `ck_template_param`. No leak was observed
  in 1809 bodies, but the type filter must exist before dependent types can
  reach a signature (a template's own prototype region is handed off:
  `HANDOFF region 2: g1` in rm, with no `def`).
- **Vague linkage.** `routine_might_exist_in_multiple_copies`
  (il.c:32380, declared il.h:4215 under `DO_IL_LOWERING || NEED_NAME_MANGLING`)
  works in cpfe-cir and gives, in the `link1` probe: inline functions,
  `constexpr`, in-class members and non-specialized template instances
  TRUE; explicit specializations (`tf<int>`), non-inline functions,
  `static` and anonymous-namespace functions FALSE. With
  `storage_class == sc_unspecified` this is the linkonce_odr + COMDAT rule
  (put_routine_into_comdat_group asserts `sc_unspecified`, lower_il.c:10063).
  `use_comdat` itself is never set (1.1). Ctors/dtors: the alternate entry
  points get the primary's linkage (lower_il.c:10067-10078).
- **Templates are not instantiated by default** in the shipped cpfe-cir
  (O2). Without `--instantiate=used`, `twice<int>` is never emitted
  (tmpl probe: only `_Z4instv`).
- Explicit specialization of a member of a class template
  (`TS<char>::get`) is in-class-defined and inline: linkonce too.

### 2.6 Lambda static invoker synthesis

See also 1.1. Facts for the writer:
- Trigger: the conversion function `operator FP()` (sfk_conversion) of a
  captureless closure is *defined* only when used; its body returns the
  entry point (`enk_routine`). The entry point routine is
  `conv_op->next` (func_def.c:2648, ordinary lambda) or the instance of the
  next template in the closure's template list (generic lambda,
  func_def.c:2653-2722). It is declared `static`, not inline,
  `special_kind == sfk_lambda_entry_point` (6), `lambda_call_operator` points
  at the call operator, and its type is the call operator's type without
  `this` (return type updated from the instantiated call operator for
  generic lambdas, func_def.c:2713-2718).
- Name from the existing machinery: `_ZZ3lamvENUliE_4_FUNEi` (the `_FUN`
  name is created at class_decl.c:35445; `sfk_lambda_entry_point` is a
  normal name in lower_name.c:12741).
- Lowering's body (lower_init.c:4880-5180): a call of the call operator
  with a **null `this`** (`make_zero_of_proper_type(this_param_type, ...)`,
  lower_init.c:5139-5145) and the entry point's parameters forwarded in
  order; a class return value goes through the return-slot shape. In
  MLIR terms: `CALL gop(null_this, params...)` then `RETURN_STMT`; if the
  return type is a NONTRIVIAL_ABI record, RETURN_SLOT/RETURN_FROM_SLOT.
- Linkage/comdat: lowering puts the entry point into the COMDAT group iff its
  call operator is (lower_il.c:10350-10362, the condition
  `routine->variant.lambda_call_operator->use_comdat`). For the writer:
  copy the call operator's linkage and COMDAT status.
- A static call operator (C++23 `static operator()`) needs no entry point:
  `static_entry_pt = lambda_body` (func_def.c:2650-2655, 2706).
- The writer's queue must be fed from `ecil_function_id` when it introduces a
  routine with that special kind (not from the body walk), and drained at the
  end of the region hand-off that introduced it; the call operator is in the
  same region (a generic lambda's instances may be in later regions: the
  queue must hold the routine, not the region).

### 2.7 Array new and the cookie

- `get_array_new_padding_size` (lower_init.c:3145-3191) is lowering-only (the
  file is `#if DO_IL_LOWERING`, lower_init.c:15); it is
  `max(sizeof(size_t) [x2 if targ_ia64_abi_use_variant_array_cookies], alignof(element))`
  when `new_or_delete_type_requires_array_handling(elem, /*check_constructor=*/FALSE)`
  is TRUE (expr.c:22673-22705: non-trivial destructor, **or** a class-scope
  two-argument `operator delete[]`), and not for the placement form
  `::operator new[](size_t, void*)` (non-member, exactly two parameters,
  second `void*`).
- Probe `cookie` (predicate result per class): trivial `T1`: 0; non-trivial
  dtor `T2`: 1; class `operator delete[](void*, size_t)` `T3`: 1; class
  unsized `operator delete[](void*)` `T4`: 0; member with non-trivial dtor
  `T5`: 1; virtual dtor `T6`: 1; this matches Itanium 2.7 (clang's
  `requiresArrayCookie`); class-scope sized delete only, as in EDG.
- The cookie is the *padding*: for x86-64 `max(8, alignof(elem))`; the note's
  "non-zero cookie must equal max(sizeof size_t, align t_elem)" is right
  for the non-variant cookie. `targ_ia64_abi_use_variant_array_cookies` is
  an AArch32 variant; the writer should refuse (or send 16) rather than
  ignore it.
- **NEW: `enk_new_delete.routine` is NULL for array new of a class with a
  constructor when the default global `operator new[]` is used**
  (`NEW_AND_DELETE_FOR_ARRAY_CAN_BE_FOLDED_INTO_RUNTIME_ROUTINE`,
  expr.c:24845-24870: the routine is dropped when it is the default global
  one and not inline, il_def.h:14715-14723 comment). Probe: `new T1[n]`,
  `new T2[n]`, `new T3[n]`, `new A[3]` all show no `routine`; `new A(1)`
  and placement array new have it; `delete[]` always has `routine`. The
  writer must look up the default `::operator new[]` itself
  (`opname_function_symbol(onk_array_new)` /
  `find_default_operator_new_sym`, as expr.c:24850-24858 does) when `routine`
  is NULL for an array new. Arrays of types needing no handling (e.g.
  `new int[n]`) were not probed.
- IL shape of `new T2[n]`: `enk_new_delete` with `number_of_elements`
  (expression or NULL for a constant bound), `dynamic_init` =
  `dik_nonconstant_aggregate` -> `ck_aggregate` -> `ck_init_repeat count=N`
  -> `ck_dynamic_init` -> `dik_constructor`; `delete[]` carries the same
  structure for the destructor (`dik_none destructor=~T`). Exceptions on
  adds `freeing_of_storage_on_exception` and
  `destruction_is_for_partially_constructed_aggregate` entries (2.9).

### 2.8 Condition variables, NRVO, lifetime-extended and unordered temporaries, object lifetimes, ctor inits, dtor destruction

- **Condition variables** (probes c1-c5, t9, t10): the `if/while/for/switch`
  expression is `enk_condition` (il_def.h:13588) with `scope` (a block scope,
  child lifetime of the enclosing block, kind `olk_block`), a `dynamic_init`
  (e.g. `dik_class_result_via_ctor`: the call result constructs the variable in place,
  flag `follows_an_exec_statement`) and an `expr` (the value used, e.g.
  `eok_bool_cast` of the conversion call). One lifetime, one `destructions`
  list entry. The rewrite `while(1){decl; init; cleanup; if(!c) break; body}`
  (for: `for(init;;step){...}`) gives per-iteration destruction. `switch`
  and `if/else`: the variable lives across the whole statement, so the
  cleanup closes after the statement (no rewrite needed). `for` with a condition
  declaration: the init scope is a separate `stmk_block` with
  `implicit_scope_not_allowed` and `for_init_scope` on the `stmk_for`.
- **NRVO** (probe nrvo): `return_value_variable` is non-NULL only if every
  return returns the same top-scope local (n1, n3, n10: yes; nested-scope
  variable n2/n9, two variables n4/n7, parameter n5, reference n6: no; trivial
  copy `struct T` n8: no). All `stmk_return`s still carry
  `return_dynamic_init` (the copy ctor, `is_implicit_copy_for_copy_initialization`);
  with NRVO the writer must drop those copies and bind the variable to
  the return slot. EDG's condition (expr.c:52299-52321) is also required to
  hold for clang's elision, so no mismatch; clang can elide in a few more
  cases, which is allowed.
- **Lifetime-extended temporaries** (t4): the `enk_temp_init`'s `dynamic_init`
  has `lifetime = <block lifetime>` (kind `olk_block`) and
  `has_temporary_lifetime == FALSE` (il_def.h:4088); the reference
  variable has `extends_lifetime`. The temporary is constructed inside the
  reference's `dik_expression` initializer and destroyed with the block.
  Rule for the writer: a temporary whose lifetime is not an
  `olk_expr_temporary` of the surrounding `enk_object_lifetime` is pushed
  to the enclosing block after the full expression, as the note says.
- **Unordered temporaries** (u1, u2): see 1.3; no pre-scan.
- **Object-lifetime walk.** A function scope has one root lifetime
  (`olk_block`, parent = the file's `olk_global_static`), children per block
  and per full expression (`olk_expr_temporary`, entity = the
  `enk_object_lifetime` node); `destructions` lists chain through
  `next_in_destruction_list`. The writer does not need to walk the lifetime
  tree: the cleanups come from `stmk_init` (variables, `di->destructor &&
  di->lifetime`), `enk_temp_init`/`enk_lambda` (temporaries) and
  `constructor_inits` (members). Lifetime kinds matter only to decide
  scope (block vs full expression). The first element of
  `destructions` of the *function's* root lifetime is not special.
- **Ctor inits** (ctor1, E probes): `constructor_inits` is in construction
  order. Kinds seen: `cik_direct_base_class` (explicit `B(1)`),
  `cik_field` with `dik_constructor` (compiler_generated default ctor),
  `use_field_initializer` (NSDMI), `cik_field` + aggregate of
  `ck_init_repeat` for array members, `cik_delegation` (`D(int): D()`,
  `value_initialization=TRUE`). Implicit copy ctors use
  `is_copy_constructor_with_implied_source` on class members and
  `dik_bitwise_copy` with no `source` (implied source = the same member of
  the parameter) for scalars/trivial members. `compiler_generated` only means
  the *entry* was generated; explicit entries have it FALSE. Virtual bases
  (`cik_virtual_base_class`) must stay an error in v1.
- **Dtor destruction** (`~D`): see 1.4 (5).

### 2.9 Exceptions: the IL with exceptions off, and EH-only IL with exceptions on

Decision (orchestrator, 2026-10-10, user's answer to O3): cpfe-cir keeps FE
exceptions ON; the writer emits a no-exceptions v1 stream
(`HDR_LANG.exceptions = 0`), diagnoses `try`/`throw`/function-try-blocks
(body dropped, declaration kept) and sends the EH-only destructions below as
`CLEANUP_BEGIN(eh)`, which the v1 library decodes and drops. This
subsection records the facts behind that rule. Probes: `eh1`, `ex1`,
`ex_t` (Mac cpfe-cp for IL dumps, exceptions on).

**Facts (exceptions ON is the FE default for C++**, cmd_line.c:13352,
`exceptions_enabled = DEFAULT_EXCEPTIONS_ENABLED`; with `--no_exceptions` the
FE rejects try/catch/throw itself: `error: support for exception handling is
disabled`, `ex_t` probe; the diff of `ex1` with/without is 545 lines).

**EH-only IL (what to emit as `eh`).** Each item is IL that exists only
because a later step may throw; with exceptions off it is absent or has no
`destructor`.

| IL | Where | How to recognize | v1 emission |
|---|---|---|---|
| Member (and base) destruction after a successful member init in a **constructor** | `constructor_inits` of a ctor scope; `dynamic-init` with `destructor != NULL` and `is_constructor_init` (B probe: `a1`, `a2` entries carry `destructor=~A`, `lifetime=<ctor block>`, chained by `next_in_destruction_list`) | ctor scope, `ctor_init->initializer->destructor != NULL` (also `cik_delegation`: the delegating ctor's target destructor is set when exceptions are on) | `CLEANUP_BEGIN(eh)` with the member destructor call, begun **after the `MEMINIT_END`** of that member, at function scope (a cleanup begun inside MEMINIT would end at `MEMINIT_END` by the scope-region rule of 1c, which is wrong here); `CLEANUP_BODY`. Same for the delegation target |
| Destruction of the elements already built when an array initialization is interrupted | inner `ck_dynamic_init` of `dik_nonconstant_aggregate`/`ck_init_repeat` (members: C probe; `new T[n]`: n2 probe) | `dynamic-init.destruction_is_for_partially_constructed_aggregate == TRUE` (il_def.h:4107) | no record in v1; `ARRAY_CTOR` carries the facts (element type NONTRIVIAL_DTOR), the library adds the partial-destruction cleanup in M4 |
| Freeing the storage of a `new` if the initialization throws | `enk_new_delete.freeing_of_storage_on_exception` (il_def.h:14739-14746 area; n1/n2 probes), a `dik_none` entry with `is_freeing_of_storage_on_exception` (il_def.h:4098), `is_array_freeing` for `new[]`, `destructor` = the operator delete routine, `inside_conditional_expression` | those flags | `CLEANUP_BEGIN(eh)` calling the operator delete (G-id from the entry's `destructor`) begun after the allocation call and before the construction; it must be ended (cancelled) when the construction completes: i.e. the `eh` cleanup scope covers only the construction. Needs a defined cancel point in the format (the note's `CLEANUP_END`, an explicit close, is the tool): `CLEANUP_BEGIN(eh) ... CLEANUP_BODY <construct> CLEANUP_END` |
| Everything else in the `destructions` lists | locals (`stmk_init`), temporaries, condition variables | present identically with exceptions off (t1/t2/t6 probes) | `CLEANUP_BEGIN(all)` (v1 library maps `all` to `normal`), as in the design |

What the IL does NOT contain, with exceptions on: landing pads, a
"cleanup on throw" list separate from the normal one (they are the same
`destructions` lists; the EH-only entries above are extra entries on them,
distinguishable by the flags above), per-call "may throw" information
beyond the function type's exception specification (`noexcept`,
`exception_specification`, `never_throws` on compiler-generated members).
Since every function and call is nothrow in v1, exception specifications
need no encoding (they also vanish with `--no_exceptions`: 19 of the 545 diff
lines).

**What to diagnose (unsupported, body dropped, declaration kept):**
- `stmk_try_block` (il_def.h:15834), including `is_function_try_block`
  (t3, t5 probes: statement has `handlers`, a `lifetime` of kind
  `olk_try_block` (il_def.h:17637) with child block lifetimes, catch
  parameters as variables with `dik_bitwise_copy` inits);
- `enk_throw` (t4 probe: `enk_throw` with a `dynamic_init` for the thrown
  object; also a rethrow, `throw_info == NULL`);
- `olk_try_block` lifetimes are only reachable through those statements, so
  no separate rule is needed.
Not diagnosed (nothrow assumed): `noexcept(...)` operators evaluate to
constants in the IL; dynamic exception specifications (`throw(int)`) are
ignored (their `__unexpected` call is lowering's, not IL).

**Verified:** probes `eh1` (functions B, C, n1, n2, n3, t1-t6) on the
unlowered IL with exceptions on; in `n3` (placement new) there is no
`freeing_of_storage_on_exception` (placement `operator new` has no matching
delete in this probe, a warning is issued) so nothing to emit.

With `--no_exceptions` (not the shipped mode now) the ctor-init entries lose
their `destructor`/`lifetime`/`next_in_destruction_list`, destructor entries
keep `destructor` (`dik_none destructor=~A`), and locals/temporaries are
unchanged.

### 2.10 Design errors and omissions (consolidated)

1. **Local-static names** are not produced by `get_mangled_variable_name`
   (2.2); needs a CIR-gated helper. High.
2. **Name finality**: the mirrored predicate is too narrow, there are
   hard aborts when naming too early, and a module-id determinism hole
   (2.1). High.
3. **RETRACT hook** never fires with freed regions (2.3). High; writer-only
   fix available.
4. **Default instantiation mode** is `tim_none` (O2); without `--instantiate=used`
   no template instance exists. High (config).
5. **Exceptions default on**: resolved by the user's decision (FE stays on, writer sends a no-exceptions stream with `eh` cleanups, 2.9). Needs a defined cancel point for the `new` cleanup (2.9). Medium.
6. **`enk_new_delete.routine` NULL for array new** (2.7). Medium.
7. **`use_comdat` unset; linkage must come from `routine_might_exist_in_multiple_copies`**
   (2.5). Medium. The design's `COMDAT` flag derivation must be stated.
8. **NSDMI**: `field->initializer` with `enk_param_ref(0)` as `this`;
   `init_is_ctor_dependent` copies (1.2). Medium.
9. **NRVO guard** unnecessary in M3 (2.8). Low (simplification).
10. **Deleted-copy/move classes**: NONTRIVIAL_ABI formula differs from Itanium
    (1.2, O5). Low.
11. **Section 5 line numbers stale** (1.5). Low.
12. **Variable linkage / guarded**: `is_inline` variables and template
    static data members need `linkonce_odr` + guard + unordered semantics
    that the `unordered` flag does not give (1.4 (7)); the note does not say
    how `DECL_VAR(GUARDED|COMDAT)` is derived. Medium (not probed beyond the
    flags).
13. **Hand-off of template prototypes**: regions of uninstantiated
    templates are handed off (no body is sent, correct) but the type
    filter is missing (2.5). Low.
14. Probe artifact worth knowing: logging *every* variable of a function
    scope with `get_mangled_variable_name` asserts for pack parameters
    (`add_discriminator`, lower_name.c:8555, cpp23/subscript4 reduced to
    `sub3`); the writer must only ask for the names of static-duration
    entities.

## 3. Questions for the orchestrator / user

- **O1.** May I add a CIR-gated helper pair to `lower_name.c` (get C2/D2/D0/CI2
  names; get local-static names without mutating the entity)? It is not on
  my ownership list. Alternatively cir-config does it.
- **O2.** `cpfe-cir` instantiates nothing by default (`DEFAULT_INSTANTIATION_MODE=tim_none`
  comes from `cmake/macro-conf/support/platform/common/base.cmakedef:3` despite
  `INSTANTIATE_TEMPLATES_EVERYWHERE_USED=1` in `cpfe-cir.cmakedef:29`).
  Config change (`DEFAULT_INSTANTIATION_MODE=tim_used`) or driver flag
  (`--instantiate=used`)? Needed for every C++ test.
- **O3.** Answered: FE exceptions stay on, writer-side diagnosis and `eh`
  cleanups (2.9).
- **O4.** Determinism of internal-linkage names (module id from mtime and cwd
  when no external definition precedes). Options: pin the module id in the
  CIR configuration (e.g. the source file's base name only), or use Itanium's
  internal-linkage manglings (`_ZL...`, `_ZN12_GLOBAL__N_1...`) in the writer
  for entities with internal linkage. Which?
- **O5.** NONTRIVIAL_ABI for a class whose copy and move constructors are
  all deleted: add the Itanium rule to the writer or accept the
  difference?
- **O6.** Linkage rule for routines: vague (linkonce_odr + COMDAT) iff
  `routine_might_exist_in_multiple_copies(r)` and `storage_class ==
  sc_unspecified`; internal iff static/anonymous namespace. OK to write that
  into the spec?
- **O7.** Where is the name for `NAME_BIND` resolved: at the file-scope
  hand-off (all probes stable there) or earlier? I propose the file-scope
  hand-off with the CHECKING re-mangle of the design.
- **O8.** `targ_ia64_abi_use_variant_array_cookies` (AArch32): refuse in M3?
- **O9.** The RETRACT predicate can be evaluated in the writer at the
  file-scope hand-off (no `templates.c` change). Agree?
- **O10.** Do you want the static invoker of a *non-capturing generic* lambda
  in M3? It needs the instance lookup of func_def.c:2653-2722 to have
  happened, which it has whenever the conversion function was used.

### Answers received (orchestrator, 2026-10-10)

O1 yes (writer owns CIR-gated helpers in `lower_name.{c,h}`); O2 `DEFAULT_INSTANTIATION_MODE=tim_used`
in the cpfe-cir cmakedef; O3 see above; O4 Itanium internal manglings
(`_ZL...`, `_ZN12_GLOBAL__N_1...`), CIR-gated, deterministic (this replaces
the `_INTERNAL_` module-id names of 2.1 for entities with internal linkage;
the names of external entities that contain an unnamed or anonymous-namespace
type are part of the same work); O5 yes; O6 yes (explicit instantiation
definitions: weak_odr); O7 yes; O8 yes; O9 yes; O10 diagnose generic-lambda
static invokers in M3 unless trivially free. No M3 code before the M3a
contract is committed.

## 4. Contract-change requests (for the orchestrator)

- `NAME_BIND`/`F_NAMES` stay as designed; add that the writer may bind at the
  file-scope hand-off only.
- `DECL_FUNC.COMDAT`: set iff linkonce_odr/weak_odr (no change), with the
  derivation of O6.
- A `DECL_VAR` for a function-local static must carry the helper name of 2.2.
- `HDR_LANG.exceptions == 0` with FE exceptions on: `CLEANUP_BEGIN(eh)` must also be usable for the `new`-storage cleanup, which needs an explicit close (`CLEANUP_END`) and may sit between `MEMINIT_END` and the next `MEMINIT_BEGIN` (2.9).
- Nothing else in `ecil_format.h`/`edg_cir.h` needs to change for what was
  verified here.

## 5. Reproduction

On the box (`~/edg-cir-writer`): `.tmp-sweep/m3tree` (patched copy),
`.tmp-sweep/m3hack.py`, `.tmp-sweep/m3s/*.cpp` (snippets),
`.tmp-sweep/m3sw_a|b/` (corpus name sweeps; `*.out` contain `M3 def` and
`M3FINAL` lines; `an_sweep.sh m3sw_a` summarizes). On the Mac:
`build/w/m3/` (`hack.py`, `m3go.sh [-b] NAME...` runs a snippet on the box,
`ilv.sh NAME FUNC [opts]` shows the unlowered IL of a function with
`ilview.py`, `snips/`). Nothing under `src/` was modified for this document.
