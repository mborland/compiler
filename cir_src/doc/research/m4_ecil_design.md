# ECIL 1.2: virtual functions, virtual bases, RTTI, exceptions, member pointers (M4)

**Status: superseded by `ecil_format.md` §12 (normative, ECIL 1.2, gate M4a, 2026-10-12).** This file keeps the
design draft v3 (below) and the history of how it became the contract. Where they differ, the spec wins.

**From draft v3 to the normative text** (the changes; details in §12 of the spec):
- **cir-writer check** (against `il_to_ecil_map_m4.md` and the `probes/m4` probes):
  - B1: base and virtual-base fields end at `offset + nvsize` of their class for the data-size rule (also
    amends 11.3).
  - B2: a constant of a dynamic class is a first need for its facts, and its `C_AGG_BEGIN` uses the vtable of
    the complete or member class (not of a base subobject). The writer decides the linkage.
  - B3: `H h{}` is a partial aggregate constant, and `INIT_ZERO` comes from `dik_zero`.
  - E1: the free in a `new` is the sized `operator delete`.
  - E2/E3: a class typeinfo has the linkage its vtable would have, and a dropped virtual function leaves it
    alone.
  - E4: the key-function wording.
  - E6: the implicit rethrow is inside the handler block.
  - W1–W6: `STRUCTOR_DELETING` per virtual destructor, `DECL_ABI_DATA` re-issue, `FUNC_EH_SPEC` scope,
    devirtualization through the base conversion, implicit definitions, deactivatable cleanups in
    `STATIC_INIT`.
- **cir-lib check** (against the pin, spikes K1–K8 and `lib/abi`):
  - member-pointer constants carry the byte offset in the complete class (the library derives the field
    path), and `C_METHOD` with `adj` ≠ 0 is valid only in `VAR_INIT`;
  - dead code may follow `THROW_END`/`RETHROW` as after `UNREACHABLE` (no tightening of 1.1);
  - a member-function pointer is catch kind `scalar`;
  - `C_ZERO` of a dynamic class is narrowed;
  - `CLASS_INFO` is sent only at need, and `CLASS_VFUNCS`/`CLASS_OVERRIDES` only for dynamic classes;
  - `g_d0` has its own `DECL_FUNC`;
  - `esk_visibility` sets the typeinfo non-unique-name flag;
  - K8 (vptrs in constants) PASS.
- **User decision (2026-10-12): throw-operand temporaries are destroyed during unwinding, as clang does.**
  - Measured on the box: g++ 13 destroys them before `__cxa_throw` (`std::uncaught_exceptions()` 0); clang
    destroys them during unwinding (1).
  - `THROW` is therefore not a cleanup-only region. `THROW_BEGIN` begins an implicit deactivatable free
    cleanup, and `THROW_END` deactivates it.
  - `throw T(*new U)` is valid, and a throw is allowed in `MEMINIT` and `STATIC_INIT`.

**Inputs:**
- `research/m4_cir_shapes.md`: spikes K1–K7 and K4d, all PASS; gaps G1–G7.
- `research/m4_fe_facts.md`: the FE facts against clang (7712/7716 agree). `vbase13` is a documented g++ layout
  that the FE keeps.
- `research/m4_upstream_drift.md`: no pin bump helps.
- `cir_src/lib/abi`: the vtable/VTT/RTTI/names port, diff-clean against clang.
- `research/il_to_ecil_map_m4.md`: the writer's IL map, with 29 probes. K6c is done (§1e).

## 0. Principles

1. **Facts and mechanics.** The FE sends authoritative facts: slot numbers, final overriders per class and
   subobject, the primary base, nvsize/nvalign, base offsets, class manglings, emission and linkage decisions,
   the operator delete of D0, typeinfo linkage, and EH facts. The library ports clang's Itanium mechanics.
   - It derives everything derivable: dynamic-ness, emptiness, the `vbases()` order, covariant return
     adjustments, thunks, vtable groups, the VTT, construction vtables, the RTTI layout, and the
     `_ZTV/_ZTT/_ZTI/_ZTS/_ZTC/_ZTh/_ZTv/_ZTc` names.
   - The stream carries no redundant fact: two sources of truth drift.
