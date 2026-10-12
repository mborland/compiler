# M4a spikes at the pin: CIR shapes for virtual functions, RTTI, EH and member pointers

Written by cir-lib, 2026-10-12. Input for the ECIL 1.2 contract and the M4b
slices. The sources of every spike are in `cir_src/spike/m4/` (its `README.md`
says how to run them); nothing there is part of the build or the tests.

**Verdict: no blocker.** Every construct M4 needs compiles, lowers, links with a
g++-compiled translation unit and runs with the same output as an all-g++
build, through the pass list of our pipeline. Two upstream gaps need a library
workaround (both tested, K4 and K6); one clang/g++ difference follows clang.

## 1. Method

- The LLVM pin is `894bfff5` (`cir_src/LLVM_REVISION`), the box image `cir-env`
  (`/edg/llvm-cir`).
- A spike's `cir.cpp` goes through the pin's CIRGen:
  `clang -cc1 -fclangir -emit-cir -clangir-disable-passes`. That output stands
  for what our library will emit, because the library builds the same ops from
  the same facts (the shapes below are quoted from it). Where the library will
  emit a shape that CIRGen does not (K4b, K6b), a `patch.py` edits the CIR text
  to that shape.
- Then the pass list of `lib/driver/pipeline.cpp`, with the pin's tools
  (`spike/m4/cir2obj.sh`): `cir-opt --cir-canonicalize --cir-target-lowering
  --cir-cxxabi-lowering --cir-lowering-prepare
  --cir-call-conv-lowering=target=x86_64`, then `cir-translate --cir-to-llvmir`
  (which runs hoist-allocas, flatten-cfg, eh-abi-lowering and goto-solver, as
  `lowerDirectlyFromCIRToLLVMIR` does), then `clang -c`. `edg-cir` cannot read
  `.cir`, so the tools were used; `runCIRToCIRPasses` is the same function list.
- Each program has a `host.cpp` compiled by g++ that creates objects, calls
  across the two compilers in both directions, derives its own classes from the
  CIR-compiled ones and prints events. The result is compared (output and exit
  code) with an all-g++ build, and the pin's own `clang -fclangir` is also run
  as a control.
- 16 spike programs (sources in `cir_src/spike/m4/`); each prints its events,
  so a difference in construction order, cleanup order or handler choice shows
  as a diff.

## 2. Verdict table

