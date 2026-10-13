# lib/abi: Itanium vtable layouts and VTTs

LLVM-free C++17. A port of clang's `ItaniumVTableBuilder`,
`ItaniumVTableContext`, `VCallAndVBaseOffsetBuilder` and `VTTBuilder`
(`clang/lib/AST/VTableBuilder.cpp`, `VTTBuilder.cpp`; Apache-2.0 WITH
LLVM-exception, attribution in each file), without the Microsoft ABI and
without `FinalOverriders` (the front end supplies the final overriders).
**Standalone for now**: it is not referenced from `cir_src/CMakeLists.txt`.

    cmake -S cir_src/lib/abi -B build/cir-abi -G Ninja
    ninja -C build/cir-abi && ctest --test-dir build/cir-abi

## Non-recursive by construction

Every recursion of the original (AddMethods down the primary chain,
LayoutPrimaryAndSecondaryVTables / LayoutSecondaryVTables,
DeterminePrimaryVirtualBases, LayoutVTablesForVirtualBases,
AddVCallAndVBaseOffsets / AddVCallOffsets / AddVBaseOffsets,
ComputeBaseOffsets, the base path searches, and in the VTT builder LayoutVTT /
LayoutSecondaryVirtualPointers / LayoutVirtualVTTs) is a loop over an explicit
heap stack. The inheritance depth is a property of the input. The ctest entry
`abi_deep_chain_small_stack` builds 5000-level primary chains and 5000-level
chains with a secondary base at every level under `ulimit -s 256`.

## Input: the class facts (abi_facts.h)

Dense ids (a class, a virtual function). Each field maps to one ECIL 1.2 fact
(the record names are the plan's; `research/m4_fe_facts.md` has the front-end
side):