2. **Additions only** (§9). `HDR_LANG.exceptions = 1`, `MEMINIT` kind `vbase`, `STRUCTOR.has_vbases` and
   `CLEANUP_END` become valid; they were reserved in 1.0.
3. **Strict.** Every rule is a format error (or a fatal finish-time diagnostic, where noted) with a `bad_*`
   fixture.
4. **Non-recursive** on both sides.
5. **Clang tiebreak**, with one exception: object layout is the FE's (`vbase13`).

## 1. Records

All opcode values are free in 1.1 (checked).

### 1a. Class facts (declaration sections)

| Op | Record | Rules |
| --- | --- | --- |
| 0x1B | `TY_VPTR(g)` | Pointer size and alignment. At most one per record, only as an unnamed field 0 at offset 0: the class's own vptr. A record that has one must get `CLASS_INFO`. |
| — | `REC_FIELD` flag `VIRTUAL_BASE` 0x40 | One field per virtual base class, direct and indirect, after the data members. The set must equal the union of the direct virtual bases and the virtual bases of the direct bases. Field order is free: the library derives clang's `vbases()` order from `CLASS_BASES`. Offsets are complete-object offsets. `EMPTY_FOR_ABI` means no storage of its own. `FIELD`/`EXTRACT`/`AGG_SEL`/`C_SEL` on such a field is an error. The 1.1 dsize rule counts these fields: a class's dsize is the complete-object dsize, max(non-virtual data size, offset + nvdsize of each non-empty virtual base). The validator checks it from `size`, `nvsize` and the fields. The writer orders the `VIRTUAL_BASE` fields in the FE's virtual-base order (preorder), the same order as its `MEMINIT(vbase)` groups. |
| 0x1E | `CLASS_INFO(t, nvsize, nvalign, primary_idx\|nil, nslots)` | At most once per `t`, after its body and before the other `CLASS_*`. Sent for every C++ class with a base, a vptr or a virtual base. `nvsize <= size`, and nvsize ≥ the end of the last non-vbase field. `nvalign` is a power of two ≤ align. `primary_idx` is a `BASE` or `VIRTUAL_BASE` field at offset 0 of a dynamic class. `nslots` is the number of primary-vtable function slots: `highest_virtual_function_number + 1`, or 0 for the 65535 sentinel (a class with only virtual bases has 0 slots but still has a vptr). It and must be ≥ the primary base's `nslots` and ≥ max own slot + 1 (+2 for a destructor). `.base` is `nvsize` bytes, distinct iff `nvsize != size`. Derived by the library: dynamic (own vptr, or any dynamic base), has-vbases, primary-is-virtual, empty. |
| 0x1F | `CLASS_BASES(t, arr[(field_idx, flags)…])` | The direct bases in **declaration order** (FE `direct_base_number`), virtual and non-virtual interleaved. Flag: `PUBLIC` 0x01. Virtual or not comes from the field's flag. No duplicates. The `BASE` entries appear in increasing field order and equal the set of `BASE` fields. Direct virtual bases are a subset of the `VIRTUAL_BASE` fields. |
| 0x20 | `CLASS_VFUNCS(t, arr[(g, slot, flags, sig)…])` | Every virtual function declared in `t`, implicit ones included, in declaration order (implicit ones last). Each `g` is a `DECL_FUNC` whose `this` parameter points to `t`; a destructor is its **D1** (the complete dtor of a dtor `STRUCTOR` pair), with D1 at `slot` and D0 at `slot+1`. Flags: `PURE` 0x01, `DELETED` 0x02, `IMPLICIT` 0x04. `sig` (u32) is an arbitrary non-zero id assigned by the writer, and 0 exactly for destructors. Two functions have the same `sig` iff they have the same name, identical parameter types as the front end compares them (`param_type_list` with `identical_types`: top-level cv removed, arrays and functions decayed), and the same cv/ref qualifiers of `this`. It drives vcall-offset sharing. The library never compares parameter types. Slot collisions are a fatal finish-time diagnostic (`lib/abi check_slots`). |
| 0x21 | `CLASS_OVERRIDES(t, arr[(g_fn, sub_off, g_ovr, ovr_off)…])` | Sent for every dynamic class, including classes seen only as bases (construction vtables use the base's own lists). In `t`'s complete layout, for the subobject (class of `g_fn`, `sub_off`), the final overrider is `g_ovr`, declared in the subobject at `ovr_off`; for a virtual base, that is `t`'s virtual-base offset. Only entries with `g_ovr ≠ g_fn`; no entry means `g_fn` is final. The format checks ids and stride only; subobject existence is checked by `validate_deep()` (a fatal finish-time diagnostic). |
| 0x25 | `CLASS_MANGLE(t, sid)` | The Itanium `<type>` mangling of class `t`, what follows `_ZTV` (`1D`, `N1N1CE`, or the deterministic internal form `N12_GLOBAL__N_12AnE` of 11.2). Required before `CLASS_INFO` of `t`. It may be re-issued before `F_NAMES` (late typedef name for linkage, e.g. `typedef struct {…} TN` mangles as `Ut_`, later `2TN`); the latest wins, and the library derives every name at finish from the final mangling only. It feeds `_ZTV/_ZTT/_ZTI/_ZTS`, `_ZTC` and the catch-copy thunk name. |
| 0x23 | `TY_DATA_MEMBER(g, t_member, t_class)` | `!cir.data_member`. Null is −1 (see 1e). |
| 0x24 | `TY_METHOD(g, t_func, t_class)` | `t_func` is the member-function `TY_FUNC` (object pointer first); `!cir.method`. |

**Ordering ("first need"; Q6: class facts are sent only at first need, never at class completion).** These records precede the first record that needs them, and so do the facts of all
of the class's bases, in this order: `TY_RECORD_BODY` and fields, `CLASS_MANGLE`, `CLASS_INFO`, `CLASS_BASES`,
`CLASS_VFUNCS`, `CLASS_OVERRIDES`.
- The needing records are:
  - `VPTR_INIT` and `VBASE_CAST` in the class;
  - `VTT_ADDR`;
  - `CALL VIRTUAL` through a function of the class or a class derived from it;
  - `DYNAMIC_CAST`, `CONST_METHOD`/`C_METHOD` with `VIRTUAL`;
  - `DECL_ABI_DATA` of the class;
  - `CATCH_PARAM`;
- Every G-id that `CLASS_VFUNCS` and `CLASS_OVERRIDES` name is introduced before them (rule 1.2). In practice the
  writer emits a `DECL_FUNC` for every virtual function of a class at the class's first need.

### 1b. ABI data and structors (declaration sections)

| Op | Record | Rules |
| --- | --- | --- |
| 0x34 | `STRUCTOR_DELETING(g_d0, g_d1, g_delete, flags)` | Flags: `SIZED` 0x01, `ALIGNED` 0x02, matched against `g_delete`'s type. `g_d1` is the complete dtor of a dtor `STRUCTOR`; one per `g_d1`. `g_d0` is new, with D1's type, linkage and COMDAT. `g_delete` is the FE's `assoc_operator_delete_routine`. The library synthesizes `cleanup.scope all { D1(this) } cleanup { operator delete }`. A destroying delete is diagnosed by the writer. |
| 0x35 | `DECL_ABI_DATA(g, kind, t, linkage, flags, sid\|nil)` | Kinds: `vtable` 0 (needs a dynamic class), `vtt` 1 (needs a class with vbases), `typeinfo` 2. Flags: `DEFINED_HERE` 0x01, `COMDAT` 0x02 (iff linkonce/weak). **Class kinds** (vtable, vtt, typeinfo of a class): at most one per (kind, t). The name is derived from `CLASS_MANGLE`; `sid` must be nil or equal to the derived name. **Non-class typeinfo** is unique by its name, and `sid` is required (`_ZTIPKi`; ECIL types carry no cv). Re-issue as for `DECL_VAR`. A class typeinfo with `DEFINED_HERE` needs its bases' typeinfo introduced first. The typeinfo of an **incomplete class** (`typeid(I*)`) is a plain `__class_type_info` with no bases and no `CLASS_INFO`; it is internal, and pointers to it carry the `incomplete_class` mask (clang). ABI-data G-ids appear only in `C_ADDR` and the typeinfo operand positions, never in `GREF` in bodies. `_ZTS` (name and string, no `*` prefix: clang) is derived. |
| 0x36 | `TYPEINFO_DEF(g_ti, kind, flags, g_a\|nil, g_b\|nil)` | Non-class typeinfo. Kinds: `fundamental` 0 (external declaration; no definition: `_ZTIi`, `_ZTIDn`), `pointer` 1 (`g_a` pointee ti; flags = the `__pbase_type_info` masks const 0x1, volatile 0x2, restrict 0x4, incomplete 0x8, incomplete_class 0x10, transaction_safe 0x20, noexcept 0x40), `member_pointer` 2 (`g_a` pointee ti, `g_b` class ti; same flags), `function` 3, `array` 4, `enum` 5. Class typeinfo has none: the library derives `__class`/`__si`/`__vmi`. |

**`STRUCTOR.has_vbases` = 1** (it must equal "the class has `VIRTUAL_BASE` fields"):
- Both `g_complete` and `g_base` bodies are streamed, each once. The alias/redirect C1 strategy is off. Each
  variant has its own COMDAT, as clang does.
- C1 has `MEMINIT(vbase, idx)` groups (`idx` is the `VIRTUAL_BASE` field index); C2 does not.
- D2 is the FE body without vbase destructions. D1 is a writer-synthesized body: `CLEANUP_BEGIN(all)` per vbase with
  a non-trivial destructor (on `VBASE_CAST COMPLETE`), then `CALL D2` with `VTT_ADDR(self)`. It has no `VPTR_INIT`.
- The library adds the hidden VTT parameter to C2/D2 and their calls. `TY_FUNC` stays the source type.
- **Delegating constructors:**
  - C1 → target C1.
  - C2 → target C2 with `VTT_ADDR(self)`.
  - The `VTT` call flag appears only on base-variant targets.
- Writer duties:
  - lambdas are streamed once;
  - CHECKING asserts that the second walk introduces no G-ids;
  - a static local may have `STATIC_INIT` in both bodies (K5a PASS).
- **MEMINIT order rule** (amends 11.4): the kinds appear in the order vbase, base, field; `idx` strictly
  increases *within each kind*.

### 1c. Function header and body records

| Op | Record | Stack | Rules and CIR |
| --- | --- | --- | --- |
| 0x6F | `FUNC_EH_SPEC(kind, arr g_ti)` | — | A **header** record, in the `PARAM` slot (after the `PARAM`s, before any body record). Kinds: `noexcept` 1, which wraps the body (the destructor prologue included) in `try{…} catch all {eh.terminate}`, clang order; `dynamic` 2, `try … filter[types] unexpected{eh.unexpected}`. An empty list is pre-C++17 `throw()`. The writer maps `throw()` to `noexcept` in C++17 and later (Q2): the FE keeps it as an empty dynamic specification. Only with exceptions = 1. **Writer duty:** sent for every definition whose exception specification is `noexcept(true)`, including implicit destructors and deallocation functions. `noexcept` without `DECL_FUNC.NOTHROW` is an error. |
| 0x6E | `VPTR_INIT` (arity 0) | — | **Constructor:** after all vbase and base `MEMINIT` groups, before the first field `MEMINIT_BEGIN` or statement, exactly once. **Destructor (D2, or the only body):** after the `CLEANUP_BEGIN(all)` prologue, exactly once. Required iff the class is dynamic (the class is the pointee of `this`). Forbidden in a delegating constructor and in a synthesized D1. Library: clang's `InitializeVTablePointers` order; `vtable.address_point` in C1/D1 and in classes without vbases, a load of `vtt.address_point(%vtt, n)` in C2/D2. |
| 0x9E | `VBASE_CAST(t_der, t_vbase, flags)` | prv ptr → prv ptr | `NONNULL` 0x01, `COMPLETE` 0x02 (static complete-object offset; required for vbase construction and destruction in C1/D1). Otherwise the vbase-offset load (clang shape), null-checked unless `NONNULL`. D→V→A is `VBASE_CAST` then `BASE_CAST`s. |
| 0x9F | `VTT_ADDR(kind, idx\|nil)` | → prv | `self` 0, `base` 1 (a `BASE` field index), `vbase` 2 (a `VIRTUAL_BASE` field index). Only as the hidden VTT operand of a `CALL` with `VTT`. |
| 0xA0 | `DYNAMIC_CAST(g_src_ti, g_dst_ti\|nil, t_result, flags)` | prv ptr → prv ptr | `REF` 0x01 (bad_cast). nil `g_dst_ti` means `dynamic_cast<void*>`. `cir.dyn_cast` with `#cir.dyn_cast_info`; the offset hint is computed by the library. An error with rtti = 0. |
| 0xA1 | `TYPEID(g_ti\|nil, t_result, flags)` | [glv] → glv | nil `g_ti`: pops the polymorphic glvalue and uses `vtable.get_type_info`. `NULL_CHECK` 0x01 is set iff the operand is an indirection through a pointer (`typeid(*p)`); the check precedes the vptr load (`__cxa_bad_typeid`). Static form: the object of `g_ti`. An error with rtti = 0. |
| 0xA2 | `PM_FIELD(t_dm)` | prv obj*, prv dm → glv | `cir.get_runtime_member` |
| 0xA3 | `PM_CALL(t_call, nargs, flags)` | prv obj*, prv method, args → result | **The object comes first** (C++17 evaluates E1 first). `cir.get_method` + indirect call. Flags and copy-elision rules as `CALL`. |
| 0xA4 | `PM_CAST(kind, t_from, t_to, offset)` | prv → prv | `to_derived` 0 = CK_BaseToDerivedMemberPointer (implicit `int B::*` → `int D::*`, adds `offset`; `derived_data_member`/`derived_method`). `to_base` 1 = CK_DerivedToBaseMemberPointer (`static_cast`, subtracts; `base_*`). `offset` is the non-negative offset of the base in the derived class; virtual paths are ill-formed. Null-preserving. |
| 0xA5 | `CONST_DATA_MEMBER(t, offset\|nil)` | → prv | nil is null (−1). |
| 0xA6 | `CONST_METHOD(t, g\|nil, adj, flags)` | → prv | `VIRTUAL` 0x01 (slot from `CLASS_VFUNCS`). nil is null. A non-zero `adj` becomes a runtime `derived_method` (G2). |
| 0xA7 | `ARRAY_PARTIAL_DTOR(g_dtor)` | — | Directly after `ARRAY_CTOR_BEGIN`, with exceptions = 1 and a non-trivial element destructor (`g_dtor` = D1): `cir.array.ctor … partial_dtor`. Aggregate-initialized arrays (`new T[n]{…}`, `S s = {A(1), A(2)}`) use per-element `CLEANUP_BEGIN(eh, DEACTIVATABLE)` + `CLEANUP_END` instead. |

**Exceptions (bracketed; exceptions = 1 only)**

| Op | Record | Rules |
| --- | --- | --- |
| 0xD7 | `TRY_BEGIN(flags)` | Opens the try body (a scope region). `FUNCTION_TRY` 0x01 is valid only in a constructor or destructor body (C1/C2/D2/the only body): it comes right after the header (`PARAM`s, `FUNC_EH_SPEC`), and the `MEMINIT`/`VPTR_INIT` state machine and the destructor prologue continue inside the try body. Other functions use an ordinary `TRY` around the body. |
| 0xD8 | `CATCH_BEGIN(g_ti\|nil)` | Closes the body or the previous handler and opens a handler (a scope region). nil is `catch(...)`, which must be last. At least one per TRY. Optionally followed by `LOCAL l` and then `CATCH_PARAM`. The library defers the handler head (`construct_catch_param`, `begin_catch`, the `cleanup.scope … cleanup all {end_catch}`) until it sees `CATCH_PARAM` or the first record that is neither that `LOCAL` nor `CATCH_PARAM`. One record of lookahead, no more. It adds `unwind {resume}` when there is no catch-all. |
| 0xD9 | `CATCH_PARAM(l, kind, g_copy\|nil)` | `cir.init_catch_param` kinds, chosen by the writer from the parameter type: reference_to_record_pointer 7 (a reference to a pointer whose pointee is a record), reference_to_pointer 6 (a reference to any other pointer), reference 0 (any other reference), pointer 1, scalar 2 (arithmetic, enum, bool, `nullptr_t`, pointer to data member), trivial_copy 4 (a class with a bitwise-copy init, or a pointer to member function, an Itanium aggregate: cir-lib confirms at S4, fallback scalar), non_trivial_copy 5 (a class with a copy-constructor init). 3 (objc) is invalid. An unnamed parameter still has its `LOCAL` and `CATCH_PARAM`. Kinds 5 and 6 make the library start the handler with `construct_catch_param` (kind 5 with the copy thunk). `g_copy` is required for 5 (the C1 copy constructor) and nil otherwise. `l` is a record for 4 and 5. A destructible parameter gets the usual `CLEANUP_BEGIN(all)`, nested inside the `end_catch` scope. The writer diagnoses a catch copy with default arguments or temporaries. |
| 0xDB | `TRY_END` | Closes the last handler. The shape is `TRY_BEGIN … CATCH_BEGIN … [CATCH_BEGIN …] TRY_END`, like `IF_ELSE`/`IF_END`. The value stack is empty at all three. TRY, CATCH and RETHROW are statement brackets for the switch rule. In a constructor or destructor function-try handler, the FE's IL already ends each handler with the implicit rethrow, so it arrives as an ordinary `RETHROW`. A destructor's function-try encloses its cleanup prologue (clang's `EmitDestructorBody`). |
| 0xDC | `THROW_BEGIN(t)` | Pushes the glv of the exception object (`cir.alloc.exception`). A **cleanup-only region**, like `FULLEXPR` (11.5): `LOCAL`s belong to the enclosing scope region, but every `CLEANUP_BEGIN` begun inside ends at `THROW_END`, innermost first. A `DEACTIVATABLE` or conditional cleanup inside is an error. The initializer consumes the glv (`STORE INIT` or as `this`); the value stack at `THROW_END` equals that at `THROW_BEGIN`. The library adds the `__cxa_free_exception` eh scope around the initialization (G1). Allowed in a `TERN` arm and the right operand of `LAND`/`LOR`; not in `STATIC_INIT`, `MEMINIT` or cleanup code. A `new` whose deactivatable cleanup would fall inside the region (`throw T(*new U)`) is diagnosed by the writer; `throw new T(f())` is fine. |
| 0xDD | `THROW_END(g_ti, g_dtor\|nil)` | `cir.throw %exn, @ti[, @dtor]` + `unreachable` |
| 0xDE | `RETHROW` | `cir.throw` (no operands) + `unreachable`. Allowed anywhere with exceptions = 1. |

**Terminated blocks.** After `THROW_END`, `RETHROW` or `UNREACHABLE`, the current block is terminated, and only
bracket closes may follow. A `TERN` arm, or a `LAND`/`LOR` operand, ended that way contributes no value; the other
arm decides the result. If both arms are terminated, the `TERN` is void.

**Constants**
- 0xEB `C_DATA_MEMBER(t, offset|nil)`.
- 0xEC `C_METHOD(t, g|nil, adj, flags)`. Any `adj` is valid; the library emits the lowered global form (G2).
- **vptrs in constants (Q4):** there is no `C_VPTR`. A `C_AGG`, `INIT_CONST` or `VAR_INIT` of a dynamic class
  never selects a `TY_VPTR` field (`C_SEL` of it is an error). The library stores every vptr of the object, as
  clang's `ConstStructBuilder` does:
  - for base subobjects, from the vtable group of the enclosing complete object;
  - for a member, from its own type.

  `C_ZERO` of a dynamic class is an error. A class with virtual bases is never literal.

### 1d. Flags and meaning

| Record | Addition |
| --- | --- |
| `DECL_FUNC` | `NOTHROW` 0x80, from `is_non_throwing_routine` only |
| `CALL`/`CALL_INDIRECT`/`PM_CALL` | `NOTHROW` 0x08: the callee or call type is non-throwing (`is_nothrow_type`). A call is nothrow iff it has the flag or its callee's `DECL_FUNC` has `NOTHROW`. |
| `CALL` | `VIRTUAL` 0x10: argument 0 is already cast to `g`'s class; the slot comes from `CLASS_VFUNCS`. The writer devirtualizes, as clang does (Q1): a call on a complete object that is not a reference or a dereference (`local.f()`), and a call of a `final` function or on a `final` class, are plain calls. |
| `CALL` | `VTT` 0x20: `nargs` counts the parameters of `t_call`, and the VTT operand (a `VTT_ADDR`) is inserted between argument 0 and argument 1. It is required iff the callee is the base variant of a `has_vbases` pair. |
| `DELETE` | `VIRTUAL` 0x08: a null check, then D0 through the vtable (slot+1 of `g_dtor`'s D1 slot). The class must have a `STRUCTOR_DELETING` (validated); the library calls D0, and `fn` is that record's `g_delete`. `::delete` of a polymorphic object is diagnosed until implemented. |
| `CLEANUP_BEGIN` | `DEACTIVATABLE` 0x01 (eh/all only; invalid on `exit`). Cleanup code may read `LOCAL`s declared before the `CLEANUP_BEGIN` (as M3's `this`). The writer keeps the addresses that the partial-aggregate and free-on-exception cleanups need (the `operator new` result, an element pointer), and the operands of a sized or placement delete, in pointer/value `LOCAL`s, evaluated once. |
| `CLEANUP_END` | Deactivates one cleanup: the innermost *active deactivatable* one begun in the same function, the same `FULLEXPR` and the same conditional arm. It disables both the normal and the eh path. Its range still ends with its region. No match is an error. `new U[3]{…}` with two deactivatable cleanups needs two. Library: CIRGen's flag + `cir.if`. The unconditional `new` begins its delete cleanup right after the allocation call (outermost by LIFO, K4d). The conditional `new` begins it inside the arm, and the library hoists the guarded scope to enclose the `FULLEXPR`, with function-level slots (`k4d2`). |
| `HDR_LANG` | `exceptions` = 1 is valid: calls and functions may throw unless nothrow; eh/all cleanups are live; EH records are allowed; `lowering_lang_options<exceptions=true>`. With exceptions = 0 the 1.0 rule stays (every function and call is nothrow) and EH records are errors. Supp key `esk_nothrow` is a hint only. |
| `HDR_LANG` | `rtti` = 0: the vtable RTTI slot is null; typeinfo objects exist only for EH; `TYPEID`/`DYNAMIC_CAST` are errors; `CLASS_MANGLE` is still required. |

### 1e. Member-pointer zero (K6c, gap G8)

A data-member pointer's null is −1, so a type that contains one anywhere (fields, bases, array elements) is **not
zero-initializable**. CIRGen expands such zeros into explicit null constants (`EmitNullConstant`); g++ does the same,
including static-storage zero-init.
- **Library.**
  - A cached `zero_initializable(t)` predicate.
  - For non-zero-initializable types, `C_ZERO`, `INIT_ZERO`, a definition with no `VAR_INIT`, the unlisted parts of
    `C_AGG`/`INIT_CONST` and `C_REPEAT` padding all produce the expanded null constant. Above 256 bytes it is
    copied from a private constant.
  - `#cir.zero` stays valid for method pointers (null `{0,0}`) and the vptr.
- **Format.** `ZERO_BYTES` on a glvalue whose type is not zero-initializable is a format error.
- **Writer.**
  - Use `INIT_ZERO` on the typed glvalue, a base subobject included.
  - `new S[n]{}` uses the same shape as `new S[n]()`.
- `TO_BOOL` and `CMP` eq/ne accept method and data-member prvalues; relational compares are errors.

## 2. Emission and linkage (writer decisions)

- **Key function** (clang's rule, computed at the end of the TU): the first own virtual function, in declaration
  order, that is `!pure && !compiler_generated && !inline_in_class_definition`. There is none if that function is
  `is_inline` (defined inline later), and none for a class template instance.
- **Vtable:**
  - key function defined here, not inline → external;
  - no key function (or an inline one), used here → `linkonce_odr` + COMDAT;
  - explicit instantiation definition → `weak_odr`;
  - TU-local class → internal.

  "Used" means a `VPTR_INIT`, a `VTT_ADDR` or a constant of the class (whose vptrs the library fills), or a
  reference from a construction vtable or VTT. A virtual call alone does not make a vtable used.
- **`extern template`:** a class instance covered by an explicit instantiation declaration, and not explicitly
  instantiated here, gets an external vtable declaration and no typeinfo definition (clang).
- **A dropped virtual function (Q5):**
  - A `linkonce_odr`/`weak_odr` vtable that would reference a dropped definition is not defined here. It is an
    external declaration, together with its VTT, construction vtables and typeinfo, and the writer warns, as for
    any dropped definition.
  - An internal or strong vtable is defined, and the library's unsupported rule applies to its references: a
    referenced dropped function is an error. The VTT,
  the construction vtables and the class typeinfo follow the vtable.
- **A defined vtable odr-uses every non-pure, non-deleted function in it.** They are declared, implicit definitions
  (destructor, copy assignment) are streamed, and inline ones of a `linkonce` vtable are defined here. Otherwise
  the writer diagnoses.
- **Typeinfo linkage** (clang's `getTypeInfoLinkage`):
  - fundamental types, and pointers to cv-unqualified or const fundamental types, are external;
  - a dynamic class goes with its vtable;
  - others are `linkonce_odr` + COMDAT where used;
  - TU-local types are internal;
  - pointers to incomplete classes are internal with the incomplete mask.

  The per-TU set is the closure over thrown, caught, `typeid`'d and base types.
- **Thunks** go with the overrider's definition (its linkage and COMDAT). Variadic thunks use `musttail`.

## 3. Library-owned names

**Names:**
- the derived ABI names: `_ZTS`, `_ZTC<D><off>_<C>`, `_ZTh`/`_ZTv`/`_ZTc` thunks, D0 bodies;
- `__cxa_pure_virtual`, `__cxa_deleted_virtual`;
- the `__cxxabiv1` class-type-info vtables;
- `__clang_call_terminate`, `__gxx_personality_v0`;
- `__cxa_{allocate_exception, free_exception, throw, rethrow, begin_catch, end_catch, get_exception_ptr, bad_cast,
  bad_typeid, call_unexpected}`, `__dynamic_cast`;
- the catch-copy thunk `__clang_cir_catch_copy_<canonical type mangling>`: linkonce_odr, hidden, tagged with the
  catch-copy-thunk attribute, `(ptr, ptr) -> void`, body `C1(dst, src)`, one per `g_copy` per TU.

**Clashes.** Each name reuses a stream entity of the same name and type. A different type uses replace-and-bitcast
(R8). Only a clash with a *definition* is an error.

## 4. Worked encodings (to write for §12)

1. Virtual call.
2. Constructor with `VPTR_INIT`.
3. Diamond C1/C2/D1/D2 with VTT.
   - 3b. Delegating constructor in a `has_vbases` class.
4. Thunk (library only; the facts that cause it).
5. typeid (static and dynamic).
6. `dynamic_cast` ptr/ref/void.
7. `throw T(A())`.
8. try/catch by value and by reference, catch-all, rethrow.
9. Function-try constructor.
10. `new T{A(), B()}` with `DEACTIVATABLE` + `CLEANUP_END`.
11. Member-pointer call.
12. A constant of a dynamic class (vptrs completed by the library).
13. Conditional `new`.
14. `S s{}` with a data-member pointer (null −1 via `INIT_ZERO`).

## 5. Open questions for the writer's IL map

- The FE-canonical parameter type identity for `sig`.
- The "used" marker for vtables.
- The EH dynamic-init flags mapped to `DEACTIVATABLE` (`is_array_freeing`, partial aggregates).
- Catch kinds from `a_handler.dynamic_init`.
- The per-TU typeinfo closure.
- `CLASS_MANGLE` of TU-local classes under the deterministic internal manglings.
- Whether the destructor function-try encloses the cleanup prologue.

## 6. Version history of this draft

- **v1:** orchestrator.
- **v3:** the writer's IL map: Q1 devirtualize; Q2 the writer maps `throw()`; Q3 the catch-kind predicates; Q4 no
  `C_VPTR` (library-completed vptrs); Q5 dropped virtuals and vague vtables; Q6 first need only. Also: the `sig`
  text, the vbase dsize formula, incomplete-class typeinfo, `extern template`, `throw T(*new U)`, the FE's implicit
  rethrow, cleanup-code locals, late class names.
- **v2:** cir-lib's critique.
  - Fixed: `CLASS_MANGLE`, non-class typeinfo uniqueness, member-pointer zero, the MEMINIT idx rule, THROW as a
    cleanup-only region, the PM_CALL order, catch kinds and the handler head, the `FUNCTION_TRY` state machine.
  - Dropped redundant facts: the `CLASS_INFO` flags, the `CLASS_BASES` virtual flag, `VFUNC_COVARIANT`, `TYPEID`
    `DYNAMIC`, `DYNAMIC_CAST` `TO_VOID`, `CATCH_END`, and the `VPTR_INIT` operand.