| Spike | What | Result | Fallback / note |
| --- | --- | --- | --- |
| K1 `k1_vtables` | single, multiple, diamond virtual inheritance, virtual destructors (D0/D1/D2), construction vtables, VTT with C1/C2/D1/D2 and the VTT parameter, virtual calls in constructors and destructors, g++ classes derived from CIR classes and the reverse | **PASS** | none needed |
| K2 `k2_thunks` | non-virtual and virtual this-adjusting thunks, covariant returns with null check (also through a virtual base), variadic `musttail` thunk, targets with a `NONTRIVIAL_ABI` by-value parameter and result (indirect, through CallConvLowering), trivially copyable by-value record | **PASS** | none needed |
| K3 `k3_rtti` | `#cir.typeinfo` for class, si, vmi (virtual bases, private base), pointer (cv, incomplete pointee), pointer to member, function pointer, array, enum, external fundamentals; `get_type_info`, `dyn_cast` ptr/ref/void with offset hints, `bad_cast`, `bad_typeid`; address identity against g++'s typeinfo objects | **PASS** | none needed |
| K4 `k4_eh` | nested try, catch-all, rethrow, return/break/continue/goto out of try bodies and handlers, cleanups in try, new free-on-throw, throw with temporaries, ternary arm that throws, function-try-block constructors (implicit rethrow), `noexcept` terminate, every catch-param kind incl. the copy thunks, NRVO | **PASS except** `__cxa_free_exception` (below) | K4b |
| K4b `k4b_free_exception` | `__cxa_free_exception` while constructing the thrown object | upstream **omits** it (exception storage leaks); with the library's `cleanup eh` shape: **PASS** | emit the cleanup (section 3.1) |
| K4c `k4c_eh_spec` | dynamic exception specifications (`throw(int)`, `throw()`, `bad_exception`, `set_unexpected`) | **PASS** | none needed |
| K5a `k5a_local_init` | `cir.local_init` with a throwing initializer; the same static local in the C1 and C2 bodies of a structor; an inline function's static | **PASS** (the guard is aborted: `__cxa_guard_abort` is called, the next call retries) | none needed |
| K5b `k5b_array_partial` | partial destruction when an element constructor throws (`T a[5]`, member arrays, `T[3][2]`, `new T[4]`) | **PASS** | none needed |
| K5c `k5c_unwind_c`, K5c2 | exceptions through C frames compiled by us (callback, recursion, libc `qsort`) | **PASS**, with and without `uwtable` | keep `uwtable` (decision 3); see section 3.5 |
| K5d `k5d_global_throw` | a throwing global initializer; `init_priority` | **PASS** (`terminate`, same output and status as g++) | none needed |
| K6 `k6_member_pointers` | by-value `{i64,i64}` member function pointers (parameter and result), global constant arrays, virtual method pointers, base-to-derived conversion of pointers to members, runtime `derived_method`, comparison, bool conversion, g++-built pointers consumed by CIR code and the reverse | **PASS except** one constant (below) | K6b |
| K6b `k6b_method_const_adj` | a `#cir.method` constant with a non-zero this-adjustment in a global | upstream **drops the adjustment silently**; with the pre-lowered constant: **PASS** | emit the global in lowered form (section 3.2) |
| K6c `k6c_pm_zero`, `k6c2_zero_attr` | zero- and value-initialization of records, arrays, bases and nested aggregates that contain pointers to data members (null is -1): globals with and without initializer, `S s{}`, `S s = {}`, `S()`, `new S()`, `new S[n]()`, placement new, static locals, return values, NSDMI | CIRGen: **PASS** (52 cases, mixed == g++ == `clang -fclangir`). Library's current zero paths: **WRONG, silently** (K6c2: 0 instead of -1) | expand zero constants for such types (section 3.7) |
| K6d `k6d_base_member_const` | `int D::*` designating a member of a non-virtual base (empty base E, bases B1, B2 at non-zero offsets, `D : E, B1, B2`): globals, locals, comparisons, use on `D` objects | **PASS** (mixed == g++ == `clang -fclangir`) | none needed (section 3.8) |
| K7 `k7_layout` | record layout of polymorphic classes from the streamable facts (a Python port of `CIRRecordLowering`) | **48 record types, 0 differences** from clang's | section 3.6 lists the facts |
| K10 `k10_throw_model` | the final THROW model: `throw T(A(3), fail)` in statement position, in a `?:` arm, and inside a nested try with rethrow; implicit deactivatable free of the exception object, temporaries' cleanups inside it; a throwing T constructor; a plain `throw int` | with the library shape (`patch.py`): **PASS vs classic clang** (identical output, 3 frees). g++ differs by design (below). `clang -fclangir` alone leaks the exception object (G1) | section 3.9 |

## 3. Findings

### 3.1 `__cxa_free_exception` is missing upstream (K4, K4b)

CIRGen emits `throw X(init)` as

```
%4 = cir.alloc.exception 16 -> !cir.ptr<!rec_Ex>
%6 = cir.call @_Z2mki(%5)                  // the initializer may throw
cir.call @_ZN2ExC1Ei(%4, %6)               // so may the constructor
cir.throw %4 : !cir.ptr<!rec_Ex>, @_ZTI2Ex, @_ZN2ExD1Ev
```

with no cleanup, so the exception storage leaks when the initializer or the
constructor throws (clang's classic CodeGen pushes `FreeException`,
`CGException.cpp:355`). Counted with `--wrap` of `__cxa_allocate_exception` and
`__cxa_free_exception`: g++ 2 frees, ClangIR 0.

