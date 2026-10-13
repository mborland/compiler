# The ECIL writer (what the code does today)

Status: M4 (C and C++, ECIL 1.2; sections 9.16 to 9.19 and 10 are M4). This document describes the
implementation in `src/ecil_*.c`, `src/cir_be.c` and the front-end hooks. The
format is `ecil_format.md`; the design rationale is `design.md`; this file is
what a reviewer should check the code against. File:function names, not line
numbers, are given on purpose.

## 1. Files

| File | Contents |
| --- | --- |
| `ecil_write.h/.c` | Encoders (shortest tag encodings), the frame builder (declaration and body buffers, marks), the string table, the word-interning table, the G-id table, id storage, the stream state machine and header, `ecil_hand_off_region`, `ecil_abort` |
| `ecil_type.c` | Canonical types, records, function types, `DECL_FUNC`/`DECL_VAR` introduction and re-issue, symbol-name binding, the library-builtin list, attribute checks |
| `ecil_const.c` | Constant initializers (`C_*` records, C11 6.7.9 normaliser), string literals, variable definitions, `F_GLOBALS` |
| `ecil_code.c` | Function frames, statements, expressions, calls, builtins, dynamic initializers, the unsupported-construct policy |
| `ecil_sink.c` | Sinks: file or stdout (`write(2)`), in-process library session |
| `cir_be.c/.h` | `back_end()`, the library diagnostic queue, `cir_be_warning` |

Everything is compiled only when `BACK_END_TYPE == BACK_END_TYPE_CIR` (or
`ECIL_COMPILE_FOR_TEST` for the writer-only test harnesses in
`cir_src/test/writer`).

## 2. Entry points and lifecycle

`ecil_stream_open` (from `fe_init.c`, after the option processing) opens the
sink and writes the preamble. `F_HEADER` is sent with the first hand-off. Its triple is
captured earlier: `ecil_note_target_triple` is called from `fe_init.c` right after
`init_predefined_macros`, before any source is read (section 7).
`ecil_hand_off_region(n)` is called from `mem_manage.c`
(`hand_off_memory_region`): for a function region it builds one `F_REGION`
frame per function; for the file-scope region it builds `F_GLOBALS`.
`ecil_stream_finish` (`fe_wrapup.c`) writes `F_END` or, when the compilation
has errors, `F_ABORT`. `back_end()` calls `ecil_stream_finish_back_end`, which
lets the in-process sink run the pipeline. `ecil_stream_close` releases the
sink and all tables (`fe_wrapup_part_2` and `fe_cleanup`). `ecil_abort`
(called from `error.c` after the first error) is idempotent.

State machine: `ess_closed` -> `ess_open` -> `ess_ended` | `ess_aborted` |
`ess_failed`. After an abort or failure every hand-off is a no-op.

### Region lifecycle (the front-end hooks)

* `check_for_done_with_memory_region` (`mem_manage.c`) decides streaming and
  freeing separately. A region is streamed when it is not kept for the
  front end's own use, or when it is kept but every function in it has
  finished body processing and it is not in a secondary translation unit
  (constexpr, local class types, `--ecil_keep_regions`). It is freed unless
  it is kept (keep tests, `--ecil_keep_regions`).
* The end-of-compilation sweep (`check_for_done_with_all_function_memory_regions`)
  streams every function region not yet streamed. A function-state map
  indexed by `function_def_number` in `ecil_code.c` records which ones were;
  its states are streamed, dropped, retracted and delayed, and number 0 is
  never tracked. The sweep frees each region it streams unless
  `--ecil_keep_regions`.
* `free_memory_region` clears `function_def_table[n].scope` for the
  functions of the freed region (CIR only), so no stale scope pointer
  survives; `scope_for_function_def` already returns NULL for a freed region.
* `finish_function_processing_for_function_def` (`scope_stk.c`) re-checks a
  region after a late body finish unless the function is still the
  innermost function scope or its region is in use by it.
* The file-scope region is handed off from `fe_wrapup.c` just before it is
  freed; `F_GLOBALS` must therefore not need anything freed earlier.
* `fe_wrapup.c` frees all regions at the end unless `--ecil_keep_regions`
  (`il_display` keeps them too).

The consequence for diagnostics: after wrap-up the file-scope IL (source
file entries) is gone, so every diagnostic reported afterwards by `cir_be.c`
uses a null source position; the library's messages carry their own file
and line. While the stream is open, queued library diagnostics are reported
only after the API call returns; the F_ABORT submission discards them.

## 3. Ids and storage

* G-ids (types, routines, variables, fields) are dense, in order of first
  use; L-ids are per function frame; sids (strings) are dense in order of
  first use. All are assigned in the declaration section of the frame that
  first needs them, and the id tables remember them for later frames.
* L-ids are stored in the IL entry as base + n. The base is the number of
  L-ids used by earlier functions (`ecil_end_lid_epoch`), so the ids of an
  earlier function read as 0.
* Where the id lives: with `ECIL_ID_IN_PREFIX` (CIR builds) in
  `an_il_entry_prefix.ecil_id` (a `uint32_t` that fits the prefix padding;
  `mem_tables.h` has static assertions and `clear_il_entry_prefix` clears it
  so recycled blocks never keep a stale id). In test builds a pointer-keyed
  side table is used.
* Types are interned structurally through a word-sequence table, so equal
  ECIL types share one G-id however many IL types map to them. Records are
  declared first (`TY_RECORD_DECL`) and their bodies emitted once complete,
  after their field types; incomplete ones are pending and completed in a
  later frame (`ecil_complete_pending_records`).
* Symbol names: external and weak functions and variables are bound by
  final symbol name in the word table, so two IL entities with one symbol
  (`__builtin_memcpy` and `memcpy`, an extern declaration and its
  definition) share one G-id; a differing type is a re-issue.
* Re-issue (spec 8.2): a last-emitted table per G-id; `DECL_FUNC` /
  `DECL_VAR` are re-sent when type, linkage, flags or name changed (for
  example when the definition starts, or at `F_GLOBALS` when a C99 inline
  function ends up inline-only).

## 4. What is translated

Frames: `F_REGION` per function (type and string introductions, `DECL_*`,
`FUNC_BEGIN`, `PARAM`s, body, `FUNC_END`); `F_GLOBALS` with pending record
bodies, definitions of file-scope variables (including tentative ones, with
no `VAR_INIT`) in list order, then function re-issues. Static locals are
defined in the frame of their function (named `<function>.<name>`, made
unique) and are never visited at `F_GLOBALS`. String literals are private
`.str`, `.str.N` globals, deduplicated by content in insertion order.

Filters (nothing is sent for): prototype instantiations, deduction guides,
consteval routines, non-real or binding variables, typedefs, extern-only
variable declarations, unreferenced file-scope statics.

Conversions are the writer's job (spec 8.7/8.8): casts are classified from
the canonical ECIL types, `TO_BOOL` for raw conditions, `bool_to_int` after
`CMP`/`LOG_NOT`/`LAND`/`LOR` with the peephole in condition context,
explicit `LOAD` for lvalue-to-rvalue, compound assignment expanded with
`DUP`. K&R definitions use unpromoted parameters with promoted ABI types.
`break`/`continue` are gotos to flagged labels in the front end's IL and are
sent as `BREAK_STMT`/`CONTINUE_STMT`.

Types: integers, `_Bool`, floats (`FBITS` for x87 `long double`; the
constant is checked to convert exactly), pointers, arrays, records
(bit-fields: byte offset plus bit offset and width; zero-width bit-fields
as unnamed fields of width 0; zero-length arrays and zero-size record
members are `empty_for_abi`), functions (variadic, no-proto).  `_Float16` (GNU
or C23 spelling) is `TY_FLOAT` half and `_Float128` is `TY_FLOAT` quad (not
opaque): declarations
and operations on them are sent as ordinary ones, and the support of the
library for them was not checked (their constants are unsupported by the
writer: `arg_float_value`).  Complex, vector and bit-precise types (and any
other type ECIL has no encoding for) are `TY_OPAQUE`: laid out by size and
alignment, so a record containing one keeps its layout.  Declarations with
opaque parameter or return types are sent; a function definition that has
one, and every value-level use of an opaque scalar (a load, an operation, a
conversion, a call argument or result) is unsupported (spec 8.11).  A record
that has a `_BitInt` bit-field is opaque storage as a whole.  A record whose
body could not be written (an unsupported member) is marked failed: its
uses that need a complete record are unsupported.

### Builtins

* Library rule: a call to `__builtin_X` is an ordinary `CALL` of X for the
  names in `builtin_libc_names` (M2.1). That list is in
  `src/builtin_libc.h`, which `dev_tools/builtins/generate-libc-builtins.py`
  generates from the scraped `builtins_*.txt` data:
  - a name X is included when every scrape that lists both `__builtin_X` and
    X gives them the same signature;
  - names with special semantics, or that are not usable with glibc, are
    excluded by a documented list.

  Only `ecil_type.c` includes the header. The large FE builtin tables are
  untouched (see `research/builtin_table_regen.md`).
* `BUILTIN` records: `expect`, `unreachable`, `trap`, `alloca` (also
  `alloca_with_align`), `bswap16/32/64`, `clz`, `ctz`, `popcount`, `parity`,
  `ffs`, `clrsb` (plain, `l`, `ll`, `g` variants), `clzg`/`ctzg` (with a
  constant result for zero, via a temporary and a `TERN`), `add/sub/mul_overflow`
  and their `_p` forms (a temporary receives the result),
  `is_fp_class` (`isnan`, `isinf`, `isfinite`, `isnormal`, `issignaling`),
  `signbit`, `fpclassify` and `isinf_sign` (`TERN` chains over a
  temporary), the ordered comparisons (`CMP` kinds `lt/le/gt/ge`), `one`
  (`islessgreater`) and `uno` (`isunordered`).
* `__builtin_c23_va_start(ap, ...)` is `VA_START`; `__builtin_object_size`
  and `__builtin_dynamic_object_size` fold to the GCC answer for an unknown
  object; `__builtin_offsetof` with a run-time subscript is the address of
  the member of a null object converted to an integer.
* Everything else is unsupported (`setjmp`/`longjmp`, `apply`, `return`,
  ...), named in the diagnostic.

## 5. The unsupported-construct policy

`ecil_note_unsupported(reason)` records the first reason (the global
`ecil_unsupported_reason`); translation unwinds without touching the
work stacks' invariants. For a function body: the body records are
truncated (the declaration section is kept whole, because the id and string
tables already count what it introduced), `DECL_FUNC` is re-issued with the
`unsupported` flag, and a `cir_warning` names the function and the
construct (for unknown node kinds, the enumerator number: see
`tools/gen_enums.sh`). For a global initializer: `DECL_VAR` is re-issued as
an unsupported declaration. A record left half built when a reason is set
need not be complete. Non-ignorable GNU attributes (constructor, destructor,
alias, ifunc, weakref, cleanup) are unsupported; optimization-only
attributes are ignored.

A declaration whose function type cannot be expressed (for example a
parameter of an unsupported type) is warned about and not sent at all.

## 6. Determinism and non-recursion

Output depends only on the IL and the options: string and G-id order is
first use, tables are insertion-ordered, and nothing iterates over pointer
values. Verified by running every corpus file twice and comparing bytes
(`tools/sweep.sh`, 0 differences over 2564 files).

