# The ECIL writer (what the code does today)

Status: M3 (C and C++, ECIL 1.0). This document describes the
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
* Fields: the vptr (opaque, unnamed), then the **direct** non-virtual bases (the
  list is followed through `next_direct`; the front end's base list also has
  indirect bases), then the data members. A class with a virtual base or one that
  cannot be written is a failed record: its body is not sent and its uses are
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
  `ZERO_BYTES` of their whole size, a base only with its data size.
* A destructor starts with `CLEANUP_BEGIN(all)` for each member and base with a
  non-trivial destructor, in construction order (so they run in reverse after
  the body, also on an early return).
* `CLEANUP_BEGIN(kind, g|nil, flags)` ... `CLEANUP_BODY`: `all` (every exit),
  `exit` with the object's G-id (static storage duration: run at exit), `eh`
  (exception paths only: M4, so ignored by `lifetime_needs_cleanups`).
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
* Not supported: a non-throwing allocation function (the null case), an
  initializer of a new[] with a run-time count, a destroying delete, a virtual
  destructor.

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

**Unsupported in C++ (M3)**, each a named reason in the warning:
virtual functions, virtual bases, polymorphic classes' constructors, destructors
and constants (M4: a vptr needs the vtable); exception handling (`try`, `throw`,
`catch`, `typeid`, `dynamic_cast`); coroutines; pointers to members (types,
constants, calls and `.*`); a `for` with a condition declaration
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
the zero-initialization (explicit, by static storage, or by value-initialization)
of an object that contains a pointer to a data member, whose null value is -1 and
not 0 (`writer-cxx-dmp-zero-init`; until M4 implements member pointers, the types
are opaque storage); an address constant that names a local variable;
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
`the expression kind "throw expression"`, `the statement "try statement"`,
`the operator "three-way comparison (<=>)"` and
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
- a range-based for whose body contains `try`.

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
  does not know (a try, coroutine, if consteval statement); `if constexpr` is
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
