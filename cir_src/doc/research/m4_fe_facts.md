# The front end's layout facts against clang (M4a input)

Written by cir-config, 2026-10-12. Input for cir-writer's M4a map and
cir-lib's vtable builder.

In `cpfe-cir` (`DO_IL_LOWERING=0`) the layout of classes comes from the front
end: offsets, the primary base, the virtual function numbers and the lists of
overriding functions. The M4 library builds Itanium vtables from them. This
note says what each field means (read in `src/il_def.h` and checked against
the debug front end's `--il_display`), and compares the values with clang's
`-fdump-vtable-layouts -fdump-record-layouts` (the pin's clang) over 260 files.

Result: **the front end's facts agree with clang's in every check except one
real case (`abi/vbase13`, a g++/clang difference, section 4.1)**, and two
traps for the writer (sections 4.3 and 4.4).

## 1. Tool and method

`dev_tools/bin/edg-cir-fe-facts` (new; self-test: `--self-test`; run it in the
cir-env image from the repository root, after building `build/gcc-cir`):

```
edg-exec --img cir-env -- python3 dev_tools/bin/edg-cir-fe-facts facts F.cpp
edg-exec --img cir-env -- python3 dev_tools/bin/edg-cir-fe-facts check -v F.cpp ...
edg-exec --img cir-env -- python3 dev_tools/bin/edg-cir-fe-facts check --list FILES --json OUT
```

- `facts` runs `util/eccp.sh -S -tused --c++17 --il_display` with `CPFE=cpfe-cir`
  (the debug front end writes the IL as text on standard output, between the
  diagnostics), parses it, and prints per polymorphic class: size, alignment,
  nvsize, nvalign, data size, vptr offset and the class whose vptr it is, the
  primary base (and whether it is virtual), every base with its offset and the
  override list of its entry, every declared virtual function with its number
  and flags, and the key function.
- `check` compares these facts with clang's: the layout and vtable dumps of
  `edg-cir-abi-diff` (canonical text) and clang's LLVM IR (for the linkage of
  the vtables).
- Compared, per class that has virtual functions or virtual bases: size,
  align, nvsize, nvalign, dsize (a class without virtual bases, computed as
  `ecil_data_size_of_type` does), the primary base and whether it is virtual,
  the vptr offset, the offsets of the direct non-virtual and of all virtual
  bases, each declared virtual function's number against clang's "VTable
  indices", the final overrider of every slot of every vtable group (address
  point) of clang's `Vtable for` dump against the front end's lists, and the
  key function against clang's vtable linkage.
- Not compared: construction vtables, VTTs, `vbase_offset` / `vcall_offset` /
  `offset_to_top` entries and the thunk adjustments (the library computes them
  from the offsets), RTTI, templates that clang does not instantiate, the
  Microsoft ABI.

Files (260 distinct; the box scratch is `~/edg-cir-config/m3-final/.tmp/m4a`):
the 24 clang `CodeGenCXX` tests our front end accepts in `--c++17` (clang's
`vtable-layout*.cpp`, `vtt-layout.cpp`, `thunks`-related and virtual-base
tests, gathered by `edg-cir-abi-diff gather`), the 56 `imported/gnu/cpp/abi/`
tests that mention `virtual`, the 18 virtual tests of the M4 draft smoke list
(`tests/.edgy/filters/cir-cpp-smoke-m4-draft.txt`), a sample of 159 more
tests with virtual functions (`inherit/`, `eh/`, `rtti/`, `other/`, `edg/ccs`,
`edg/rma`, `edg/daveed`, `cwg/`), and 5 synthetic cases made for this note
(`dev_tools/cir-fe-facts-cases/`).

| Group | File entries | Not checked | Classes | Checks | Disagreements |
| --- | ---: | ---: | ---: | ---: | ---: |
| clang CodeGenCXX tests | 24 | 0 | 293 | 2873 | 0 |
| M4 draft smoke list (virtual part) | 18 | 0 | 65 | 857 | 4 (`vbase13`) |
| gnu `abi/` with virtual (not in the draft) | 50 | 6 | 131 | 1312 | 0 |
| sample of other gnu, edg, cwg | 159 | 60 | 205 | 2010 | 0 (2 checker artifacts, fixed: 4.7) |
| `vbase8-*`: 4 originals, 4 copies with `int main` | 8 | 4 | 34 | 234 | 0 |
| synthetic cases | 5 | 0 | 33 | 430 | 0 |
| **Total** | **264** | **70** | **761** | **7716** | **4 (one root cause)** |