There is no recursion on IL depth: types, expressions, statements,
initializers and constants run on explicit heap stacks (`ecil_type.c`,
`ecil_code.c`, `ecil_const.c`).

The call-graph check (`cir_src/test/writer/tools/callgraph_check.py`) finds
230 functions and **0 cycles** (M2.1). At M2 there was one cycle:
`emit_leaf` could define a static local (`__func__`, a compound literal)
while building a constant. Since M2.1, such a static local is introduced at
once (`DECL_VAR`). Its initializer is queued and built at the end of the
function or of `F_GLOBALS` (`ecil_queue_variable_definition`,
`ecil_drain_variable_definitions`), so building a constant never builds
another.

In CHECKING builds
`ecil_code_stack_high` records the work-stack high-water mark
(`ECIL_STATS=1` prints it from the writer-only harness).

## 7. Target

The header's target triple has two parts:
- the architecture, from the front end's target flags;
- the operating system, from the target's predefined macros (`__linux__`,
  `__APPLE__`, `_WIN32`).

The whole triple is captured by `ecil_note_target_triple`, which `fe_init.c`
calls (CIR configuration only) right after `init_predefined_macros`, so a
source-level `#undef __linux__` or `#define __APPLE__` cannot change it (golden
`writer-target-undef-os`; GNU mode lets a source undefine a predefined macro).
If the capture did not happen (an error during start-up), the triple is
derived when the header is written, and then only once the file scope exists
(the symbol table cannot be asked before). A command-line `-U__linux__` is
processed before the capture, on purpose: it changes the target identity, and
the library rejects the resulting `unknown` triple.

The sizes of `int`, `double` and pointers, and the integer promotion width,
come from the FE target.

`CHEAP_UNALIGNED_BF` is set for x86 and AArch64.

The in-process sink requires x86-64.