The library emits an eh-only cleanup scope around the construction (the shape
`k4b_free_exception/patch.py` produces; tested with 5 throw sites, alloc/free
counts equal to g++'s):

```
%4 = cir.alloc.exception 16 -> !cir.ptr<!rec_Ex>
cir.cleanup.scope {
  ... evaluate the initializer, construct into %4 ...
  cir.yield
} cleanup eh {
  %fe = cir.cast bitcast %4 : !cir.ptr<!rec_Ex> -> !cir.ptr<!void>
  cir.call @__cxa_free_exception(%fe) : (!cir.ptr<!void>) -> ()
  cir.yield
}
cir.throw %4 : ...
```

The scope ends before `cir.throw`, so no deactivation flag is needed (the
runtime owns the object once `__cxa_throw` is reached).

### 3.2 A `#cir.method` constant cannot carry a this-adjustment (K6, K6b)

`static_cast<DMethod>(&B2::ng)` (a base-class member function converted to a
derived class pointer to member) in a global initializer becomes
`#cir.method<@_ZN2B22ngEi>`, which has no adjustment field: the value is
`{ptr, 0}` instead of `{ptr, 16}`. It is **silent**: `(d.*g_methods[4])(2)`
still works when the function does not use `this`, but the value differs from
g++'s (`g_methods[4] == gm[4]` is false; the words are `1 0` against `1 16`).
In an expression the same conversion is correct (a runtime `derived_method`,
or a folded constant of the right value).

Fallback, tested (`k6b_method_const_adj`): the library emits the global in the
form CXXABILowering produces, an array of
`#cir.const_record<{#cir.global_view<@fn> : !s64i, #cir.int<16> : !s64i}>`
(virtual: `{vtable_offset + 1, adj}`, null: `{0, 0}`) with symbol type
`!cir.array<!rec x N>` where `!rec = !cir.struct<{data !s64i, data !s64i}>`,
and every `cir.get_global` of it is followed by a `cir.cast bitcast` to the
pointer-to-member array type. All values and uses then match g++. This is
better than the plan's "diagnose a non-zero `adj` in a `VAR_INIT`" (decision 2
asked only for a workaround or a diagnosis); the library can do either.

### 3.3 Deactivation is needed: new/throw with temporaries (K4, K4b, K4d)

Corrected after K4d (the first version of this section said new needs no flag).
With no temporaries, free-on-throw of `new` is a closed
`cir.cleanup.scope { ctor } cleanup eh { operator delete }` around the
constructor. With temporaries in the initializer it cannot be: g++ and classic
clang, when the constructor throws, destroy the temporaries first and call
`operator delete` last (`T(7) ~B(2) ~A(1) T::delete`). So the free scope must
be the **outermost** scope of the full-expression with the temporaries nested
inside, and it must stop freeing once the constructor has returned: CIRGen
guards the `cleanup eh` with a flag (`cleanup.isactive`, set after the
allocation, cleared after the constructor). A closed scope around the
constructor alone runs delete before `~B ~A`; one that contains the
temporaries destroys them at the end of the new-expression, too early for a
larger full-expression. Cases (spike `k4d_deactivation`, compared with g++ and
classic clang):

| Case | Needs a flag | Note |
| --- | --- | --- |
| `new T{A(1), B(2), fail}` | yes | CIRGen's flag shape == g++ |
| `new U[3]{U(A(1),0), U(A(2),fail), U()}` | yes (two flags: storage, partial array) | CIRGen == g++ |
| `c ? new T(A(4), fail) : nullptr` | yes, and CIRGen is wrong | it closes the delete scope inside the ternary arm, so delete runs before `~A`; the library shape (`k4d2_cond_new_order`, == g++): save the pointer in function-level slots, flag set in the arm and cleared after the ctor, delete as the outermost eh cleanup, A's conditional cleanup inside |
| `throw T(A(3), fail)` | yes (corrected by K10, section 3.9) | the free scope is outermost with the temporaries' cleanups nested inside, and it is deactivated right before `cir.throw`; a closed scope around the construction alone cannot hold the temporaries |