(260 distinct source files: clang rejects the four `vbase8-*` originals for
`main()` without a type, so they are checked as copies; 66 files were not
checked at all.) The run was made before the two artifact fixes of 4.7 (they
removed 2 disagreements in the sample group, `inherit/typeinfo1` and
`inherit/covariant21`; both files were re-run clean afterwards).

By kind of check (checks / agreements): size 761/760, align 761/761, nvsize
761/761, nvalign 761/761, dsize 415/415, primary base 761/760, primary base is
virtual 370/370, vptr offset 317/317, bases (name, offset, virtual) 761/760,
slot numbers 441/441, final overriders per slot 1383/1382, key function and
vtable linkage 224/224. The four failures are all `abi/vbase13` (4.1).

Not checked (66): 51 files that our front end rejects in `--c++17` without
the options of the test (it then prints no IL) and 15 that clang rejects (6 of
the gnu `abi/` ones include libstdc++ 15 headers that clang 24 cannot parse:
`invalid suffix 'bf16'`; the other 9 are in the sample).

## 2. How the slot numbers map to clang's indices

Established empirically (441 slot checks, no exception; 1383 final-overrider
checks, one exception, 4.1): **the front end's `number.virtual_function` is clang's index in
"VTable indices", i.e. the position relative to the address point of the
primary vtable.** `FIRST_VIRTUAL_FUNCTION_NUMBER` is 0 (`targ_def.h:2964`):
the first virtual function of a class with no primary base has number 0, which
is the entry right after the RTTI pointer. No offset has to be added.

The rules (`class_decl.c: update_virtual_function_number`, confirmed by the
cases of `dev_tools/cir-fe-facts-cases/`):

1. A virtual function that overrides a function of the **primary base chain**
   reuses that function's number (`Q::~Q` is 0 like `P::~P`; `B::f` is 0 like
   `A::f`).
2. A **destructor takes two numbers**: n for the complete-object destructor
   and n+1 for the deleting one. The next function gets n+2 (`P::~P` is 0,
   `P::p1` is 2; `S::~S` is 1, `S::b` is 3). `highest_virtual_function_number`
   counts the deleting slot (a class with `f` and `~A` has highest 2, three
   slots).
3. A virtual function that is **new**, or that overrides only functions of
   **non-primary bases**, gets the next unused number: `Z::h` (overrides
   `Y::h` of the second base) is 1, after `Z::g` 0, and `Z::z` is 2. The
   overrider also appears in the override list of its non-primary base entry
   (4.6).
4. A **covariant overrider whose primary base is a virtual base** gets a new
   number and keeps the overridden slot (`D2::get` is 1 while `B2::get` is 0;
   clang has the same two slots, the old one a return-adjusting thunk). A
   covariant overrider of a non-virtual primary base reuses the number
   (`D0::clone` is 0).
5. The numbers are assigned in **declaration order**, implicit members last:
   the implicit destructor of a class whose base has a virtual destructor is
   in the routine list after the user-declared functions, but takes the
   base's number so the order does not matter. An implicit destructor of a
   class without a virtual base destructor is not virtual and has no number
   (`X : W`).
6. A class with a vptr but no virtual function of its own or inherited (it has
   only virtual bases) has `highest_virtual_function_number` 65535 (the
   unsigned value of `VIRTUAL_FUNCTION_NUMBER_NONE`): treat it as "none", not
   as a count (section 4.4).

## 3. Field semantics

Class type entry (`kind` `tk_struct`, `tk_class`, `tk_union`) and its
`class-type-supplement` (`extra_info`):