The library accepts x86-64 Linux only; other
front-end targets (the Mac's aarch64) produce valid streams that are only
useful for the writer-only loop, and their goldens are not authoritative.

## 8. Testing

`cir_src/test/writer/` has the unit harness (stubs, goldens), the
integration driver (the real front end with a test back end), probes and
`tools/` (decoder, `stackcheck.py` value-stack checker, corpus sweep).
`tests/tests/cir/writer/` has the regression tests (`//script:
edg-cir-ecil-dump`, recorded on the x86-64 box).

CHECKING builds verify id introduction (spec 7): G-ids and sids at `F_END`,
L-ids at `FUNC_END`. For the first 160 G-ids (or all of them with
`ECIL_GID_STRESS`) they also move the G-id table on every new G-id, so ASan
catches stale `gids[]` pointers.

The tools in `cir_src/test/writer/tools/` include:
- `callgraph_check.py`;
- `perf_ir.sh`, `cg_prof.py` and `cg_sum.py` (callgrind Ir);
- `gen_perf_tus.py`;
- `asm_cmp.sh`.

## 9. C++ (M3)

This section covers the C++ additions of M3. Sections 1–8 describe the C
writer, which is unchanged for C: C streams are byte-identical to M2.1 apart from
the `bswap128` mapping, the `constructor`-attribute error and full-width 128-bit
constants.

### 9.1 Files

| File | Contents |
| --- | --- |
| `ecil_type.c` | Also: C++ names and linkage (`ecil_cxx_variable_facts`, `compute_func_state`), constructor/destructor variants (C1/C2, D1/D2, `STRUCTOR`), the Itanium data size (`ecil_data_size_of_type`), static invokers' queue, retractions, late-name resolution (`ecil_resolve_late_names`), `ecil_emit_dropped_decl_var`, `ecil_check_variable_use` |
| `ecil_const.c` | Also: static-initialization decisions (`prepare_variable_definition`, `begin_variable_definition`: GUARDED, template-instance and inline-variable flags), the body section of `F_GLOBALS` with one `STATIC_INIT` per dynamically initialized variable, 128-bit constants (`ecil_arg_integer_constant`) |
| `ecil_code.c` | Also: classes (constructors with `MEMINIT`, destructors with cleanups, `RETURN_SLOT`), `new`/`delete`, lambdas and static invokers, C++ statements and expressions |
| `lower_name.c/.h` | CIR-only entry points of the mangler: `cir_structor_name`, `cir_local_static_name`, `cir_entity_is_tu_local` (with `cir_tu_local_names_are_final`) |
| `scope_stk.c` | CIR-only `cir_name_may_change` (names that are not final yet), and the region hooks of section 2 |
| `cir_be.c/.h` | `back_end()`, the library diagnostic queue, `cir_be_warning`, `cir_be_error` |

Other front-end files are unchanged. Everything else is C++-gated: C streams
are byte-identical to M2.1 (checked by `mac_cmp.sh` over the C corpus, apart
from the `bswap128` builtin mapping, the `constructor` attribute error and the
128-bit constants).

### 9.2 Region lifecycle additions

* **Region keeps (CIR only), in `memory_region_should_be_kept_for_routine`
  (`mem_manage.c`).** In the cpfe-cir configuration `MAINTAIN_NEEDED_FLAGS` is 0, so the
  front end's usual rule for regions that look unneeded does not apply, and
  the writer needs three more keeps. They are all still streamed when their
  functions are finished; they are only not freed until the end of the
  compilation:
  1. a lambda call operator (the constant evaluator calls it later and needs its
     scope);
  2. a prototype instantiation (later instantiations read what the file scope
     holds about it: `find_binding_for_pack` reads the variable of a
     structured binding pack; found by AddressSanitizer on
     `edg/cmeerw/gnats-27775-run`). The writer does not stream it
     (`ecil_routine_is_filtered`);
  3. a region with `expr_node_refs` (a file-scope template argument refers to
     an expression in it: `find_local_expr_node`; `torture/pr69355`).
  To be measured: the RSS cost of keeps 2 and 3 on the C++ perf TU
  (cir-config).
* **AddressSanitizer poisoning.** `free_mem_block` poisons the storage of a freed
  block and `alloc_mem_block` unpoisons it when the block is handed out
  again, under `__SANITIZE_ADDRESS__` only (`CIR_ASAN_REGIONS`). The blocks are
  pooled, so without this a use of a freed region is silent. The C corpus, the
  C++ goldens, the probes and the C++ corpus sweep run clean.
* **The sweep and the file-scope hand-off.** The late function regions
  (including those freed in the sweep) are streamed before the file-scope
  region; the instantiations that are wanted late (static invokers of lambdas,
  see section 9.10) go into one more `F_REGION` frame after `F_GLOBALS`.

### 9.3 Ids and storage additions

* A G-id for a constructor or destructor has a partner: the complete variant
  (C1/D1) and the base variant (C2/D2) are declared together with `STRUCTOR` and
  have the same linkage and flags; the body is streamed under the base
  variant. An array of objects and a `new` expression use the complete variant.
* An L-id space is fresh for every `STATIC_INIT` body in `F_GLOBALS`
  (`ecil_check_begin_lids` in CHECKING builds).
* The gid table remembers the data size that was sent with a record body
  (`data_size`), which later records and the copy code use.

### 9.4 C++ names and linkage

* Names are the Itanium names from the front end's mangler: ordinary entities
  through `mangled_function_name` / `get_mangled_variable_name`, constructor and
  destructor variants through `cir_structor_name` (even for the closure types'
  special members that have no source name), static locals through
  `cir_local_static_name` (`_ZZ1fvE1a`, with the discriminator).
* **Late names.** `cir_name_may_change` says an entity's name is not final yet
  (an unnamed class that may get a name for linkage, an anonymous union, a
  closure of a structured binding, a lambda in a default argument whose parent
  routine is not recorded). Such an entity is sent with a nil name and
  `ecil_resolve_late_names` binds the names (`NAME_BIND`, `F_NAMES`) when the
  file scope is complete. A function whose name was sent may not change
  (CHECKING asserts it, except the multiversioned ones, which are unsupported).
  Namespace-scope closures are exempt: a closure's name is its own.
* **Linkage.** Internal linkage for everything TU-local (see below), also
  though the language says external; `linkonce_odr` with a COMDAT group for what
  may exist in several translation units (inline functions and variables,
  template functions and static data members, member functions defined in a
  class, the statics of such a function); `weak_odr` for an explicit
  instantiation definition (functions, static data members, variable
  templates); explicit specializations are external and always sent; a dropped
  definition is re-issued external with no COMDAT, GUARDED or definition flag
  (`ecil_emit_dropped_decl_var`; spec 11.2).
* **TU-local entities.** `cir_entity_is_tu_local` walks, with a heap work list and
  a hash set, an entity and what it is made of: internal linkage, unnamed
  namespace, unnamed class without a linkage name, local classes and lambdas of
  such entities, template arguments (types, template template arguments,
  addresses of non-type arguments, and members of C++20 class-type arguments),
  parameter and return types, members of such classes. Its answers are
  remembered only after the names are final (`cir_tu_local_names_are_final`,
  called from `ecil_resolve_late_names`), because a lambda whose parent routine is
  not recorded yet is first treated as unnamed. Test: the multi-TU probes
  `tu_local*` (same names in two TUs).
* The object of an anonymous union at namespace scope is an internal symbol named
  `anonymous`; a nameless static local (`__func__`, `__PRETTY_FUNCTION__`) is
  `<function>.var`; both are unique and keep their name across re-issues.

### 9.5 Records

* Layout: size, alignment and field offsets are the FE's, whatever macros the
  configuration has (so `TARG_ZERO_WIDTH_BIT_FIELD_AFFECTS_STRUCT_ALIGNMENT` and
  the like need nothing in the writer). The data size sent is the Itanium dsize
  computed by `ecil_data_size_of_type`: the size for a POD class, else the end of
  the last non-empty subobject (the vptr, a direct non-virtual base by its own
  data size, a data member by its size, or by its data size for
  `[[no_unique_address]]`). The front end's `data_size_of_type` is not used: it
  gives a short value for a member of a POD class type.
* Fields: the vptr (`TY_VPTR`, unnamed, at offset 0, only for a class without a
  primary base: 9.16), then the **direct** non-virtual bases (the
  list is followed through `next_direct`; the front end's base list also has
  indirect bases), then the data members, then the virtual bases (direct and
  indirect: `VIRTUAL_BASE` fields, 9.17). A class that cannot be written is a
  failed record: its body is not sent and its uses are
  unsupported (a use of the object of such a class, including a global's
  address in a constant, drops the function or definition:
  `ecil_check_variable_use`).
* A member of an anonymous union or struct is selected through the unnamed
  member that holds it, as a chain of `FIELD`/`EXTRACT` records
  (`anonymous_member_path`). A constructor initializer of such a member is
  unsupported.
* Empty classes: copying one reads and writes nothing (`INIT_EMPTY`); the
  value-initialization of an empty base is not sent (its byte belongs to a
  neighbour).
* **Completion order.** While the types in progress are built, the pointee of
  a pointer is only declared (`need_pointee`, `decl_only` work items). Its body
  follows when the outermost `ecil_type_id` returns (`num_deferred_records`,
  `ecil_complete_pending_records`). A by-value member's record body is
  therefore sent before its container's, even in CRTP shapes: `E<B>` has a
  `W<B>` member, `W<B>` has a `B *`, and `B` derives from `E<B>`.
* **Function types** need their return, `this` and parameter records only
  declared (`need_pointee`), never with a body. A record already on the type
  stack is never started again (`type_is_in_progress`). Without both rules, a
  record with a pointer to a function that takes or returns the record by
  value made the walker push the record's fields forever
  (`struct S { S (*clone)(); }`, `std::function<Node()>` in `Node`, and in C
  `struct S { struct S (*f)(void); }`). Goldens:
  `writer-cxx-recursive-function-types`, `writer-struct-function-pointer`.
* Object sizes in the G-id table are 64 bits (`an_ecil_gid_info.size`).

### 9.6 Constructors, destructors, lifetimes and cleanups

* A constructor body is `MEMINIT` per constructor-initializer entry, in the
  front end's order, then the body. A member or base copy with an implied source
  is a call with the source's member as second operand. Value-initialization
  zeroes first where the front end does
  (`need_zeroing_for_value_initialization`): records and arrays with
  `ZERO_BYTES` of their whole size, a base only with its data size (a type with
  a pointer to a data member uses `INIT_ZERO` / `INIT_ZERO_BASE`: 9.18).
* A destructor starts with `CLEANUP_BEGIN(all)` for each member and base with a
  non-trivial destructor, in construction order (so they run in reverse after
  the body, also on an early return).
* `CLEANUP_BEGIN(kind, g|nil, flags)` ... `CLEANUP_BODY`: `all` (every exit),
  `exit` with the object's G-id (static storage duration: run at exit), `eh`
  (exception paths only; live since S4, 9.19).
* Temporaries: `FULLEXPR_BEGIN`/`END` around an expression whose lifetime has
  temporaries to destroy; a class prvalue is materialized and its cleanup begun
  as soon as it is complete. A temporary whose lifetime is extended (bound to a
  reference of a declaration) has its cleanup begun at the end of the declaring
  statement (`add_pending_cleanup`, `flush_pending_cleanups`; arrays use
  `ARRAY_DTOR`). A conditionally extended temporary is unsupported.
* Locals: `LOCAL` at the declaration, the cleanup right after the initialization.
  This includes the NRVO variable (`LOCAL` with `RETURN_SLOT`): the consumer
  guards its cleanup with an nrvo flag that `RETURN_FROM_SLOT` sets (spec 11.6).
* Returns of a class use `RETURN_SLOT` ... `RETURN_FROM_SLOT` when it is not
  trivially copyable or when the returned expression has temporaries.
* `if constexpr` (`stmk_constexpr_if`) streams the branch the FE selected
  (`value`), as the C-generating back end does; the other branch does not
  exist. An unresolved one, or one with an init-statement, is unsupported.
  Probe: `cxx_constexpr_if`.
* A condition that declares a variable (`if (A x = ...)`), a range-for and
  `switch` with an init statement have their variable scoped over the statement;
  a `for` with a condition declaration and an increment is unsupported.
* **`[[no_unique_address]]` members.** A constructor initializer, default
  member initializer or zeroing before a constructor call writes nothing for an
  empty member (`is_optimized_empty_class`: its byte belongs to another
  member), and zeroes only the data size of a non-empty one. A constant
  initialization of such a member is unsupported. Golden:
  `writer-cxx-no-unique-address`.

  Aggregate initialization (`do_agg`, `agg_nua_member`) follows the same
  rule:
  - an entry for an empty member writes nothing (no `AGG_SEL`,
    `INIT_CONST` or zeroing; a constructor entry still calls the
    constructor);
  - a constructor or `dik_zero` entry of a non-empty member zeroes only its
    data size;
  - any other entry is unsupported.

  This covers locals, dynamically initialized globals and static locals
  (probe `cxx_nua_aggregate`, golden `writer-cxx-nua-aggregate`). The library
  also ignores writes through an omitted empty field, as a second line of
  defence.
* **Aggregate entry numbering.** The entries of an aggregate initializer are
  numbered like the record's `REC_FIELD` records, bases first
  (`ecil_gid_of(field) - 1`). The init-level cursor
  (`an_init_level.lead`, the index of the first data member),
  `ecil_member_type_gid` and `agg_nua_member` use the same numbering:
  - an entry before `lead` is a base;
  - a later entry is data member `index - lead`.

  Probe: `cxx_nua_base_aggregate`. Golden: `writer-cxx-nua-base-aggregate`.
* **Trivial delegation.** A constructor that delegates to a trivial default
  constructor (`test() = default;`, value-initialization) zeroes the data
  size of the object (`ZERO_BYTES`).
* **Static member through an object.** When a static member is selected
  through an object expression with side effects (`f->foo()` with an
  overloaded `operator->`), the object expression is evaluated and its value
  dropped; the call is then indirect.
* **Implicit assignment of an array of class objects.** The FE builds the
  element loop around a temporary index that no declaration statement
  introduces, so the writer declares it (`LOCAL`) before the statement
  `tmp = 0;` that starts the loop.
* **Range-based for.** `stmk_range_based_for` (the unlowered form) is sent
  as:
  1. `BLOCK_BEGIN`;
  2. the init-statement declarations;
  3. `LOCAL` and initialization of `__range`, `__begin` and `__end`, each
     through its own var-init item (cleanups and extended temporaries as for
     any local);
  4. `FOR_BEGIN`, `FOR_COND(__begin != __end)`, `FOR_STEP(++__begin)`,
     `FOR_BODY`;
  5. `BLOCK_BEGIN`, the loop variable (with the bindings of a structured
     binding) and its initialization, the body, `BLOCK_END`;
  6. `FOR_END`, `BLOCK_END`.

  The temporaries of the range initializer live to the end of the outer
  block. The FE's lifetimes for C++23 P2718R0 are in the IL.

  With a tuple-like structured binding, the hidden object and the binding
  references are declared in the body block, and their initializations come
  before the body (probe `cxx_range_for_tuple`, golden
  `writer-cxx-range-for-tuple`). `std::map` itself does not compile yet,
  because libstdc++'s `_Rb_tree` members use constructs that are not
  supported (section 9.12).

### 9.7 Static storage duration

* A variable with a dynamic initializer or a destructor has a `STATIC_INIT`
  body in the body section of `F_GLOBALS`, in the front end's order
  (`dynamic_inits`), with its own L-id space; the `ECIL_DECL_VAR_GUARDED` flag
  for a static local and for a variable with vague linkage; the flags
  `inline variable` and `template instance` select the ordering rules (spec
  11.7). A constant initializer with a destructor sends both.
* `ECIL_DECL_VAR_CONST` is sent only when the object is really constant: not
  for a const object with a dynamic initializer or destructor, nor for one with a
  mutable member.
* A temporary bound to a reference of static storage duration is a static
  object `_ZGR...` made when needed (`make_static_temp`), including for the
  reference of a static local; its destructor is an `exit` cleanup.
* `thread_local` with dynamic initialization at namespace or class scope is
  unsupported; a function-local one is passed through (the library handles the
  guard).
* A variable whose initializer cannot be translated is re-issued as an
  unsupported declaration. If it has the `used` attribute (or is otherwise a
  root), this is an error, not a warning (section 9.12).
* **Arrays of class objects** at namespace scope with a constructor (including
  multi-dimensional arrays and template instances) become a `STATIC_INIT` with
  `ARRAY_CTOR`, and an exit cleanup with `ARRAY_DTOR`. The FE may give the
  array as a constant aggregate of repeated constructor calls; only
  `variant.constant.non_constant` shows that it is not a constant initializer.
* **Class conditionals.** A class conditional that initializes a static builds
  its value in the static: the arms' destination is the static's `GREF`, not a
  local.
* A nested aggregate element (a `dik_nonconstant_aggregate` entry) of a
  static array of class aggregates is initialized as a nested dynamic
  aggregate.

### 9.8 `new` and `delete`

* `new T` is the allocation call (the front end's routine, or the class's
  own when the front end folded the allocation into the constructor:
  `new_routine_of`, needed in the release configuration where
  `NEW_CAN_BE_FOLDED_INTO_CTOR` is 1), the constructor call or initializer on the
  pointer, and `DELETE(operator delete, destructor, type, flags)` for `delete`
  (sized and aligned forms from the routine chosen).
* `new T[n]`: `NEW_ARRAY_SIZE(elem, cookie)` and `NEW_ARRAY_COOKIE`; the count is
  in a local. For `new T[n][K]` the front end keeps only the first bound, so the
  count of elements is `n * K`, saturated to `SIZE_MAX` when it overflows (the
  allocation function then fails). The pointer is converted to the element
  pointer for the cookie and `ARRAY_CTOR`, and to the result type (a pointer to
  an array) for the value; `delete[]` converts back. A new[] that value-initializes
  a class that needs zeroing is zeroed first (constant counts only).
* A constant count (the product of the bounds) of 2^32 elements or more is
  unsupported. Before this rule, the truncated count gave `operator new[]` a
  size that was too small. Golden: `writer-cxx-new-huge-count`.
* Not supported: an initializer of a new[] with a run-time count other than a
  list of elements (9.19), a destroying delete, a virtual destructor through
  `::delete`.

### 9.9 Value categories and evaluation order (C++)

* `++x`, `--x` are lvalues (`DUP`/`DROP` around `INC_DEC` when the lvalue is
  used); assignments store with the `glv` result when used as an lvalue;
  `eok_ref_cast`/`eok_lvalue_cast` are an address conversion; an
  `eok_lvalue_adjust` of a const object loads it.
* A reference to a function converted to a pointer is its pointer value.
* **C++17 order of assignments.** The rules apply only where the FE sets
  `eval_right_to_left` (C++17 and later). C and earlier C++ keep the
  left-to-right shape, where the order is unsequenced anyway.
  * `a = b`. The target is designated first only if:
    - it is a variable or a member of one (`designation_reads_no_memory`); or
    - neither operand has side effects (`expression_is_pure`).

    Pure means variables, constants, conversions, member selections,
    indirections, subscripts and arithmetic. A call, an assignment or an
    increment has side effects. In every other case the value goes into a
    temporary first, so `p[i] = i++` stores into `p[i]` with `i` already
    incremented.
  * `a op= b`, including pointer `+=`/`-=`. The expansion
    `<target> DUP LOAD <value>` reads the target before the value, so the
    target goes first only if both operands are pure. Otherwise the value goes
    into a temporary first (`a[i++] += i`, `b |= f(b)`).
  * **Class assignment (built-in).** The right operand of a class is bound to
    the parameter of the (trivial) assignment and read when the assignment is
    made, after the target is designated. When the order could be observed, what
    is kept in a pointer `LOCAL` is therefore the *address* of the right operand:
    the object itself (the glvalue under its load), or the temporary of the full
    expression that holds a prvalue, which keeps its normal cleanup (a class with
    a destructor, `NONTRIVIAL_ABI`, works the same way). Then come the target,
    `LREF tmp`, `LOAD`, `DEREF`, `LOAD` and `STORE` (with `DSIZE` as before).
    A call whose result is a trivially copyable class value is a fixed value and
    is kept as it is; a cv-adjusted explicit glvalue is kept by address
    (`CIF_GLV`). Any other right operand (a conditional, a comma expression, the
    result of an assignment) is diagnosed: neither its address can be kept nor
    its value fixed (a copy at that point would miscompile
    `*getp() = c ? a : b`). The suppressed-load state is a single node, not a
    stack: it is reset at the start and the end of every code stack run, and a
    class assignment nested in the right operand of another is diagnosed
    (`writer-cxx-class-assign-rhs-unsupported`; probe
    `cxx_class_assign_rhs_shapes`). `p->pt = make_pt()` works. g++ stores `*p = next(&p)` through the old `p`; clang and the writer
    use the new one, as C++17 requires (golden `writer-cxx-class-assign-order`);
    `*getp() = obj` where `getp` changes `obj` agrees with g++ and clang (golden
    and probe `writer-cxx-class-assign-address`, `cxx_class_assign_address`).
  * **Overloaded calls.** An overloaded assignment or compound assignment, and
    a reversed rewritten comparison, are calls with `eval_right_to_left`. With
    two operands, the call is emitted left to right when
    `target_may_be_evaluated_first` holds: the first operand designates a
    variable or a member of one, or both operands are pure. Otherwise the
    second operand is evaluated first:
    1. `LOCAL tmp`, `LREF tmp`;
    2. the operand, as the call would pass it: the value, or the address for
       a reference parameter, so a materialized prvalue keeps its lifetime;
    3. `STORE(INIT)`;
    4. the first operand (the object);
    5. `LREF tmp`, `LOAD`;
    6. the call.

    `designation_reads_no_memory` requires a glvalue: the value of a pointer
    variable, as in `p->operator=(f())`, is a memory read.

    These cases are unsupported:
    - a second operand that is a class passed by value and not trivially
      copyable;
    - more than two operands where any has side effects;
    - an indirect callee;
    - a local id of 2^24 or more (the temporary's id is packed above bit 8).

    Probe: `cxx_call_assign_order` (`v.at(i++) = i` with `operator=(int)` and
    `operator=(const int &)`, `v[idx()] = f()`, a reversed comparison of two
    calls; C++20). Golden: `writer-cxx-call-assign-order`.
  * **Known difference.** g++ evaluates `a[i++] += i` left to right, a GCC bug
    for compound assignments. The writer follows the standard, as clang
    does. Golden: `writer-cxx-assign-order`.
* `__builtin_is_constant_evaluated()` is false in code that runs; the vacuous
  destructor call (`x.~T()` of a scalar) evaluates its object; the discarded
  class conditional is unsupported.
* `__builtin_addressof(x)` is the address of the object (`ADDR`).
* **Structured bindings.** A binding that is not a reference variable
  (`init_kind == initk_binding`: a member of the hidden object or an element
  of an array) has no ECIL object. Each use is the expression it names,
  visited in place; a copy is converted to a prvalue when the use is a value.
  The bindings of a tuple-like binding are reference variables, declared with
  the hidden variable (they are on its `bindings` list, not on the
  declaration statement).

### 9.10 Lambdas

* A closure is an ordinary class. The capture of a class by copy is a call to
  its copy constructor with the captured variable (`[*this]`: the object the
  pointer designates) as source. The call operator is an ordinary member function.
* The conversion to a function pointer has a static invoker (the front end
  leaves the entry-point routine without a body): `ecil_queue_invoker` queues it
  and `ecil_drain_invokers` synthesizes a body that calls the call operator with a
  null object pointer, in a `F_REGION` frame after `F_GLOBALS` for the ones still
  queued. A variadic lambda's invoker is unsupported.
* Lambdas in default arguments, in initializers of variables and in templates
  get names through `cir_name_may_change` / late names; their TU-locality follows
  the parent.
* **Captures.**
  - A structured binding captured by copy is the address of the object it
    names.
  - A trivially copyable object (for example a closure) captured by copy is
    loaded and stored (`dik_bitwise_copy` with an implied source).
  - A reference captured by copy (`[r]` with `int &r`) copies the referent:
    the reference is loaded, dereferenced and the value loaded (golden
    `writer-cxx-capture-ref`).
  - Not yet supported: a capture of a variable that the enclosing closure
    captured (`source_closure_field`), and a capture of an array of arrays of
    objects.

### 9.11 Integers wider than 64 bits

128-bit constants are sent as u128/i128 operands with all their bits, from the
front end's part array (`integer_value_low_128`) or host integer, also in
`CASE_VALUE`/`CASE_RANGE` and in the operands of the count builtins.
`__builtin_bswap128` is the `bswap` builtin.

### 9.12 Unsupported constructs and diagnostics

Non-ignorable GNU attributes (constructor, destructor, alias, ifunc, weakref,
cleanup, multiversioning by `target`) are unsupported; optimization-only
attributes are ignored.

**Warnings and errors.** A dropped function or variable is a `cir_warning`,
except when it is a root: a function with the `constructor`, `destructor` or
`used` attribute, or a variable with `used`. Nothing refers to such an entity, so
the library's rule that erases a dropped definition nobody references would
lose it silently. Dropping a root is a `cir_error` (`cir_be_error`), and the
translation unit fails (the stream is aborted), in C and in C++. Tests:
`writer-ctor-attribute-negative.sft.c`, `writer-cxx-ctor-attribute-negative.sft.cpp`.

**Unsupported in C++ (M3, as of M4 slice S1)**, each a named reason in the
warning: coroutines; `typeid` of a complex type, a `.*` that yields a function address (a g++
extension); a `for` with a condition declaration
and an increment; a conditionally lifetime-extended temporary; a non-trivial class
through `...`; a constructor initializer of an anonymous-union member; a repeated
initializer of a multi-dimensional array; the copy of an array of objects;
`thread_local` with dynamic initialization at namespace scope;
`new` with a non-throwing allocation function; a destroying `operator delete`;
`__attribute__((constructor))` and friends, GNU multiversioning; a function whose
symbol name is that of a constructor or destructor variant of another routine,
or of a defined function of another type (a symbol clash: `writer-cxx-symbol-clash`, and `-negative` for
the clashing definition that cannot be sent, which is a `cir_error`); under
`--cfront_2.1`, a case or goto label after a declaration of an object with a
destructor (the front end accepts a jump past it there, which would run the
cleanup of an unconstructed object);
a local aggregate with a pointer to a member function that needs a `this`
adjustment (9.18); an address constant that names a local variable;
a constant `new[]` count of 2^32 or more; a constant initialization of a
`[[no_unique_address]]` member; structured bindings in conditions; variable
length arrays.

**Expected differences from the gcc-built reference** (documented, not bugs):
falling off the end of a non-void C++ function is `trap` + `unreachable` as clang
does (`parse/crash78a`, `rma/mri_bug03`); a variable initialized from another
that is initialized dynamically before it is dynamic, as in clang
(`cpp0x/constexpr-static4` is the exception that is split: see 9.15); in GNU mode the front end does not extend the lifetime
of `const int &r = A().i` the way default mode does (`init/ref19`).
**Kind names.** Unsupported kinds are named in diagnostics, for example
`the expression kind "GNU statement expression"`, `the statement "inline
assembly"`, `the operator "three-way comparison (<=>)"` and
`the constant "pointer-to-member constant"`.
- The names come from `src/ecil_kind_names.h`, which
  `cir_src/test/writer/tools/gen_kind_names.py` generates from `il_def.h`.
- Only enumerators that exist in every configuration are named; the others
  keep their number.
- Regenerate the header when an enumerator is added to one of the five enums.

**`constructor` and `destructor` attributes**, with or without a priority,
always produce the `cir_error`. This was checked on `init/attrib1` on the Mac,
where the FE configuration itself rejects a priority argument.
**Error status.** After an error the mangler gives every type the encoding
`1?`. The CHECKING re-mangle comparison of `ecil_resolve_late_names` is
therefore skipped once `is_at_least_one_error()`. The compilation ends with
status 2 and an `F_ABORT` stream.

**More unsupported and documented cases (M3):**
- `init_priority` (a `cir_error` for a variable);
- a temporary that a mem-initializer binds to a reference member (a `MEMINIT`
  ends its locals, spec 11.5);
- a class conditional used as an operand rather than an initializer
  (`b = (c ? f() : b)`);
- a repeated default argument in an array initialization (`B b[3]` with
  `B(const A & = A())`): the arguments would be built for each element.

A gnu_inline function in GNU C++ mode is always inline-only: the IL does not
record whether its definition is `extern`, so `inline` without `extern` is
treated like `extern inline`, where gcc would emit the function.

### 9.13 Determinism and non-recursion (M3)

The call-graph check finds 480 functions and 0 cycles (the release build 333).
`cir_entity_is_tu_local`, the anonymous-member path, the constant scan of a template
argument and the work-stack walkers all use heap stacks or bounded loops; the only
calls that scale are on a function's nesting of statements, which are on the
work stack. The `STATIC_INIT` bodies, `new` and the lambda invokers add work-item
kinds (`cik_new`, `cik_meminit`, `cik_ctor_call`, ...) to the same stack.

### 9.14 Testing (M3)

* Goldens: the C++ ones (`writer-cxx-*`) and the C ones in
  `tests/tests/cir/writer/`, all recorded on the x86-64 box. Since the M3 gate,
  cpfe-cir accepts C++ by default and `--ecil_allow_cxx` no longer exists.
* Run probes against g++: `cir_src/test/writer/probes/cxx_*.cpp` through
  `tools/cmp_cxx_box.sh` (set `CMP_FE` to try another front end build, for example the
  release one), and the multi-TU ones in `probes/multi` through `cmp_cxx_multi_box.sh`.
  Probes for each fix are named after what they check (`cxx_new_multidim`,
  `cxx_lambda_class_capture`, `cxx_class_operator_new`, `cxx_zero_init_padding`, ...).
* CHECKING aids: `ECIL_GID_STRESS` (also moves the gid table at every new id);
  `ecil_check_begin_lids` / `ecil_check_function_lids`; the late-name check; asserting
  that the name of a function that was sent has not changed; ASan region poisoning
  (section 9.2).
* Corpus sweep tooling (not part of the tree's tests): a script that runs every
  `imported/gnu/cpp` rp/lp test through `eccp` with the same options as the corpus
  run, runs the program, and buckets the result (diagnosed, library error, front-end
  diagnostic, crash, run failure); with a reference column from the checkpoint
  json.
* The debug and the release front end must be built with the same macro
  configuration to compare streams: `linux-gcc-release` differs from `linux-gcc` in
  `PRAGMA_WEAK_ALLOWED` (a `#pragma weak` is not recognized, so a weak declaration is
  external), `NEW_CAN_BE_FOLDED_INTO_CTOR` (handled, above),
  `ASSIGNMENT_TO_THIS_ALLOWED`, `TARG_ZERO_WIDTH_BIT_FIELD_AFFECTS_STRUCT_ALIGNMENT`
  and `USE_INLINE_EXPANSION`. The streams of all goldens and probes are identical
  for both configurations apart from the weak pragma, and so are the run results of the
  C++ corpus sweep.

### 9.15 ECIL 1.1 and the M3.1 changes

* **`unsupported_reason` supp key (ECIL 1.1).** Every re-issue of an
  unsupported `DECL_FUNC` or `DECL_VAR` is followed by a supp blob with key 13
  and the reason that the warning uses (at most 255 bytes, cut at a UTF-8
  boundary; bytes that are not valid UTF-8, in a source identifier, are replaced by
  `?` so that an optional string cannot make a stream invalid). The reason is
  remembered per G-id when the definition is dropped (`ecil_remember_drop_reason`)
  and is only ever attached to that G-id (both variants of a constructor or
  destructor); the frame has `HAS_SUPP`, and the preamble has the `SUPP` feature
  bit on every 1.1 stream. The diagnostics are cut at `ECIL_REASON_MAX - 1` bytes,
  not at 100 characters (golden `writer-cxx-long-reason`, `writer-cxx-supp-reason`).
* **Pending records.** `ecil_complete_pending_records` rescans only when a class
  was completed since the last scan (the hook `ecil_note_class_completed`, called by
  `complete_class_definition` after `do_class_layout`) or when a complete record
  is pending only for the order. CHECKING builds verify that nothing complete is
  pending when the scan is skipped.
* **Built-in `<=>`.** `CMP3WAY(t, lt, eq, gt, unordered|nil)` pops the converted
  operands and stores its integer result into the member of a temporary of the
  comparison category (a record with one integer member), which is read back
  as the record value. The values are read from the constants that initialize the
  category's static members (`initialize_ordering_constants`), not assumed. A
  FE-synthesized local that has an initialization and no declaration statement
  (the result variable of a defaulted `operator<=>`) is declared where it is
  initialized. Golden and probe `writer-cxx-spaceship`, `cxx_spaceship`.
* **Constant `if`.** An `if` whose condition the front end folded to an integer
  constant sends only the arm that is taken, unless the other arm contains a label
  (a `goto` or a `case` can reach it) or a statement whose structure the search
  does not know (a coroutine statement); `if constexpr` is
  unchanged. Goldens `writer-constant-if`, `writer-cxx-constant-if`; probe
  `cxx_constant_if`.
* **Partial static initialization.** A partly constant aggregate initializer of
  a variable of static storage duration (not thread_local, not vague-linkage, a
  function-local static included unless its function may exist in several
  translation units: inline functions and template instances, whose statics are
  COMDAT, get zero data and the full guarded initialization, as clang does) is
  `VAR_INIT` with the constant elements (zero for the dynamic ones) plus a
  `STATIC_INIT` with the `PARTIAL` flag that stores only the dynamic elements,
  without zero-fill (`cpp0x/constexpr-static4`; golden and probe
  `writer-cxx-partial-static-init`).
  - **Skipping is a property of the level** (`an_init_level.skip_constants`): the
    variable's own aggregate and the nested aggregates of its constant that have a
    dynamic part skip the constant elements; every other aggregate that the same
    `STATIC_INIT` builds (the aggregate of a dynamic element, a temporary in a
    call argument, a `new`, a closure) is initialized completely and keeps its
    `ZERO_FILL` flag. One predicate (`entry_has_dynamic_part`: a dynamic element, a
    repeat of one, an aggregate with a dynamic part, merged sources with one)
    decides what is skipped; a repeat of an aggregate with a dynamic part cannot be
    built element by element and is diagnosed. Goldens `writer-cxx-partial-static-
    levels`, `-repeat`; probe `cxx_partial_static_levels`.
* **Nested lambdas.** A capture of a variable that the enclosing closure captured
  is read from the member of the enclosing closure through the enclosing lambda's
  `this` (`captured_source_field_of`; golden and probe `nested-capture`).
* **Inheriting constructors.** The inherited constructor is called with the
  original arguments: the front end's copy of a by-value parameter of a class that
  is not trivially copyable is replaced by the parameter itself
  (`forwarded_parameter`; g++ and clang agree; probe `cxx_nontrivial_abi_forwarding`).
* **Linkage of members of nested unnamed classes.** A member function of an
  unnamed class declared inside a class has the Itanium name with `Ut_` and the
  linkage of the enclosing class (vague for an inline one), so do its local statics;
  an unnamed class at namespace scope or in a function stays internal. The
  names of such members are final once the enclosing class is complete
  (`cir_name_may_change`). A function instance whose template arguments or type
  hold a lambda in a default argument that has no parent yet is named late
  (`cir_routine_name_may_change`).
* **`--old_for_init`.** The whole init of a for (declaration, initialization,
  cleanup) is sent before `FOR_BEGIN`, in the enclosing scope region, when the
  loop's IL has no for-init scope (`for_init_scope == NULL`), so the cleanup ends with
  the enclosing block (unbraced bodies of `case` and `if` are scope regions of their
  own).
* **Data size.** A zero-width bit-field ends the data size at its offset
  (`struct S { char c; int : 0; }` has dsize 4); a `[[no_unique_address]]` member
  declared through an alias counts as its data size.
* **Hardening (M3.1).**
  - The walk that decides whether an entity is local to the translation unit
    queues each class once per walk (a type of nested template arguments that share
    parts is a tree of exponential size: `gnats-26324`).
  - A `#pragma redefine_extname` is applied at the end of the translation unit: the
    DECL_FUNC of a function that was sent before is re-issued with the asm name
    (`ecil_resolve_late_names`; `pragma-re-2`).
  - A function that is declared constexpr and found not to be one keeps its region
    (the constant evaluator reads its scope; `constexpr-fno-elide-ctors1`).
  - An automatic variable is never a G-id (`ecil_variable_id`, C too).
  - The symbol clash of a definition (no G-id for it) is an error only for an
    external definition that no other translation unit can supply.
  - Under `--cfront_2.1` the scope regions are followed in the record writer
    (`ecil_track_regions`) and a case or goto label with a cleanup open is
    diagnosed. The check is deliberately coarse: it diagnoses any label that
    follows the declaration of an object with a destructor in an open scope
    region, whether or not a jump bypasses it (the front end accepts such jumps
    there and the cleanup would run for an unconstructed object).
    `FUNC_BEGIN` resets the region stack; `STATIC_INIT_BEGIN` pushes a region
    (a static local's initialization is inside the body;
    `writer-cxx-cfront-static-init-regions`).

### 9.16 M4 slice S1: polymorphism without virtual bases

ECIL 1.2, spec section 12. Map: `research/il_to_ecil_map_m4.md`. All of it is in
`ecil_type.c` (class facts, vtables), `ecil_code.c` (bodies) and `ecil_const.c`
(constants).

* **Predicates.** `ecil_class_is_dynamic` (`any_virtual_functions_including_in_base_classes
  || any_virtual_base_classes`; not `needs_virtual_function_table`, which ignores
  inherited functions) and `ecil_class_has_own_vptr` (dynamic and no
  `primary_base_class`). CHECKING asserts that the class is dynamic iff its record
  has a `TY_VPTR` field, a `VIRTUAL_BASE` field or a `BASE` of a dynamic class
  (`ECIL_GF_DYNAMIC`). A dynamic class is never ABI-opaque; a `LOAD`, a bitwise
  copy or a conversion of a value of one is diagnosed (copies are constructor
  calls).
* **Layout facts** (`CLASS_MANGLE`, `CLASS_INFO`, `CLASS_BASES`) follow the record
  body of a dynamic class at once (`send_layout_facts`); a plain class gets them at
  its first need (`ecil_class_layout_facts`, bases first on an explicit stack).
  `CLASS_INFO` sends the FE's `size_without_virtual_base_classes` (CHECKING:
  `data_size <= nvsize <= size`). `CLASS_MANGLE` is `mangled_typeinfo_string` and
  is re-issued in `ecil_resolve_late_names` when a typedef name for linkage
  changed it.
* **Vtable facts** come at the first need (`ecil_class_vtable_facts`, dynamic
  bases first): a `DECL_FUNC` for every virtual function (a destructor as D1, plus
  D0 with its own `DECL_FUNC` and `STRUCTOR_DELETING` naming the class's
  `assoc_operator_delete_routine`; the flags are `is_default_operator_delete`'s,
  CHECKING compares them with the parameter count), `CLASS_VFUNCS` (declaration
  order, implicit functions last in slot order, `consteval` excluded, `sig` from
  `virtual_signature_id`: name, parameter types, `this` cv, no ref-qualifiers) and
  `CLASS_OVERRIDES` (each base entry's own list). D0 is re-issued with D1, never
  retracted. The needs: `VPTR_INIT`, a virtual call, a virtual `delete`, a
  `C_AGG_BEGIN` of a dynamic class, the end of the translation unit for a vtable.
* **Bodies.** `VPTR_INIT`: in a constructor after the base `MEMINIT`s and their eh
  cleanups (before the first field `MEMINIT`, or at the end of the groups), not in
  a delegating constructor; in a destructor after the cleanup prologue
  (`vptr_init_pending`). `CALL VIRTUAL`: `setup_virtual_call` devirtualizes as
  clang does (complete object, final class, final function) to the final overrider
  found from the base entry's override list; a covariant overrider keeps the
  dispatch; CHECKING compares with `final_overrider`. `DELETE VIRTUAL` for a
  non-array delete through the class's own operator delete; `delete[]` calls the
  static type's destructor. A conversion to an indirect non-virtual base is a chain
  of `BASE_CAST` (`emit_base_chain`), a downcast a chain of `DERIVED_CAST`.
  Zero-initialization of a base subobject is `INIT_ZERO_BASE`.
* **Constants.** The FE's constants list no vptr, so entry indexes are shifted past
  the `TY_VPTR` field (`an_init_level.vptr_shift`); `C_AGG_BEGIN` of a dynamic
  class sends its vtable facts and marks its vtable used (not as a base subobject);
  `C_ZERO` of one is diagnosed.
* **Emission** (`ecil_emit_class_abi_data`): at the end of the translation unit
  (at the end of the function's region for a local class) each dynamic class gets
  its vtable and class typeinfo `DECL_ABI_DATA` by clang's rules (`vtable_key_of`,
  `plan_class_abi`): the key function (first non-pure, non-inline,
  non-compiler-generated virtual function not defined in the class) defined here:
  external and defined; elsewhere: an external declaration, if used; none: a
  `linkonce_odr` COMDAT vtable if used; explicit instantiation: `weak_odr`;
  `extern template`: a declaration; TU-local: internal; the typeinfo has the
  linkage the vtable would have, with `TU_LOCAL` when the class is, its bases' typeinfo
  introduced first, and is re-issued at the end if an earlier one changed
  (`reissue_class_typeinfos`). "Used" is logged and taken back with a dropped body
  (`ecil_vtable_use_mark`, `_rollback`, `_commit`). A linkonce vtable that would
  refer to a dropped function becomes an external declaration.
* **Diagnosed (S1):** a destroying operator delete, `::delete` of an object with
  a virtual destructor (`nds->global_new_or_delete`).
* **Covariant returns.** The vtable facts of a class also send, for each override
  with a covariant return class, that class's layout facts, and its vtable facts when
  the path from it to the overridden return class goes through a virtual base
  (`send_covariant_return_facts`; golden `writer-cxx-virtual-covariant-facts`).
* **Review fixes (S1).** The signature table keeps no function of a local class
  after its region (`ecil_forget_local_signatures`; golden
  `writer-cxx-virtual-sig-local`). The definition of a virtual function sends
  the facts of its class, so that the library emits the thunks to it
  (`writer-cxx-virtual-thunk-facts`). A local class whose vtable is first used
  after its region ended (a constructor instantiated later) gets its vtable
  decided at that use (`ecil_note_vtable_used`, `writer-cxx-virtual-local-late`).
  With `rtti = 0` no class typeinfo is issued for a vtable. A declared-only
  vtable (key function elsewhere) has its typeinfo declared too. A local class
  with internal linkage is `TU_LOCAL` (g++'s `*`). CHECKING: every virtual
  function of a vtable defined here is defined, external, dropped, pure or
  deleted.
* **Tests.** Goldens `writer-cxx-virtual-*` (basic, hierarchy, key, names, devirt,
  constants, sig, sig20, delete, zero-base, template, vbase-negative,
  destroying-delete), `writer-cxx-class-assign-constant`,
  `writer-cxx-agg-closure-init`, `writer-cxx-static-local-late-name`; run probes
  `probes/m4/s1_*.cpp` (against g++); `stream_sweep.py --reasons` counts the
  reasons of the dropped functions of a corpus.

### 9.17 M4 slice S2: virtual bases

ECIL 1.2, spec 12.2 (`VIRTUAL_BASE`), 12.3 (`STRUCTOR has_vbases`), 12.4
(`VBASE_CAST`, `VTT_ADDR`). Map: `research/il_to_ecil_map_m4.md` (sections 2.3,
3.4, 4.3, 4.4).

* **Record.** The `VIRTUAL_BASE` fields follow the data members, one for each
  virtual base, direct or indirect, at its complete-object offset, in the order the
  front end constructs them: **its postorder base list** (`base_classes`, the
  `next` chain), not its `preorder_base_classes` (which lists `V2` before `V1`
  for `N : virtual V1, virtual V2` with `V2 : virtual V1`). That is also the order of
  the `MEMINIT(vbase)` groups, whose `idx` must increase. A virtual base with no
  storage of its own has `EMPTY_FOR_ABI` (flag 67): an empty class, or the primary
  virtual base of the class or of one of its bases, direct or indirect
  (`vbase_is_primary_of_a_base`, clang's hasOwnStorage; CHECKING: its offset is
  that base's; golden `writer-cxx-vbase-primary`). The data size is the largest end of any field: a base, direct
  or virtual, ends at its offset plus its nvsize (`base_layout_size`: the
  `CLASS_INFO` nvsize, else the data size). Checked against clang's
  `-fdump-record-layouts` (size, dsize, align, nvsize) for the 15 classes of
  `writer-cxx-vbase-layout`.
* **Structors.** `STRUCTOR` has `has_vbases` for a class with `VIRTUAL_BASE` fields
  (`ECIL_GF_STRUCTOR_VBASES` on both G-ids; the record body is sent first). A
  constructor is walked twice by `emit_function_walk` (C1 with the `MEMINIT(vbase)`
  groups and their eh cleanups, then C2 without them; the second walk starts with
  fresh L-ids, and CHECKING asserts that it introduces no G-id: a static
  temporary is found by owner and sequence number, a lambda is streamed
  once with its region, a static local keeps one `DECL_VAR`, with its
  `STATIC_INIT` in both bodies). A destructor's front-end body is D2 (no virtual
  base entries in its prologue); D1 is synthesized (`emit_synthesized_d1`): a
  `CLEANUP_BEGIN(all)` per virtual base with a destructor, begun in C1's order
  (the destruction list from its end), each calling D2 on `VBASE_CAST COMPLETE`,
  then `CALL` of D2 with `VTT_ADDR(self)`; no `VPTR_INIT`.
* **Casts.** `VBASE_CAST` (`emit_base_chain`): `NONNULL` as for a `BASE_CAST`,
  `COMPLETE` for the construction and destruction of the virtual bases and for a
  cast of `this` in C1; a base reached through a virtual one starts with `VBASE_CAST`
  and continues with `BASE_CAST`s; a `static_cast` from a virtual base is ill-formed.
  The facts of the class come before it.
* **VTT.** The `CALL` of a base variant of a `has_vbases` pair has `VTT` and a
  `VTT_ADDR` after argument 0: `base idx` or `vbase idx` for the bases of the
  class being constructed or destroyed, `self` for a delegation in C2 and for D1's
  call of D2 (`emit_vtt_addr` marks the vtable used). A delegating constructor
  calls the target's C1 from C1 and C2 from C2. `DECL_ABI_DATA(vtt)` follows the
  vtable with its linkage and flags, and the typeinfo of every dynamic base,
  indirect ones included, is declared for the construction vtables
  (`emit_class_abi_data`).
* **Copies.** A bitwise-copy entry of a virtual base in an implicit copy
  constructor, and the assignment of a virtual base subobject in an implicit
  `operator=`, go through the same casts (`emit_this_subobject`).
* **Tests.** Goldens `writer-cxx-vbase-{diamond,structors,casts,layout,iostream,
  clone-impl,order}`; run probes `probes/m4/s2_*.cpp` against g++ (the real
  `<iostream>` program included); stress: 200 virtual bases and a chain of 150
  virtual bases validate under `ulimit -s 256`. `writer-cxx-vbase-static-local`
  waits for the library's relaxed `STATIC_INIT` rule (the validator rejects two
  `STATIC_INIT`s of one variable).


### 9.18 M4 slice S3: RTTI and member pointers

ECIL 1.2, spec 12.2 (`TY_DATA_MEMBER`, `TY_METHOD`), 12.3 (`TYPEINFO_DEF`,
non-class `DECL_ABI_DATA`), 12.4 (`TYPEID`, `DYNAMIC_CAST`, `PM_*`,
`CONST_DATA_MEMBER`, `CONST_METHOD`, the `C_` forms), 12.7 (the null
data-member pointer), 12.8 (typeinfo linkage). Map: `research/il_to_ecil_map_m4.md`
(sections 6, 7).

* **Types.** A pointer to member is `TY_DATA_MEMBER(member type, class)` or
  `TY_METHOD(function type, class)`, introduced by `finish_member_pointer`
  after both children (the class only declared, as a pointee); the function type
  of a method has the object pointer first (it is the front end's non-static
  member function type). They are interned by (kind, member, class); a data-member
  pointer has `ECIL_GF_HAS_DMP`, inherited by arrays and records that contain it,
  which is what makes a type "not zero-initializable". They are no longer
  opaque.
* **Zero-initialization (12.7).** `ZERO_BYTES` is used only for a type without a
  data-member pointer. `emit_zero_object` (value-initialization, `new T()`,
  `T t{}`) uses `INIT_ZERO` on the typed glvalue; `emit_zero_pointee` and a base
  use `INIT_ZERO_BASE`; the data-size zeroing of a `[[no_unique_address]]` member
  or of a delegation to a trivial constructor is `emit_zero_data_size`
  (`INIT_ZERO_BASE`, or `ZERO_BYTES` of the data size without a data-member
  pointer); `new T[k]()` of a constant count uses `INIT_ZERO` on the array
  (`ecil_array_of`). A partial aggregate constant (`H h{}`, the unlisted parts of a
  definition) is left to the library, which expands the null constants, so the
  `ecil_note_zero_init_of_dmp` diagnostics are gone.
* **Constants** (`ecil_emit_ptr_to_member_constant`). `ck_ptr_to_member`: a data
  member's offset is the field's own offset (plus those of the anonymous unions that
  contain it) plus the offset of the base the pointer was converted through
  (`casting_base_class`, negative for a conversion to a base: not expressible);
  null is nil; a bit-field or a virtual base path is diagnosed. A function: `g`
  of the routine, `adj` the non-negative base offset, `VIRTUAL` (with the class's
  vtable facts) for a virtual function. A global (`VAR_INIT`, `C_AGG`) uses
  `C_DATA_MEMBER` / `C_METHOD`; a body uses `CONST_DATA_MEMBER` /
  `CONST_METHOD`, and a local initialized by a method-pointer constant is a
  `CONST_METHOD` and a `STORE` (a `C_METHOD` with a non-zero `adj` is only valid in a
  `VAR_INIT`: an aggregate in a body that contains one is diagnosed:
  `writer-cxx-memptr-negative`).
* **Operations.** `.*` / `->*` on data are `PM_FIELD` (object address first: a
  value is materialized; a glvalue, with `LOAD` if the node is a value); a call
  is `PM_CALL(t_call, nargs, flags)` with the object, the method pointer, then the
  arguments (`do_pm_call`, C++17 order: the object is evaluated first though the
  node lists the pointer first; `nargs` counts the object, `NOTHROW` from
  `is_nothrow_type`); `eok_pm_base_class_cast` / `eok_pm_derived_class_cast` are one
  `PM_CAST` step to a direct base (`to_base` 1, `to_derived` 0, the offset of the
  direct base entry; virtual: diagnosed); `==`, `!=` and the conversion to bool
  are the ordinary `CMP` / `TO_BOOL`.
* **Typeinfo** (`ecil_typeinfo_id`). A class's typeinfo is `issue_class_typeinfo`
  (its bases' first, its layout and vtable facts, linkage from `plan_class_abi`); the
  typeinfo of a class that is incomplete when first needed is a plain internal
  `__class_type_info` (`ECIL_GF_TI_INCOMPLETE` on its record, never re-planned,
  sticky as in clang). A type that is not a class gets a `DECL_ABI_DATA` named by
  `mangled_typeinfo_name` of the type with references and top-level cv removed
  (unique by name through the word table), then a `TYPEINFO_DEF`, after the
  typeinfo of its pointee and of the class of a member pointer (an explicit
  work stack, not recursion: `typeinfo_stack`). Kinds: fundamental, pointer, member
  pointer, function (a noexcept pointee has its specification stripped and the
  `NOEXCEPT` mask), array, enum. Masks: `CONST`, `VOLATILE`, `RESTRICT` of the
  pointee, `INCOMPLETE` where the pointee chain contains an incomplete class
  (clang's `ContainsIncompleteClassType`), `INCOMPLETE_CLASS` for a member pointer
  of an incomplete class. Linkage: a fundamental type and a pointer to a
  cv-unqualified or const one are external declarations (not defined here);
  an incomplete chain, a TU-local type (`cir_entity_is_tu_local`, which now looks
  through pointers, member pointers, arrays and functions to the tag types) and a
  chain with a function-local class of internal statics are internal (the
  last two with `TU_LOCAL`); the rest `linkonce_odr` in a COMDAT group.
  `float _Complex` and other non-standard types are diagnosed.
* **`TYPEID` and `DYNAMIC_CAST`.** `enk_typeid`: static form with the typeinfo of the
  type operand (nothing popped); a dynamic one has a nil typeinfo and pops the
  glvalue of the polymorphic operand (a value is materialized), `NULL_CHECK` iff
  the operand is `eok_indirect`. `&typeid(T)` in a constant (`abk_typeid`) is
  `C_ADDR` of the typeinfo, in a body `TYPEID` and `ADDR`. `eok_dynamic_cast`: the
  operand pointer, `DYNAMIC_CAST(src typeinfo, dst typeinfo | nil, result type,
  0)`; `eok_ref_dynamic_cast`: `ADDR`, `DYNAMIC_CAST ... REF`, `DEREF` (the node's
  type is `T`). The layout facts (and vtable facts for a dynamic class) of both
  classes come first.
* **Run-time-count `new[]` and `if consteval`.** `new T[n]()` and `new T[n]{}` with
  a run-time count zero every element in a loop (`emit_zero_new_array_loop`:
  `INIT_ZERO` in an `ARRAY_CTOR` loop for a record element, a `WHILE` over a
  counter and a local pointer otherwise, because `cir.array.ctor` takes only
  records), also the zeroing that precedes the constructor loop of a class
  whose implicit constructor value-initializes (`writer-cxx-new-array-zero`);
  `new S[n]{S{...}}` stays unsupported (it needs `bad_array_new_length`). `if
  consteval` and `if !consteval` write only the arm that runs at run time
  (`writer-cxx-if-consteval`; libstdc++'s C++23 `std::__is_constant_evaluated`
  and `type_info::operator==`).
* **Checks.** `check_vtable_members_available` (CHECKING) covers, besides the
  class's own virtual functions, the final overrider of each virtual function of
  each base (the `overriding_virtual_functions` of the base entry). The front end's
  `virtual_functions_marked_as_required` is **not** an invariant of a used vtable
  (a class whose vtable a derived constructor uses can lack it), so it is not
  asserted. `push_covariant_return_class` (the covariant-return facts of 9.16) goes
  on the class stack, not the C stack.
* **Tests.** Goldens `writer-cxx-memptr-{types,constants,ops,negative}`,
  `writer-cxx-dmp-zero-init` (re-recorded: supported now), `writer-cxx-typeid`,
  `writer-cxx-typeinfo-{kinds,incomplete,tu-local}`, `writer-cxx-dynamic-cast`,
  `writer-cxx-virtual-declared-base-typeinfo`; probes `probes/m4/{m1_memptr,
  m2_valinit,m3_pm_casts,t1_typeinfo,t2_incomplete,n2_typeinfo_names}.cpp`.


### 9.19 M4 slice S4: exceptions

ECIL 1.2, spec 12.4 (`FUNC_EH_SPEC`), 12.5 (`TRY_BEGIN`, `CATCH_BEGIN`,
`CATCH_PARAM`, `TRY_END`, `THROW_BEGIN`, `THROW_END`, `RETHROW`), 12.6
(`HDR_LANG exceptions`, `NOTHROW`, `DEACTIVATABLE`, `CLEANUP_END`). Map: sections
5.1 to 5.8.

* **Header and flags.** `HDR_LANG exceptions` is set for C++ with the front end's
  `exceptions_enabled` (`ecil_has_exceptions`; `--no_exceptions` turns it off, and
  nothing in the environment changes it): every function and call may
  throw unless marked, and the `eh` and `all` cleanups M3 sent are live. `DECL_FUNC
  NOTHROW` comes from `is_non_throwing_routine` only and is kept by every
  re-issue (`emit_func_decl_if_changed`); a `CALL_INDIRECT` and a `PM_CALL` have it
  from `is_nothrow_type` of the call's function type; a direct call needs none.
* **`FUNC_EH_SPEC`** (`emit_func_eh_spec`), after the PARAMs of every definition
  (functions, synthesized D1, lambda invokers): `noexcept` for a noexcept
  specification that is true (the implicit destructor and deallocation function
  included) and for `throw()` in C++17; `dynamic` with the typeinfo of each
  listed type otherwise; nothing for `noexcept(false)`, an attribute's nothrow
  and a trivial member (those are `NOTHROW` only).
* **`throw`** (`do_throw`): `RETHROW` for `throw;` and the front end's implicit
  rethrow; else `THROW_BEGIN(t)`, the initialization with the destination on the
  stack (`cik_init` with `DEST_ON_STACK`: a store, a constructor call with the
  glvalue as `this`, `INIT_CONST`), `THROW_END(typeinfo, D1)`. It is not a region:
  the operand's temporaries are cleanups of the enclosing `FULLEXPR`. A throw in a
  conditional region (a `TERN` arm, the right operand of `&&`/`||`) needs a
  `FULLEXPR` around the region: the outermost conditional operation of a full
  expression that has none open wraps itself (`conditional_needs_fullexpr`) when
  its conditional operands may contain a throw or a new with a free cleanup
  (`expr_may_contain_throw`, a heap walk that treats forms it does not know as
  throwing); `fullexpr_depth` and `cond_depth` track both.
* **`try`/`catch`** (`do_try_block`): `TRY_BEGIN(0)`, the body, then each handler as
  `CATCH_BEGIN(typeinfo | nil)`, `LOCAL` and `CATCH_PARAM(l, kind, g_copy)` for its
  parameter (an unnamed one too), the `all` cleanup of a destructible parameter,
  the handler block; `TRY_END`. The kind comes from the parameter's type
  (`catch_param_kind`: 7 reference to a record pointer, 6 reference to another
  pointer, 0 other references, 1 pointer, 2 scalar including member pointers, 4
  trivial copy, 5 copy constructor with the C1 id). A copy with default arguments
  or temporaries is diagnosed. The function-try-block of a constructor or
  destructor is `TRY_BEGIN(FUNCTION_TRY)` right after the header (before the
  initializers, `VPTR_INIT` and the destruction prologue, `function_try_open`);
  its handler ends with the front end's implicit rethrow. A plain function's is
  an ordinary `TRY`. The label search for constant `if`s looks into try blocks.
* **The free of a new** (`do_new_delete`, spec encoding 10): with an initialization
  that may throw (`new_has_throwing_initializer`) and an operator delete the front
  end recorded, the size is kept in a local before the allocation, each
  placement operand in a local, the result in a pointer local, then
  `CLEANUP_BEGIN(eh, DEACTIVATABLE)` calls the delete function (sized when it has
  a size parameter, with the placement operands), and `CLEANUP_END` follows the
  initialization (phase 5 of the item). The delete function must have one
  parameter for the pointer, optionally one for the size and one for each
  operand of the allocation function. Nothing for the global placement new.
* **Partial destruction.** `ARRAY_PARTIAL_DTOR` follows the `ARRAY_CTOR_BEGIN` of a
  loop of default constructions with elements that have a destructor (local arrays
  and `new T[n]`). An aggregate with members or elements that have destructors
  keeps its address in a local (`an_init_level.addr_lid`) and begins
  `CLEANUP_BEGIN(eh, DEACTIVATABLE)` after each entry's initialization (a
  constructor, a constant, an empty list; an array member through an `ARRAY_DTOR`
  loop), ending them before `AGG_INIT_END` (`agg_note_initialized`,
  `agg_emit_partial_cleanup`).
* **Also.** A `ck_void` constant pushes nothing; the typeinfo objects of
  exceptions exist with RTTI off (`ecil_typeinfo_id` has no RTTI check: `typeid`
  and `dynamic_cast` check it); the new-array zero loop and the if-consteval
  statement are in 9.18.
* **Throwing destructors.** A destructor that may throw (`noexcept(false)`) run as a
  normal-path cleanup is fine for locals, temporaries and enclosing scopes: the library
  (the pin) makes calls inside a cleanup region invokes into the enclosing scopes, so the
  cleanups that remain run (log-for-log with g++ in `cxx-s4-eh-cleanup-throw`). Only the
  destructor of an array element that may throw is diagnosed (`emit_cleanup`): the pin
  skips the elements before the one that threw, while clang and g++ destroy them
  ("watch upstream").
* **S5 additions.** `new (std::nothrow) T`: the result is kept in a local and the
  construction is in the arm of a `TERN` on it, with a null arm (`NEW_NOTHROW`,
  `new_null_test_begin`; a free cleanup there gets a `FULLEXPR` of its own).
  `new T[n]{...}` with a run-time `n` (`NEW_LIST`, `new_list_rest`): a check
  `n < k` calls `__cxa_throw_bad_array_new_length` (a runtime function declared on
  first use, `ecil_throw_bad_array_new_length_function`), the first `k` elements are
  initialized through a pointer to the array of `k` (their destruction is a
  `DEACTIVATABLE` cleanup while the others are built), the other `n - k` are a
  loop of the default constructor (with `ARRAY_PARTIAL_DTOR`) or of `INIT_ZERO`.
  A catch, throw or `FUNC_EH_SPEC` in a dropped function leaves its typeinfo
  declared (an incomplete class's is a plain internal one, no facts).
* **Session-3 corrections.**
  * A class that was first met as a pointee and completed later (a template
    instantiation) gets its size and alignment in the G-id table when its
    `TY_RECORD_BODY` is sent, so the size of a `new` of it is right (opt/cleanup1).
  * `CLASS_VFUNCS`: the sig of a virtual function includes the cv-qualification of
    `this` (`rtsp->qualifiers`), so `foo()` and `foo() const` differ (Test38);
    `ECIL_VFUNC_REF_RESULT` (0x08) is set for a function that returns a reference.
  * `E1[E2]` with the integer first (`2[p]`) keeps the integer in a temporary and
    evaluates the pointer after it (C++17 order, `SUBSCRIPT_INT_FIRST`).
  * `new T[n]` with a run-time count, not a nothrow allocation, from C++11 on:
    `NEW_ARRAY_SIZE_THROW` (a negative or overflowing count throws
    `std::bad_array_new_length`); for one-byte elements a negative signed count is
    tested before the conversion to `size_t`, since it does not overflow the size.
  * The free-on-throw of a new expression calls a placement `operator delete(void *,
    void *)` that the program declares; the one from a system header (nothing to
    free) has no cleanup. A destroying operator delete recorded by the front end
    for a new expression is unsupported (a reason, not a wrong call).
  * The LOCAL alignment of a variable of a cv-qualified alignment typedef is that of
    the typedef (`ecil_variable_alignment` walks the typeref chain).
  * In C++17 the front end lists the base entry of `{ .x = 1, .y = 2 }` after the
    members; it is sent as the entry of the base (`stream_next`).
  * A `const int &k = P{T(5), 6}.k` in GNU mode with `--gnu_version=150200` binds to a
    copy and ends the temporary at the end of the declaration: that is the front
    end's IL (g++ 15 and the plain mode extend it), not a writer decision.
* **S5 corpus fixes.**
  * A class conditional whose value is discarded (`(c ? D() : throw 0);`) is a void
    `TERN`: the temporary that holds its value (`is_discarded_conditional_temp`) is
    skipped and each arm is a discarded temporary with its own cleanup, or, when the
    front end builds the arms in the temporary of the conditional (a master entry
    without a variable), the arms build in that temporary (`note_master_dest`).
    `value_is_pushed` is false for such a conditional (eh/cond1).
  * `::delete p` of a polymorphic object (and a delete whose operator delete is not the
    class's) is `DELETE VIRTUAL|GLOBAL` (0x10): the library calls D1 through the vtable
    and then the operator delete the front end chose, even if the destructor throws
    (eh/delete1's `virt_glob`; its `ary` case fails in the library).
  * A reference bound to a temporary array (`auto &&r = Arr{B()};`) is bound to the
    temporary: the array-to-pointer decay and `lvalue_adjust` of an array temporary use
    its storage (`is_array_temp_init`, `CIF_GLV`), and an `lvalue_adjust` between two
    aggregate types is a bitcast of the address.
* **Pointer-to-member builtins.** `__builtin_is_corresponding_member` and
  `__builtin_is_pointer_interconvertible_with_class` (and the MS forms with type
  operands) that the front end could not fold are translated by `do_pm_builtin` as its
  own lowering does: each pointer to a data member is read as a pointer-sized
  integer (`MATERIALIZE_TEMP`, `ADDR`, `CAST bitcast`, `DEREF`, `LOAD`; the null value
  is all ones), and the result is `a == b && a < limit` (`common_initial_sequence_limit`)
  or `a == 0`. A pointer to a member function, or a class that is not standard-layout
  (a union is exempt for interconvertible), gives false after the operands are
  evaluated. A member that cannot be told from the first one by its offset
  (`[[no_unique_address]]` empty members) gives true, as in the front end's lowering.
  `__builtin_launder` is the identity.
* **Known differences from g++ that clang shares (eh/array2, eh/return1).** When the
  destructor of a temporary of the default argument of an array element's constructor
  throws (`array2`), g++ destroys the element that was just built; classic clang (21.1.8
  aborts on the test) and the library do not. Likewise the object returned by value is not
  destroyed when a destructor that runs after its construction throws (CWG 2176,
  `return1`): g++ destroys it, clang and the library do not. The writer no longer
  diagnoses either case.
* **Members of anonymous unions and structs in constructors.** A constructor-initializer
  entry for a member of an anonymous union or struct (`struct { int m = 0; };` in the
  class) is a `MEMINIT` group numbered by the unnamed member that holds it; its body
  reaches the member through `FIELD` of the unnamed member(s) and then of the member
  (`emit_this_field`, `anonymous_member_path`), and the entries of one holder share one
  group (`merge_anon_meminit`). One whose initializer has a destructor stays
  unsupported.
* **Class values returned through a comma or a conditional.** The arms of
  `return X(1), X(2);` and `return c ? X(1) : X(2);` build the return slot: the arm's
  master entry has no variable, and the destination noted by the initialization that owns
  it (`note_master_dest`, 0 is the slot) is used. A destination on the stack (`throw c ?
  B(1) : B(2);`) stays unsupported.
* **More clang-consistent differences (eh/ref-temp1, eh/ref-temp2, rma/eh40).**
  Checked with classic clang 21.1.8 and g++ on the box: `ref-temp1` (a reference-bound
  temporary of a lifetime-extended `A(int)` is not destroyed when the destructor of the
  temporary that holds the member throws) aborts in clang and in the library, exits 0 in
  g++; `ref-temp2` (`auto &&r = Arr{B()};`, the elements built before `B`'s destructor
  throws) exits 3 in clang and in the library, 0 in g++. `rma/eh40` (a placement `new`
  whose constructor throws, no handler: the placement `delete` must run and exit)
  terminates in clang, in g++ and in the library: with no handler the unwinder does not
  run the cleanups; the reference passes only because the front end's own lowering
  wraps the new in a `catch (...)`.
* **The assignment of an empty class copies nothing** (found by the run failure of
  `cpp0x/variadic-tuple`): `tail() = other.tail()` of an empty base (the `tuple<>` of a
  `tuple<std::string>`) was a one-byte `STORE` through a reference to a subobject that shares
  its address with the first member, and overwrote the first byte of the `std::string`.
  `type_is_empty_class` assignments now evaluate the operands and write nothing (probe
  `s5_empty_assign`, `s5_variadic_tuple`; golden `writer-cxx-empty-assign`). A class whose
  only member is an empty base has data size 0 and is already copied by data size.
  "Empty" means no data: `gid_is_empty_class` is true for a complete class with no
  `REC_FIELD`, or with data size 0 (only empty bases: `Tag2 : Tag`, `std::allocator<T>`), so
  `std::get<0>(t) = std::allocator<int>{}` of a `tuple<allocator<int>, int>` writes nothing
  (the assignment evaluates the right operand, as an address if it is an object, then the
  target, and stores nothing: `empty_assign_operand`, `EMPTY_ASSIGN_RHS`/`LHS`). Golden
  `writer-cxx-empty-nodata`; probe `s5_empty_overlap` includes `Tag2` and the tuple.
  The sweep for siblings (probe `s5_empty_overlap`, 45 checks against g++ and clang, an
  empty base or `[[no_unique_address]]` member sharing its address with a `char`): copy and
  move construction, aggregate initialization (`A{'1', {}}`, `C{{}, 'a'}`), a member initialized
  from a function result, delegating constructors, empty arguments and temporaries, defaulted
  copy and move constructors and assignments of classes with empty bases, and
  `new C()` all write nothing already (the library omits the store through a field of an
  omitted empty subobject, `INIT_EMPTY` covers the copy). Two sites wrote the byte through a
  raw pointer and are fixed: the value-initialization of an empty class (`new (p) E()`,
  `emit_zero_object`) and its constant initialization (`new (p) E{}`, `INIT_CONST` of an empty
  aggregate); both now drop the destination (golden `writer-cxx-empty-init`).
  `__builtin_*_overflow_p` against a bit-field is diagnosed (the check is made against the
  width of the bit-field).
* **A constant of another integer type initializes a bool or integer object with the type of
  the object** (`emit_leaf`): `inline constexpr bool is_integral_v<T>` gave a `C_INT` of a
  narrower type, which the library now rejects ("VAR_INIT constant type differs from the
  variable type", `#include <tuple>` in C++20; probe `s5_include_tuple_cxx20`).
* **Known difference from clang (typeinfo of a class completed later).** The first need of a
  typeinfo that comes from a file-scope initializer sees the class already completed
  (`t2_incomplete`: `_ZTIP5Later` is `linkonce_odr` where clang has it internal); only
  which translation units share the object differs (map 7.3).
* **Destroying operator delete.** `delete p` of a class with a destroying operator
  delete and no virtual destructor is `DELETE DESTROYING` (0x20): no destructor
  (`g_dtor` nil), the size and alignment as the function takes them, the empty tag not
  counted in the flags; with a virtual destructor it is `DELETE VIRTUAL` and the
  `STRUCTOR_DELETING` of the class has the flag `DESTROYING` (0x04): D0 calls the
  destroying delete and runs no D1 (the parameter count includes the tag). A destroying
  delete of an array and the destroying delete the front end records as the free function
  of a new expression stay unsupported.
* **Known gaps** (the library or the contract): the destructor of an array element that
  throws (`eh/array3`, `ref-temp1`, `ref-temp2`: map 5.9); `delete[]` of an array of a
  class with a virtual throwing destructor (`eh/delete1`, `ary`).
* **Tests.** Goldens `writer-cxx-exc-{throw,cond-throw,catch,function-try,spec,
  spec-dynamic,nothrow,new,partial,no-rtti,negative,hierarchy}`; probes
  `probes/m4/{e1_try,e1b_catch_kinds,e1c_unnamed_catch,e2_throw,e3_spec,
  e3b_spec17,e4_new,e4b_new_flag,e5_rangefor,e6_ftb,e7_cond_throw}.cpp` (rewritten
  to avoid unspecified evaluation order), `s4_partial.cpp`, `s4_stdexcept.cpp`
  (`<stdexcept>`, `std::string`, `std::vector::at`, `exception_ptr`) and
  `s4_c_frames.sh` (a throw through a C frame built by the back end), `s5_ftb`, `s5_dynspec`, `s5_nothrow_new`, `s5_new_list`, `s5_dropped_catch`, all equal
  to g++; the `imported/gnu/cpp/eh` corpus directory passes 45 of 73, the others
  being the gaps above and vectors, inline assembly and variable length arrays.

## 10. ECIL versions (what the writer sends)

* **1.0** (gate M3): C and core C++ (spec sections 1 to 11). Sections 9.1 to 9.14.
* **1.1** (gate M3.1): the spaceship `CMP3WAY` and the M3 re-look items (section 9.15).
* **1.2** (as implemented, frozen at the M4 gate): the records and flags of spec section 12
  that the writer sends. It is the format the C++ corpus run, the goldens in
  `tests/tests/cir/writer` and the cir-suite are checked against.

  | Area | 1.2 records and flags the writer sends | Code |
  |---|---|---|
  | Polymorphism | `TY_VPTR`, `CLASS_BASES`, `CLASS_VFUNCS` (`PURE`, `DELETED`, `IMPLICIT`, `REF_RESULT` 0x08, the `sig`), `CLASS_OVERRIDES`, `CLASS_MANGLE`, `STRUCTOR_DELETING` (`SIZED`, `ALIGNED`, `DESTROYING` 0x04), `VPTR_INIT`, `CALL` flags `VIRTUAL` and `VTT`, `DELETE` flag `VIRTUAL` | `ecil_type.c` (`ecil_class_vtable_facts`, `deleting_destructor_id`), `ecil_code.c` (`setup_virtual_call`, `do_delete`) |
  | Virtual bases | `REC_FIELD` `VIRTUAL_BASE`, `VBASE_CAST` (`NONNULL`, `COMPLETE`), `VTT_ADDR`, `MEMINIT` kind `vbase`, the C1/C2 and D1/D2 pairs, `INIT_ZERO_BASE` 0xA8 | `ecil_code.c` (`emit_class_cast`, `emit_zero_data_size`), `ecil_type.c` |
  | RTTI | `TYPEINFO_DEF` kinds, `DECL_ABI_DATA` (`DEFINED_HERE`, `COMDAT`, `TU_LOCAL` 0x04: the typeinfo of a TU-local type or of an incomplete class, whose name starts with `*`), `DYNAMIC_CAST` (`REF`), `TYPEID` (`NULL_CHECK`) | `ecil_type.c` (`ecil_typeinfo_id`) |
  | Member pointers | `TY_DATA_MEMBER`, `TY_METHOD`, `CONST_DATA_MEMBER`, `CONST_METHOD`, `C_DATA_MEMBER`, `C_METHOD`, `PM_FIELD`, `PM_CALL`, `PM_CAST` | `ecil_type.c`, `ecil_const.c`, `ecil_code.c` |
  | Exceptions | `HDR_LANG` exceptions, `DECL_FUNC` `NOTHROW`, `FUNC_EH_SPEC`, `TRY_BEGIN`/`CATCH_BEGIN`/`CATCH_PARAM`/`TRY_END`, `THROW_BEGIN`/`THROW_END`, `RETHROW`, `CLEANUP_END` and `DEACTIVATABLE`, `ARRAY_PARTIAL_DTOR` 0xA7, `NEW_ARRAY_SIZE_THROW` 0xA9 | `ecil_code.c` (`do_throw`, `do_try_block`, `new_cleanups`, `emit_ctor_loop`) |
  | Deletion | `DELETE` flags `GLOBAL` 0x10 (`::delete` of a polymorphic object: D1 through the vtable, then the chosen operator delete) and `DESTROYING` 0x20 (a destroying operator delete without a virtual destructor) | `ecil_code.c` (`do_delete`) |

  Additions made during M4b (after the contract gate M4a), all part of 1.2:
  `NEW_ARRAY_SIZE_THROW` (a dynamic-count `new T[n]` that is not a nothrow allocation,
  from C++11), `CLASS_VFUNCS` `REF_RESULT` (a covariant reference result is adjusted
  without a null check), `DELETE` `GLOBAL` and `DESTROYING`, `STRUCTOR_DELETING`
  `DESTROYING` (D0 calls the destroying delete, no D1; the parameter count includes the
  empty tag), `DECL_ABI_DATA` `TU_LOCAL`, and `INIT_ZERO_BASE` (the zero-initialization of
  a base subobject writes only `nvsize` bytes and the null constants of data member
  pointers).
* **Not in 1.2** (diagnosed by the writer, section 9.12 and the map, `il_to_ecil_map_m4.md`):
  see the final status there.
