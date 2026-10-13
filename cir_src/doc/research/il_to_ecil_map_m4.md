# IL to ECIL map for M4 (cir-writer research, gate M4a)

Author: cir-writer, session 3, 2026-10-12. Status: input to gate M4a.
Subject: where each fact of the ECIL 1.2 draft v2 (`m4_ecil_design_draft.md`)
comes from in the EDG IL, which writer function will send it, in what order,
and what the IL does not give. It answers the draft's section 5 and the plan's
"cir-writer research" list, and ends with a writer-side critique of the draft.
`src/` was **not** changed for this research (the R2 and R3 fixes of gate M3.1
were made meanwhile and are described in `m31_reassessment.md` and
`writer.md`; line numbers of `src/ecil_*.c` below are the working tree after
them).

**Status (2026-10-12, after M3.1 was committed as 511d7a76fc):** this map has been
brought in line with the normative ECIL 1.2 (`ecil_format.md` section 12 and
`ecil_format.h`, minor 2). Where the first version of this note and section 12
differ (the THROW model, catch kind 2 for method pointers, `C_VPTR` replaced by
the library filling vptrs, `CLASS_INFO` at need or (vptr, virtual bases) right after the body,
the data-size rule, the
`g_d0` declaration, `C_METHOD` in bodies), section 12 is right and this note
follows it. Section 11 lists what was resolved.

**Review update (gate M4a, PROCEED WITH FIXES, 2026-10-12):** the map follows the
review-updated section 12 (`ecil_format.h` has `INIT_ZERO_BASE` 0xA8). Changed: 2.2
(`data_size <= nvsize`), 2.6 (implicit virtual functions in slot order), 2.9 (the layout
facts of a dynamic class (a `TY_VPTR` or `VIRTUAL_BASE` field, or a `BASE` of a dynamic class) follow its body; the vtable
facts stay first-need), 3.2 (`D0` defined iff `D1` is; flags by arity), 3.4 and 4.4 (the
D1 synthesis order), 3.5 (ref-qualifiers ignored), 4.1 (devirtualization calls the final
overrider; the covariant exception; sticky `NOTHROW`), 5.4 and 5.5 (a throw in a
conditional region gets its own `FULLEXPR`; deactivation before the range ends), 7.3
(incomplete-class masks, the `*` prefix), 9 (`INIT_ZERO_BASE`), 10 (gates and the S1
list) and 12 (probes). Section 11.4 lists the review items.

## 0. Method and evidence base

- **Unlowered IL.** `cpfe-cir` compiles with `DO_IL_LOWERING=0`, so the IL is
  the IL the writer sees. The debug build (`build/gcc-cir/bin/cpfe-cir` on the
  box) accepts `--il_display`. Command (box container, from the repository
  root; the `EDG_*_SCRAPE` exports are what `edg-container-exec` makes):

  ```
  source build/gcc-cir/environment.sh
  export CPFE=$CPFE_CIR CPFE_CP=$CPFE_CIR EDG_USE_SYSTEM_HEADERS=1 \
         EDG_GCC_INCL_SCRAPE=$(edg-scrape-compiler gcc --lang c++ includes) \
         EDG_GCC_CINCL_SCRAPE=$(edg-scrape-compiler gcc --lang c includes) \
         EDG_GCC_VER_SCRAPE=$(edg-scrape-compiler gcc version)
  util/eccp.sh -S -tused --c++17 --il_display probe.cpp > probe.il
  ```

  The display prints every entry as `<scope>-scope <kind>@<address>` followed
  by `key: value` lines; the supplements of statements and of the throw
  expression are flattened into their owner (there are no `try-supplement` or
  `throw-supplement` entries, only `handler` entries).
- **Fields that exist.** Layout *inputs* are present (`primary_base_class`,
  `size_without_virtual_base_classes`, `highest_virtual_function_number`,
  `virtual_function_info_offset`, `virtual_function_info_base_class`,
  `number.virtual_function`, `overriding_virtual_functions`,
  `assoc_operator_delete_routine`); lowering outputs are not
  (`virtual_function_table_var`, `first_vcall_offset_index` and the
  construction-vtable indices are under `DO_IL_LOWERING`; a dump of
  `v1_slots` has none of them).
- **Probes.** `cir_src/test/writer/probes/m4/*.cpp` (index in section 12).
  Each compiles and runs with g++ (checked) and is a candidate end-to-end test
  of M4b. `probes/*.cpp` of the M3 sweep are unchanged and do not include
  this directory.
- **Query tool.** `cir_src/test/writer/tools/il_query.py` (parses the display with
  the parser of `dev_tools/bin/edg-cir-fe-facts`; `inits`, `handlers`,
  `throws`, `specs`, `lifetimes`, `newdel`, `fn NAME [special]` print the facts
  quoted below).
- **Clang.** The pin's clang in the box image (`/edg/llvm-cir/bin/clang`):
  `-cc1 -fdump-vtable-layouts -fdump-record-layouts` and `-emit-llvm` for the
  linkage of vtables and typeinfo objects.
- **Instrumented build (box only, not in the tree).** `ecil_type.c` printed
  `mangled_typeinfo_string`, `mangled_typeinfo_name` and `mangled_vtbl_name` of
  every type that the writer introduces (section 2.8). The box tree was
  restored from the Mac tree afterwards.

## 1. Summary: the answers

The questions of the draft's section 5 first, then the others of the plan.