| `abi_facts.h` | Writer fact |
| --- | --- |
| `a_class_fact::size`, `nvsize` | `TY_RECORD_BODY` size, `CLASS_INFO.nvsize` |
| `is_dynamic` | the class has a vptr (`CLASS_INFO` polymorphic or has_vbases) |
| `primary`, `primary_virtual` | `CLASS_INFO.primary_idx` and the base's virtual flag (use the front end's `primary_base_class`, not `virtual_function_info_base_class`, which is NULL for an indirect virtual primary base) |
| `nslots` | `CLASS_INFO.nslots` (`highest_virtual_function_number + 1`; 65535 means none) |
| `bases[]` | `CLASS_BASES`, declaration order (`direct_base_number`), offsets from `REC_FIELD` (a virtual base: its complete-object offset) |
| `vbases[]` | the `VIRTUAL_BASE` fields: every virtual base, direct or indirect, with complete-object offsets |
| `fns[]` | `CLASS_VFUNCS`, declaration order, implicit ones included |
| `a_fn_fact::slot`, `is_pure`, `is_deleted`, `special` | `CLASS_VFUNCS` slot and flags (a destructor: complete's slot, deleting = slot + 1) |
| `a_fn_fact::sig` | the proposed per-method signature class (0 for a destructor) |
| `a_fn_fact::ret_class` | the result class of a function returning a pointer or reference to a class (from the result type of its `TY_FUNC`; the covariant adjustment is derived from base offsets, no record carries it) |
| `a_fn_fact::overridden` | every function it overrides (the whole set, not a chain: `finish()` derives it, sorted and unique, from the `CLASS_OVERRIDES` records and `add_overridden`; no transitive closure is computed) |
| `a_class_fact::overrides[]` | `CLASS_OVERRIDES(t, [(g_fn, sub_off, g_ovr, ovr_off)])`: final overriders **per class t**, the offsets being offsets in t (a virtual base's own offset in t). A function without an entry is its own final overrider. |

Also in the facts: `a_class_fact::mangled` (the `<type>` mangling of the class
as the stream names it, "1D", "N1N1CE"), `is_empty` (clang's `isEmpty`, for the
RTTI single-inheritance test), `a_base_fact::is_public` (RTTI base flags) and
`a_fn_fact::mangled` (the function's symbol; a destructor: any variant).

`a_class_db::validate()` rejects unknown ids, bad lists and cycles in the
inheritance graph before anything runs. `validate_deep()` (abi_validate.cpp) is
the check of the CLASS_* facts that the decoder runs after the last record:
bases and virtual bases fit inside the class, a virtual base is not inside the
non-virtual part, the primary base is a direct base (or the virtual base) at
offset 0, slots are in `[0, nslots)` (a destructor takes two), `sig` is 0 for
destructors only, only dynamic classes declare virtual functions, every
overridden function is declared in a base, and every `CLASS_OVERRIDES` entry
names a subobject (class, offset) that exists in that class. Primary virtual
bases are exempt from the fit and overlap checks the way clang's
`hasOwnStorage` walk defines them (a primary virtual base shares the storage of
the class that has it as primary; the walk follows the chain of primary bases),
and empty classes are exempt from them altogether (`is_empty`). The checks use
an ancestor bit matrix (bounded at 16384 classes) and positions maps, not
repeated walks: a 2000-deep override chain validates in well under a second
under `ulimit -s 256`.

## The fact builder (abi_builder.h)

The interface the ECIL decoder fills. Records name classes and functions by
their stream ids (sparse 64-bit keys, any order, forward references allowed):
`set_class` (CLASS_INFO), `add_base` / `add_vbase` (REC_FIELD BASE /
VIRTUAL_BASE, declaration order), `add_vfunc` (CLASS_VFUNCS),
`add_overridden`, `add_override` (CLASS_OVERRIDES). A class that is not dynamic
has no CLASS_INFO unless something needs one (12.2: only dynamic classes have
their layout facts right away): the decoder defines it with `set_plain_class`
(size, data size, emptiness, from its record body) and feeds its BASE fields
through `add_base` (a non-dynamic class has no virtual bases; `is_public` is
not known there and true is passed). `vmi_class_flags` then walks such a
class by those BASE fields (`test_plain_bases`). `finish(db)` resolves the
keys, reports a key that was used but never defined, derives each function's
`overridden` list, runs `validate_deep()` and then `check_all_slots(db)` (the
structural slot check for every dynamic class; the strict comparison with the
builder's own method indices only for classes that are not the primary base of
another class, because the front end numbers a primary base's functions first),
and resets the builder; it returns the first problem.

## Special names (abi_names.h)

`_ZTV/_ZTT/_ZTI/_ZTS` from the stream's class mangling (`typeinfo_name_string(
mangled, tu_local)`: the `_ZTS` string, `'*'`-prefixed for a TU-local type so
the name never compares equal across translation units), `_ZTC<D><off>_<C>`
(construction vtables; the base name is compressed against the substitutions
the complete name introduces, for names made of source-names, `St` and
template arguments of builtin types; anything else gives the plain
concatenation and `exact = false`: the symbol is still unique and names the
same construction vtable), thunk symbols `_ZTh/_ZTv/_ZTc` (this and result
call offsets in front of the target's symbol; the deleting and complete
destructor variants by `dtor_variant_symbol`).

clang emits a method's thunks in two ways: those a vtable entry uses (with the
vtable, and with a construction vtable if its VTT is emitted), and all thunks
its vtable groups recorded for a method (`a_vtable_info::thunks`) when the
method is defined (`CodeGenVTables::EmitThunks`); `abi_dump --symbols` prints
the first kind as `thunk`, the second as `thunk-opt`.

## RTTI (abi_rtti.h)

`rtti_layout(ctx, c)`: which of `__class_type_info`, `__si_class_type_info`,
`__vmi_class_type_info` describes the class, and its fields in memory order
(vtable pointer + 2 slots, name pointer, then the base typeinfo, or the vmi
flags, base count and `{typeinfo, offset_flags}` per base: offset for a
non-virtual base, the virtual base offset offset for a virtual one, shifted by
8, with the virtual (1) and public (2) bits). The name field is always the
plain `_ZTS` pointer: there is no non-unique-name bit in the layout (the
`rtti_layout(ctx, c, out, error)` signature takes no such argument; a TU-local
name is handled by `typeinfo_name_string`, whose `tu_local` is the
DECL_ABI_DATA `TU_LOCAL` flag and never derived from the linkage). `can_use_single_inheritance` and
`vmi_class_flags` follow `clang::CodeGenUtils` exactly (the flag walk is the
original pre-order, on an explicit stack).

`dynamic_cast_offset_hint(db, src, dst)` is `CodeGenUtils::computeOffsetHint`,
the `src2dst` argument of `__dynamic_cast` (the offset hint of `cir.dyn_cast`):
-1 when a public path from `dst` down to `src` goes through a virtual base, -2
when `src` is not a public base of `dst`, -3 when it is a public base at
several places, else the offset of the unique public non-virtual `src`
subobject. The paths are the direct base lists' paths with their access, on an
explicit stack. The typeinfo of the types that are not classes (S3) needs no
facts: lib/cirgen builds them from the stream's `TYPEINFO_DEF`.

## The ECIL decoder and the printers' names (lib/format, ecil-dump)

`lib/format/ecil_abi_decode.{h,cpp}` turns the facts the validator kept of an
ECIL stream (`ecil_ids.h`: class facts with the CLASS_* entries, the
`REC_FIELD` fields, the functions and their types) into the tables of this
library, through `a_fact_builder`, once, after the last record:
`build_abi_facts(ids, db, problem)`.
- The classes with a CLASS_INFO start a worklist (a dynamic class whose vtable
  facts were never sent stays out of the initial set: there are no functions
  to check its slots against). Bases, virtual bases and the result classes of
  listed functions, any class (12.2: the library derives covariance from the
  return types), are added as the walk finds them; a dynamic class that enters
  that way has `layout_only`: its vtable is never built or checked, its
  layout gives the result adjustment; those without CLASS_INFO go in through `set_plain_class`
  with their BASE fields (12.2), and only if they never got one: a class whose
  CLASS_INFO and CLASS_BASES come after it was used as a base is decoded from
  those records (`abi_decode_tests`).
- Function names are spelled the way clang's vtable dump spells them: the
  demangler in `abi_demangle.{h,cpp}` (source-names, nested names, structors,
  operators, cv-qualified member functions, builtin, class, pointer and
  reference parameter types; no template arguments or substitutions, which
  fall back to the mangled name) and the result type from the function's
  `TY_FUNC` (builtin types by width and signedness; ABI-identical alternatives
  such as `long` and `long long` cannot be told apart, a reference prints as a
  pointer). `ecil-dump --vtable-layouts` prints the layouts of the classes that
  have vtable facts (the same text as `clang -fdump-vtable-layouts`; the
  `edg-cir-abi-diff` harness found no difference on the S1 test programs).
- The finish-time checks (`validate_deep`, `check_all_slots`) run inside
  `finish()`; the code generator reports a failure as a fatal format error
  ("class facts: ...").

## Output and API (abi_vtable.h, abi_print.h)

- `a_vtable_context::vtable_info(c)`: the vtable group (components with vcall
  and vbase offsets, offset to top, RTTI, function pointers, unused entries;
  address points; thunks by component index), the method indices, the
  virtual-base offset offsets and the thunks per function.
- `construction_vtable_layout(most_derived, offset, is_virtual, layout_class)`
  (`_ZTC`), `vtt_layout(c)` (the VTT order, `SubVTTIndices` and
  `SecondaryVirtualPointerIndices`), `virtual_base_offset_offset`,
  `find_original_method`.
- `print_vtable`, `print_construction_vtable`, `print_vtt`: the format of
  `clang -cc1 -fdump-vtable-layouts` (function names are the facts' names, with
  no result type); `tools/abi_dump FACTS.txt` prints them for a facts file.
- `check_slots(ctx, c)`: the structural check of the front end's slots (prefix
  chain, contiguity, destructor pairs, coverage of `[0, nslots)`, a new slot only
  for a covariant override); `check_slots_against_builder(ctx, c)`: the front
  end's slot of every method against the index the builder's AddMethods gives it.

## Tests

- `test/abi_tests.cpp` (CTest): hand-made classes, a gap in the slots, the text
  round trip, a cycle, the builder (scrambled sparse ids, forward references,
  missing and duplicate records), nine rejected mutations for `validate_deep`,
  the special names, the RTTI layouts, and the deep chains.
- `tools/compare_with_clang.py` (inside cir-env, needs `build/gcc-cir`): the
  front end's IL display of a C++ file becomes the facts (the same facts the
  writer will stream), `abi_dump` prints the layouts, and they are compared with
  clang's dump block by block (`edg-cir-abi-diff`'s canonical form, function
  names reduced to `Owner::name(params)`, template arguments and anonymous
  namespace qualifiers dropped). The dump needs no ECIL.

      python3 cir_src/lib/abi/tools/compare_with_clang.py -v F.cpp ...
      python3 cir_src/lib/abi/tools/compare_with_clang.py --list FILES.txt

- `tools/compare_names_with_clang.py`: the same facts plus the mangled names
  clang's own IR gives (a symbol is matched to a class or function through
  `c++filt` and the facts' `Owner::name(params)` key), then `abi_dump --symbols`
  and `--rtti` against that IR: the VTT defined or not, the set of `_ZTC`
  symbols, the thunk symbols (those the entries need must exist; those clang
  defines must be among the entry thunks or the per-method ones), and every
  `_ZTI` initializer (kind, vptr address point, flags, base list, offset
  words). `test/abi-probes/names/names1.cpp` is a directed case (namespaces,
  templates, an anonymous namespace, covariant and virtual-base thunks).

      python3 cir_src/lib/abi/tools/compare_names_with_clang.py -v F.cpp ...

## Not yet

The decoding layer (ECIL records to the builder's calls; waits for the 1.2
contract), the CIR emission (M4b S1/S2: `#cir.typeinfo`, vtable and VTT
globals from these layouts), and the record layout with `!cir.vptr` and `nvsize` (see `research/m4_cir_shapes.md`,
K7).
