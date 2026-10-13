# M4 clang CIR shape differential (cir-lib, gate M4c evidence)

Author: cir-lib, 2026-10-13.  The M3c method
(`cxx_cir_differential.md`) applied to the M4 features: virtual functions,
virtual bases and VTTs, RTTI, exceptions, member pointers, new/delete.

## 1. What was compared

- All 84 probes in `cir_src/test/writer/probes/m4/*.cpp` were run, 69 could
  be compared (the other 15 are in the table below); the writer's M4 feature
  probes are s1_* virtual functions, s2_* virtual bases,
  s3_* RTTI and member pointers, s4_*, s5_*, e* exceptions, v* vtable
  layouts, t* typeinfo, m* member pointers, i1, l1, n2).
- Pipeline (`cir_src/scripts/box_diff_m4.sh`, inside cir-env): real front end
  (`cpfe-cir`, the writer tree's binary of 2026-10-13 09:01) to ECIL, then
  `edg-cir --emit=cir` (no CIR passes, library of the working tree) against
  `clang -cc1 -fclangir -emit-cir -clangir-disable-passes -fcxx-exceptions
  -fexceptions` (the pin, 894bfff5) on the same source.
- `cir-compare.py --cxx --fe --no-structor-fold --dump DIR` normalizes both
  sides (SSA numbering, locations, writer-order noise, alloca order: the
  M3c normalizations) and writes the per-function text; the structor
  variants C1/C2/D0/D1/D2 are NOT folded any more (so the synthesized D0 is
  compared as D0).
- `cir-op-compare.py` counts, per function, the *material* ops (calls by
  callee with the variant digit folded, vtable/vtt/base-class ops, EH ops
  and cleanup kinds, array loops, member-pointer ops, memcpy/memset,
  overflow and select ops, loops, switches, get_global of a symbol) and
  reports the multiset difference.  Not counted (shapes of the same thing
  or plumbing): alloca/load/store/const/cast/br/yield/return,
  cir.scope, cir.unreachable, cir.trap, if/ternary, ptr_stride, get_member,
  get_element.  Aliases (`alias(@x)`) are resolved to the body.  Functions
  that one side only declares are not compared (clang defers inline
  functions; 197), nor are clang's `cir.trap` stubs (24).

Programs that could not be compared (not library differences):

| probe | reason |
| --- | --- |
| s5_pmb_corresponding_member2, s5_pmb_corresponding_member5, s5_pmb_pointer_interconvertible_with_class2 | the front end fails (C++20 library traits it rejects) |
| n1_names, s1_devirt, s5_anon_meminit | the writer aborts the stream (F_ABORT, a diagnosed unsupported construct) |
| e6_ftb, s5_ftb | clang CIR: NYI "function-try-block destructor" |
| m2_valinit, s3_dmp, s5_new_list | clang CIR: NYI `emitNewArrayInitializer` (`new S[n]{}`) |
| s5_class_cond, s5_delete_throw | clang CIR: NYI "emitVirtualObjectDelete: global delete" |
| v9_zero_base | clang CIR: NYI `emitNullBaseClassInitialization` |
| s1_local_late | clang cannot parse the container's libstdc++ headers with this setup |

## 2. Result

69 programs, 1221 functions present as a body on both sides:

- **873 (71.5%) have the same material op multiset.**
- **348 differ.  281 of those are explained by seven categories below; 67
  are shape differences listed in section 4.**  No difference is a missing
  semantic operation of ours that clang has, except the ones that are
  clang's own bugs or stubs (2.1 to 2.4).

Function-level categories (number of functions):

| n | category | who is right |
| --- | --- | --- |
| 102 | edg-cir stores the vptr in destructors (D1/D2: the class's own address point, from the VTT in classes with virtual bases); the pin does not | ours (Itanium and classic clang do; a virtual call from a destructor body must reach the class being destroyed) |
| 79 | clang has a dead `cir.get_global @printf` (or `@free`, `@malloc`, a builtin) before a direct call | cosmetic (an unused value) |
| 33 | clang has a dead `cir.base_class_addr` of a base subobject it never uses (empty or unused base in a constructor) | cosmetic |
| 31 | clang calls a constructor where the front end constant-initialized a class whose only dynamic state is the vptr (a `cir.const` record with the vtable address point and a store) | equivalent; the front end's choice (spec 12.7) |
| 18 | edg-cir calls `std::forward`/`std::move`/`__addressof`/placement `operator new`/`__invoke_r` etc., clang folds them to the identity | cosmetic (the callee is an inline function the writer sends) |
| 10 | edg-cir zeroes a base subobject with `cir.libc.memset` (INIT_ZERO_BASE), clang stores a zero constant record | equivalent |
| 8 | edg-cir frees the exception object (`__cxa_free_exception` in a DEACTIVATABLE eh cleanup) when the initializer of `throw T(...)` can throw; clang CIR has no such cleanup | ours (the pin leaks the storage; classic clang and g++ free it) |

### 2.1 Deleting destructor D0 (24 functions skipped, 125 try wrappers ignored)

The pin emits D0 as a stub (`cir.try { cir.trap } catch all { terminate }`)
for classes without a user D0 body, and, when it does emit one, wraps it in
`try { D1; delete } catch (...) { terminate }`.  Ours is the real D0
(`cleanup.scope all { D1(this) } cleanup { operator delete }`).  The wrapper
cannot trigger: D1 is noexcept itself.

### 2.2 Conditional `new` with a throwing constructor and a temporary

`c ? new T(A(4), f) : nullptr`, constructor throws: g++ and classic clang
run `~A` and then `operator delete`; ours does the same (one
`cleanup.scope all` around the full expression); clang -fclangir runs
`operator delete` first (it scopes the free in the arm).  Checked on the
box with all three: gcc and classic `~A, delete`; clangir `delete, ~A`.
The pin deviates; ours matches the other two.

### 2.3 Array element destructors that throw

Verified in the cleanup work (`cxx-s4-eh-cleanup-throw.test`): the pin
skips the destruction of the elements before the one that threw, as ours
does (the pin cannot express it).

### 2.4 Typeinfo names of internal-linkage types

The `*` prefix that libstdc++'s `type_info::name()` skips
(`"*Z4mainE5Local"`) is in our `_ZTS` strings (the front end's names) and
not in the pin's.

## 3. Globals and types

374 globals differ textually, 348 of them only by the spelling of the
structor variant in a vtable slot (`_ZN3DogD1Ev` vs `D2Ev`, the D1 is an
alias of D2 in classes without virtual bases), the numbering of
anonymous record types, `"private"`/`dso_local`/`linkonce_odr` spellings.
26 differ in content:

- 6: the `*` typeinfo-name prefix (2.4).
- `@dm` (s3_memptr): an array of method pointers, one with a this-adjustment,
  is in the lowered `{i64, i64}` form (G2); clang keeps `#cir.method`.
- `_ZTI5Later`, `_ZTIP5Later`, `_ZTS5Later`, `_ZTSP5Later` (t2_incomplete): a
  class completed after its first typeinfo use: clang makes all four
  `internal`, the stream's linkage for them is `linkonce_odr` (a writer
  question: the first-use rule of the typeinfo of an incomplete class).
- `_ZTIPA4_Kc` (s3_typeinfo), `@all`/`@infos`/`@after`/`@ga`/`@_ZSt4cout`/
  `@_ZSt7nothrow` (type name spelling `!rec_basic_ostream` vs
  `!rec_std3A3Abasic_ostream...`, a `constant` flag on `ga`, an initializer
  the writer leaves to dynamic initialization in `all`): no semantic
  difference found; they are writer/spelling items.

Record types: 582 common, 130 differ, all in packed/padding spelling or
member naming of the `.base` records (M3c section 4).

## 4. The 67 unexplained differing functions (shapes)

By group (counts are functions):

- ~12 structor variant bodies: our complete variant is the base variant's
  body or a call to it (D1 inlined, e3b_spec17, s1_template, v4_key,
  v4b_key20; local-class structors in l1/s1_names/s1_zoo); clang calls D2
  from D1.
- ~12 cleanup structure of `new` in full expressions and conditional
  returns (e4_new cond/main/mkarr, e4b, s5_class_cond_return,
  s5_nothrow_new, s5_ref_array_temp, s4_partial, s1_delete make,
  v1_slots clone, i1 main, s2_structors main, s1_sig20 main): one
  `cleanup.scope all` for the whole expression and `eh` cleanups for
  array partial destruction (ours) against several nested scopes (clang);
  array construction/destruction as `cir.array.ctor/dtor` with a
  partial-dtor region (ours) against do-while loops (clang).
- ~12 `base_class_addr` shapes in classes with virtual bases: ours casts
  through a null-checked `V*` and then `base_class_addr [8]`, clang adds
  the vbase offset and the base offset in one ptr_stride.
- 4 member-pointer conversions (m1_memptr, m3_pm_casts, s3_memptr main): the
  counts of the run-time `derived_method`/`base_data_member`/
  `derived_data_member` ops differ in both directions (which side builds
  the conversion as a constant and which as an op depends on the
  expression), all values equal in the run tests.
- 3 trivial copy assignment of a base: ours `cir.copy`, clang calls the
  trivial `operator=` (s2_structors FaSERKS_, v11_class_copy WaSERKS_).
- 2 `__builtin_memcpy`/`memcpy` of `char_traits::copy`: ours calls
  `memcpy`, clang uses `cir.libc.memcpy` (s4_stdexcept).
- ~12 libstdc++ template bodies in s3_stdlib/s4_stdexcept (different
  inline-call shapes of `std::function`, `std::string`, iterator tags),
  all call-for-call the front end's and clang's different inlining
  decisions of constexpr helpers.

Nothing in this list changes behaviour that the run tests (`cxx-s4-*`,
the writer's run tests) observe.

## 5. Library items found while doing this

- DELETE of a destructor-bearing class, member-pointer conversions and
  vptr stores: no library defect.
- Compare-only tools added: `cir-compare.py --no-structor-fold`,
  `--keep-d0`, `--dump`; `cir-op-compare.py` (with `--categorize` and
  `--groups`); `box_diff_m4.sh`.

## 6. Reproduce

```
# inside cir-env at a tree root that has build/gcc-cir/bin/cpfe-cir and build/cir:
bash cir_src/scripts/box_diff_m4.sh
# or with the front end of another tree and an edg-cir build elsewhere:
FE_TREE=/edg/other CIR_BUILD=/edg/mycir OUT=/work/d bash cir_src/scripts/box_diff_m4.sh
```

The summary (`OUT/summary.txt`) has the op tables, the categories, the
list of unexplained functions and the common whole-function deltas.