| # | Question | Answer (details in the section named) |
| --- | --- | --- |
| 1 | FE-canonical parameter identity for `sig` | The routine type's `param_type_list[i].type` (C++: top-level cv removed, arrays and functions decayed; `declared_type` is the original) plus `this_qualifiers` and `ref_qualifiers`, and the name. Equality is the FE's `identical_types`, so the writer keeps a table from (name, parameter types, qualifiers) to a dense id. **ECIL type G-ids cannot be used**: they carry no pointee cv (`f(const A*)` and `f(A*)` are one G-id) and are merged by width (`long` and `long long`, `char` and `signed char`). 3.5 |
| 2 | The vtable "used" marker | Writer-side: a vtable is used when a `VPTR_INIT` or `VTT_ADDR` that needs it, or a `C_AGG_BEGIN` of the class as a complete object or member of a constant (not as a base subobject), is streamed in a body or constant that is not dropped, or when it is the VTT or the construction vtables of one that is. The FE's `virtual_functions_marked_as_required` is neither sufficient (it recurses into direct bases and is set by typeinfo forcing and explicit instantiation) nor needed, and not usable even as a cross-check: a class whose vtable a derived constructor uses can lack it (`writer-cxx-vbase-layout`, class `L`). The CHECKING cross-check that the virtual members exist is the availability of the class's own virtual functions and of the final overrider of each virtual function of each base (`check_vtable_members_available`). A vtable is *defined here* if it is used and has no key function or an inline one, or if its key function is defined in this TU (clang emits it at the key function's definition even when unused), or if it is explicitly instantiated here. 8.1 |
| 3 | EH dynamic-init flags to `DEACTIVATABLE`/`CLEANUP_END` | `new`: the FE gives `nds->freeing_of_storage_on_exception`, a destruction entry whose `destructor` is the delete function; it is `DEACTIVATABLE` and ends (`CLEANUP_END`) right after the initialization completes. A partially constructed aggregate: one `destruction_is_for_partially_constructed_aggregate` entry per element or member, each a `DEACTIVATABLE` eh cleanup, ended (all of them) when the aggregate is complete. Details and the traps (sized array delete, placement arguments, a destroying delete is chosen) in 5.5 |
| 4 | Catch kinds from `a_handler.dynamic_init` | By the parameter type, then by the init kind: `dik_bitwise_copy` for a scalar, pointer, reference and trivially copyable class (`trivial_copy`), `dik_constructor` with `is_copy_constructor_with_implied_source` for a class that needs its copy constructor (`non_trivial_copy`, `g_copy` = its routine); a pointer to member function by value is kind 2 (scalar). `...` has no parameter and no init. An unnamed parameter still has the variable, and g++ and clang both copy it. 5.3 |
| 5 | Typeinfo closure per TU | Writer-side, on demand: a typeinfo is introduced when a `THROW_END`, `CATCH_BEGIN`, `TYPEID`, `DYNAMIC_CAST`, dynamic `FUNC_EH_SPEC`, or an emitted vtable needs it, and a `DEFINED_HERE` one first introduces the typeinfo of its bases, pointee and class. The FE's `used_in_exception_or_rtti` flag and `il_header.nontag_types_used_in_exception_or_rtti` cover the first group (they also force the virtual functions, which already happened) and are a CHECKING cross-check, not the source. 7.3 |
| 6 | `CLASS_MANGLE` of TU-local classes and late typedef names | `mangled_typeinfo_string(t)` (lower_name.c:15219) is the `<type>` of `CLASS_MANGLE`, and agrees with what the FE puts in the class's member function names: `N12_GLOBAL__N_14AnonE`, `Z5localvE3Loc`, `UlvE_`-forms for closures. A typedef name for linkage arrives after the class body: the first mangling of `typedef struct { virtual void f(); } TN;` is `Ut_` and the member functions are later named `_ZN2TN1fEv`, so `CLASS_MANGLE` must be re-issued before `F_NAMES` for exactly the classes for which `cir_name_may_change` says so (the draft already allows it). 2.8 |
| 7 | Does a destructor function-try enclose the cleanup prologue? | Yes. Clang's `EmitDestructorBody` enters the try before the epilogue cleanups, so the try encloses the member and base destruction and the vptr initialization, as the C++ rule says. In the IL the function's `assoc_block` is the `stmk_try_block` (for every function: `is_function_try_block` is TRUE also for a plain function) and the destruction list sits in the scope's `constructor_inits`, so the writer opens `TRY_BEGIN(FUNCTION_TRY)` before `emit_dtor_prologue`. The front end already supplies the implicit rethrow at the end of the handler of a constructor or destructor (`compiler_generated` `stmk_expr` of an `enk_throw` with no `throw_info`). 5.1 |
| 8 | The double walk (C1/C2) against the work stack, lambdas, static locals, the function-state map | Compatible. The stack is empty after each walk; lambdas are separate function scopes of the region and are not part of either walk; static locals have their `DECL_VAR` once and `STATIC_INIT` per body (K5a). Four things must change: `make_static_temp` creates a new G-id per call (memoize per owner and sequence number), the L-id epoch must end between the walks, `ecil_set_function_state` and `ECIL_GF_DEFINED` are set once, and a failure in the second walk truncates both bodies. 4.4 |
| 9 | Value-initialization of classes with data-member pointers; does EDG reach `ZERO_BYTES`? | Yes, in ten places (`ecil_code.c:3721-5538`): `dik_zero` (`new S()`, `new S[n]()`), `ctor_needs_zeroing`, value-initialized bases and delegations, `[[no_unique_address]]`. `S s{}` is `dik_constant {}` with unlisted members, and `S()` is a `ck_aggregate` with an explicit `ck_ptr_to_member` null element. 9 |
| 10 | `new T[n]{}` against `new T[n]()` | Constant count: `()` is `dik_zero` of the array type, `{}` is `dik_constant {}`; run-time count: `()` is `dik_zero` of `T[]` (class without constructor) or `dik_nonconstant_aggregate` with zero repetitions (class with one), `{}` is `dik_constant {}`. All of them are one per-element loop for a run-time count and an `INIT_ZERO` of the typed array for a constant one. 9 |
| 11 | D0's operator delete | `class_type_supp(cls)->assoc_operator_delete_routine`, set by `set_class_assoc_operator_delete_routine` (class_decl.c:22361) when a destructor is defined (decl_inits.c:10655) and callable by the writer. It is the result of the FE's full "usual deallocation function" selection (destroying, then aligned for an over-aligned type, class unsized over class sized, global sized when sized deallocation is enabled and the type is complete). `is_default_operator_delete` (symbol_tbl.c:11678) gives `SIZED`, `ALIGNED` and `DESTROYING`. Probe `v5_delete` checked all seven cases. 3.3 |
| 12 | Key function at the end of the TU | `first own virtual function with !pure && !compiler_generated && !inline_in_class_definition`, none if it is `is_inline` (inline defined later), none for an implicit instantiation of a class template, and no definition here for an `extern template`. Matches clang for every class of `v4_key` and `v4b_key20` (including `constexpr`, deleted, `consteval`, an out-of-class `= default` and an explicit specialization). It must be computed at the end of the TU because `inline` may come later. 8.1 |
| 13 | `tim_used`: are the virtual members there for every emitted vtable? | Yes, by construction: every use that makes the writer emit a vtable definition is a constructor or destructor definition, a decider definition, an explicit instantiation or a typeinfo force, and the FE calls `require_definitions_of_virtual_functions_in_class` for each (func_def.c:289, 423; templates.c:42416, 42481; types.c:18196). That marks the virtual members of a template instance `instance_required`, defines defaulted virtual functions and, if there is no decider or it is defined here, the implicit virtual destructor. Probe `i1_inline_vtable`. The writer must not emit a vtable for a use of its own that the FE did not see (a CHECKING assertion). 8.2 |
| 14 | Exception specifications | `is_nothrow_spec(esp)` after the FE resolves it; the cases are in 5.6. `NOTHROW` is `is_non_throwing_routine`, which also includes trivial members and `never_throws`; `FUNC_EH_SPEC(noexcept)` is the narrower "noexcept(true) specification written or implied", and an implicit destructor has one. |
| 15 | Range-for with `try` | No new IL: the loop has no implicit try, and `break`/`continue` in the body and the handlers are the compiler-generated labels that M3 already maps to `BREAK_STMT`/`CONTINUE_STMT`. `__range`, `__begin` and `__end` get ordinary `all` cleanups. 5.7 |
| 16 | Member-pointer values | `ck_ptr_to_member` (type: the original pointer type; an outer implicit cast constant with `casting_base_class`, `cast_to_base`), computed by the writer from `field->offset` and `base->offset`; null is a NULL `field`/`routine`. Expressions: `eok_pm_field`, `eok_dot_pm_call`, `eok_points_to_pm_call`, `eok_pm_derived_class_cast` (the implicit base-to-derived conversion), `eok_pm_base_class_cast`, `eok_bool_cast`, comparisons. 6 |

Findings that are not in the questions and that the draft needs (details at
the sections named):

1. **The own-vptr predicate of M3 is wrong** (`ecil_type.c:1095, 1206`,
   `ecil_const.c:1143`): `any_virtual_functions && virtual_function_info_base_class == NULL`
   misses a class that has only virtual bases and counts a vptr for a class whose
   primary base is an indirect virtual base. The right one is "dynamic and
   `primary_base_class == NULL`" (2.1).
2. **The data size of a class with virtual bases** is not what `ecil_data_size_of_type`
   computes (it excludes virtual bases); clang's is the maximum of the non-virtual
   data size and, for every non-empty virtual base, its offset plus its non-virtual
   data size. Validated on 11 classes of `v3_layout` (2.2).
3. **The front end lists every virtual base, indirect ones too, in the
   constructor and destructor lists** of every class that has any (3.4): the
   `MEMINIT(vbase)` groups and the D1 cleanups come straight from there.
4. **`is_virtual_call` is TRUE for a call on an object** (`D local; local.f()`)
   where clang calls directly, and FALSE for a call of a `final` function, as clang
   does (4.1).
5. **The front end's free-on-exception entry for a class that has only a
   destroying `operator delete` is the destroying function** (5.5): it must be
   diagnosed, not called.
6. **Both g++ and clang copy an unnamed by-value catch parameter** (5.3), so
   `CATCH_PARAM` is sent for it.
7. **`make_static_temp` is the only G-id creation in a body walk that is not
   keyed**, and the double walk would create it twice (4.4).

## 2. Class facts (draft 1a)

All of these are declaration-section records. The layout facts of a class with a
vptr, a virtual base or a dynamic base follow its record body, all the others are sent at the **first
need** of the class (section 2.9). The IL entries are the class type entry
(`type.variant.class_struct_union`), its `class-type-supplement` (`extra_info`),
the `base-class` entries of `base_classes_of(t)` and the routine entries of the
class scope's `routines` list.

### 2.1 `TY_VPTR` and who has a vptr

IL: `any_virtual_functions` (il_def.h:10468), `any_virtual_functions_including_in_base_classes`
(10484), `any_virtual_base_classes`, `primary_base_class` (type supplement,
il_def.h:8759), `virtual_function_info_offset` (8811),
`virtual_function_info_base_class` (8822).

A class is **dynamic** if it has virtual functions of its own or inherited, or
virtual bases. It has an **own vptr** iff it is dynamic and `primary_base_class`
is NULL; the vptr is then at offset 0 (`virtual_function_info_offset` is 0 in
every probe). A class with a primary base shares that base's vptr, including a
primary base that is a *virtual* base (`B : virtual A`, with `A` dynamic: `A` is
at offset 0 of `B`'s complete object).

| Class (probe) | `primary_base_class` | `virtual_function_info_base_class` | Own vptr |
| --- | --- | --- | --- |
| `A { virtual f; }` (`v1_slots`) | NULL | NULL | yes |
| `B : A { }` | A | A | no |
| `B2 : virtual RB { }` (RB has a virtual function) | RB (virtual) | NULL | **no** (shares RB's) |
| `E : virtual D, D : virtual C, C { virtual f; }` (`m4_fe_facts` 4.3) | C (indirect virtual) | NULL | **no** |
| `W { virtual ~W; }` (`v7_vbase_casts`) | NULL | NULL | yes |
| `F : virtual W { }` | W (virtual) | NULL | **no** (shares W's) |
| `D2 : B2 { RD *get(); }` (`v1_slots`; `B2 : virtual RB`, `RB` dynamic) | B2 | NULL | **no** (shares the vptr that B2 shares with RB) |
| `X : virtual E0` where `E0` is empty (`v3_layout` `F`) | NULL | NULL | yes (a class with only virtual bases) |

The M3 predicate `any_virtual_functions && virtual_function_info_base_class == NULL`
(`ecil_type.c:1095` in `ecil_data_size_of_type`, `ecil_type.c:1206` in the record
body, `ecil_const.c:1143` in the constant gate) is wrong for a class
that has own virtual functions but whose primary chain ends in a virtual base
(`D2` above: `virtual_function_info_base_class` is NULL, the predicate gives it a
vptr of its own, and clang lays out `B2` at 0 with the vptr shared), and for a
class whose only virtual bases are not dynamic (the last row: no virtual function
of its own, so the predicate says no vptr, and it needs one). (`B2`, `E` and `F :
virtual W` declare no functions, so the predicate happens to give the right
answer for them.) Replace it by
one helper (`class_has_own_vptr`: `needs_virtual_function_table(t) ||
any_virtual_functions_including_in_base_classes`, and `primary_base_class ==
NULL`; `needs_virtual_function_table` is il.h:3962). A second helper,
`class_is_dynamic` (`any_virtual_functions_including_in_base_classes ||
any_virtual_base_classes`), says that the record has a `TY_VPTR`, a `VIRTUAL_BASE` or a
`BASE` of a dynamic class, which is what 2.9 sends layout facts for (`B : A` has no own
vptr and is dynamic). The M3 opaque vptr
(`ecil_vptr_type`, `ECIL_GF_ABI_OPAQUE`) becomes `TY_VPTR` and the record is no
longer ABI-opaque.

Writer function: `ecil_type.c`, the record-body path that sends `REC_FIELD`
(now at 1186-1260).

### 2.2 Sizes: `CLASS_INFO(nvsize, nvalign)` and the data size

- `nvsize` = `size_without_virtual_base_classes` (il_def.h:8767), `nvalign` =
  `alignment_without_virtual_base_classes` (8768). **`nvsize` excludes tail
  padding** (the clang and FE value for `{ vptr; char c; }` is 9, not 16; it is
  what a derived class may reuse). **A class without virtual bases has
  `data_size <= nvsize <= size`** (12.2): the writer's `data_size`
  (`ecil_data_size_of_type`) ignores an empty base and counts the members and
  bases by their data size, so a displaced empty base makes `nvsize` larger. Probe
  `v3b_empty_bases` (FE `size_without_virtual_base_classes`, clang `nvsize` and the
  writer's `data_size`): `A6 : E, E2 {}` has `E2` displaced to offset 1, size 2,
  FE 2, clang `dsize 0 nvsize 2`, `data_size` 0; `A7 : E, E2 { char c; }` has
  FE 2, clang `dsize 1 nvsize 2`, `data_size` 1; the five others (`A1`..`A5`:
  2, 4, 12, 3, 9) have all three equal. `CLASS_INFO` sends the FE's value, never
  the `data_size`, and CHECKING asserts `data_size <= nvsize <= size` for every
  class that gets a `CLASS_INFO` (with virtual bases the rule below). They
  agree with clang's `-fdump-record-layouts` `nvsize`/`nvalign` in 761 of 761
  classes (`m4_fe_facts`), and again in the classes of `v3_layout` that I compared. `nvsize <= size` holds always.
- A class without a vptr (own or primary base's) has `nslots` 0 (12.2): the writer sends
  0 whenever `highest_virtual_function_number` is the 65535 sentinel, which is also the
  value of a class with no virtual function at all.
- `nslots` = `highest_virtual_function_number + 1`, or 0 if it is
  `VIRTUAL_FUNCTION_NUMBER_NONE` (65535: a class with only virtual bases; a loop
  from 0 to `highest` would run 65536 times). `FIRST_VIRTUAL_FUNCTION_NUMBER` is
  0 (targ_def.h:2964), so no offset is added to `number.virtual_function`.
- **Data size (dsize) of a class that has virtual bases** (the normative rule is
  in the `VIRTUAL_BASE` row of 12.2). `ecil_data_size_of_type` (`ecil_type.c:1079`)
  stops at the non-virtual part. Clang's dsize of the complete object is the
  maximum of that non-virtual data size and, for every *non-empty* virtual base
  `V` (direct and indirect, as listed in `base_classes_of(t)` with `is_virtual`),
  `offset(V) + nvsize(V)`: a `BASE` or `VIRTUAL_BASE` field that is not
  `EMPTY_FOR_ABI` ends at its offset plus the `nvsize` of its class (its
  `CLASS_INFO`, or its `data_size` if the class has none), and the writer sends
  `data_size` = the largest end of any field. (Below, `ndsize` is `nvsize` of V: for a class that is POD for layout it is
  the full size, for any other the end of its last non-virtual member, which is how
  the FE and clang report it.) This reproduces clang for all 11 classes of
  `v3_layout` that have virtual bases (clang's dsize in parentheses):

  | Class | non-virtual data | virtual bases (offset, ndsize) | dsize |
  | --- | ---: | --- | ---: |
  | `B : virtual A` | 0 (no own vptr) | A (0, 8) | 8 (8) |
  | `L : virtual V1 { int i; }` | 12 | V1 (12, 4) | 16 (16) |
  | `F : virtual E0 { int i; }` (E0 empty) | 12 | E0 empty, skipped | 12 (12) |
  | `H : G, virtual E0 { char h; }` | 9 | E0 empty | 9 (9) |
  | `I : virtual G`, `J : virtual I` | 0 | G (0, 8) | 8 (8) |
  | `V2 : virtual V1 { int j; }` | 12 | V1 (12, 4) | 16 (16) |
  | `M : virtual V2 { int m; }` | 12 | V2 (16, 12), V1 (28, 4) | 32 (32) |
  | `N : virtual V1, virtual V2 { char n; }` | 9 | V1 (9, 4), V2 (16, 12) | 28 (28) |
  | `O : N { char o; }` | 10 | V1 (10, 4), V2 (16, 12) | 28 (28) |

  `m4_fe_facts` 4.4 compared dsize only for classes without virtual bases; the
  checker should compare this formula for the others. The ECIL 1.1 rule "dsize is
  at least the end of the last field" must count the `VIRTUAL_BASE` fields at
  `offset + ndsize` (the draft says it does).
- The `.base` type's size is `nvsize` (K7 finding 1), and is a distinct type iff
  `nvsize != size`.

### 2.3 `VIRTUAL_BASE` fields

IL: `base_classes_of(t)` entries with `is_virtual` (il_def.h:8094): one entry per
virtual base, direct or indirect, `offset` = byte offset in the **complete object
of t** (8184-8190), `type`, `direct`. Probes: `E : D`, `D : A, B`, `A, B : virtual V`
(`v7_vbase_casts`): `E` has one entry for `V`; `V` is also in `D`, `A` and `B`.

- Field order: **the front end's postorder base list** (`base_classes`, the `next`
  chain), keeping the virtual entries, **not** `preorder_base_classes` (found while
  implementing S2: for `N : virtual V1, virtual V2` with `V2 : virtual V1` the
  preorder list has `V2` before `V1`). The constructor list is built from that
  postorder list (decl_inits.c, `ctor_initializer`: depth first, left to right: V1,
  V2), and the `MEMINIT(vbase, idx)` groups must have strictly increasing `idx`
  (3.4); the validator rejects any other order.
- `EMPTY_FOR_ABI`: the base type is an empty class (`is_empty_class`).
  `is_optimized_empty_base` is unreliable (`m4_fe_facts` section 3).
- A virtual base that is the primary base of `t` or of one of its bases has no
  storage of its own in `t`'s record (K7 rule 3): the library decides from
  `CLASS_INFO.primary_idx` and the bases' facts, the writer sends the field and
  its offset (0).

### 2.4 `CLASS_BASES`

IL: `class_type_supp(t)->direct_base_classes` threaded by `next_direct`, in
`direct_base_number` order (il_def.h:8169; the first is 1). The `PUBLIC` flag is
the `access` of the derivation with `direct == TRUE`
(`a_base_class_derivation.access`, il_def.h:7993; a direct virtual base has
several derivations, take the direct one). The field index is that of the `BASE`
or `VIRTUAL_BASE` field of the base. The `is_pack_expansion` entries do not
occur in a complete class.

### 2.5 `CLASS_INFO.primary_idx`

IL: `primary_base_class` of the *type supplement* (il_def.h:8759), the base entry
that shares the vptr, NULL if none. It is a direct non-virtual base at offset 0,
a direct virtual base, or an **indirect virtual** base (`E : virtual D` with `D :
virtual C`: primary `C`). The `base-class` entry's own `primary_base_class`
field (il_def.h:8036) is a different thing (null for a virtual base that was
allocated inside another base). Use the type supplement's. The index is that of
the `BASE` or `VIRTUAL_BASE` field, which exists in all three cases because
`VIRTUAL_BASE` includes indirect virtual bases.

### 2.6 `CLASS_VFUNCS`

IL: the routine entries of `class_type_supp(t)->assoc_scope->routines` (the
class scope's list, declaration order, implicit members last: probe `v1_slots`
`B`: `f`, `h2`, then the implicit `~B`), with
`rout->is_virtual` (il_def.h:12354), `rout->number.virtual_function` (13115),
`pure_virtual` (12365), `is_deleted` (12992), `compiler_generated` (12451),
`special_kind == sfk_destructor`.

- `slot` = `number.virtual_function`. A destructor takes two numbers: the
  complete destructor (D1) gets `n`, the deleting one (D0) `n+1`; the next
  function gets `n+2` (`A { f; ~A; g }` is 0, 1 (and 2), 3). The writer sends
  the D1 G-id (`ecil_function_id` of the destructor routine, which returns the
  complete variant).
- **A consteval virtual function has no slot.** `number.virtual_function` is
  `VIRTUAL_FUNCTION_NUMBER_NONE` (65535, il_def.h:13115 "undefined when
  is_consteval"; `update_virtual_function_number`, class_decl.c:6898, skips it)
  and clang gives it none either (`v1b_consteval`: vtable `A` has `g`, `h`
  only). The writer skips `is_consteval` functions in `CLASS_VFUNCS` **and in
  the override lists** (`B`'s list has an entry `A::f -> B::f` for the
  consteval `f`; `m4_fe_facts` did not mention it).
- **Order** (12.2): declaration order of the class scope's routine list, **implicit
  virtual functions last and sorted by slot.** The FE lists implicit members in the order
  in which it declared them (on demand), which is not the slot order when both an
  implicit destructor and an implicit `operator=` override a base's virtual functions; the
  writer sorts the implicit entries by `number.virtual_function` (stable) before it
  sends the record.
- Flags: `PURE` = `pure_virtual`; `DELETED` = `is_deleted` (`P { virtual void d() =
  delete; }`: number 1, `is_deleted`); `IMPLICIT` = `compiler_generated`. An implicit
  virtual destructor is in the list and has the base's number, but is `defined`
  only once used (the FE defines it when the vtable is needed, 8.2).
- Numbering rules (primary-chain reuse, new numbers for overriders of
  non-primary bases, covariant return over a *direct* virtual primary base takes
  a new number) are the FE's and agree with clang in 441 of 441 slot checks
  (`m4_fe_facts` section 2); my probes reproduce them: `D0 : R0` covariant
  `clone` reuses 0; `D2 : B2, B2 : virtual RB` covariant `get` in `D2` keeps 0
  (the primary of `D2` is the non-virtual `B2`, not a virtual base: the rule in
  `m4_fe_facts` section 2 item 4 is about a *direct* virtual primary); `Z : X, Y`
  with `Z::h` overriding `Y::h` gets the new number 1.
- **`sig`** is section 3.5.
- The G-id of every function named here must exist before the record: send
  `ecil_function_id` first. A function of a class template instance that was not
  instantiated is only declared: a declaration, as for any other call.

### 2.7 `CLASS_OVERRIDES`

IL: for every entry `b` of `base_classes_of(t)` (**all** entries: direct and
indirect, repeated non-virtual bases once per subobject, virtual bases once),
the list `b->variant.overriding_virtual_functions` (il_def.h:8227; guard
`!b->is_pack_expansion`) of `an_overriding_virtual_function` (il_def.h:7850):
`primary_function` (the overridden function, declared in `b->type`),
`overriding_function`, `base_class` (the entry, on the list of `t`, of the class
that declares the overrider; **NULL when it is `t` itself**),
`return_adjustment_base_class` (ignored by the writer: the library derives
adjustments).

Mapping to `(g_fn, sub_off, g_ovr, ovr_off)`: `g_fn` = the G-id of
`primary_function` (D1 for a destructor), `sub_off = b->offset`, `g_ovr` = the
G-id of `overriding_function`, `ovr_off = base_class != NULL ? base_class->offset
: 0` (for a virtual base `offset` is the complete-object offset, as the draft
requires). Only overridden functions have an entry; no entry means final.

- **One entry per (base entry, primary function) in a valid program.**
  `copy_virtual_function_override_list` (class_decl.c:5403) drops an override that is
  dominated by one already on the list and replaces a dominated entry, and
  `report_virtual_function_ambiguities` (class_decl.c:5213) diagnoses a
  non-dominated pair. CHECKING asserts at most one per pair. The list is sorted
  by the number of the primary function.
- **Per entry, not per class.** Probe `v2_overriders`, class `D : B, C` with
  `B, C : A` (output of `edg-cir-fe-facts facts`):

  ```
  base A offset 0 shares-vptr         A::f() -> D::f()
  base B offset 0 direct shares-vptr  B::f() -> D::f()
  base A offset 16 shares-vptr        A::f() -> D::f()   A::g() -> C::g() (in base C)
  base C offset 16 direct
  ```

  The two `A` entries have different lists. `final_overrider()`
  (exprutil.c:19000) finds *the* entry by type (`find_base_class_of`) and must
  walk the `eok_base_class_cast` chain when the base is ambiguous: that is a
  lookup the writer must not use. `A::g -> C::g` in the second entry shows the
  rule: the overrider belongs to an intermediate class, `ovr_off` is the offset of
  the *entry of C* in D (16).
- A virtual base reached through another base's primary chain is a single entry
  (`Msg : virtual Ent`, `Ent : virtual Base`, `Msg2 : Msg`): `Base::m -> Msg2::m`
  at `Base`'s offset 0, `Ent::m -> Msg2::m` at `Ent`'s offset 0 (`v2_overriders`).
- Classes that occur only as bases need their own lists, because the
  construction vtables use them: `CLASS_OVERRIDES` is sent for every dynamic
  class that gets `CLASS_INFO`, from that class's own entries.
- An overrider that is an implicit destructor not yet defined needs its
  `DECL_FUNC` (and the `STRUCTOR` pair): declare it; its body comes from 8.2.

### 2.8 `CLASS_MANGLE`

IL: none; the mangling is computed. `mangled_typeinfo_string(t)`
(lower_name.c:15219) is the Itanium `<type>` and is what the FE uses inside the
names of the class's member functions, so the library's `_ZTV/_ZTI/_ZTS/_ZTT/_ZTC`
agree with them. Measured values (instrumented writer, `n1_names`,
`l1_local_names` classes):

| Class | `mangled_typeinfo_string` |
| --- | --- |
| `A` | `1A` |
| `Tm<int>`, `Tm<N::Pub>`, `Tm<Anon>` | `2TmIiE`, `2TmIN1N3PubEE`, `2TmIN12_GLOBAL__N_14AnonEE` |
| `Anon` in the anonymous namespace | `N12_GLOBAL__N_14AnonE` |
| `N::Hid` (anonymous namespace inside `N`) | `N1N12_GLOBAL__N_13HidE` |
| local class `Loc` of `local()` | `Z5localvE3Loc` |
| local class of an inline function | `Z9in_inlinevE3Loc` |
| closure `[](int){...}` of a global variable | `N3lamMUliE_E` |
| local class of that closure's `operator()` | `ZNK3lamMUliE_clEiE3InL` |
| `typedef struct { virtual void f(); } TN;` first mangling | `Ut_` |

The anonymous-namespace forms contain no module id: they are what clang
produces and are deterministic (spec 11.2). The last row is the **late name**:
the class is introduced (it is needed by the body of its own inline member) before
the typedef declarator gives it a name for linkage; `get_mangled_function_name`
later says `_ZN2TN1fEv`, which is also what clang says (`2TN`, `_ZTV2TN`). The
writer sends `CLASS_MANGLE` with the mangling it has, and **re-issues it before
`F_NAMES` for every class whose name may change** (the draft allows it; the
predicate is the class half of `cir_name_may_change`, scope_stk.c:9609:
`originally_unnamed && type_is_unnamed(type) && !local && !class_member &&
!closure`, an anonymous union parent, a structured-binding closure, a parent with
a parentless default-argument lambda). `ecil_resolve_late_names` (ecil_type.c:2803)
is the place; CHECKING re-mangles every class and asserts equal for the rest.

### 2.9 Ordering: layout facts right away, vtable facts at first need

12.2 splits the facts of a class in two groups, and the writer sends them in two places.

**Layout facts** (`CLASS_MANGLE`, `CLASS_INFO`, `CLASS_BASES`).

- **Right away** for a **dynamic** class (r3 confirmation review): a class whose record has a
  `TY_VPTR` field, a `VIRTUAL_BASE` field, or a `BASE` field of a dynamic class. The
  predicate is a new helper, `class_is_dynamic(t)` = `any_virtual_functions_including_in_base_classes
  || any_virtual_base_classes` (il_def.h:10484 and 10454; the latter counts indirect virtual
  bases). **Not `needs_virtual_function_table`** (il.h:3962): it is the *own* `any_virtual_functions`
  (or `any_virtual_base_classes`), so it is FALSE for `struct B : A {}` with `A` polymorphic,
  which must get its facts right away (`struct D : B, virtual V {}` has primary base `B`, and
  `D`'s `CLASS_INFO` needs `B`'s `CLASS_INFO`; Boost's `clone_impl` hierarchy has this shape).
  It is not `class_has_own_vptr` either (2.1), which is FALSE for `B`. CHECKING asserts that the
  predicate equals "the record has a `TY_VPTR`, a `VIRTUAL_BASE` or a `BASE` of a dynamic class".
  The facts follow directly after the `TY_RECORD_BODY` and its `REC_FIELD`s. The hook is the record-body path of
  `ecil_type.c` (the function that completes a pending record, `ecil_type.c:1195`
  onwards; `ecil_complete_pending_records` calls it): after the fields, when the predicate
  holds, call `ecil_send_layout_facts(frame, t)` (layout facts only: it costs no `DECL_FUNC`). A record body with a `BASE`,
  `VIRTUAL_BASE` or `POTENTIALLY_OVERLAPPING` (`[[no_unique_address]]`) field of such a
  class needs that class's `CLASS_INFO` first, because the library needs it to build the
  record (the `.base` type is `nvsize` bytes; a primary virtual base has no storage of its
  own): `std::stringstream` has a vptr and a virtual base even in a TU that never calls a
  virtual function. This holds by construction, because a record's by-value members and
  bases are completed before the record (they supply its size), and the right-away rule
  sends the layout facts with each of them. CHECKING asserts it (every such field's class
  has `ECIL_GF_LAYOUT_FACTS`).
- **At first need** for every other class (a class that is not dynamic): a class whose typeinfo is declared (and every base of it: the library derives
  `__si`/`__vmi` from `CLASS_BASES`, so a thrown non-dynamic class with bases needs
  `CLASS_INFO` and `CLASS_BASES` with `nslots` 0), and a class named by `CATCH_PARAM` or
  `DYNAMIC_CAST`. Its `nvsize` is its `data_size` until then.
- **Bases first, within each group** (12.2, re-review). The `CLASS_BASES` of a class
  needs the layout facts of each *dynamic base* (those were sent right away with that
  base's body, so this holds by construction and is a CHECKING assertion). A plain base
  (not dynamic: no virtual function, own or inherited, and no virtual base) is **not**
  given layout facts there: its facts are sent only by the records that use them (a class
  typeinfo with `DEFINED_HERE`, `CATCH_PARAM`, `DYNAMIC_CAST`). In particular nothing but
  `STR`/`LOC`/`EXT` records may sit between a right-away class's `TY_RECORD_BODY` and its
  `CLASS_MANGLE`: with `struct Q { int q; }; struct P : Q { int p; }; struct D : P { virtual
  void f(); };`, `P` and `Q` need no facts at `D`'s body (`D`'s own `CLASS_MANGLE`,
  `CLASS_INFO`, `CLASS_BASES` follow it directly, `CLASS_BASES` naming the `BASE` field of
  `P`). The facts a needing record asks for are sent bases-first by an explicit stack of
  classes (as `run_type_stack`: the inheritance depth scales with the input; recursive
  tuple inheritance), a class's `CLASS_VFUNCS` waiting for the vtable facts of each of its
  *dynamic* bases (the vtable group below). A `DEFINED_HERE` class typeinfo of `D` needs
  the typeinfo of `P` and `P`'s layout facts (the library derives `__si`/`__vmi` from the
  chain), which come at that record.
- `CLASS_MANGLE` precedes `CLASS_INFO` and may be re-issued until `F_NAMES` (a late
  typedef name for linkage).

**Vtable facts** (`CLASS_VFUNCS`, `CLASS_OVERRIDES`, the `DECL_FUNC`s they name and
`STRUCTOR_DELETING`) **stay first-need**: `ecil_class_facts(frame, t)` (in `ecil_type.c`,
`ECIL_GF_VTABLE_FACTS` in `an_ecil_gid_info.flags` so that it runs once) sends them before
the first record that needs them, never at class completion:

1. Ensure the record body and the layout facts are complete (`ecil_type_id`; a pending
   record is completed first by `ecil_complete_pending_records`). A record that failed
   (`ECIL_GF_FAILED`) makes the need unsupported.
2. Run the same for every direct *dynamic* base first (the explicit stack above); a plain
   base needs nothing here.
3. Send `DECL_FUNC` for every virtual function (`ecil_function_id`; for a destructor the pair
   and `STRUCTOR`, and for a virtual one the `DECL_FUNC` of D0 immediately followed by
   `STRUCTOR_DELETING`, 3.2), then `CLASS_VFUNCS` and `CLASS_OVERRIDES` (dynamic classes
   only).
4. **A need record for a dynamic class whose vtable facts have not been sent is an error**
   (12.2): every call site below goes through `ecil_class_facts`, and CHECKING asserts the
   flag at the record.

The needing records are the normative list of 12.2: `VPTR_INIT` and `VBASE_CAST`,
`VTT_ADDR`, `CALL VIRTUAL`, `DYNAMIC_CAST`, `CONST_METHOD`/`C_METHOD` with `VIRTUAL`,
**`C_AGG_BEGIN` of a dynamic class** (and of each dynamic class that the constant contains as
a member or base), `DECL_ABI_DATA`, `CATCH_PARAM`. The call sites in the writer are the
ones that emit them: the `VPTR_INIT` in the constructor/destructor path (`emit_function`,
4.2), `emit_class_cast` for `VBASE_CAST` and `VTT_ADDR`, `do_call` for `CALL VIRTUAL`,
`do_delete` for `DELETE VIRTUAL`, the typeinfo code (7), `begin_dynamic_aggregate` and the
constant emission in `ecil_const.c` for `C_AGG_BEGIN`. All of these write to the declaration
section of the current frame, which precedes the body, so the "introduced before first use"
rule holds. A class of a function memory region (a local class) is described in that
region's frame while its entries are alive.

Classes that are never needed are never described beyond the layout facts of the right-away
group: a header full of unused polymorphic classes costs no `DECL_FUNC`s. (Describing every
dynamic class at completion, through the `ecil_note_class_completed` hook, would introduce a
`DECL_FUNC` for every virtual function of every class of every header.)

## 3. ABI data, structors, deleting destructors and `sig` (draft 1b)

### 3.1 `STRUCTOR` and `has_vbases`

M3 already pairs the variants (`introduce_base_variant`, `ecil_type.c:2494`: the
routine's `ecil_function_id` is the complete variant and `partner` the base one,
`ecil_function_body_id` gives the one a body is streamed under). `has_vbases`
= `any_virtual_base_classes` of the constructor's or destructor's class
(il_def.h: type, `any_virtual_base_classes`). The `STRUCTOR` record is sent when
the pair is introduced; the flag must be known then, so the class type is
completed first (`ecil_type_id` of the class, as for the function type).

### 3.2 `STRUCTOR_DELETING`

One per virtual destructor routine, sent with the destructor's class facts
(2.9): `g_d1` = the complete variant, `g_d0` = a new G-id introduced by **its own
`DECL_FUNC` immediately before the `STRUCTOR_DELETING`** (12.3), with D1's type, linkage
and flags and the name `_ZN..D0Ev` (the writer already builds the C1/C2/D1/D2 names with
`cir_structor_name`, lower_name.c; D0 is the same helper with the deleting kind), `g_delete` =
the function of 3.3, `flags` = `SIZED`/`ALIGNED`. The class of `g_d1` must be complete (its
`TY_RECORD_BODY` sent) before this record, and before a `STRUCTOR` with `has_vbases`. The D0 of a destructor that is
`= delete`d or whose class's operator delete is deleted is not sent (the FE
diagnoses the virtual-destructor case: decl_inits.c:10620).

Two review rules (12.3). **D0 is defined iff D1 is defined here**: the `DECL_FUNC` of D0 is
issued, re-issued and marked defined together with D1's (`ecil_reissue_function` and
`reissue_one_function`, `ecil_type.c:2651`, re-issue the pair; D0 is a third member of that
group with D1's linkage, flags and `DEFINED`), so a virtual destructor that is only
declared here (its definition is in another TU) has a declared D0, and the library
synthesizes the D0 body exactly when D1 has a body. **`SIZED`/`ALIGNED` must match the arity
of `g_delete`'s type** (1 + `SIZED` + `ALIGNED`): ECIL cannot tell `size_t` from
`align_val_t`, so the library checks the parameter count; the writer takes the flags from
`is_default_operator_delete` (3.3) and CHECKING asserts the arity (the table of 3.3 is
that arithmetic: `AL2`'s `(void*, align_val_t)` is `ALIGNED` only, 2 parameters).

### 3.3 The operator delete of D0

IL: `class_type_supp(cls)->assoc_operator_delete_routine` (il_def.h:9139, under
`DELETE_CAN_BE_FOLDED_INTO_DTOR`, which is on; the field is in the display).
It is **the routine the front end selected**: `find_class_assoc_operator_delete_routine`
(class_decl.c:22325) looks up the class's `operator delete` (or the global one) and
runs `find_default_operator_delete_sym` (symbol_tbl.c:11761), which implements
the usual-deallocation-function rules: a destroying delete if the set has one
(only destroying ones are then considered), the aligned version for an
over-aligned type, a class with both an unsized and a sized version uses the
unsized one, and for a global one the sized version when sized deallocation
is enabled and the type is complete. It is set by
`set_class_assoc_operator_delete_routine` (class_decl.c:22361) when a destructor
is defined (decl_inits.c:10655) or a constructor with a new is (10125), so it is
NULL for a class whose destructor was never processed; the writer calls the
setter itself if it finds NULL (a symbol lookup that does not depend on the
current scope).

Probe `v5_delete` (`--c++20`), values of `assoc_operator_delete_routine`:

| Class | The routine | `SIZED` | `ALIGNED` | |
| --- | --- | --- | --- | --- |
| `G { virtual ~G; }` | global `operator delete(void*, size_t)` | yes | no | |
| `S` (class sized only) | `S::operator delete(void*, size_t)` | yes | no | |
| `S2` (class sized and unsized) | `S2::operator delete(void*)` | no | no | the unsized one |
| `alignas(32) AL` | global `(void*, size_t, align_val_t)` | yes | yes | |
| `AL2` (class aligned) | `AL2::operator delete(void*, align_val_t)` | no | yes | |
| `DD` (class destroying delete) and `Dd : DD` | `DD::operator delete(DD*, destroying_delete_t)` | | | destroying: diagnose |
| `NP` (class unsized) | `NP::operator delete(void*)` | no | no | |

`is_default_operator_delete(routine, &sized, &aligned, &destroying)`
(symbol_tbl.c:11678) returns the three flags. Delete **expressions** of these
classes have `nds->routine == NULL` (the delete is folded into the destructor
call; `dynamic_init` is `dik_none` with `destructor = ~G`), and for `DD` there
is no dynamic init at all; `delete_routine_of` (`ecil_code.c:3912`) already falls
back to `assoc_operator_delete_routine`, so `DELETE VIRTUAL`'s `fn` and
`STRUCTOR_DELETING.g_delete` come from the same field of the *static* class, as
the draft's validation needs. (For a virtual destructor the delete expression does not
call the function; D0 of the dynamic type does.)

### 3.4 What the constructor and destructor lists contain

IL: `scope->variant.routine.constructor_inits` of a constructor's or
destructor's function scope: `a_constructor_init` entries (il_def.h:16866-16872
kinds). A destructor's list is the destruction list, in destruction order.
Output of `il_query.py inits` for `v6_vbase_structors` and `v7_vbase_casts`
(`D : A, B`, `A, B : virtual V`, `E : D`, `F : virtual W`, `G : F`):

```
== A sfk_constructor       vbase V (user init), field m
== ~A sfk_destructor       field m, vbase V (compiler_generated)
== D sfk_constructor       vbase V, direct A, direct B, field dm
== ~D sfk_destructor       field dm, direct B, direct A, vbase V
== E sfk_constructor       vbase V (user init V(9)), direct D
== ~E sfk_destructor       direct D, vbase V
== G sfk_constructor (implicit)  vbase W, direct F       (W is G's indirect vbase)
== ~G                      direct F, vbase W
```

- **The list holds every virtual base of the class, indirect ones too, first
  (constructor) or last (destructor), also for implicitly defined members.** `G`
  has no virtual base of its own and the list has `W`, constructed by `G`'s
  constructor and not by `F`'s base-variant. So: `C1` sends `MEMINIT(vbase, idx)`
  for each `cik_virtual_base_class` entry, `C2` skips them; `D2` skips the
  `cik_virtual_base_class` entries, `D1` is synthesized from them (a
  `CLEANUP_BEGIN(all)` per virtual base with a non-trivial destructor, **begun in the order
  of C1's `MEMINIT(vbase)` groups**, then `CALL D2`; 4.4 item 6 gives the walk).
- The initializer of a virtual base entry (`init=dik_constructor V`) is the call
  of V's constructor. Like every base subobject it is built by the **base
  variant** (C2), with the VTT of the class being constructed
  (`VTT_ADDR(vbase, idx)`) if V itself has virtual bases (clang's
  `EmitBaseInitializer` does the same); its own virtual bases are separate
  entries of the same list.
- Entries of a delegating constructor: one `cik_delegation` entry, no vbase
  entries (`D(int) : D()` in `v6`); the delegation calls C1 from C1 and C2 from C2
  (draft 1b).
- Order: vbases (inheritance order), direct bases (declaration order), fields.
  This is also the `MEMINIT` kind order the draft requires.

### 3.5 `sig`

The front end's overload identity of a virtual function is its routine type's
`param_type_list` (`a_param_type`, il_def.h: "In C++, any top-level type
qualifiers have been removed", array and function parameters are decayed;
`declared_type` keeps the original) and the method qualifiers in the routine
type supplement (`this_qualifiers`, il_def.h:7522). **Ref-qualifiers are ignored**
(`ref_qualifiers`, 7530, is not compared): 12.2 follows clang's
`HasSameVirtualSignature`, which compares name, parameter types and the cv of `this`
only (`void f()` and `void f() const` differ; `void f() &` and `void f() &&` do not).
The return type is not part of it.

The writer therefore assigns `sig` itself: a table keyed by the routine's name
(an `id-name`, or the conversion type for a conversion function) and then
compared with `f_identical_types` over the parameter list and an equality of
the two `this_qualifiers`, with ids 1, 2, ... in order of first encounter
(deterministic, no pointers in the stream). 0 for destructors.

**Why not ECIL type G-ids:** an ECIL type carries no cv and no distinction that
the encoding does not need. `f(A*)` and `f(const A*)` are one ECIL pointer type;
`f(long)` and `f(long long)` are one 64-bit integer; `f(char)` and `f(signed
char)` one 8-bit signed integer. Clang's `MethodsCanShareVCallOffset` tells them
apart, so a `sig` made from G-ids would let two functions share a vcall offset
that clang gives separate slots.

Cost: one table lookup per virtual function and a type comparison per candidate
of the same name; it runs once per class.

## 4. Function headers and bodies: calls, vptr, casts, structors (draft 1b, 1c)

### 4.1 Calls: `CALL VIRTUAL`, `VTT`, `NOTHROW`

IL: `eok_dot_member_call` / `eok_points_to_member_call` (il_def.h:14246-14272)
with `is_virtual_call` (15262: "the call uses virtual semantics"; FALSE for a
qualified name). Operand 1 is the `enk_routine` of the **statically named**
function, operand 2 the selector (a class glvalue or a pointer). The front end
wraps the selector in a compiler-generated `eok_base_class_cast` to the class
that declares the function, so argument 0 is already of `g`'s class (the draft's
rule); the function is the one found by name lookup, whose
`number.virtual_function` is the slot (an overrider reuses its base's).
Probe `v8_calls` (output of `il_query.py`, abridged):

| Source | `is_virtual_call` | Note |
| --- | --- | --- |
| `pd->g()`, `d.f()` through a reference, `a.f()`, `pb->g()` | TRUE | dispatch |
| `f()` inside `D::D()` and `~D()` (`this->f()`) | TRUE | dispatch (the vtable has D's own entries by then) |
| `pb->h()` where `h` is `final` | FALSE | clang devirtualizes it too |
| `d.A::f()`, `pd->D::g()` | FALSE | qualified name |
| `a.~A()` (explicit destructor call) | TRUE | `CALL VIRTUAL` of D1's slot |
| `local.f()` where `local` is a `D` object | **TRUE** | clang calls directly |
| `c.f()`, `r.f()` in `usingcall` (`UC : UB { using UA::f; }`, `UB::f` overrides `UA::f`) | TRUE | the callee is `UA::f` and the selector is cast to `UA` (`eok_base_class_cast`); the object `c` gives a plain call of **`UB::f`** (the final overrider in `UC`); the reference `r` stays virtual |
| `p->g()`, `r.g()`, `o.g()` on a `final` class `FB` that declares `g` | FALSE | the named function is `FB::g` |
| `p->h()`, `o.h()` on `FB final : FA` where `h` is declared only in `FA` | TRUE | the selector is cast to `FA`: look through the cast to `FB` (`final`), plain call of `FA::h` (final in `FB`) |
| `c.f()` in `covcall` (`CC : CB { using CA::f; }`, `CB::f` overrides `CA::f` with a covariant return) | TRUE | stays virtual: the final overrider's return type differs |
| `d.f()` in `covcall` (`CD : CB`) | TRUE | names `CB::f` (the lookup finds the overrider): plain call of `CB::f` |

Writer: `do_call` (`ecil_code.c:2321`, the "a virtual call" gate at 2394) sets
`CALL VIRTUAL` (0x10) and sends the vtable facts of `g`'s class first (2.9), **after
the devirtualization below** (12.6: a plain call needs none). A call to a pure virtual
function through the vtable of an object under construction is the program's problem.

**Devirtualization (normative since the review, 12.6).** For a member call with
`is_virtual_call` TRUE, `g_named` the routine of operand 1, `s` operand 2:

1. Look through the compiler-generated `eok_base_class_cast` chain of the selector (one
   node per cast; the object selector for a dot call, the pointer for an arrow call). What is
   left is the **object expression** `o` and its class `C_obj` (the pointee class for a
   pointer).
2. The call is **plain** iff (a) `o` is a complete object that is neither a reference nor
   an indirection (an `enk_variable`, `enk_temp_init` or a member of one, of class
   type; not `eok_ref_indirect`, not `eok_indirect`), or (b) `C_obj` is `final`
   (`class_struct_union.final`, il_def.h:10435, read from `C_obj` **after** the cast is
   looked through: `p->h()` above, where the FE's `is_virtual_call` is TRUE because the
   named function's class `FA` is not final), or (c) `g_named` is `final` (the FE gives
   FALSE already). Otherwise it stays `CALL VIRTUAL` of `g_named` on the cast operand.
3. A plain call calls the **final overrider of `g_named` in `C_obj`**, not `g_named`:
   - find the entry `bcp` of `class_of(g_named)` in `base_classes_of(C_obj)` (the entry
     itself when the classes are equal: `g_named` is then final in `C_obj`; the lookup the
     CLASS_OVERRIDES code of 2.7 uses). For a repeated non-virtual base pick the entry whose
     `offset` equals the sum of the offsets of the cast chain's steps (a virtual step: the
     virtual entry); in a well-formed program the entry is unique, since an ambiguous base is a
     diagnosed lookup, so the offset test is a safeguard;
   - search `bcp->variant.overriding_virtual_functions` for `primary_function ==
     g_named` (D1 for a destructor). Found: `g_ovr = overriding_function`, declared in the
     subobject at `ovr_off` (`base_class->offset`, or 0 when `base_class` is NULL, the class
     `C_obj` itself); not found: `g_ovr = g_named`;
   - the `this` argument is the object address converted to `g_ovr`'s class: no cast when
     `ovr_off` designates `C_obj`, else `BASE_CAST` (or `VBASE_CAST` with `COMPLETE` for a
     virtual entry: the object is a complete object or of a `final` class, so every offset
     is static);
   - emit `CALL g_ovr` without `VIRTUAL`.
4. **Covariant returns keep the dispatch**: when the return type of `g_ovr` is not
   identical to `g_named`'s (the `covcall` rows), clang's `getDevirtualizedMethod` returns
   null and the vtable call stays; so does the writer (the library applies the return
   adjustment thunk). Checked with the pin on `covcall`: `c.f()` loads the vtable,
   `d.f()` calls `_ZN2CB1fEv`.
5. A qualified call (`d.A::f()`) has `is_virtual_call` FALSE and calls exactly `g_named`;
   an explicit destructor call (`a.~A()` through a reference) stays virtual, and on an
   object (`local.~D()`) it is a plain call of the final overrider's D1 (the override
   lists contain the destructors: `P::~P() -> Top::~Top()` in `v6b_vbase_order`).

Probe `v8_calls` (`usingcall`, `fin`, `covcall`) holds the shapes; all print the same under
g++.

`CALL VTT` (0x20): required iff the callee is the base variant of a `has_vbases`
pair. The sites: the base and virtual-base initializers in `do_meminit`, the
base destructor calls in `emit_cleanup`/`emit_dtor_prologue`, a delegation in a
`has_vbases` class (C2 to C2, `VTT_ADDR(self)`), and D1's call of D2. The
operand is `VTT_ADDR(base, idx)`, `VTT_ADDR(vbase, idx)` or `VTT_ADDR(self)`, the
`idx` being the field index of the `BASE`/`VIRTUAL_BASE` field (the writer
already has `ecil_gid_of(base_class) - 1` for `BASE` fields).

`NOTHROW` (0x08): the callee's `DECL_FUNC` has `NOTHROW`, or for an indirect
or member-pointer call `is_nothrow_type` of the pointer's function type (works
on `tk_ptr_to_member` too; types.c:10490). For a virtual call the callee is the
statically named function: clang uses the declared type too. **A re-issue never clears
`NOTHROW`** (12.6): the flag is `is_non_throwing_routine(rout)` when the `DECL_FUNC` is
composed (`compute_func_state`, `ecil_type.c:2335`), but the answer can change between
issues (the exception specification of an implicit member is completed at its first use, a
deferred `noexcept(expr)` at instantiation), and a wrong flag silently skips cleanups: keep a
sticky bit in `an_ecil_gid_info.flags` (`ECIL_GF_NOTHROW`), set at the first issue and OR'd
into `state` by `compute_func_state` (so by `reissue_one_function` for both variants and
for D0). The call's own `NOTHROW` flag is the call-type bit (`is_nothrow_type`), which
does not depend on the callee's flag.

### 4.2 `VPTR_INIT`

IL: nothing; the writer places the marker. The constructor path is
`emit_function` plus `do_meminit` (`ecil_code.c:7059`, `5562`):

- **Constructor** (C1, C2; not a delegating one): after the last `cik_virtual_base_class`
  and `cik_direct_base_class` group (which come first in the list, 3.4), before the
  first `cik_field` group and before the body statements. If the list has no field
  entry the marker comes at the end of the groups; if there is no list at all (an
  implicit constructor of a class with only an empty list) it comes at the start of the
  body. A class is "dynamic" for this if it has virtual functions or virtual bases,
  even if it has no own vptr (it still stores its vtable address into the shared one).
- **Destructor** (D2, or the only body of a class without virtual bases): right
  after `emit_dtor_prologue` (`ecil_code.c:5666`). Not in the synthesized D1.

### 4.3 Casts: `BASE_CAST`, `VBASE_CAST`, and why M3 rejected them

`emit_class_cast` (`ecil_code.c:1543`) looks for the target among
`direct_base_classes` and diagnoses an indirect base and a virtual one. The IL
is: **one `eok_base_class_cast` per node, to a direct or an indirect base**
(`E : D` casts `E` to `V`, an indirect virtual base, in one node; `v7_vbase_casts`).
For a base that is reached through a virtual base the front end decomposes: `X&
x = e;` with `X` a base of `V` is `E -> V` and then `V -> X` (two nodes).

New algorithm (explicit loop, no recursion): find the entry `bcp` of the target
class in `base_classes_of(derived)` (`identical_types`; never ambiguous, the
front end diagnoses that in the source).

- `bcp->is_virtual`: `VBASE_CAST(derived, target, flags)`; `COMPLETE` when the
  object is the complete object of the constructor or destructor being written
  (`this` in C1/D1: static offset from `bcp->offset`), `NONNULL` by the rule
  `emit_class_cast` already has.
- else if the derivation has a virtual step (`any_virtual_steps_in_derivation`,
  il.h:3955; the steps are `cast_derivation_path_of(bcp)`, il.h:3951): the path
  from the first virtual step on is `VBASE_CAST` to the virtual class and then one
  `BASE_CAST` per remaining step with that step's `offset`. The decomposed nodes
  of the front end already end in this case, so this is a guard.
- else `BASE_CAST(derived, target, bcp->offset, flags)` (the offset of the
  unique non-virtual path).

`eok_derived_class_cast` through a virtual base is ill-formed. Member access
through a virtual base is the same cast as its first operand
(`e.v` where `v` is a member of `V`: `eok_dot_field` with a compiler-generated
cast `E -> V`).

### 4.4 The double walk of a `has_vbases` structor

The front end has one body, the constructor or destructor function scope
(`emit_function`, `ecil_code.c:7059`). With `has_vbases`:

1. Send the declaration facts once (`ecil_reissue_function` re-issues both
   variants together, as M3 does).
2. A constructor is walked twice, first for `g_complete` (C1) and then for
   `g_base` (C2): `ecil_rec2(frame, eop_func_begin, gid, 0)` ... `FUNC_END` twice,
   in one `F_REGION` ("a function and its lambdas ... one after another",
   ecil_format.md section 4). A destructor is walked once, for `g_base` (D2);
   its `g_complete` (D1) is synthesized from the destruction list and needs no
   walk. The work stack is empty after `run_code_stack`, so the second
   walk starts clean; `suppress_load_node` and the aggregate levels are reset at
   the start of every run (the R2 fix).
3. Per-walk state to reset: `next_lid` and `ecil_end_lid_epoch(next_lid)` between
   the walks (the second walk gives variables new L-ids: `ecil_lid_of` is epoch
   based), `static_temp_count`, `this_lid`, `pending cleanups`.
4. State that must **not** be duplicated:
   - **`make_static_temp` (`ecil_code.c:2136`) creates a new `egk_var` G-id and
     a new `DECL_VAR` at every call**, so the second walk of a body that has a
     lifetime-extended temporary in a function-local static
     (`static const A& r = A(1);`) would define `_ZGRZ..._` twice. Key it by (owner
     variable G-id, sequence number) in a small table. Everything else that
     creates a G-id in a body is keyed already: types and string literals by
     content (`ecil_word_lookup`), variables and routines by IL entry
     (`ecil_gid_of`), lambda closures through their class types.
   - Lambdas: a lambda's call operator and closure class are separate function
     scopes of the same memory region and are handled by the loop of
     `ecil_stream_function_region` (`ecil_code.c:7501`), not by either walk:
     streamed once, as the draft requires. A lambda inside the constructor
     (`Lam::Lam() : V(0), x([]{ return 5; }())`) gets its closure type and
     operator() G-ids at the first walk; the second walk finds them.
   - Static locals: `DECL_VAR` and the `VAR_INIT` of the constant part once
     (`ecil_drain_variable_definitions` after the walks); `STATIC_INIT` in both
     bodies (K5a). `ECIL_GF_NEEDS_STATIC_INIT` is a flag of the variable, so both
     walks agree.
   - `ecil_set_function_state(rout, efs_streamed)` and `ECIL_GF_DEFINED`
     once, after the pair; `ecil_function_is_handled` is read between walks by
     nobody.
5. Failure: take one `ecil_frame_mark` before the first walk. If either walk
   is unsupported, truncate to the mark (both bodies go) and re-issue both
   `DECL_FUNC` as unsupported, as `emit_function` does for one.
6. The content differs by variant only here: C1 sends the `cik_virtual_base_class`
   entries as `MEMINIT(vbase)`, C2 skips them; D2 skips the `cik_virtual_base_class`
   entries of the destruction list, and D1 is synthesized (not walked: no FE body
   to walk, 3.4). **The D1 order** (12.3): the `CLEANUP_BEGIN(all)` of the virtual bases are
   begun in the order of C1's `MEMINIT(vbase)` groups, so that they run in reverse
   construction order. That is the walk of `emit_dtor_prologue` (`ecil_code.c:5666`: the
   destruction list from its end) restricted to the `cik_virtual_base_class` entries,
   because the FE's destruction list is exactly the reverse of its constructor list. Probe
   `v6b_vbase_order` (`Top : L1, virtual R, L2` with `L1 : virtual Q, virtual P`, `L2 :
   virtual R`; g++ prints `Q P R L1 L2 Top ~Top ~L2 ~L1 ~R ~P ~Q`): the constructor list is
   `vbase Q, vbase P, vbase R, base L1, base L2`; the destruction list is `base L2, base L1,
   vbase R, vbase P, vbase Q`; walking the latter from its end and keeping the virtual
   bases begins Q, P, R (running R, P, Q). A class whose virtual bases have trivial
   destructors begins none, and D1 is just the `CALL D2`. CHECKING asserts that the second walk introduced no G-id
   (`ecil_num_gids()` before and after) except those from lazily created
   `DECL_FUNC`s of functions that the first walk did not reach (none by
   construction: the bodies are equal).

### 4.5 `DELETE VIRTUAL` and `ARRAY_PARTIAL_DTOR`

- `do_delete` (`ecil_code.c:3935`, gate at 3959): `VIRTUAL` (0x08) when the
  destructor of the pointee class is `is_virtual` and the expression is not
  `::delete` (`nds->global_new_or_delete`); `fn` = `delete_routine_of(nds)` of
  the static class (3.3). `delete[]` of a polymorphic class destroys the elements
  with the complete variant and no vtable: no change.
- `ARRAY_PARTIAL_DTOR(g_dtor)` goes directly after each `ARRAY_CTOR_BEGIN` that
  the writer emits for a default-constructed array (a local array, a member
  array in a constructor, `new T[n]`/`new T[n]()` with a constructor loop:
  `array_default_construct` at `ecil_code.c:4818` and `do_new_delete`) when
  exceptions are on and the element has a non-trivial destructor. The front end has no
  entries for these (the loop is implicit); aggregate initialization has them,
  5.5.

## 5. Exceptions (draft 1c "Exceptions", 1d)

### 5.1 `try`, handlers and the function-try-block

IL (flattened in the display): `stmk_try_block` (il_def.h:15834) with
`is_function_try_block`, `statement` (the body block), `handlers` (a list of
`a_handler`, il_def.h:16316: `parameter`, `statement`, `dynamic_init`;
`parameter` and `dynamic_init` are NULL for `catch (...)`), `lifetime`.
Mapping: `TRY_BEGIN(0)`, the body, then per handler `CATCH_BEGIN(g_ti|nil)`, an
optional `LOCAL l` and `CATCH_PARAM`, the handler block, and `TRY_END` (do_stmt,
`ecil_code.c:6173` is where the M3 `stmk_try_block` is diagnosed).

**Function-try-block.** For every function that has one, the function scope's
`assoc_block` *is* the `stmk_try_block` and `is_function_try_block` is TRUE,
a plain function included (`f` in `e6_ftb`):

```
FUNCTION C sfk_constructor ci=<mem-initializers>
  stmk_try_block ftb=TRUE
    stmk_block               (the body; the last statement is the compiler-generated return)
    handler
      stmk_block             (the user's handler, then a stmk_expr that is
                              compiler_generated: an enk_throw with throw_info == NULL)
FUNCTION ~C sfk_destructor ci=<destruction list>
  stmk_try_block ftb=TRUE ... the same, with the same implicit rethrow
FUNCTION f   (plain)  stmk_try_block ftb=TRUE
```

- The **constructor's mem-initializers and the destructor's destruction list
  are on the scope** (`constructor_inits`), not in the try's statements: they
  are inside the try by the language. The writer therefore sends, for a
  constructor or destructor whose `assoc_block` is such a try, `TRY_BEGIN(FUNCTION_TRY)`
  right after the header (`PARAM`s and `FUNC_EH_SPEC`) and *before* the `MEMINIT`
  groups, `VPTR_INIT` and `emit_dtor_prologue`; then the try's body block as the
  statements; then the handlers; `TRY_END`. For a plain function it sends an
  ordinary `TRY_BEGIN(0)` around the body (the draft's rule).
- **Does a destructor's try enclose the cleanup prologue? Yes.** Clang's
  `EmitDestructorBody` enters the try before the epilogue cleanups ("if the body
  is a function-try-block, enter the try before anything else"), so a member
  destructor that throws is caught by the handler, as the language requires
  ([except.handle]), and the library's `FUNCTION_TRY` state machine (draft 1c)
  already continues the `VPTR_INIT` and prologue inside the try body.
- **The implicit rethrow is supplied by the front end** (a `compiler_generated`
  `stmk_expr` at the end of the handler of a constructor or destructor; the
  statement's `end_of_block_reachable` tells whether it is reachable). The draft
  says "the writer emits an explicit `RETHROW` at each reachable handler end":
  the writer only translates that `enk_throw`. A handler of a plain function
  has none (`f` returns from it).
- D1 of a `has_vbases` class is synthesized and has no try; the try belongs to D2
  (and to C1 and C2, which are walked twice).

### 5.2 `CATCH_BEGIN`: the type

`g_ti` is the typeinfo of the parameter's type with references and top-level cv
removed (`eff_type_for_typeinfo`, lower_eh.c, is the FE's version; the
parameter types of arrays and functions are already pointers in the IL).
`catch (const char *const &s)` has the typeinfo of `const char *`
(`_ZTIPKc`). Section 7.3.

### 5.3 `CATCH_PARAM`: kinds from `a_handler.dynamic_init`

Output of `il_query.py handlers` for `e1_try` (the IL of each handler: parameter
type, kind of `dynamic_init`):

```
catch(int i)                   init=dik_bitwise_copy
catch(T t)  [T has a copy ctor] init=dik_constructor routine=T(const T&) destructor=~T
                                 is_copy_constructor_with_implied_source
catch(Tr t) [trivially copyable] init=dik_bitwise_copy
catch(int *p)                  init=dik_bitwise_copy
catch(const char *const &s)    init=dik_bitwise_copy
catch(Base &b)                 init=dik_bitwise_copy
catch(...)                     parameter NULL, dynamic_init NULL
catch(const int &) / catch(int)  (unnamed)  the variable exists, init=dik_bitwise_copy
```

The init kind alone does not give the kind (`dik_bitwise_copy` is used for
everything that needs no constructor), so the writer decides from the parameter's
type first:

| Parameter type | Kind | `g_copy` |
| --- | --- | --- |
| reference to a pointer whose pointee is a record (`R *&`, `R *const &`) | `reference_to_record_pointer` 7 | nil |
| reference to any other pointer (`const char *const &`, `int *&`) | `reference_to_pointer` 6 | nil |
| any other reference | `reference` 0 | nil |
| pointer | `pointer` 1 | nil |
| arithmetic, enum, `bool`, `nullptr_t`, pointer to data member, **pointer to member function** | `scalar` 2 | nil |
| class, `dik_bitwise_copy` | `trivial_copy` 4 (`l` is a record) | nil |
| class, `dik_constructor` | `non_trivial_copy` 5 (`l` is a record) | `ecil_function_id` of the copy constructor (`di->variant.constructor.ptr`, the C1) |

This is the normative 12.5 table (6 and 7 as above; a pointer to member function by value
is kind 2, "as CIRGen classifies them", not 4). The type of `l` is a record for 4 and 5 and a
pointer to a pointer for 6 and 7 (the natural type of a reference to a pointer, as
`declare_variable` makes it; the pin's verifier requires it). `construct_catch_param` starts the
handler for kinds 5 and 6 only, and kind 7 binds the reference to a temporary copy of the
pointer (so assigning through it does not change the exception object: the documented
departure), which is invisible to the writer.

- An **unnamed** parameter still has the variable (`catch (B)` in `e1c_unnamed_catch`):
  g++ and clang both copy-construct and destroy it (`B copy`, `~B` appear),
  so the writer sends `LOCAL` and `CATCH_PARAM` for it as for a named one.
- A destructible parameter: its `dynamic_init->destructor` (`~T`) is begun as the
  usual `CLEANUP_BEGIN(all)` after `CATCH_PARAM` (the helper `emit_local_cleanup`),
  which is nested in the library's `end_catch` scope.
- **Diagnose** a catch copy whose constructor call has more than the implied
  source argument (a default argument) or whose `init_expr_lifetime` has
  temporaries: the library's single-block thunk cannot hold them (the draft).
  Detect with `di->variant.constructor.args` (one implied argument) and
  `lifetime_needs_cleanups(di->init_expr_lifetime)`.
- The `catch (...)` handler must be last (the front end enforces it).

### 5.4 `throw`

IL: `enk_throw` (il_def.h:15434 supplement): `throw_info == NULL` is a rethrow
(`throw;`, and the implicit one of 5.1: the display shows an `enk_throw` with
no `type`/`dynamic_init`/`destructor` lines); otherwise `type` (the thrown type,
arrays and functions decayed: `const char *` for a string literal),
`dynamic_init` (how the exception object is initialized) and `destructor`
(NULL if trivial). Output of `il_query.py throws` for `e1_try`:

```
THROW type=int       dtor=NULL init=dik_expression   (throw 1)
THROW type=T         dtor=~T   init=dik_constructor  (throw T(2), args)
THROW type=Tr        dtor=NULL init=dik_constant     (throw Tr{3})
THROW type=int *     dtor=NULL init=dik_expression
THROW type=const char *          init=dik_expression
THROW type=Der       dtor=~Der init=dik_constant {{}} (throw Der(): a class with a vptr!)
THROW type=double    dtor=NULL init=dik_expression
THROW type=void                  (rethrow)
```

Mapping: `THROW_BEGIN(t)` pushes the glvalue of the exception object; the
initializer consumes it: `dik_expression` as `STORE INIT` of the value,
`dik_constructor` as `this` of the call (the constructor's own operands, with
`setup_ctor_call`), `dik_constant` as `INIT_CONST` of the constant;
`THROW_END(g_ti, g_dtor|nil)` with `g_dtor` = the complete destructor of
`destructor`. A rethrow is `RETHROW`. The thrown expression is a full
expression whose temporaries (`throw E(A(1), 2)`) are destroyed when the
exception object is constructed, **but not in the first draft's way**: 12.5 follows clang
(user decision 2026-10-12): `THROW_BEGIN` is *not* a region. The writer wraps the
expression in the `FULLEXPR` that `do_expr` already opens for an `enk_object_lifetime` with
cleanups (`lifetime_has_cleanups`); the temporaries' `CLEANUP_BEGIN`s are ordinary cleanups of that
`FULLEXPR`, so on the normal path they run during unwinding (`std::uncaught_exceptions()` is 1
there; g++ gives 0). `THROW_BEGIN` itself begins an implicit `DEACTIVATABLE` eh cleanup that
frees the exception object, and `THROW_END` deactivates it (the writer sends no `CLEANUP_END` for it:
`CLEANUP_END` never matches the implicit cleanup). Every `DEACTIVATABLE` cleanup begun after
`THROW_BEGIN` (the delete cleanup of a `new` in the operand, partial-aggregate elements) must be
ended before `THROW_END`: they are, because they end when their initialization completes, which is
before the throw. So `throw T(*new U)` is valid and needs no diagnosis. A throw is valid wherever an
expression is, including a `TERN` arm, `MEMINIT` and `STATIC_INIT`, but not in cleanup code (a
destructor call is not an `enk_throw` in cleanup code in any case). Worked encoding (7) of 12.10 is
the shape. Records may follow `THROW_END` and `RETHROW` (dead code), as after `UNREACHABLE`.

**As implemented (S4): the `FULLEXPR` goes around the conditional region, not in the
arm.** The validator's rule is an *enclosing* `FULLEXPR`, so a `FULLEXPR` opened inside
the arm does not satisfy it. The writer opens one around the outermost `?:`, `&&` or
`||` of a full expression that has none open, when its conditional operands may contain a
throw or a new with a free cleanup (`conditional_needs_fullexpr`; the value is carried
out of it, so a class prvalue result is diagnosed). The text that follows is the
first draft.

**A throw in a conditional region needs its own `FULLEXPR`** (12.5: "in a conditional region, a
`THROW_BEGIN` needs an enclosing `FULLEXPR` in the same function or `STATIC_INIT`", because
the library guards the implicit free cleanup as it guards a conditional `new`). The writer
opens a `FULLEXPR` only where `lifetime_has_cleanups` (`do_expr`), and probe `e7_cond_throw`
shows the IL does not give one for a throw that has no temporaries: all seven throws of the
probe have `inside_conditional_expression` TRUE and only the two `E(A(n))` throws have a
lifetime (`il_query.py throws`/`lifetimes`):

```
tern     c ? throw E(A(1)) : 5     lifetime: dik_constructor ~A (conditional)
tern2    c ? 5 : throw 7           none
lor      b || (throw E(A(2)), false)   lifetime: ~A
land     b && (throw 3, true)      none
sinit    g ? throw 9 : 4           none (inside a function called by a dynamic initializer)
s2       int s2 = g ? throw 9 : 5; none (a namespace-scope initializer: STATIC_INIT)
loc      static int l = g ? throw 8 : 6;   none (a static local's STATIC_INIT)
```

So the rule is the writer's own: **every `enk_throw` that the writer meets inside a conditional
region (a `TERN` arm, the right operand of `LAND`/`LOR`, the state the writer already tracks for
conditional temporaries and a conditional `new`) is bracketed `FULLEXPR_BEGIN` ...
`FULLEXPR_END` (void), in the arm itself, around `THROW_BEGIN ... THROW_END`.** A throw that is
already inside the `FULLEXPR` of its lifetime does not need another one, but a second nesting is
harmless and the simple rule needs no test: do it whenever the conditional depth is non-zero
and the throw is not itself the whole `FULLEXPR`'s expression. The shape in a `STATIC_INIT`
(`s2`, `loc`) is the same, with the `FULLEXPR` inside the `STATIC_INIT` region. An
unconditional throw needs none (its implicit cleanup is unconditional). Expected output of the
probe, under g++ and the other back end alike: `~A1 E1 i7 ~A2 E2 i3 5 5 1 0` (the temporaries are
destroyed before the handler runs; the `std::uncaught_exceptions()` difference of the user
decision does not show here).

`RETHROW` is invalid in cleanup code (12.5): a `throw;` inside a destructor call that runs as
cleanup does not arise from the IL (cleanup code is only the destructor or delete call), so
the writer needs no test beyond CHECKING.

**Note the `Der()` row.** A `dik_constant` of a class with virtual functions is
a constant that has no vptr in the FE's representation (`{{}}` lists the base
subobject only). In 1.2 **no record names a vptr in a constant** (12.7): the writer sends the
`C_AGG` of the members and bases that the FE lists, never selects the `TY_VPTR` field, and the
library stores every vptr (from the vtable group of the complete object for a base
subobject, from the member's own class for a member). The writer's duties are the class
facts (`CLASS_*`) of every dynamic class whose `C_AGG_BEGIN` it streams, marking the vtable of
the complete class and of each member class "used" (a base subobject's `C_AGG_BEGIN` does not use the
base's own vtable), and never producing `C_ZERO` of a dynamic class (a constant of one lists its
members). `ecil_const.c:1150` is the gate that diagnoses it today; `ecil_init_level`'s index
mapping (`lead`) must skip the `TY_VPTR` field.

### 5.5 The EH-only destructions

In the IL they are **entries of the lifetime's destruction list** (il_def.h:4098-4107),
not statements, and the front end puts them in destruction order (the one that runs
first is first):

```
LIFETIME olk_expr_temporary            (new T(A(1), B(2), f))
    DI dik_constructor dtor=~B  unordered has_temporary_lifetime
    DI dik_constructor dtor=~A  unordered has_temporary_lifetime
    DI dik_none        dtor=operator delete  is_freeing_of_storage_on_exception     <- last
LIFETIME olk_expr_temporary            (new U[3]{ U(1), U(f?9:2), U() })
    DI dik_constructor dtor=~U  destruction_is_for_partially_constructed_aggregate  (x3)
    DI dik_none        dtor=operator delete[]  is_freeing_of_storage_on_exception is_array_freeing
LIFETIME olk_block                      (U loc[4] = { U(1), U(2), U(9), U(4) })
    DI dik_nonconstant_aggregate dtor=~U var=loc      <- the normal destructor of the variable
    DI dik_constructor dtor=~U destruction_is_for_partially_constructed_aggregate  (x4)
```

and the `a_new_delete_supplement` of the `new` has the same entry as
`freeing_of_storage_on_exception` (il_def.h:14745). The M3 writer
(`lifetime_needs_cleanups`, `ecil_code.c`) skips both flags, which is what
`exceptions = 0` allows.

Mapping (ECIL: `CLEANUP_BEGIN(eh, g|nil, DEACTIVATABLE)`, the destructor or
delete call, `CLEANUP_BODY`; `CLEANUP_END`):

- **`new` (free storage on exception).** In `do_new_delete`, phase 3 to 4
  (after the allocation `CALL`): keep the returned pointer in a local, begin the
  cleanup `CLEANUP_BEGIN(eh, DEACTIVATABLE)` whose body loads the local and
  calls the delete function, then the initialization (its temporaries nest inside:
  the order `~B ~A operator delete` of the list above is the LIFO order), and
  `CLEANUP_END` right after the initialization completes (the constructor
  returned): from then on the object is owned by the program. The normative shape
  is worked encoding (10) of 12.10: the size is stored once in a `LOCAL` *before*
  the allocation call (`LREF l9 CONST_INT size_t N STORE INIT`), the call takes it
  (`LREF l9 LOAD`), the result is stored in a pointer `LOCAL`, and the free cleanup is
  `LREF l5 LOAD LREF l9 LOAD CALL _ZdlPvm`; with an unsized delete the size operand
  is absent, an aligned one also takes the alignment, a placement delete the placement
  operands (kept in locals). The same cleanups are valid in `STATIC_INIT` (12.6): a
  namespace-scope `T *p = new T(...)` or `U g[3] = {...}` gets them, the range ending
  with the `STATIC_INIT` region. Details that the stream must carry that the IL gives:
  1. The delete function is `nds->freeing_of_storage_on_exception->destructor`
     (a routine). Probe `e4_new`/`v5_delete`: global **sized** `operator
     delete(void*, size_t)` for a class without its own, `operator delete[](void*,
     size_t)` for arrays, the class's own (`S::operator delete(void*,
     size_t)`), the **aligned** one for an over-aligned type, and for a placement
     new the matching placement `operator delete(void*, int)`.
  2. A sized delete takes the **size passed to the allocation function** (for
     an array: the whole allocation, the cookie included); the pointer is the
     one the allocation function returned (before the cookie is skipped). Keep
     both in locals. The placement arguments of a placement new are passed
     again: keep their *values* in locals (they are not re-evaluated).
  3. `PL` (class `operator new(size_t, int)` with `operator delete(void*, int)`
     and `operator delete(void*)`) gets `operator delete(void*, int)` here and
     `operator delete(void*)` for `delete p`: the right one, as the standard says.
  4. **A class that has only a destroying `operator delete` gets it here too**
     (`Dd : DD`, `v5_delete`: `freeing: operator delete(DD*, destroying_delete_t)`),
     which would run a destructor on storage that holds no object. Diagnose it
     (`is_default_operator_delete(..., &destroying)`), as D0 does.
  5. When the front end has no entry (`freeing_of_storage_on_exception == NULL`)
     there is nothing to free.
  6. `inside_conditional_expression` is TRUE on **all** the entries of the lifetime of
     a `new`, also in an unconditional `T *mk() { return new T(A(1), B(2), f); }`
     (`e4b_new_flag`: both `mk` and `cond` show it). Do not use the flag to decide the
     conditional shape; the writer knows whether the `new` is in a `TERN` arm or the
     right operand of `LAND`/`LOR` from its own state, as it does for temporaries.
- **Deactivate before the range ends** (12.6, `CLEANUP_BEGIN`): a `DEACTIVATABLE` cleanup must be
  ended by its `CLEANUP_END` before its range ends (otherwise a later throw runs it a second
  time). Every begin site of this section satisfies it by construction: the `new` cleanup ends
  right after the initialization, an element cleanup at `AGG_INIT_END`, a `THROW_BEGIN` cleanup
  at `THROW_END`. A cleanup begun in `STATIC_INIT` ends with the region at the latest
  (`U g[3] = {U(1), U(2), U(9)};`, `T *p = new T(...);`). The writer counts the open
  deactivatable cleanups per frame region and asserts zero at `FULLEXPR_END`, `STATIC_INIT`
  end, `FUNC_END` and the region closes of a scope (CHECKING); a `return` inside an initializer
  cannot occur, and a `goto` out of a `FULLEXPR` is not an expression.
- **A partially constructed aggregate** (`destruction_is_for_partially_constructed_aggregate`
  on the `di` of an element or member): in `do_agg`, after the element's initialization
  (`dik_constructor` with a destructor), `CLEANUP_BEGIN(eh, DEACTIVATABLE)` whose
  body destroys that element, computed from a pointer local that holds the aggregate's
  address (the `AGG_INIT_BEGIN` consumed the glvalue); at `AGG_INIT_END` as many
  `CLEANUP_END` as were begun *at that level* (innermost first), then the variable's own
  `all` cleanup (do_var_init phase 1, as in M3). For a `new U[3]{...}` there are the
  element cleanups and the storage cleanup: all of them are ended in LIFO order, the
  elements before the storage (the draft: "two deactivatable cleanups needs two").
  The element and the member cases are the same code (`Agg g{A(1), A(2)}`).
- **Constructor mem-initializers** (`is_constructor_init`) already get their
  `eh` cleanup after each `MEMINIT_END` (`do_meminit`, `ecil_code.c:5562`: `emit_cleanup(...,
  ecl_eh, ...)`), which has been correct since M3 and ends with the function; no
  change except the virtual base entries (`CALL D2` with `VTT_ADDR(vbase, idx)`).
- **Default-constructed arrays** have no entries (the loop is implicit): the writer's own rule,
  `ARRAY_PARTIAL_DTOR` (4.5).
- `overlaps_temps_in_inner_lifetime` marks an entry that must not count as on the cleanup
  list "until the entity has actually been initialized": the begin-after-init placement
  above is that rule.

### 5.6 Exception specifications

IL: the routine type's `exception_specification` (`an_exception_specification`,
il_def.h:7129): `is_noexcept`, `throw_any`, `indeterminate`, `compiler_generated`,
`from_attribute`, `noexcept_arg` or the type list; and `routine->never_throws`
(il_def.h:12555). Output of `il_query.py specs` for `e3_spec` and `e3b_spec17`:

| Declaration | `exception_specification` | `never_throws` |
| --- | --- | --- |
| `void f() noexcept` / `noexcept(true)` | `is_noexcept` (`noexcept_arg` true) | TRUE |
| `noexcept(false)` | `is_noexcept`, `throw_any` | FALSE |
| `template<class T> noexcept(sizeof(T) > 2)` | prototype: `throw_any` (dependent); instances: `noexcept_arg` true or `false`+`throw_any` | per instance |
| `throw()` (`--c++14` **and** `--c++17`) | not `is_noexcept`, empty type list | **TRUE** |
| `throw(int)`, `throw(int, char)` | type list | FALSE |
| implicit destructor `~N`, `~S` | `is_noexcept`, `compiler_generated` | **FALSE** |
| implicit destructor `~W` (a member has `noexcept(false)`) | none | FALSE |
| implicit `operator=` etc. | `indeterminate`, `compiler_generated` | |
| `operator delete(void*) noexcept` from `<new>` | `is_noexcept` | TRUE |
| trivial default constructor, trivial copy | | `is_non_throwing_routine` is TRUE through `is_trivial_*` |

- `NOTHROW` on `DECL_FUNC` and on calls: `is_non_throwing_routine(rp)`
  (types.c:10525; it completes a defaulted function's specification and
  instantiates a lazy one, then `is_nothrow_type`, types.c:10490, which is TRUE
  for everything when `exceptions_enabled` is off) and `is_nothrow_type` of the
  function type for an indirect call. It can run FE code that resolves an
  `indeterminate` specification (`resolve_indeterminate_exception_specification`,
  `complete_defaulted_exc_spec`, `instantiate_exception_spec_if_needed`): it is called
  from the writer's declaration and call code, which already run at the function
  end hook, and gives the same answer whenever it is called.
- `FUNC_EH_SPEC(noexcept)`: the **narrower** predicate: the routine type's specification
  is `is_noexcept && !throw_any && !from_attribute` after resolution (`is_nothrow_spec`
  minus the attribute case): `noexcept`, `noexcept(true)`, an instance with a true argument,
  and the implicit destructor or deallocation function with a compiler-generated one.
  `__attribute__((nothrow))` and a trivial member are `NOTHROW` only (no terminate scope).
  The draft's "`noexcept` without `DECL_FUNC.NOTHROW` is an error" holds: `is_noexcept`
  implies `is_nothrow_type`.
- `FUNC_EH_SPEC(dynamic, [g_ti...])`: a non-`is_noexcept` specification with a type list
  (`throw(int, char)`), the typeinfos of `exception_specification_type_list` entries
  (skip `redundant`; a pack expansion is not possible in a body). **`throw()` has
  an empty type list and `never_throws` TRUE in `--c++17` too**; the FE keeps it as a
  dynamic specification, so the writer must map an empty list to `noexcept` when the
  language mode is C++17 or later ("C++17 `throw()` is `noexcept`", draft): the writer
  tests `cpp17_mode` (lang_feat.h:2590) and sends `noexcept`, otherwise `dynamic` with an
  empty list.
- `FUNC_EH_SPEC` goes in the `PARAM` slot, once per definition, before any body record; for a
  constructor with a function-try-block it precedes `TRY_BEGIN(FUNCTION_TRY)`. A `noexcept` one
  wraps the **whole function** (member and base initializers, `VPTR_INIT`, a destructor's
  cleanup prologue and the body), outside any `FUNCTION_TRY` (12.4).

### 5.7 Range-based for with `try`

IL (`e5_rangefor`): `stmk_range_based_for` has `range`, `begin`, `end`,
`iterator`, `ne_call_expr`, `incr_call_expr` and no try. A `try` in the loop body
is a `stmk_try_block` in the body block; `break` and `continue` in the body, in the
try block and in the handlers are `stmk_goto` to the compiler-generated labels
(`label->break_label`, `continue_label`) that `do_stmt` maps to `BREAK_STMT`
and `CONTINUE_STMT`. K4 verified jumps out of try bodies and handlers. No new
record; `__range` (a temporary bound to `auto&&`, `mk()` above) is a lifetime-extended
local with the `all` cleanup of the existing path, and if it or the iterator
throws, the `eh` half of that cleanup runs.

### 5.8 Header and language bits

- `HDR_LANG.exceptions` = `exceptions_enabled`. `ecil_write.c:2192` hard-codes 0 today
  ("The stream is a no-exceptions stream whatever the front end does"). The front end
  has `exceptions_enabled` on by default in C++ (`eccp`, `-tused` run above); with
  `--no_exceptions` every `is_nothrow_type` is TRUE, which agrees with the stream's
  exceptions = 0 rule.
- `rtti` = FE `rtti_enabled` (`--no_rtti`): without it a `typeid` or `dynamic_cast` is a
  front-end error already; the flag matters for the vtable's RTTI slot.
- **Switching on exceptions = 1 changes the whole C++ stream**: every call is a possible
  throw, so every `eh` cleanup the M3 writer already sends (members, bases, temporaries) is
  now live, and `NOTHROW` must be right everywhere. This is what the K4 spikes and the gate
  runs will exercise first.

### 5.9 A destructor that throws while cleanups run (what the library would need)

Observed with the corpus (`eh/init-temp1`, `eh/array3`, `eh/ref-temp1`, `eh/array2`): when
a destructor that may throw (`noexcept(false)`) runs as a normal-path cleanup, the
cleanups that remain (those of the enclosing scopes and full expression, the other
elements of the array) must still run as the exception unwinds. Today they do not:

- **Calls inside cleanup code must unwind into the enclosing cleanup scopes.** Clang pops
  the cleanup stack one entry at a time, so while the destructor of entry N runs on the
  normal path, entries N+1.. are still on the EH stack and a throw from it runs their EH
  cleanups. The consumer's normal-path cleanup blocks need the same property.
- **`ARRAY_DTOR` must continue on a throw.** Clang's `pushRegularPartialArrayCleanup` makes
  the elements not yet destroyed the cleanup of the loop, so a throwing element destructor
  destroys the rest; `cir.array.dtor` has no such cleanup (the constructor loop has
  `partial_dtor`).
- **A cleanup begun inside a `FULLEXPR` ends with it**, so the object whose initialization
  the full expression is (`b tmp(f(a(), a()))`) cannot be registered before the
  temporaries' destructors run: `eh/init-temp1` expects `~b` before the other `~a`.
  Either the variable's cleanup begins inside the `FULLEXPR` but belongs to the enclosing
  scope (a flag on `CLEANUP_BEGIN`), or the `FULLEXPR` must be able to enclose the
  declaration.

Update (session 3, after cir-lib's run tests against g++): at the pin, calls inside a
cleanup region are invokes into the enclosing cleanup scopes, so the first and the third
points hold for other pending locals, temporaries (one or several in a full expression)
and enclosing scopes, and the writer no longer diagnoses them (`emit_cleanup`). Still
diagnosed: the destructor of an **array element** that may throw (the second point: the
pin skips the elements before the one that threw). Two cases that clang itself gets
differently from g++ are not diagnosed and documented in writer.md: the element whose
default-argument temporary's destructor threw (`eh/array2`) and the returned object
(CWG 2176, `eh/return1`).

### 5.10 Front-end issue found by the corpus: no lifetime extension of a member of a prvalue (GNU mode)

Test: `tests/tests/cir/cirgen/cxx-s2-temporaries.sft.cpp`, function `member_ext`, and
`cir_src/doc/research/cxx_cir_differential.md` R2.

```
struct T { int id; T(int); ~T(); };  struct P { T a; int k; };
const int &k = P{T(5), 6}.k;
```

With `--g++ --gnu_version=150200` the front end's IL has the aggregate temporary `P`
with the lifetime of the full expression (`LOCAL` flag 2, its `eh`/normal cleanup is
inside the `FULLEXPR`) and a second, extended temporary of type `int` that holds a copy
of `.k`; the reference is bound to the copy, so `~T(5)` runs at the end of the
declaration (`T(5)`, `~T(5)`, `k=6`). g++ 15 and the front end's plain mode extend the
`P` temporary (`T(5)`, `k=6`, `~T(5)`), as [class.temporary] requires for a member
access of a prvalue bound to a reference. The writer streams what the IL says (the
stream shows the `P` cleanup in the `FULLEXPR` and the reference stored from the copy);
the decision is made by the front end in that mode, so it has to be fixed there. The
class-typed analogue (`const T &r = P{T(5), 6}.a;`) is right in both modes.

## 6. Member pointers (draft 1a, 1c, "Constants")

### 6.1 Types

IL: `tk_ptr_to_member` (`pm_member_type(type)`, `pm_class_type(type)`).
`ecil_type.c:614` makes a pointer to data member opaque storage of the right size
with `ECIL_GF_HAS_DMP` and diagnoses its zero-initialization (R1-10). M4: a pointer
to data member is `TY_DATA_MEMBER(g, t_member, t_class)`; a pointer to member
function is `TY_METHOD(g, t_func, t_class)`, `t_func` being the member function's
`TY_FUNC` with the object pointer first (the helper that `DECL_FUNC` of a member
uses, `this_class` of the routine type). `ECIL_GF_HAS_DMP` stays as the
"not zero-initializable" bit of 9.

### 6.2 Constants (`CONST_DATA_MEMBER`, `CONST_METHOD`, `C_DATA_MEMBER`, `C_METHOD`)

IL: `ck_ptr_to_member` (il_def.h:5219): `is_function_ptr`, `variant.field` /
`variant.routine` (NULL = the null pointer to member), `casting_base_class`,
`cast_to_base`. The FE builds a conversion as a **second constant** of the
converted type (`implicit_cast`) that carries `casting_base_class` and the original
`field`/`routine` (output of `il_query.py`, `m1_memptr`):

```
DD dd1 = &D::b2;   constant type=DD, implicit_cast, orig_type=int B2::*,
                   casting_base_class = B2 (in D), cast_to_base = FALSE, field = b2
int (B2::*)(int)  = &B2::g:   constant, casting_base_class NULL, routine = g
```

Values (computed by the writer; `lower_il.c:180` is not usable in this
configuration):

- Data member: `offset` is **the byte offset in the complete class of `t`** of the
  designated member (12.4, `CONST_DATA_MEMBER`): the member's own offset
  (`field->offset`, a non-bit-field; a member of an anonymous union or struct is
  diagnosed until verified) plus the offset of the non-virtual base subobject that contains it,
  for a conversion `casting_base_class->offset` when `cast_to_base` is FALSE
  (base-to-derived); for `cast_to_base` TRUE (derived-to-base `static_cast`) it is the member's
  offset minus that base's, diagnosed if negative. The library finds the field path by
  descending the `BASE` fields; the member type of `t` selects among union members at one
  offset. A member of a virtual base cannot be designated. Null: `C_DATA_MEMBER(t, nil)`
  (the library stores -1).
- Method: non-virtual `g` = `routine`, `adj` = `casting_base_class->offset` (the 12.4 `adj` is
  **non-negative**: a `cast_to_base` constant of a method would need a negative one and is diagnosed;
  the runtime `eok_pm_base_class_cast` is `PM_CAST to_base`);
  virtual: `CONST_METHOD ... VIRTUAL` with `g` = the virtual routine named (its
  slot is its `number.virtual_function` through `CLASS_VFUNCS` of its class: a function
  found in a non-primary base has the slot of that base's vtable and `adj` brings
  `this` there); null: nil. Method pointers to a virtual function need the class facts
  of the routine's class (2.9).
- A non-zero `adj` in an expression is a runtime conversion in the library, in a
  `VAR_INIT` the lowered global form (draft G2). **`C_METHOD` with a non-zero `adj` is valid only
  in `VAR_INIT`** (12.4); in a function body the writer sends `CONST_METHOD` and a `STORE` instead
  (also for an aggregate that contains such a constant: the element is stored separately), so the
  constant emitter needs to know whether it writes a `VAR_INIT` or a body constant.

### 6.3 Expressions

| IL operator (il_def.h) | ECIL | Notes |
| --- | --- | --- |
| `eok_pm_field` (14182) `x.*pm`, `eok_pm_points_to_field` `p->*pm` | `PM_FIELD(t_dm)` | object pointer first (take the address of the class glvalue; the pointer operand as is), then the pointer to member; result is a glvalue (load as for `eok_dot_field`) |
| `eok_dot_pm_call` (14274), `eok_points_to_pm_call` | `PM_CALL(t_call, nargs, flags)` | "the selector object is always evaluated first" (il_def.h): the draft's order; `NOTHROW` from `is_nothrow_type` of the pointer's member type; arguments and copy elision as `CALL` |
| `eok_pm_derived_class_cast` (13980) | `PM_CAST(to_derived, t_from, t_to, offset)` | the implicit `int B::*` to `int D::*` conversion |
| `eok_pm_base_class_cast` (13975) | `PM_CAST(to_base, ...)` | `static_cast` |
| `eok_bool_cast` of a pointer to member, `eok_eq`, `eok_ne` | `TO_BOOL`, `CMP eq/ne` | the draft's extension; a relational comparison is an error in the language |
| `eok_dot_pm_func_ptr`, `eok_points_to_pm_func_ptr` | unsupported | a g++ extension |

**Every pm cast node is one direct step.** `m3_pm_casts`: `int A::*` to `int C::*`
(A to B to C) is two `eok_pm_derived_class_cast` nodes, `int C::*` to `int M::*` one more;
so `offset` is the `offset` of the entry of the from-class in the `direct_base_classes` of the
to-class (derived-to-base: of the to-class in the from-class). A virtual base is ill-formed
there; diagnose if `is_virtual`.

## 7. RTTI: `typeid`, `dynamic_cast`, typeinfo (draft 1b, 1c, 2)

### 7.1 `typeid`

IL: `enk_typeid` (il_def.h:13600, supplement 15456): `type_with_opt_expr` is a
list of one or two nodes, the first `enk_type_operand` (the type, top-level
cv removed; for an expression operand the static type of the expression), the
second the operand expression if there is one; `is_dynamic` is TRUE when the result
depends on the dynamic type. In this configuration even `typeid(int)` is an
`enk_typeid` (the constant form `ck_address`/`abk_typeid` is only for constexpr
evaluation; the writer also handles that constant, as `C_ADDR` of the typeinfo).
`il_query.py` on `t1_typeinfo`:

```
typeid(a)   a is A&   dyn=TRUE   [type_operand A, operation eok_ref_indirect]
typeid(*np) np is A*  dyn=TRUE   [type_operand A, operation eok_indirect]
typeid(D) / typeid(int) / typeid(const int *) / typeid(int D::*) / typeid(int[3]) ...
                      dyn=FALSE  [type_operand only]
```

`TYPEID(g_ti|nil, t_result, flags)`: `g_ti` nil iff `is_dynamic` (the operand glvalue is
on the stack); `NULL_CHECK` iff the operand is an `eok_indirect` of a pointer
(`typeid(*p)`), not an `eok_ref_indirect`; static form: the typeinfo of the type operand
(7.3). `t_result` = the node's type (`const std::type_info`). The comparison
`typeid(a) == typeid(D)` is an ordinary call of `type_info::operator==`.

### 7.2 `dynamic_cast`

IL: `eok_dynamic_cast` (pointers, and `void *` targets, il_def.h:13985) and
`eok_ref_dynamic_cast` (13987: operand and result are glvalues, the node's type is `T`,
not `T&`). `DYNAMIC_CAST(g_src_ti, g_dst_ti|nil, t_result, flags)`: `g_src_ti` is the
typeinfo of the operand's static class (the pointee or the referent), `g_dst_ti` that of the
target class, nil for `void *`; `REF` for `eok_ref_dynamic_cast`. Both classes need
their facts (2.9), because the library computes the offset hint from the class graph.

### 7.3 Typeinfo: the closure per TU and the linkage

The front end's bookkeeping (`used_in_exception_or_rtti`, il_def.h:9810, set by
`set_used_in_exception_or_rtti_flag` at `typeid`, `dynamic_cast`, `throw`, `catch`
declarations and dynamic specifications, expr.c:20555, 22408, 22429, 38476,
decls.c:14684, declarator.c:2012; types without a name are also on
`il_header.nontag_types_used_in_exception_or_rtti`) exists to drive IL lowering's
`generate_typeinfo_vars` and to force the virtual functions
(`force_definition_of_typeinfo_for`, types.c:18146). The writer does not need it
for the set: it **introduces a typeinfo on demand**, at the record that needs it,
and the closure follows from the rules of the draft:

1. Introduce `DECL_ABI_DATA(typeinfo)` for the type the record names: the `THROW_END`
   type, the `CATCH_BEGIN` type, a `TYPEID` or `DYNAMIC_CAST` type, the types of a
   dynamic `FUNC_EH_SPEC`, and the class of every vtable that is emitted (the vtable's
   RTTI slot).
2. A `DEFINED_HERE` typeinfo needs its parts introduced first: for a class the typeinfo of
   each direct base (`class_type_supp(t)->direct_base_classes`; a virtual base too); for a
   pointer the pointee; for a pointer to member the member type and the class; for an
   array and for a function nothing (clang's `__array_type_info` and
   `__function_type_info` objects carry no members).
3. A type that is `incomplete` stays declared, internal, with the incomplete mask: **the typeinfo
   of an incomplete class** is a plain `__class_type_info` without `CLASS_INFO` or bases;
   **a pointer or member pointer whose pointee chain contains an incomplete class** (clang's
   `ContainsIncompleteClassType`: `I*`, `I**`, `const I*`, `int I::*`, `I* C::*`) has
   internal typeinfo, every pointer level of that chain has the `INCOMPLETE` mask (0x08),
   and a member pointer whose *class* is incomplete also has `INCOMPLETE_CLASS` (0x10).
   Measured on the pin with probe `t2_incomplete` (`typeid` of each type; linkage of the
   object): `_ZTIP1I`, `_ZTIPP1I`, `_ZTIPK1I`, `_ZTIM1Ii`, `_ZTIM1CP1I` (the `I* C::*` case:
   the class `C` is complete, the pointee chain is not: masks `INCOMPLETE` only) and `_ZTI1I`
   are **internal**; `_ZTI1C`, `_ZTIM1Ci` are `linkonce_odr` and `_ZTIi` external. The
   writer walks the pointee chain with the same explicit stack as the type id code, never by
   recursion.
   **The decision is taken when the typeinfo is first needed, and is sticky.** Clang decides at the
   point of use: `Later`, a class completed after `typeid(Later*)` in the TU, gives internal
   `_ZTIP5Later` **and** `_ZTI5Later`. The FE's `incomplete` flag flips when the class is
   completed, so a check at the end of the TU would differ from clang. The writer therefore
   takes a snapshot of "incomplete" at the first need of each typeinfo (the record that names it:
   function bodies are streamed at the end of the function, before a later completion) and keeps
   it in the typeinfo's `an_ecil_gid_info` (`ECIL_GF_TI_INCOMPLETE`); the re-issue in `F_GLOBALS`
   copies it and never changes the decision or the masks. A first need that comes from a
   file-scope initializer (`infos[]` in the probe: initializers of namespace-scope variables
   are streamed with the file-scope hand-off, 8) sees the class already completed, which is the
   one place this differs from clang (`_ZTIP5Later` `linkonce_odr` where clang has it internal); report
   it, it only changes which TUs share the object.
   **`TU_LOCAL` (0x04, `ECIL_ABI_DATA_TU_LOCAL`) on the typeinfo `DECL_ABI_DATA`** (re-review):
   the writer sets it from `cir_entity_is_tu_local(iek_type, t)` **applied to the whole type,
   pointee chains included** (`Hid *`, `int Hid::*`, `Hid **`: any component that is TU-local
   makes the typeinfo TU-local), on every issue and re-issue. It implies internal linkage, and the
   `*` that starts the name string of a TU-local type (user decision 2026-10-12, 12.3) is
   derived by the library **only from this flag**. Internal linkage alone is not enough: the
   incomplete-chain typeinfo `_ZTIP1I` is internal, but its name string is `P1I` in every TU.
   Checked with g++ on `t2_incomplete` (`g++ -S`): `_ZTSP1I` is `.string "P1I"`, and so are
   `_ZTSPP1I` (`"PP1I"`), `_ZTSPK1I`, `_ZTS1I` (`"1I"`), `_ZTSM1Ii` and `_ZTSM1CP1I`; no
   `*` (g++ makes them weak comdat names as it does for any complete type, and only a
   type that is TU-local gets the `*`: for `H` in an anonymous namespace g++ writes
   `"*N12_GLOBAL__N_11HE"` and, for `H *`, `"*PN12_GLOBAL__N_11HE"`). The writer's incomplete-chain decision (internal, masks) and the TU-local flag are
   thus two independent bits; the writer sets `TU_LOCAL` for the second only.

CHECKING cross-check: every type that has `used_in_exception_or_rtti` set and appears in a
streamed (not dropped) body must have been introduced by 1.

**The name of a non-class typeinfo** (`sid`, required for the non-class kinds) is
`mangled_typeinfo_name(type)` (lower_name.c:15184) of the *front end's* type after
`eff_type_for_typeinfo` (references and top-level cv removed; arrays and functions are
as they are): ECIL types have no cv, so the string, the cv masks and the linkage must
come from the FE type. The values measured in `n2_typeinfo_names` (clang's IR of
`typeid` of each type, linkage of the object it defines or declares):

| Type | Name | Clang linkage | Writer rule |
| --- | --- | --- | --- |
| `int`, `unsigned long long`, `wchar_t`, `char16_t`, `bool`, `__int128`, `long double`, `decltype(nullptr)`, `void` | `_ZTIi`, `_ZTIy`, `_ZTIw`, `_ZTIDs`, `_ZTIb`, `_ZTIn`, `_ZTIe`, `_ZTIDn`, `_ZTIv` | external (declaration) | `fundamental` |
| `int *`, `const int *`, `void *` | `_ZTIPi`, `_ZTIPKi`, `_ZTIPv` | external | pointer to a cv-unqualified or const fundamental type |
| `volatile int *` | `_ZTIPVi` | `linkonce_odr` | volatile pointee: not the standard-library set |
| `A **`, `const A *const *`, `A *` | `_ZTIPP1A`, `_ZTIPKPK1A`, `_ZTIP1A` | `linkonce_odr` | pointer |
| `int A::*`, `int (A::*)()`, `int B::*const *`, `void (A::*)() const` | `_ZTIM1Ai`, `_ZTIM1AFivE`, `_ZTIPKM1Bi`, `_ZTIM1AKFvvE` | `linkonce_odr` | pointer to member |
| `void (*)(int, ...)`, `void (*)() noexcept` | `_ZTIPFvizE`, `_ZTIPDoFvvE` (and the function types `_ZTIFvizE`, `_ZTIFvvE`) | `linkonce_odr` | pointer / function; the pointer carries the noexcept mask (0x40) |
| `int[3]`, `const char (*)[4]` | `_ZTIA3_i`, `_ZTIPA4_Kc` (and `_ZTIA4_c`) | `linkonce_odr` | array |
| `E`, `enum class EC : short` | `_ZTI1E`, `_ZTI2EC` | `linkonce_odr` | enum |
| `float _Complex` | `_ZTICf` | `linkonce_odr` | complex is not fundamental here |
| `I *`, `const I *` (`I` incomplete) | `_ZTIP1I`, `_ZTIPK1I`, and **`_ZTI1I`** | **internal** | an incomplete class makes the chain internal, including the class object (`incomplete`, `incomplete_class` masks) |
| `Hid *`, `HE` (anonymous namespace) | `_ZTIPN12_GLOBAL__N_13HidE`, `_ZTIN12_GLOBAL__N_12HEE` | internal | TU-local |
| class `A` with a vtable | `_ZTI1A` | per the vtable (8.1) | "goes with the vtable" |
| class `B` (no vtable object here) | `_ZTI1B` | `linkonce_odr` | dynamic class whose vtable is elsewhere: external declaration in the other TU, `linkonce` where the key-less rule applies |

The mask bits of `TYPEINFO_DEF(pointer, ...)` come from the FE type: const/volatile/restrict
of the pointee, `incomplete` (pointee incomplete or a pointer to an incomplete class),
`noexcept` (`is_nothrow_type` of a pointed-to function type in C++17), `transaction_safe`
is never set.

The FE's typeinfo-related manglers are available in the CIR configuration
(`mangled_typeinfo_name`, `mangled_typeinfo_string`, `mangled_typeinfo_string_name`,
`mangled_virtual_table_table_name`, `mangled_vtbl_name`, lower_name.h:88-110) and were
exercised in the instrumented run (2.8); the draft derives the class names in the library
from `CLASS_MANGLE`, so only the non-class `sid` is a writer string.

## 8. Emission and linkage decisions (draft 2)

All of these are decided in `ecil_resolve_late_names`' neighborhood, at the
hand-off of the file scope (the file-scope emission in `ecil_const.c`, which ends with `ecil_resolve_late_names`, and `ecil_hand_off_region` in `ecil_write.c`),
when the whole TU is known: key functions need it ("inline later", 8.1).
Everything is a declaration-section record sent in the globals frame.

### 8.1 Key function, vtable linkage, "used"

**Key function** (clang's rule, `ASTContext::getCurrentKeyFunction`; computed at the
end of the TU): the first own virtual function, in declaration order, that is
not pure, not `compiler_generated` and not `inline_in_class_definition` (il_def.h:12927;
TRUE for a body in the class, `inline` or `constexpr` on the in-class declaration,
and for a function defaulted or deleted in the class); none if that function is
`is_inline` (an `inline` definition later); none for a class template
instance (`is_template_class && !is_specialized`, an explicit specialization is an
ordinary class). The FE's own decider (`vtbl_decider_function_for_class`, il.c:31928)
answers "first non-pure non-`is_inline`" in this configuration (the
`inline_in_class_definition` variant is under `DO_IL_LOWERING`, il.c:31974): different for
an implicit member (`is_inline`) and a user-written one is the same, but it can disagree
with clang for a class whose first candidate is `inline`-declared *after*; the writer does
not use it for the answer, only the CHECKING cross-check that
`decider == key function` where both exist. Checked against clang for every class of
`v4_key` and `v4b_key20` (linkage of `_ZTV`):

| Case | Key | Clang vtable |
| --- | --- | --- |
| `K1 { virtual a(); virtual b() {} }`, `K2 { a() {} b(); }` | `a` / `b` | external |
| `K3 { a(); }` + `inline void K3::a() {}` | none | `linkonce_odr` |
| `K4 { inline virtual a(); virtual b(); }` | `b` | external |
| `K5 { a() = 0; b(); }`, `C3 { a() = delete; b(); }` | `b` | external |
| `K6 { virtual ~K6(); }`, `C2` with `C2::~C2() = default;` after the class | the destructor (out-of-class default is user-provided) | external |
| `K7 { virtual ~K7() = default; a(); }` | `a` (the default in the class is inline) | external |
| `C1 { constexpr virtual a() const {} b(); }`, `C5` with `a` constexpr, `C4` with a `consteval` virtual | `b` | external |
| `K9 : K1 { }` (declares none) | none | `linkonce_odr` (used) |
| `TK<char>` (implicit instance), `TK<int>` (explicit instantiation) | none | `linkonce_odr`, `weak_odr` |
| `TK<long>` (explicit specialization) | `a` | external |
| `extern template struct TK<short>;` | | an **external declaration, not defined here** |
| `C6 { a(); b(); }` + `inline void C6::a() {}` | none | `linkonce_odr` |

The last-but-one row is missing from the draft: a class with `do_not_instantiate`
(il_def.h:10565) that is not `explicitly_instantiated` here has no vtable definition.

**Vtable linkage and `DEFINED_HERE`** (the draft's list with the conditions the IL
gives):

- key function defined here (`routine->defined`) and not inline: external, defined here, used
  or not (clang emits the vtable at the key function's definition; the FE's
  `virtual_functions_needed_due_to_definition_of`, func_def.c:362, is the same decision);
- explicit instantiation definition (`explicitly_instantiated && !do_not_instantiate`):
  `weak_odr` + COMDAT, defined here;
- no key function, used here: `linkonce_odr` + COMDAT;
- a key function that is not defined here: an external declaration only (not even a
  reference needs its definition);
- TU-local class (`cir_entity_is_tu_local(iek_type, t)`, lower_name.c:16630): internal;
- a class local to a function: the linkage of the function's statics: `linkonce_odr` if
  the function may exist in several TUs (inline or a template instance,
  `ecil_local_static_may_have_multiple_copies`' predicate on the enclosing routine), else
  internal (clang: `_ZTVZ5localvE3Loc` internal, `_ZTVZ9in_inlinevE3Loc` `linkonce_odr`,
  both measured).

**"Used"** is writer-side state: set when the writer streams a `VPTR_INIT` (the
class), a `VTT_ADDR` (the class of the VTT), a `C_AGG_BEGIN` of the class as a
complete object or member of a constant (12.7; **not** as a base subobject: it uses the vtable
group of the enclosing complete object), in a body or constant that survives (a dropped body is truncated: the flag is
taken from the frame mark), and transitively for a VTT or construction vtable that
references a base's vtable (the library derives these, so the base's `DECL_ABI_DATA`
must be introduced when the derived one is). The FE's own flag is
`virtual_functions_marked_as_required` (il_def.h:10643): it is set when the FE
requires the virtual functions (constructor, destructor, decider definition, explicit
instantiation, `inline template`, typeinfo forcing) and **recurses into direct bases**
(func_def.c:210-238), so it says "the program needs the definitions *somewhere*", not "this TU
emits the vtable of this class"; as a marker it would define linkonce vtables of bases whose
constructors are only declared here, and then 8.2's rule would diagnose their
undefined functions. It is not even usable as a CHECKING assertion ("a used class has
the flag" fails: a class whose vtable is used only by a derived class's constructor
need not have it).

The VTT and the construction vtables follow the vtable. The **class typeinfo has the linkage
the vtable would have, whether or not the vtable is used or emitted here** (12.8: a thrown class
with no key function gets a `linkonce_odr` typeinfo and no vtable; clang does the same: `_ZTI1N`
`linkonce_odr` and no `_ZTV1N`, measured with the pin's clang). A `DECL_ABI_DATA` is introduced at first use and **re-issued
in the globals frame** with the final linkage, flags and `DEFINED_HERE` (12.3), because
typeinfo G-ids appear in bodies before the TU's key-function and "used" decisions; vtable and VTT
G-ids occur in no body and are sent once, at the end.

### 8.2 Are the virtual members there? (`tim_used`)

`eccp -tused` is the front end's `instantiation_mode == tim_used` (cmd_line.c:10743):
template members are instantiated where they are used or required. A defined vtable
refers to every non-pure, non-deleted virtual function it holds, so those functions must
exist as definitions here (or be external declarations, when the function is a base's). The
FE arranges the definitions at exactly the points where the writer decides to define a
vtable: `require_definitions_of_virtual_functions_in_class` (func_def.c:289), called from
`require_definitions_of_virtual_functions_due_to_definition_of` (func_def.c:411: the
definition of a constructor or destructor of the class, or of its decider function), from
`force_definition_of_typeinfo_for` (types.c:18146), from the explicit instantiation of a
class template (templates.c:42481) and from `inline template` (42416):

- every virtual function of a template instance gets `set_instance_required(...,
  SIR_DEFER_INLINE)`; the instantiations happen at the end of the TU (probe `i1_inline_vtable`:
  `Tm<char>` is used and `Tm<int>` explicitly instantiated, `Tm<char>::f` and `::h`
  are both `defined`);
- a defaulted virtual function and an implicit virtual assignment operator
  that overrides one are defined (`force_definition_of_compiler_generated_routine`);
- an implicit virtual destructor is defined through
  `define_virtual_generated_dtor_if_needed` (func_def.c:241) **unless the class has a
  decider that is not defined in this TU** (then the vtable is not emitted here either), and
  for a class that has virtual bases also along the derivation paths of its virtual bases (the
  clang/old-g++ behavior).

So a vtable that the writer defines on the 8.1 rules has its members available; the writer
asserts that in CHECKING (every function in `CLASS_VFUNCS` of a defined vtable is
`defined`, declared in the stream as external, or pure/deleted) and otherwise diagnoses the class
(the draft's "otherwise the writer diagnoses"). A non-template inline virtual function is in the
IL whether it is used or not (`U2` in `i1_inline_vtable` is `defined` and never constructed):
streamed as every defined function is.

### 8.3 Thunks and names

Library-owned (draft 3); the writer sends nothing for them. The one fact a thunk needs from the
writer is the linkage and COMDAT of the overrider's definition (draft 2: thunks go with it), which
the `DECL_FUNC` already has. The clash rule (reuse a same-name same-type stream entity) needs the
writer's symbol table: `symbol_binding_lookup` (the R1-3 mechanism): a user function named
`__cxa_pure_virtual` or `__dynamic_cast` with another type is `replace-and-bitcast` in the library;
only a definition clash is an error.

## 9. Zero- and value-initialization of types with data-member pointers (draft 1e)

A pointer to a data member is null when it holds -1, so a type that contains one
(a field, a base, an array element, recursively) is not zero-initializable. What the
IL gives (probe `m2_valinit`, `il_query` over its variables and `new` expressions):

| Source | IL | Today (M3) |
| --- | --- | --- |
| `S gs;`, `S garr[3];`, `U gu;` (static, no initializer) | `init_kind = initk_none` | definition without `VAR_INIT`: diagnosed (`ECIL_GF_HAS_DMP`, `ecil_const.c:1666`) |
| `S gs2{};`, `T gt{};` | `initk_static`, constant `{}`, `{<implicit element> {}}` (a partial aggregate: the unlisted members are zero) | `ecil_const.c:1135` diagnoses |
| `S l{};`, `S l2 = {};`, `S l4 = {1};`, `T t{};`, `U u{};` | `dik_constant` with an aggregate constant, `is_braced_initializer`, `is_partially_initialized` | `INIT_CONST` of a constant with unlisted parts: diagnosed |
| `S l3 = S();` | `dik_constant` `S{0L, 0, 0L}`, a `ck_aggregate` whose member-pointer element is an **explicit `ck_ptr_to_member` with a NULL field** | would work as `C_AGG` with `C_DATA_MEMBER(nil)` |
| `new S()`, `new S`... with `()`; `new S[2]()`; `new U()` | `dik_zero` (zero-initialization of the allocated object) | `emit_zero_object` -> `ZERO_BYTES`, diagnosed |
| `new S{}`; `new S[2]{}` (constant count) | `dik_constant` `{}` (partial) | `INIT_CONST` |
| `new S[n]()`, class without a user constructor | `dik_zero` of the array type `S[]` (run-time count) | "an initializer of a new[] with a run-time count": unsupported |
| `new S[n]{}` | `dik_constant` `{}` of `S[]` | same |
| `new S[n]{S{1, nullptr, 2}}` | `dik_constant` `{S{1L, (long S::*)0, 2L}}` of `S[]` (the rest value-initialized) | unsupported |
| `new ND[n]()`, `ND` has a user constructor | `dik_nonconstant_aggregate` with `<0 repetitions of dynamic-init: constructor-call>` | the `array_default_construct` loop |
| `new ND()` | `dik_constructor`, `value_initialization` | the constructor |
| `static S ls{};` | `initk_function_local`, same as a namespace-scope one | |

**Does EDG ever reach `ZERO_BYTES` for a DMP type? Yes.** The M3 writer emits
`ZERO_BYTES` (or the `ZERO_FILL` flag) in ten places, each preceded by
`ecil_note_zero_init_of_dmp` (`ecil_code.c`, lines of the working tree):

| Lines | Site | Why it zeroes |
| --- | --- | --- |
| 3721-3725 | `emit_zero_object` | `dik_zero`; a record or array is zeroed byte by byte "so that its padding is zero too" |
| 3742-3743 | `emit_zero_pointee(..., TRUE)` | a base subobject, data size only: **`INIT_ZERO_BASE`** |
| 4225-4226 | `do_new_delete`, `new A[k]()` with a loop constructor | `ctor_needs_zeroing` before the constructor loop (constant count) |
| 4674 | `begin_dynamic_aggregate` | the `ZERO_FILL` flag of a partial aggregate |
| 5179-5180, 5253-5254 | `do_agg`, `[[no_unique_address]]` member | `dik_zero` or `ctor_needs_zeroing`, data size only |
| 5451-5452, 5483-5484 | `do_meminit`, `[[no_unique_address]]` member | the same, in a mem-initializer |
| 5528-5529 | `do_meminit`, a base with `dik_zero` | data size only: **`INIT_ZERO_BASE`** |
| 5537-5538 | `do_meminit`, delegation to a trivial constructor | the whole object, data size |

They are the sites that need the draft's treatment:

- `emit_zero_object`: `INIT_ZERO` on the typed glvalue for a record or array too
  (the library's `zero_fill` stores the zero attribute for up to 16 bytes and a
  memset above, and the expanded null constant for a non-zero-initializable type;
  both zero the padding), `ZERO_BYTES` only where the type is zero-initializable
  and the byte count is *not* the whole object (below).
- A **base subobject** zero-initialization is **`INIT_ZERO_BASE`** (0xA8, review of the draft): it
  pops a glvalue of the base class and zeroes the first `nvsize` bytes (`data_size` without
  a `CLASS_INFO`), with the null constants of 12.7 for members that are not zero-initializable;
  `INIT_ZERO` of a base subobject would write `size` bytes and clobber a derived member
  placed in the tail padding. Whole objects, members and array elements stay `INIT_ZERO`
  (typed glvalue). FE facts, probe `v9_zero_base` (IL dumped with `il_query.py inits`):

  | Source | IL | Writer |
  | --- | --- | --- |
  | `D() : B() {}`, `B` non-POD (destructor) with a trivial default constructor; also `F() : E()` (`E() = default`) and `DM() : M()` (`M` has a data-member pointer) | the base entry `cik_direct_base_class` with `dik_zero` | `do_meminit` (`ecil_code.c` ~5528): `INIT_ZERO_BASE` on the base glvalue, in place of `ZERO_BYTES` of the data size |
  | `DV() : BV()`, a dynamic base with an implicit non-trivial constructor | `dik_constructor` with `value_initialization` and a `compiler_generated` constructor | the `ctor_needs_zeroing` pre-zero of the base (`emit_zero_pointee(..., TRUE)`, ~3742): `INIT_ZERO_BASE`, then the constructor call (it sets the vptr) |
  | `new G()` (`G : B`, `G() = default`) | `dik_zero` on the complete object | `INIT_ZERO` of the whole object (the base is part of it) |

  Sites that change: `do_meminit`'s base `dik_zero` case, `emit_zero_pointee` with its `TRUE`
  (base) argument, and the `ctor_needs_zeroing` paths that reach a base. A
  `[[no_unique_address]]` member keeps its own `ZERO_BYTES` of data size (below) and the diagnosis
  of a type that is not zero-initializable.
- A **`[[no_unique_address]]` member** (data size only): the one site with no
  typed-glvalue form that stops at the data size. Keep the diagnosis when the type is
  not zero-initializable (`ZERO_BYTES` of its data size is a format error then).
- `new S[n]()` and `new S[n]{}` with a run-time count: one `ARRAY_CTOR_BEGIN(elem)` loop
  whose body is `INIT_ZERO` of the element (the pointer is on the stack), or, for a
  zero-initializable element, `ZERO_BYTES` of `n * sizeof` as now for a constant count; this is
  what the draft means by "`new S[n]{}` uses the same shape as `new S[n]()`" (and is the
  reason: upstream CIRGen is NYI for `new S[3]{}` of such an element). With a
  constant count `INIT_ZERO` of the typed array works.
- A **dynamic class** (12.7): the zero-fill before a constructor (`ctor_needs_zeroing` then the
  constructor, `new D()`) stays `INIT_ZERO`/`ZERO_BYTES` as above (the constructor then
  sets the vptrs, and `#cir.zero` is valid for the vptr); what is an error is a `C_ZERO` constant
  of a dynamic class (as a `VAR_INIT`, an `INIT_CONST` source or the unlisted part of a `C_AGG`):
  a constant of a dynamic class lists its members, and a definition without `VAR_INIT` is zero as
  before.
- A constant with unlisted members and a definition without `VAR_INIT` need no writer
  change: the library expands them.

## 10. Writer work: where the gates are and what replaces them

The M3 gates that M4 removes (line numbers of the working tree), each with the
replacement of this note. The slice names are the plan's M4b slices.

| Gate (file:line) | Today | Replacement | Slice |
| --- | --- | --- | --- |
| `ecil_code.c:6959` `check_cxx_function` | "a virtual function" | send the function; its class facts at first need (2.9) | S1 |
| `ecil_code.c:6972` | constructor/destructor of a class with virtual functions or virtual bases | `VPTR_INIT` (4.2); with virtual bases the double walk (4.4) | S1, S2 |
| `ecil_code.c:2394` `do_call` | "a virtual call" | `CALL VIRTUAL` (4.1) | S1 |
| `ecil_code.c:3959` `do_delete` | delete with a virtual destructor | `DELETE VIRTUAL` + `STRUCTOR_DELETING` (3.2, 4.5) | S1 |
| `ecil_code.c:1580` `emit_class_cast` | "a virtual base class", an indirect base | `VBASE_CAST`, `BASE_CAST` by `base_classes_of` (4.3) | S2 |
| `ecil_code.c:5607` `do_meminit` | "a virtual base class initializer" | `MEMINIT(vbase)` with `VTT` (3.4, 4.1) | S2 |
| `ecil_code.c:5718` `emit_dtor_prologue` | "a virtual base class destructor" | skip the entries in D2, synthesize D1 (3.4) | S2 |
| `ecil_type.c:1364` | a class with virtual bases fails | `VIRTUAL_BASE` fields, `CLASS_INFO` right after the body (2.2, 2.3, 2.9) | S2 |
| `ecil_type.c` record-body path, after `TY_RECORD_BODY` (~1195-1400) | nothing | `ecil_send_layout_facts` when `class_is_dynamic` (2.9) | S1 |
| `compute_func_state` / `reissue_one_function` (`ecil_type.c:2335, 2651`) | no `NOTHROW` | `NOTHROW` from `is_non_throwing_routine`, sticky across re-issues; D0 re-issued with D1 (3.2, 4.1) | S1, S4 |
| `emit_zero_pointee(..., TRUE)` (3742), `do_meminit` base `dik_zero` (5528) | `ZERO_BYTES` of the data size | `INIT_ZERO_BASE` (9) | S3 |
| `do_expr` for `enk_throw` in a conditional region | no throw | own `FULLEXPR` around `THROW_BEGIN`..`THROW_END` (5.4) | S4 |
| `ecil_const.c:1150` | a constant of a class with virtual functions or bases | class facts at `C_AGG_BEGIN`, vtable "used", never `C_ZERO`, skip the `TY_VPTR` field in the index mapping (5.4, 8.1; the library fills the vptrs); never literal with virtual bases | S1 |
| `ecil_type.c:1095, 1206`, `ecil_const.c:1143` | the own-vptr predicate | `class_has_own_vptr` (2.1) | S1 |
| `ecil_type.c:614` | pointer to data member is opaque | `TY_DATA_MEMBER`/`TY_METHOD` (6.1) | S3 |
| the `ecil_note_zero_init_of_dmp` sites (9) | diagnose | `INIT_ZERO`/expanded constants | S3 |
| `ecil_code.c:6173` and `do_expr` for `enk_throw` | try/throw unsupported | 5.1-5.4 (THROW inside the enclosing `FULLEXPR`, implicit free cleanup in the library) | S4 |
| `ecil_write.c:2192` | `exceptions` always 0 | `exceptions_enabled` (5.8) | S4 |
| `enk_typeid`, `eok_dynamic_cast`, `eok_ref_dynamic_cast` (default of `do_expr`/`do_operation`) | unsupported | 7.1, 7.2 | S5 |

New writer functions (names are proposals): `class_has_own_vptr`, `ecil_class_facts`
(`ecil_type.c`: the 2.9 stack, `CLASS_*`, `STRUCTOR_DELETING`), `ecil_vfunc_sig` (3.5),
`ecil_note_vtable_use` and `ecil_decide_abi_data` (8.1, run from the file-scope hand-off),
`ecil_typeinfo_id(frame, fe_type)` (7.3: normalization, mangling, masks, base closure),
`emit_catch_param`, `emit_throw`, `emit_eh_cleanup_begin/end` (5), `emit_pm_*` (6),
and the per-walk reset of 4.4. Every traversal is on an existing heap stack: the class
facts use a new one (inheritance depth), the rest is `run_code_stack`.

**Non-recursion and pointer-free.** No record carries a pointer: the new ids are G-ids
(classes, functions, ABI data), L-ids and small integers; `sig` is a table index; offsets are
values. The base closure of 2.9 and 7.3 and the catch/throw type walk have no recursion
that scales with input; the only recursion in the FE functions the writer calls is
`traverse_type_tree_full` (a heap walk in `cir_has_parentless_lambda`, already used) and
`is_nothrow_type`/`identical_types` (bounded by the type's nesting, as everywhere in the
M3 writer).

### Carried into the S1 list (from the M3.1 final review)

1. A glvalue `enk_constant` under `eok_class_rvalue_adjust` takes the address path
   (`rhs_is_adjusted_glvalue` looks only at `is_glvalue_node(inner)`, an `enk_constant` can be one), so
   `ADDR` gets a prvalue: value-spill it (`rhs_is_fixed_value` must win over the adjusted-glvalue test for
   a constant operand).
2. A test whose static-local function is named late (the F5 path: a static local of a function whose name
   `cir_routine_name_may_change` says may change).
3. Goldens for the constant branch of `rhs_is_fixed_value` (`*p++ = T{1, 2}`) and for the F1(a) closure case
   (`S s = {1, [n = 5]{ return n; }()};`).
4. N4: a reference bound to an empty base of an automatic local (`B1 &b1 = c0;`) is dropped ("the address of a
   local variable in a constant"): implement it as `LREF` + `ADDR` + base cast (it fires 0 times in both corpora).
5. **A `LOAD` of a record with a vptr or virtual bases is an error**, except as a `NONTRIVIAL_ABI` by-value
   argument (review of the draft, finding 10). M3 is protected today only because the opaque vptr makes such a record
   `ECIL_GF_ABI_OPAQUE` ("a value of an unsupported type" in `emit_load_if_value`); with `TY_VPTR` in the record
   that gate is gone, so `emit_load_if_value` needs an explicit check of `class_has_own_vptr ||
   any_virtual_base_classes` (the by-value argument of a non-trivial-ABI class is a `STORE INIT` into a temporary,
   not a `LOAD`). The FE does not ask for a load of such a class: probe `v11_class_copy` shows copies of `B`
   (a vptr) as `dik_constructor` (the implicit copy constructor, non-trivial), the by-value parameter and return
   through the copy constructor / `dik_class_result_via_ctor`, `b = c` as a call of the implicit `operator=`
   (member-wise: it does not copy the vptr), and for `W : virtual V` the implicit copy constructor and
   assignment copy the virtual base `V` as a `dik_bitwise_copy` entry / an `eok_assign` of the class `V`
   through a virtual-base cast, so `do_meminit` (bitwise copy of a `vbase` entry, `MEMINIT(vbase)`) and the class
   assignment path must accept a `VBASE_CAST` glvalue.
6. The ordering and `data_size`/`nvsize` checks of 2.2 and 2.9 as CHECKING assertions
   (`ECIL_GF_LAYOUT_FACTS`, `ECIL_GF_VTABLE_FACTS`).

## 11. The critique of the draft: what became of it

The first version of this section proposed 15 text changes and asked 6 questions about
draft v2. All were folded into the normative `ecil_format.md` section 12 (the contract check of
2026-10-12: B1-B3, E1-E6, W1-W6), and the later decisions below. The list is kept as the
record of why each rule is there; **the normative text is section 12**.

| Item | Disposition in section 12 |
| --- | --- |
| 1 `sig` | 12.2 `CLASS_VFUNCS`: writer-assigned id, same name and identical FE parameter types and qualifiers |
| 2 dsize of a class with virtual bases | 12.2 `VIRTUAL_BASE` row: a non-empty `BASE`/`VIRTUAL_BASE` field ends at offset + `nvsize` of its class; `data_size` is the largest end |
| 3 `nslots` | 12.2 `CLASS_INFO` |
| 4 incomplete class typeinfo | 12.3 `DECL_ABI_DATA` |
| 5 `extern template` | 12.8 |
| 6 catch kinds 6/7 and method pointers | 12.5 `CATCH_PARAM` (method pointers are kind **2**, not 4: the library check) |
| 7 `throw()` in C++17 | 12.4 `FUNC_EH_SPEC` (the writer maps it, `cpp17_mode`) |
| 8 `throw new` / temporaries | **superseded**: THROW is not a region (user decision); `throw T(*new U)` is valid, 12.5 |
| 9 `C_VPTR` | removed: the library fills vptrs (12.7); the writer sends class facts at `C_AGG_BEGIN` and marks the vtable used |
| 10 `MEMINIT` order | 12.3 |
| 11 `CALL VIRTUAL` on an object | 12.6 (devirtualization, final class and function) |
| 12 `DELETE VIRTUAL` validation | 12.6 |
| 13-14 cleanup locals, sized/placement delete | 12.6 `CLEANUP_BEGIN`, worked encoding (10) |
| 15 late class names | 12.2 `CLASS_MANGLE` |
| Q1 devirtualize | yes (12.6). Q2 `throw()` | the writer. Q3 catch kinds | decided (12.5). Q4 `C_VPTR` | the library fills. Q5 dropped virtual function | 12.8 (a linkonce vtable becomes an external declaration, a typeinfo is unchanged, an external vtable referencing a dropped function is a library error). Q6 first need only | yes (12.2) |

Also in 12 because of the contract check: the `g_d0` `DECL_FUNC` before `STRUCTOR_DELETING`, the `FUNC_EH_SPEC`
extent, `CLEANUP_BEGIN` valid in `STATIC_INIT`, a typeinfo's `DECL_ABI_DATA` re-issued at the end, the typeinfo
linkage that follows the vtable's decision (not its use), `CLASS_INFO` only at need (right after the body for a vptr or virtual bases, review), and `C_METHOD` with a non-zero
`adj` only in `VAR_INIT`.

### 11.1 The original list: things the writer could not emit as written (superseded where marked)

1. **`sig`** (1a): "FE-canonical parameter types" must be said to be the FE's `param_type_list`
   and method qualifiers compared with `identical_types`; the writer owns the table. It cannot be
   derived from ECIL types (3.5). *Proposed text:* "`sig` is an arbitrary non-zero id assigned by the
   writer; two functions have the same `sig` iff they have the same name, identical parameter types
   as the front end compares them (top-level cv removed, arrays and functions decayed) and the same
   cv and ref qualifiers of `this`. It is 0 for destructors. The library never compares parameter
   types."
2. **dsize of a class with virtual bases** (1a, `VIRTUAL_BASE`): the draft says the 1.1 dsize rule
   counts the fields; state the formula (2.2): complete dsize = max(non-virtual data size, offset +
   non-virtual data size of each non-empty virtual base) and that the writer sends `size`, `nvsize`
   and the fields so the validator can check `dsize` with it (`consistent_data_size`).
3. **`CLASS_INFO.nslots`** is `highest_virtual_function_number + 1`, or 0 for 65535; add that a
   class that has only virtual bases has nslots 0 and *no* own vptr slots but still a vptr.
4. **Typeinfo of an incomplete class** (1b): clang defines `_ZTI1I` (internal) for `typeid(I *)` with
   `I` incomplete; the draft's class typeinfo "needs `CLASS_INFO`". State that a class typeinfo
   of an incomplete record is a plain `__class_type_info` with no bases and no `CLASS_INFO` and is
   internal with the `incomplete_class` mask on the pointers to it (7.3).
5. **`extern template`** (2): a class instance with `do_not_instantiate` that is not explicitly instantiated
   here has an external vtable *declaration* and no definition (clang: `external constant`), and no
   typeinfo definition (8.1).
6. **`CATCH_PARAM` kinds 6 and 7** (1c): "reference_to_pointer 6, reference_to_record_pointer 7 (a
   reference to a pointer to a record)" is ambiguous for the other pointers; and by-value member
   function pointers (5.3). I used 7 = reference to a pointer to a record, 6 = reference to any other pointer.
7. **`FUNC_EH_SPEC` with `throw()`** (1c): the front end keeps `throw()` as a dynamic specification with an
   empty list even in C++17 and later; the writer applies the C++17 rule (`noexcept`), as the
   draft says, using `cpp17_mode`. The draft's remark "an empty list is pre-C++17 `throw()`"
   is therefore correct and the writer is the one that decides (5.6).
8. **[Superseded: THROW is no longer a region; `throw T(*new U)` is valid.] `THROW_BEGIN` and `new`**: `throw new T(f())` is fine (the `new` is the operand of a
   different expression, its `DEACTIVATABLE` cleanup is begun and ended inside the initializer), but
   `throw T(*new U)` puts the deactivatable cleanup of the `new` inside the throw's cleanup-only region,
   which the draft makes an error. The writer diagnoses it; say so in the contract.
9. **[Superseded: `C_VPTR` was removed; the library fills the vptrs.] `C_VPTR` of a constant** (1c): the FE's constants of dynamic classes list no vptr (`Der()` is `{{}}`),
   so the *writer* must name every vptr field, bases and virtual bases included. It needs, for a
   constant of a class `t`: the offsets of all vptrs of the complete object, which is the walk of the
   record's `TY_VPTR` fields over the bases (`class_has_own_vptr` of each subobject class). If the
   library can complete missing vptr fields in a `C_AGG` itself this is simpler and cannot be wrong:
   Q4 below.
10. **`has_vbases` and `MEMINIT` order** (1b): `idx` must strictly increase within each kind
    while the front end's order of virtual bases is its own (preorder of the virtual bases).
    The writer fixes the `VIRTUAL_BASE` field order to be the same (2.3); the draft says "field order
    is free", which is compatible but the rule is why.
11. **`CALL VIRTUAL` on an object**: the front end marks `local.f()` virtual (4.1); the draft is silent. The
    writer will emit a plain call for a complete object that is not a reference (clang does); a contract
    note that a class whose vtable is never "used" must not get `DECL_ABI_DATA` only because of a virtual call
    would avoid a spurious vtable.
12. **`DELETE VIRTUAL` validation** ("`fn` must equal `STRUCTOR_DELETING.g_delete`"): both come from
    `assoc_operator_delete_routine` of the static class; the validation is trivially true for the writer
    and cannot catch a wrong routine. It can only catch a class that has no `STRUCTOR_DELETING`
    (missing facts).
13. **Cleanup code and addresses** (1c, `CLEANUP_BEGIN`): the code is stack neutral and uses no values from before it.
    Partial-aggregate and free-on-exception cleanups need an address that is not a local or `this` (an element of
    the array being initialized, the pointer returned by `operator new`): the writer keeps it in a pointer
    `LOCAL` and the cleanup code is `LREF l LOAD [DEREF FIELD/ELEM...] ADDR CALL ...`. State that locals declared
    before the cleanup may be read inside it, as `emit_cleanup` does for `this_lid`.
14. **Placement `delete` arguments and sized delete** (1d, `CLEANUP_BEGIN` for `new`): the cleanup passes the
    allocation size and the placement operands again, from locals; they must be evaluated once (5.5).
15. **Late class names** (1a): `CLASS_MANGLE` may need a re-issue before `F_NAMES`: fine, and the library must
    not use the first mangling for any derived name before the end (it computes at finish, as the draft says).

### 11.2 Things that could be simpler

- The front end already supplies the implicit rethrow of constructor and destructor handlers
  (5.1); delete the sentence about the writer emitting one.
- `STRUCTOR.has_vbases` equals "the class has `VIRTUAL_BASE` fields": the flag is redundant (the library
  can derive it); keep it as the validation the draft wants, but it is `any_virtual_base_classes`
  and not a decision.
- `TYPEINFO_DEF(function ...)` and `array`/`enum`/`fundamental`: the writer sends no
  members for them, so the kinds could be one "no parts" kind with the flags; not necessary.
- `HDR_LANG.rtti` could be deduced from the presence of RTTI records; it is cheap to keep.

### 11.3 The questions (all answered above)

- **Q1.** Devirtualize `object.f()` in the writer, as clang does (4.1)? *Recommended yes*: smaller
  stream, same code as clang, no use of the vtable. The alternative is to follow the FE flag.
- **Q2.** Who maps `throw()` to `noexcept` in C++17: the writer (as written) or the library
  (a writer that sends the dynamic specification always, faithfully)? *Recommended the writer*, which knows
  the language mode; the library has no mode.
- **Q3.** The exact predicates of `CATCH_PARAM` kinds 6 and 7, and the kind of a by-value pointer to member
  function (two words). cir-lib's K4 list defines them; please state it in the contract.
- **Q4.** `C_VPTR` of a constant: should the writer list every vptr (draft) or may the library complete a
  `C_AGG` of a dynamic class that omits them? The latter removes a class of writer bugs for
  `constexpr` objects of polymorphic classes; the former keeps the stream self-describing.
- **Q5.** A vtable the writer cannot define because a virtual function was dropped as unsupported (a body that
  the writer could not translate): diagnose the class and leave the vtable an external declaration (the draft's
  "diagnoses"), or fail the TU? *Recommended:* declaration plus a warning, as for any dropped definition;
  an error if the class is a root (a key function that must exist).
- **Q6.** Class facts for classes that are never needed (2.9): confirm "first need" only (no facts at
  completion), because it changes the record order the draft's ordering paragraph describes.

### 11.4 The review of the map (gate M4a, PROCEED WITH FIXES)

| # | Review item | Where |
| --- | --- | --- |
| 1 | Layout facts right away for a dynamic class (`TY_VPTR`, `VIRTUAL_BASE` or a dynamic `BASE`; predicate `class_is_dynamic`); vtable facts first-need; need without vtable facts is an error; `data_size <= nvsize` | 2.2, 2.9, 10 |
| 2 | Devirtualization calls the final overrider (override list of the base entry, cast offsets for the entry) | 4.1 |
| 3 | `INIT_ZERO_BASE` | 9 |
| 4 | Typeinfo masks and incomplete chains, sticky snapshot | 7.3 |
| 5 | `*` prefix of a TU-local name; `TU_LOCAL` 0x04 flag, not implied by internal linkage | 7.3 |
| 6 | D1 synthesis order | 3.4, 4.4 |
| 7 | `sig` ignores ref-qualifiers; implicit virtuals in slot order; consteval in neither list | 3.5, 2.6 |
| 8 | `STRUCTOR_DELETING`: D0 defined iff D1; flags by arity | 3.2 |
| 9 | `NOTHROW` never cleared by a re-issue | 4.1, 10 |
| 10 | Deactivation before the range ends; a conditional throw needs a `FULLEXPR`; `RETHROW` not in cleanups | 5.4, 5.5 |
| 11 | Stale `C_VPTR` text | none left in 2.9 (the other mentions are the history of 11) |
| F | `LOAD` of a dynamic record | "Carried into the S1 list" item 5 |

## 12. Probes (`cir_src/test/writer/probes/m4/`)

Each probe compiles with g++ (`g++ -std=c++17`, `--c++20` where marked) and prints deterministic
output; none compiles with the CIR back end yet (they are the M4b end-to-end candidates). The
directory is not read by `allp.sh` (`probes/*.cpp` only).

| Probe | What it pins down |
| --- | --- |
| `v1_slots` | slot numbers: destructor pairs, covariant (non-virtual and virtual primary), non-primary overriders, pure, deleted |
| `v1b_consteval` (`--c++20`) | `consteval` virtual: number 65535, no slot, in clang too |
| `v2_overriders` | per-entry override lists: repeated base, virtual diamond, primary chain through virtual bases |
| `v3_layout` | nvsize, nvalign and the dsize formula of virtual bases against clang |
| `v4_key`, `v4b_key20` | key functions and vtable linkage against clang: inline later, constexpr, deleted, `= default`, templates, `extern template` |
| `v5_delete` (`--c++20`) | `assoc_operator_delete_routine`: global/class, sized, aligned, destroying |
| `v6_vbase_structors`, `v7_vbase_casts` | constructor/destruction lists with virtual bases, base-class cast shapes |
| `v3b_empty_bases` | `nvsize` against the writer's `data_size`: displaced empty bases (`A6`, `A7`: `nvsize 2`, `data_size` 0 and 1); g++ prints `2 4 12 3 16` and `2 2` |
| `v6b_vbase_order` | constructor and destruction list order of several virtual bases; D1 cleanup order (g++ prints `Q P R L1 L2 Top ~Top ~L2 ~L1 ~R ~P ~Q`) |
| `v8_calls` | `is_virtual_call` for the forms of a member call; devirtualization: a using-declaration (`UA`/`UB`/`UC`), a `final` class (`FA`/`FB`), a covariant final overrider (`CA`/`CB`/`CC`/`CD`) |
| `v9_zero_base` | `INIT_ZERO_BASE`: base `dik_zero` of `D() : B()`, a dynamic base, a data-member-pointer base, `new G()` |
| `v11_class_copy` | copies of a record with a vptr or a virtual base: init kinds, implicit assignment (the `LOAD` check) |
| `e1_try`, `e1b_catch_kinds`, `e1c_unnamed_catch` | handlers, catch parameter kinds, an unnamed by-value parameter |
| `e2_throw` | throw operands, throw in a conditional arm and a logical operand |
| `e3_spec` (`--c++14`), `e3b_spec17` (`--c++17`) | exception specifications of every form; `throw()` in C++17 |
| `e4_new`, `e4b_new_flag` | EH-only destructions: new with temporaries, arrays, conditional, aggregates, placement delete; the flag on an unconditional new |
| `e7_cond_throw` | a throw in a `TERN` arm, a `LAND`/`LOR` operand, a namespace-scope initializer and a static local's; with and without temporaries (the own-`FULLEXPR` rule); g++ prints `~A1 E1 i7 ~A2 E2 i3 5 5 1 0` |
| `e5_rangefor`, `e6_ftb` | range-for with try; function-try-blocks in constructors, destructors, plain functions |
| `m1_memptr`, `m3_pm_casts` | member pointer constants, conversions through several levels, calls, comparison |
| `m2_valinit` | value-initialization with data-member pointers, `new T[n]{}` against `()` |
| `t1_typeinfo`, `n1_names`, `n2_typeinfo_names`, `l1_local_names` | typeid, dynamic_cast, typeinfo names and manglings |
| `t2_incomplete` | typeinfo of pointer chains that contain an incomplete class: linkage, masks, the later-completed class |
| `i1_inline_vtable` | when a vtable is used; availability of the virtual members |

The same facts, as runnable checks on the box, are in `cir_src/test/writer/tools/il_query.py` (usage in
its docstring). The Mac native loop cannot produce these dumps (the aarch64/clang base has no GNU mode
and `--il_display` needs the debug build), so they are box commands; the stream-level tests of
M4b will be the usual goldens plus these probes against g++.

## 13. Final status (the M4 gate)

What the writer does today, with ECIL 1.2 as frozen at the gate (`writer.md` section 10).
Numbers are from the C++ corpus sweep (`stream_sweep --cxx`, 337 TUs of `imported/gnu/cpp`
minus the excluded directories, every stream validates, only `DRs/dr2867-2` aborts):
528 unsupported-construct notes in total.

### 13.1 Implemented

* **Virtual functions** (section 3): the vptr as a field, the vtable facts and `CLASS_VFUNCS`
  (sigs include the cv of `this`; `REF_RESULT`), virtual and devirtualized calls, the deleting
  destructor `STRUCTOR_DELETING` (also with a destroying operator delete), `delete` through a
  virtual destructor, `::delete` of a polymorphic object (`DELETE VIRTUAL|GLOBAL`).
* **Virtual bases** (section 4): the C1/C2 and D1/D2 pairs, the VTT, `VBASE_CAST`,
  `INIT_ZERO_BASE`.
* **RTTI** (section 7): `typeid` with the null check, `dynamic_cast`, typeinfo of every kind of
  type with the masks, the TU-local and the incomplete-class forms, the closure per TU.
* **Member pointers** (section 6): types, constants, `.*`/`->*`, conversions, comparison; the
  run-time form of `__builtin_is_corresponding_member` and
  `__builtin_is_pointer_interconvertible_with_class` (the offsets read as integers; the front end's
  lowering).
* **Exceptions** (section 5): throw, try/catch of every parameter kind, function-try-blocks,
  exception specifications, NOTHROW, the cleanups of constructors, new expressions (also
  `nothrow` and `new T[n]{...}`, `NEW_ARRAY_SIZE_THROW`), arrays and aggregates, throwing
  destructors run as cleanups of locals, temporaries and enclosing scopes.
* **Constructs the above made reachable**: class conditionals whose value is discarded or that
  initialize the return slot; a destroying operator delete; members of anonymous unions and
  structs in constructor initializers; a reference bound to a temporary array;
  `__builtin_launder`; `E1[E2]` with the integer first in C++ (C is unchanged).

### 13.2 Diagnosed (the function is dropped with a reason, never miscompiled)

* **M4 scope, library or contract work**: the destructor of an array element that may throw (the
  pin skips the elements before the one that threw; "watch upstream", 1 note); a destroying
  operator delete of an array or the one the front end records as the free function of a new
  expression (1 note); a class conditional thrown (`throw c ? B(1) : B(2);`: the destination is
  on the stack); an initializer with a destructor in a member of an anonymous union or struct
  (2 notes: its cleanup would need the path through the unnamed member).
* **Later (M5/M6)**: vector types (143 notes), GNU function multiversioning (64), inline assembly
  (56), floating constants of other formats (37), `__builtin_shuffle`/`convertvector`/`bit_cast`
  (22), alias/ifunc/weakref (18), the cleanup attribute (17), address constants such as `&&label`
  and a global reference bound to an array temporary (11), `init_priority` (9),
  `std::source_location` addresses (8), array/aggregate conversions (7), variable length arrays
  (7), flexible array initializers (7), `__builtin_clear_padding` (6), reference members with
  default member initializers (5), the GNU `?:` with an omitted operand (5), complex types (4).

### 13.3 Clang-consistent differences from g++ (not diagnosed)

Each was checked with classic clang 21.1.8 and g++ on the box.

| Test | g++ | clang | library |
|---|---|---|---|
| eh/array2 (a default-argument temporary's destructor throws in an array element's constructor) | destroys the element just built | does not (aborts) | does not |
| eh/return1 (CWG 2176, the returned object when a later destructor throws) | destroys it | does not | does not |
| eh/ref-temp1, eh/ref-temp2 (a lifetime-extended temporary when a destructor of the full expression throws) | destroys it | does not | does not |
| rma/eh40 (placement new, no handler) | terminates | terminates | terminates (the reference passes through its own `catch (...)` lowering) |

A member of a class with `[[no_unique_address]]` empty members that shares offset 0 with the
first member cannot be told from it by the value of its data member pointer
(`is_pointer_interconvertible_with_class`): true, as in the front end's own lowering.

### 13.4 Front-end and configuration issues

* **Lifetime extension of a member of a prvalue in GNU mode** (5.10): the front end's IL, not
  the writer.
* **Run failures that the front end causes (the reference fails them too; g++ and clang
  agree on the behaviour we lack)**: the lambda capture of `this` in `cpp1y/pr61636-2`
  (a lambda captures `this` where gcc does not), `init/addr-const1` (the front end does
  not fold an address constant through a cast, so a static pointer is initialized
  dynamically), `abi/bitfield9` (the alignment of a class with an unnamed bit-field; an
  ilp32 test), `init/ref19` (the §5.10 lifetime extension).
* **Build-type dependent front-end macros** (fixed). `NEW_CAN_BE_FOLDED_INTO_CTOR` was 0 only in
  the debug build type (`cmake/macro-conf/support/build-type/debug/base.cmakedef`); in a release
  build the front end folded a class `new` into its constructor and recorded no free-on-throw
  cleanup, so streams differed between build types. `cmake/macro-conf/default/cpfe-cir.cmakedef` now
  pins `NEW_CAN_BE_FOLDED_INTO_CTOR=0`, `DELETE_CAN_BE_FOLDED_INTO_DTOR=1` and
  `NEW_AND_DELETE_FOR_ARRAY_CAN_BE_FOLDED_INTO_RUNTIME_ROUTINE=1` for every build type, and
  `ecil_write.h` refuses to build the CIR back end with any other value: `DELETE_...=0` does not
  compile (the writer uses `assoc_operator_delete_routine`), `ARRAY_...=0` makes the front end
  choose the sized `operator delete[]` for the free-on-throw cleanup of a `new[]` (six goldens
  differ), `NEW_...=1` loses the cleanup. With the pin, the debug and release `cpfe-cir`
  write identical streams for every C++ golden.
* The optimization level does not reach the back end under the cir test configurations (`-O2` is
  handed to the assembler; `cir_be.c` sets level 0): the pointer-to-member builtin probes were
  run at `-O2` by hand (`cpfe-cir`, then `edg-cir -O2 --emit=obj`).