| Field | Meaning (checked against clang) |
| --- | --- |
| `size`, `alignment` | clang's `sizeof`, `align` of the complete object (761/761 agree). |
| `size_without_virtual_base_classes`, `alignment_without_virtual_base_classes` | clang's `nvsize`, `nvalign` (tail padding included). |
| `any_virtual_functions` | the class declares a virtual function. |
| `any_virtual_functions_including_in_base_classes` | the class has virtual functions of its own or inherited. A class with only virtual bases is **not** counted, but has a vptr: use `any_virtual_base_classes` as well. |
| `primary_base_class` | the base class entry that is the primary base, `NULL` if none. It can be a direct non-virtual base (offset 0), a direct virtual base (`B : virtual A`), or an **indirect virtual** base (`E : virtual D`, `D : virtual C`: primary `C`). It is set also for a class that declares no virtual functions of its own. This is the field to use to decide whether the class has its own vptr. |
| `virtual_function_info_offset` | the offset of the vptr in the class; meaningful only if the class has a vptr. |
| `virtual_function_info_base_class` | the least derived class that shares the vptr, `NULL` if the vptr is the class's own **or if the primary base is an indirect virtual base** (4.3). Do not use it to detect sharing. |
| `highest_virtual_function_number` | the highest number assigned, or the highest shared one if the class declares none; 65535 if there is none (section 2). The vtable has `highest + 1` function slots. |
| `base_classes` | all bases, direct and indirect, one entry per subobject (a repeated non-virtual base has two entries at two offsets; a virtual base has one entry). `direct_base_classes` and `preorder_base_classes` list direct ones and a preorder walk. |

Base class entry (`base-class`):