Conclusion for the contract: keep `CLEANUP_BEGIN` flag `DEACTIVATABLE` plus an
explicit deactivation point (`CLEANUP_END`), for `new` (including the
conditional and array forms), for conditionally evaluated temporaries and for
the exception-object free of a `throw` whose operand has temporaries (section
3.9; an operand without temporaries can use a closed scope).

### 3.4 `noexcept` and clang: cleanups run before `terminate` (K4)

For a `noexcept` function that throws with an object with a destructor alive,
clang (and CIRGen, which copies it) catches the exception in a `catch all`
handler that calls `__clang_call_terminate` after the unwinding ran the inner
cleanup (`~Dtor` is printed); g++ finds the handler in phase one (the
specification filter) and terminates without unwinding (no `~Dtor`). The
standard leaves this open. We follow clang (decision 4 of session 3).
`k4_eh` has the case without the destructor so that the output is comparable.

### 3.5 Unwinding through C frames (K5c)

C functions compiled by us can be on the unwinding path (callbacks, `qsort`):
the three cases pass. The M3 library marks C functions `nothrow`; at the pin
`nothrow` is not lowered to LLVM `nounwind` on the function, so
`.eh_frame` entries are generated with or without the CIR `uwtable` attribute
(3 FDEs in `cir.o` both ways). `uwtable` is emitted only when the CIR function
has the attribute (`LowerToLLVM.cpp:2932`). Keep decision 3 (uwtable on every
function): it states the intent, costs a CFI row per function, and does not
depend on `nothrow` staying a no-op in the lowering.

### 3.6 What the library needs from the writer for the record layout (K7)

`k7_layout/layout_m4.py` reproduces clang's CIR record types for 48 types
(complete types and `.base` types of the 36 classes of `classes.cpp`:
single/multiple/diamond virtual inheritance, nearly-empty primary virtual
bases, empty bases and virtual bases, tail padding in dynamic classes,
dynamic members) from these facts only, with **0 differences**:

- per class: `size`, `nvsize`, whether it has its **own** vptr (a class has one
  iff it is dynamic and has no primary base; a primary *virtual* base shares
  the vptr and is laid out first, at offset 0, together with the non-virtual
  bases), the primary base and whether it is virtual;
- per direct base, in declaration order: the class, virtual or not, empty or
  not, and its offset (virtual bases: the complete-object offset);
- the virtual bases of the whole class (direct and indirect, clang's walk
  order) with their complete-object offsets;
- per field: offset, type, array dimensions.

Rules the library must port (all in `CIRGenRecordLayoutBuilder.cpp`):

1. `.base` is a distinct record **iff `nvsize != size`** (not `dsize`); its
   size is `nvsize`. For a class with virtual bases `nvsize` is smaller than
   the data size (dsize includes the virtual bases: `L : virtual V1` has
   `sizeof=32, dsize=28, nvsize=12`). The M3 library sizes `.base` by
   `data_size`; M4 must use `nvsize` (this is what `CLASS_INFO.nvsize` is for).
   The ECIL 1.1 validator rule "data_size is at least the end of the last
   field" must treat `VIRTUAL_BASE` fields consistently with that (the end of
   the last virtual base is the complete data size).
2. `determinePacked` uses the non-virtual size: `L`'s record is `packed`
   because 12 is not a multiple of the non-virtual alignment 8, although the
   members are aligned (`!cir.struct<"L" packed {data !cir.vptr, data !s32i,
   pad !cir.array<!u8i x 4>, data !rec_V1.base, pad !cir.array<!u8i x 4>}>`).
3. A virtual base that is nearly empty (`nvsize` 0 or one pointer) and is the
   primary virtual base of the class or of a base (`hasOwnStorage`: a walk
   over all bases using their primary-base facts) has no member of its own.
4. Empty bases are skipped, empty virtual bases too.
5. The vptr is `!cir.vptr` (size 8, align 8): the M3 library's opaque field
   becomes `cir::VPtrType`.

`cir-layout-compare-cxx.py` itself cannot be run on these classes with the
M3 library (it reads only scalar members, nested classes and empty bases,
and the library has no `TY_VPTR`, `nvsize` or virtual-base fields); the Python
port is the substitute until `lib/cirgen/types.cpp` has them (M4b S1/S2), and
`layout_m4.py` is then the check of the library's records: encode the facts it
parses, run `edg-cir`, compare the same `!rec_` lines.

