# The ECIL writer (what the code does today)

Status: M2.1 (C, v0 of the stream, ECIL minor 7). This document describes the
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
sink and writes the preamble. `F_HEADER` is sent with the first hand-off, because the triple's operating system comes from the predefined macros, which do not exist at open (section 7).
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
  `__APPLE__`, `_WIN32`), looked up at the first hand-off.

The sizes of `int`, `double` and pointers, and the integer promotion width,
come from the FE target.

`CHEAP_UNALIGNED_BF` is set for x86 and AArch64.

The in-process sink requires x86-64.

Follow-up: a source-level `#undef __linux__` before the first function would
mislabel the triple. The library then rejects it, so it is an error, not a
miscompile. A later milestone should capture the OS before user source is
processed.

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