| Field | Meaning |
| --- | --- |
| `type` | the base class type. |
| `offset` | the offset of this base subobject in the **class being laid out** (for a virtual base, in that class's complete object). It equals clang's offset in the record layout of that class (761/761). |
| `direct`, `is_virtual` | direct base; virtual in any step of the derivation. A direct virtual base has both set. |
| `shares_virtual_function_info` | TRUE only if a class derived from this base shares its vptr **and declares virtual functions itself**: a derived class without own functions can have a primary base whose flag is FALSE (`B : A {}` above: `A` is `B`'s primary base, flag FALSE). Do not use it. |
| `is_optimized_empty_base` | not reliable: set for `virtual` `A` (empty) in `B`, but not for the direct empty base `W` of `X : W` that has a vptr. Use the base type's `is_empty_class`. |
| `variant.overriding_virtual_functions` | the list of overriders, see below. |

Overriding list (`overriding-virtual-function`), on the entry of the class K
that **declares** the overridden function:

- `primary_function`: the overridden function, declared in K.
- `overriding_function`: a function declared in a class derived from K, at or
  below the class being laid out. There can be several entries for one primary
  function (an overrider in an intermediate class and a later one in the
  derived class); the **final overrider is the one in the most derived class**.
- `base_class`: the base class entry of the class that declares the overrider,
  `NULL` if it is the class being laid out itself.
- `return_adjustment_base_class`: for a covariant return, the base class entry
  of the return type's class (the adjustment needed from the overrider's to
  the overridden function's return type); `NULL` if none.
- The list belongs to the entry **of K at the offset of the subobject**: the
  final overrider of slot `n` of the vtable at (X, o) is found as follows.
  Take the function F with number `n` in X, or inherited from X's primary
  base chain (the number is the same up the chain); let K be its declaring
  class. Look in the entry of K at offset o (K = X, or a class at offset 0 of
  X; for a virtual primary base reached through X, the single virtual entry of
  K, wherever X puts its own copy); the item whose `primary_function` is F
  gives the overrider; none: F itself; if K is the class being laid out, F is
  already final. (`edg-cir-fe-facts` implements this in `final_overrider_fe`
  and agrees with clang 1382 times in 1383; the exception is `vbase13`.)
- There is no item for a function that the class being laid out declares and
  that overrides nothing in K: it is new (it is in the class's own function
  list).

Routine entry (a virtual function in the `routines` list of the class
scope; `this_class` of its function type names the class):

| Field | Meaning |
| --- | --- |
| `is_virtual`, `number.virtual_function` | virtual; the slot number of section 2. |
| `special_kind` `sfk_destructor` | the number is the complete destructor's; add 1 for the deleting one. |
| `pure_virtual` | pure. |
| `compiler_generated` | implicit (an implicit virtual destructor is in the list; its `defined` is TRUE only once it is used). |
| `inline_in_class_definition` | **inline at the point of the class definition: the body is in the class, or the declaration in the class has `inline`** (`K5::a` has it, `K4::a`, defined `inline` later, does not). |
| `is_inline` | inline in any way, **including an `inline` definition that comes after the class**. `is_inline` without `inline_in_class_definition` is the "inline later" case. |
| `defined` | the TU has a body for it. |
| `covariant_return_virtual_override`, `overrides_base_member` | covariant return override; overrides a base member. |

Key function (Itanium, clang's rule), computable from the flags:

```
key = first function F of the class's own virtual functions, in declaration
      order, with !pure && !compiler_generated && !inline_in_class_definition
if key is_inline (inline later): no key function
```

It matched clang's choice of vtable linkage in 224/224 checks (definition,
declaration, or `linkonce_odr`), including the `inline`-later cases of
`abi/key1` and `abi/key2`. Not tested: class template instantiations (clang
has no key function for an implicit instantiation), explicit instantiations,
dllexport.

## 4. Findings

### 4.1 `abi/vbase13`: nearly empty virtual base (g++ and clang disagree)

```cpp
struct E1 {};
struct E2 : E1 {};
struct E : E1, E2 {};
struct N : E { virtual void f() {} };
struct X : virtual N {};
```

The front end lays out `X` as g++ does (size 16, `N` at offset 8, no primary
base); clang makes `N` the primary virtual base (size 8, `N` at 0). The cause
is `is_nearly_empty_class` (`class_decl.c:9502`): "empty bases at non-zero
offsets make a class not nearly empty" (`E1` appears twice in `N`, the second
time at a non-zero offset). The test's own comment says it expects g++'s result. `--clang` does
not change it. The library follows the front end's offsets, so object layout (and the vtable
group of `X`) is g++'s here. **Decision (user, 2026-10-12): keep the front
end's g++ layout; this is a documented exception to the "follow clang on
differences" rule, and there is no front end change.** Consequences to
document for M4: a program that mixes clang-compiled and cpfe-cir-compiled
translation units with such a class (a virtual base that is nearly empty only
by clang's reading) is ABI-incompatible with clang but compatible with g++;
the `abi-diff` check lists `abi/vbase13` as a known exception, and the
expectation of that test stays the g++ layout. None of the other 760 classes
shows a layout difference.

### 4.2 Key function and "inline later"

The front end keeps enough to apply clang's rule (section 3). The point that
matters for the writer: the vtable of a class is emitted **where the key
function is defined**, so the library needs the key function and its
`defined` flag at the end of the TU, and the "inline later" test uses
`is_inline` after the class was completed. Class completion is early
(`class_decl.c` hook): a key function computed there would miss the later
`inline`. Compute it at the end of the TU.

### 4.3 `virtual_function_info_base_class` is NULL for an indirect virtual primary base

```cpp
struct C { virtual void f(); };
struct D : virtual C { };
struct E : virtual D { };     // primary base: C (virtual, indirect)
```

For `E` the front end reports `primary_base_class` = `C` (virtual),
`virtual_function_info_base_class` = NULL, vptr offset 0. Clang: `C (primary
virtual base)`, no vptr of `E`'s own. `ecil_data_size_of_type` adds the vptr
when `virtual_function_info_base_class == NULL`; for a class like `E` it would
count a vptr that is `C`'s. Use `primary_base_class` to decide.

### 4.4 The data size of a class with a virtual primary base

For `struct A { virtual void f(); }; struct B : virtual A {};` clang says
`sizeof=8, dsize=8, nvsize=8`; the front end has `nvsize` 8, and the writer's
data size formula (`ecil_data_size_of_type`: a vptr only when
`virtual_function_info_base_class == NULL`, no virtual bases) gives **0**. For
a class with other virtual bases, clang's `dsize` is that of the complete
object (it includes the virtual bases), while the writer sends the
non-virtual part, so the two are not comparable by design: the checker
compares `dsize` only for classes without virtual bases (415 checks, all
equal). The ECIL value matters for tail-padding reuse and for the by-value
classification; polymorphic classes are passed by address, so the risk is
low, but `consistent_data_size` should be told what is intended. (At M3 the
writer does not send the body of a class with virtual bases, so this was not
visible in the streams.)

### 4.5 Sentinel for "no virtual function"

A class with only virtual bases has a vptr and `highest_virtual_function_number`
65535 (`B : virtual A {}` where `A` is empty). A loop over
`0 .. highest` runs 65536 times. Treat 65535 (`VIRTUAL_FUNCTION_NUMBER_NONE`)
as no functions.

### 4.6 Rules that the writer must follow (no disagreement, but easy to get wrong)

- **Overriders of non-primary bases**: they are new numbers in the primary
  vtable (`Z::h`), and also appear in the list of the non-primary base entry.
  The final overrider of a function of a non-primary base is found on that
  base's entry (`non-primary-overriders.cpp`).
- **Repeated non-virtual bases** (`D : B, C` with `B : A`, `C : A`): two
  entries for `A`, at 0 and 8, each with its own list (`A::f -> D::f` on both,
  `A::g -> C::g` only on the second); match by type **and offset**
  (`repeated-bases.cpp`).
- **Virtual bases reached through another base's primary chain**:
  `(Ent, 8)` in `Msg : virtual Ent`, where `Ent : virtual Base`; `Base` has one
  subobject at offset 0 in `Msg`, not at 8. Look it up by type among the
  virtual entries (`inherit/covariant18`).
- **Implicit virtual destructors**: in the function list, after the declared
  ones, `compiler_generated`, with the base's destructor number; not defined
  until used (`implicit-dtor-slots.cpp`). A class with no virtual base
  destructor and no declared one has no virtual destructor.
- **Covariant return**: the list entry has `return_adjustment_base_class` for
  both the "same slot" and the "new slot" cases; the slot rule is section 2.
- **Names**: use the label of a pointer to the type for a qualified class name
  (`N::A`, `TT<int>`); the type entry's own name is the unqualified one. A
  class named by a typedef (`typedef struct {...} Heya;`) has no name in
  clang's dump of the layout of its derived classes.

### 4.7 Checker artifacts found and fixed (not front end problems)

Listed because the same traps will meet the writer: clang's `Vtable` text
omits namespace qualifiers in parameter types (`const A &` against the front
end's `const N::A &`), prints top-level `volatile`/`const` of by-value
parameters (`foo(volatile int)` for `foo(int)`), writes `[pure]` after the
destructor tags, and `A *A::get()` has no space before the name; layout
lines of a virtual base flagged "primary" can belong to a nested base (only a
flagged base at offset 0 is the class's primary base); a typedef-named base
is printed without `struct`.

## 5. Reproducers

Every disagreement above has one, in this note (4.1, 4.3, 4.4, 4.5) or in
`dev_tools/cir-fe-facts-cases/`:

| File | Rule |
| --- | --- |
| `implicit-dtor-slots.cpp` | implicit destructors, numbering with destructors |
| `non-primary-overriders.cpp` | overriders of non-primary bases |
| `repeated-bases.cpp` | repeated non-virtual bases, virtual base of a base |
| `covariant-slots.cpp` | covariant returns, with and without a new slot |
| `key-functions.cpp` | the key function: inline in class, later, pure, implicit |

Run `edg-cir-fe-facts check -v` on them to see the checks; all 430 pass.
`facts` prints what the writer will get, for example for `non-primary-overriders.cpp`:

```
class Z: size 16 align 8 nvsize 16 nvalign 8 dsize 16
  vptr offset 0 in base X; primary base X; highest virtual function number 2
  base X offset 0 direct shares-vptr
    X::g() -> Z::g()
  base Y offset 8 direct
    Y::h() -> Z::h()
  virtual Z::g() number 0 defined overrides
  virtual Z::h() number 1 defined overrides
  virtual Z::z() number 2 defined
```

## 6. What to do next

- cir-writer: the field rules of sections 2 to 4 (number mapping, dtor pair,
  primary base from `primary_base_class`, 65535, per-base lists by type and
  offset, key function at the end of the TU, the data size of classes with a
  virtual primary base).
- The user: 4.1 is decided (keep g++'s layout, documented exception).
- Next checks (not done here): construction vtables and VTTs against the
  library's own output (`edg-cir-abi-diff compare`, once the library prints the
  canonical form), class template instantiations, and the thunk adjustments.