### 3.7 Zero of a type with data-member pointers is not all-zero bytes (K6c, K6c2)

A pointer to a data member is null when it holds -1, so `S s{};`, a static
`S g;`, `new S()`, `S a[4] = {}` and `new S[n]()` must store -1 into every
data-member pointer, in nested records, bases and arrays (g++: `S{long a;
long S::*pm; long b;}` is `0 -1 0`). Method pointers are not affected (null is
`{0, 0}`).

What CIRGen emits (`k6c_pm_zero`, 52 cases, all equal to g++ and to the pin's
own `clang -fclangir`): never `#cir.zero` of such an aggregate. It expands the
null constant (`EmitNullConstant`), recursively through bases, fields and
elements:

```
cir.global external @g_s = #cir.const_record<{#cir.int<0> : !s64i, #cir.data_member<null> : !cir.data_member<!s64i in !rec_S>, #cir.int<0> : !s64i}> : !rec_S
%2 = cir.const #cir.const_record<{...#cir.data_member<null>...}> : !rec_S   // local S s{}: const + store
```

(`new S[n]()`: a loop that stores the constant per element. `new S[3]{}` is
`errorNYI emitNewArrayInitializer: unsupported initializer` at the pin, and only
for such non-zero-initializable element types: `new P[3]{}` of a plain record
works. `#cir.zero` appears only for records with method pointers and the vptr.)

What our library does today and why it is wrong:

- `zero_attribute(type)` is `getZeroInitAttr`: scalar data-member pointer gives
  `#cir.data_member<null>` (right), but a record or array gives `cir::ZeroAttr`.
  `K6c2` (a patch that replaces CIRGen's expanded constants by `#cir.zero`, as
  `C_ZERO`, a definition without `VAR_INIT` and the unlisted parts of a
  `C_AGG`/`INIT_CONST` do) builds and runs, and **silently stores 0**: `g_s 0 0
  0` instead of `0 -1 0`, in globals, locals, arrays, statics and bases (27
  constants replaced; every case that holds a null member differs from g++).
- `zero_fill` (`INIT_ZERO`, `INIT_CONST` of a zero constant) stores the zero
  attribute for objects of up to 16 bytes and calls `cir.libc.memset` above (by
  construction: 0, not -1; not run). `ZERO_BYTES` is a memset.
- `globals.cpp` also builds `cir::ZeroAttr::get(t->cir)` directly for fully
  unlisted aggregates and for `C_REPEAT` padding.

Conclusion for the contract and the library (adopted: library-side expansion):
the library defines `zero_initializable(type)` (no data-member pointer anywhere
in the type, cached per type) and makes `zero_attribute` return the expanded
constant for the other types; `zero_fill` stores that constant (copied from a
private constant when larger than 256 bytes, as for other big constants);
definitions without `VAR_INIT` of such a type get it as initial value; unlisted
parts of constants use it. `ZERO_BYTES` stays a memset and is therefore an error
(format error, "ZERO_BYTES on a type with data-member pointers") if the glvalue's
pointee type is not zero-initializable; clang does the same for a base subobject
by copying a null-constant global of the base's data size, which the writer can
ask for with `INIT_ZERO` on the base-typed glvalue (open item for the writer's
IL map: how EDG represents value-initialization of a class with a data-member
pointer).

### 3.8 A pointer to a data member of a non-virtual base (K6d)

`int D::*pm = &B2::y;` with `D : E, B1, B2` (E empty, B1 and B2 at non-zero
offsets) is expressible as `#cir.data_member<[path]>`, where the path lists CIR
member indices from `D` down to the member: `[0,0]`, `[1,1]`, `[1,2]`, `[0,1]`
in the spike. The empty base is omitted from the CIR record (it has no member),
so the library maps the ECIL field index to the CIR member index when it builds
the path. Globals, locals, comparison and `d.*pm` all matched g++ and
`clang -fclangir`; no G2-style lowered form is needed for data members (only
for method pointers with a non-zero adjustment, section 3.2).

### 3.9 The final THROW model (K10)

`throw T(A(3), fail)` is emitted by the library as

```
%e = cir.alloc.exception N
store true -> %isactive                      (function-level slot)
cir.cleanup.scope {                          // cleanup eh: if %isactive, __cxa_free_exception(%e)
  <A ctor>
  cir.cleanup.scope { <T ctor into %e>; store false -> %isactive; cir.throw %e ... ; cir.unreachable
                      ^dead: cir.yield } cleanup all { ~A }
  cir.yield
} cleanup eh { ... }
```

The free scope is outermost; the temporaries' cleanups are nested inside it; the
flag is cleared after the constructor returns and before `cir.throw` (the
runtime owns the object from then on). In a `?:` arm the same shape sits inside
the arm (flag slot at function level) and the arm ends with `cir.unreachable`;
a scope whose body cannot complete needs a dead block with `cir.yield`, or
`FlattenCFG` asserts "cleanup scope body has no exit". Results (compared with
classic clang, the reference, since g++ differs):

- Output identical to classic clang for statement position, the `?:` arm,
  nested try with rethrow, and for a throwing `T` constructor (`~A` runs, then
  the free; 3 frees for 9 allocations, as clang).
- `~A` of the temporary runs **during unwinding** (`std::uncaught_exceptions()
  == 1`) in clang and `clang -fclangir`; g++ destroys the temporary before
  `__cxa_throw` (`uncaught_exceptions() == 0`). This is the one observable
  g++/clang difference; the library follows clang (like G3). A thrown object's
  free is not observable otherwise.
- `clang -fclangir` alone: same output except it never frees (G1: 0 frees).

The writer therefore has to deliver the temporaries' cleanups in the throw's
enclosing FULLEXPR with the THROW_BEGIN..THROW_END bracket inside it (the
contract's "implicit DEACTIVATABLE eh free" rule). A THROW in a conditional
region without an enclosing FULLEXPR is a format error (validator rule added
in M4a).

## 4. Shapes the library will emit (quoted from CIRGen output at the pin)

Locations and attribute noise removed.

**Class record and vptr use** (K1). `!cir.vptr` is the first member of a class
with its own vptr; the virtual call:

```
%3 = cir.vtable.get_vptr %2 : !cir.ptr<!rec_A> -> !cir.ptr<!cir.vptr>
%4 = cir.load align(8) %3 : !cir.ptr<!cir.vptr>, !cir.vptr
%5 = cir.vtable.get_virtual_fn_addr %4[0] : !cir.vptr -> !cir.ptr<!cir.ptr<!cir.func<(!cir.ptr<!rec_A>) -> !s32i>>>
%6 = cir.load align(8) %5 : ...
%7 = cir.call %6(%2) : (!cir.ptr<!cir.func<...>>, !cir.ptr<!rec_A>) -> !s32i
```

**Vtable group** (K1): one `cir.global` per class, `#cir.vtable` of an
anonymous record with one `!cir.array<!cir.ptr<!u8i> x n>` per vtable of the
group (primary, secondary, virtual-base vtables):

```
cir.global "private" constant external @_ZTV1D = #cir.vtable<{
  #cir.const_array<[#cir.ptr<32 : i64> : !cir.ptr<!u8i>,     // vbase offset
                    #cir.ptr<null> : !cir.ptr<!u8i>,           // offset to top
                    #cir.global_view<@_ZTI1D> : !cir.ptr<!u8i>, // RTTI
                    #cir.global_view<@_ZN1LD1Ev> ..., ...]> : !cir.array<!cir.ptr<!u8i> x 7>,
  #cir.const_array<[...]> : ...}> : !rec_anon_struct align(8)
```

An address point is `cir.vtable.address_point(@_ZTV1D, address_point =
<index = 0, offset = 3>) : !cir.vptr`; the VTT is a constant array of
`#cir.global_view<@_ZTV1D, [0 : i32, 3 : i32], address_point true>` and
`@_ZTC...` construction vtables (`@_ZTC1D16_1R`, private); C1 uses
`cir.vtt.address_point @_ZTT1D, offset = 1` for its base subobjects, C2 takes
the VTT as a second parameter (`%vtt`, `!cir.ptr<!cir.ptr<!void>>`) and uses
`cir.vtt.address_point %vtt, offset = n`, loads the vptr from it and stores it
with `cir.vtable.get_vptr`; the virtual-base offset is loaded as
`vptr[-24]` (`ptr_stride -24`, `load !s64i`) from the vptr in the object.
Pure and deleted virtuals point at `__cxa_pure_virtual` and
`__cxa_deleted_virtual`.

**Thunks** (K2): ordinary `cir.func`s named `_ZThn16_...`, `_ZTv0_n24_...`,
`_ZTch0_v0_n24_...`; non-virtual: `ptr_stride` by the negative adjustment then a
call; virtual: load the vcall offset from the vtable (`ptr_stride vptr, -24`,
load `!s64i`); covariant: call, null check, `cir.ternary` adjusting
(`ptr_stride` by a constant or by the vbase offset loaded from the vtable);
variadic: `cir.call ... musttail` with `(...)`; by-value records
(`!rec_Q`) are passed as record values and lowered by CallConvLowering.

**Typeinfo** (K3): `#cir.typeinfo<{...}>` constants with the class of
`__cxxabiv1` vtable as `#cir.global_view<@_ZTVN10__cxxabiv117__class_type_infoE,
[2 : i32]>`, the `_ZTS` name, then the class-specific words: `si`: the base
`_ZTI`; `vmi`: `#cir.int<flags>`, base count, then per base (`_ZTI`,
`#cir.int<offset << 8 | flags>`); pointer: `#cir.int<qualifier flags>`
(8 for an incomplete pointee) and the pointee `_ZTI`; pointer to member: flags,
pointee, class. External fundamentals (`_ZTIi`, `_ZTIPKi`) are declarations
(`cir.global "private" constant external @_ZTIi : !cir.ptr<!u8i>`).
`typeid(*p)`: `cir.vtable.get_type_info %vptr`. The three `dyn_cast` forms are
`cir.dyn_cast ptr|ref|... #dyn_cast_info__ZTI2S0__ZTI1M`, where the info
attribute holds the two typeinfos, `@__dynamic_cast`, `@__cxa_bad_cast` and
`offset_hint` (the number clang computes: `-1` virtual base, `-2` not a
public base, else the offset of the destination base in the source or 0);
a cast to `void *` is `cir.dyn_cast ptr %p : ... -> !cir.ptr<!void>` with no
info.

**Exceptions** (K4, K4c, K5): functions with exceptions have
`personality(@__gxx_personality_v0)`; a try is

```
cir.try {
  %e = cir.alloc.exception 4 -> !cir.ptr<!s32i>
  cir.store ..., %e
  cir.throw %e : !cir.ptr<!s32i>, @_ZTIi       // (+ destructor for classes)
  cir.unreachable
} catch [type #cir.global_view<@_ZTIi> : !cir.ptr<!u8i>] (%arg1: !cir.eh_token) {
  %tok, %exn = cir.begin_catch %arg1 : !cir.eh_token -> (!cir.catch_token, !cir.ptr<!void>)
  cir.cleanup.scope {
    cir.init_catch_param scalar %exn to %3 : !cir.ptr<!void>, !cir.ptr<!s32i>   // or reference / pointer / trivial_copy / non_trivial_copy
    ...
    cir.yield
  } cleanup all { cir.end_catch %tok : !cir.catch_token; cir.yield }
  cir.yield
} unwind (%arg1: !cir.eh_token) { cir.resume %arg1 : !cir.eh_token }
```

(`catch all` instead of the type; for a by-value class parameter the handler
starts with `cir.construct_catch_param non_trivial_copy %arg1 to %8 using
@__clang_cir_catch_copy__ZTS1T` and the body is in a nested cleanup scope that
runs the parameter's destructor); a dynamic specification is
`cir.try { ... } filter [#cir.global_view<@_ZTIi> ...] (%tok) { cir.resume %tok }
unexpected (%tok) { cir.eh.unexpected %tok }`; a `noexcept` function body is a
`cir.try { ... } catch all (%tok) { cir.eh.terminate %tok }` and the function
has the `nothrow` attribute; a function-try-block constructor is a `cir.try`
around the member initializers (each initializer a nested `cir.cleanup.scope
... cleanup eh { destructor }`) and the body, with a `catch all` handler whose
last operation is the implicit rethrow, a `cir.throw` without operand; free-on-throw of `new` is
`cir.cleanup.scope { ctor } cleanup eh { operator delete }`; local statics are
`cir.local_init static_local @_ZZ4onceiE1v ctor { ... }` (an aborted guard is
emitted by LoweringPrepare: `__cxa_guard_abort` appears 9 times in the lowered
module of `k5a`).

**Member pointers** (K6): `!cir.method<!cir.func<(!cir.ptr<!rec_D>, !s32i) ->
!s32i> in !rec_D>` and `!cir.data_member<!s32i in !rec_D>`; constants
`#cir.method<@fn>`, `#cir.method<vtable_offset = 24>`, `#cir.method<null>`,
`#cir.data_member<n>`; calls through `cir.get_method`; conversions
`cir.base_method`/`cir.derived_method` and
`cir.base_data_member`/`cir.derived_data_member`; CXXABILowering turns the
types into `{i64, i64}` and `i64` records and CallConvLowering then passes
them in two registers (K6: the g++ side agreed in both directions).

## 5. Gaps and differences found (summary for the contract)

| # | Finding | Effect on the contract / library |
| --- | --- | --- |
| G1 | no `__cxa_free_exception` cleanup (K4b) | the library emits it for every `THROW` (section 3.1); no extra record needed |
| G2 | `#cir.method` constant with `adj != 0` in a global (K6b) | library emits the lowered-form global (section 3.2); `C_METHOD(g, adj, virtual)` stays valid for every `adj` |
| G3 | clang runs cleanups before terminate in `noexcept` (3.4) | documented difference from g++; follow clang |
| G4 | `.base` is `nvsize`, not `dsize` (3.6) | `CLASS_INFO.nvsize` is required; the validator's dsize rule must count virtual bases (3.6 item 1) |
| G5 | the CIR text printer can print location aliases it does not define | irrelevant to the library (no text round trip); the spikes strip locations |
| G6 | `emitDelegateCXXConstructorCall`, `__fundamental_type_info` vtables, relative vtables, MSVC deleting-dtor flag are NYI upstream | the library builds the VTT arguments and D0 itself, as planned; user-defined `__fundamental_type_info` is not needed (we emit nothing for it) |
| G7 | `new` with temporaries needs a deactivatable cleanup; CIRGen orders the conditional-new case wrongly (3.3) | keep `DEACTIVATABLE`/`CLEANUP_END`; the library emits the outermost-scope-plus-flag shape |
| G8 | zero of a type with data-member pointers is not all-zero bytes; our `C_ZERO`, zero-fill, no-`VAR_INIT` and `C_AGG` unlisted parts store 0 (K6c2) | library expands the null constant (3.7); `ZERO_BYTES` on such a type is a format error; `new S[n]{}` is NYI upstream |

## 6. Not covered, and why

- Optimization levels above `-O0` (`cir-simplify`, the LLVM pipeline) were not
  run; the library's optimizer settings are unchanged since M3.
- Coroutines, `__cxa_vec_*` array cookies with virtual destructors (the
  array-delete path with a cookie was exercised only for `new T[4]` in K5b),
  `setjmp/longjmp` EH, Microsoft ABI.
- Thunks of a covariant function in a class with a primary virtual base were
  generated (`CVVD`), not every combination of return adjustment and this
  adjustment; `lib/abi` will print the thunk table and `edg-cir-abi-diff` will
  compare it with clang's `-fdump-vtable-layouts`.
