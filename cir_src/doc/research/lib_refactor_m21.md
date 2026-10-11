# cir-lib design note for gate M2.1: L1 + L2 + L4

Author: cir-lib. Status: for the reviewer, before any code. Plan:
`in-our-last-session-distributed-hinton.md`, "Gate M2.1". ECIL is 0.7 here.

## 0. What the profile says (so expectations are honest)

`callgrind` (Ir) of `edg-cir --emit=cir --disable-cir-passes` on the
6000-function stress stream (`ecil-stress-gen.py funcs 6000`), 981 M Ir:

| Part | Ir | Share |
| --- | --- | --- |
| `process_records` (decode + all checks + handler) | 306 M | 31% |
| of which the handler/emitter (`on_record`) | 254 M | 26% |
| of which `check_refs` (all of `a_ref_checker`) | 21 M | 2.1% |
| of which `decode_record` | 14 M | 1.5% |
| MLIR print + verify (the `--emit=cir` cost) | ~500 M | 52% |

The reference layer is about 7% of consume and the emitter's own tables are a
small part of the 254 M, most of which is MLIR op creation
(`create_function` 76 M). So L1/L2 will show up as a few percent of Ir, not
more; their value is a single source of truth, no per-reference hashing,
bounded memory and a structure that M3 can extend (nested bodies, scopes).
Step 2 reports the real before/after numbers either way.

## 1. L1: one reference layer, one table per id space

**Today.** `a_ref_checker` keeps `strings_` (hash map, a copy of every
STR), `ents_` (hash map), `symbol_owner_` (string-keyed map) and a
`functions_` stack of hash maps and sets for L-ids. The session keeps
`std::map<uint64_t,std::string> strings` (second copy plus a third temporary
in `on_record`), and `int_widths`. The emitter keeps `types_` (vector grown
to `gid+1`), `entities_` (hash map), `locals_` (hash map) and `frames_`
(a second bracket stack). A GREF costs `find` x3 in the checker plus
`entities_.find`; a TY_FUNC parameter array is walked in `decode_operand`,
`check_record_operands`, `need_type` and `type_record`.

**Target.** New LLVM-free `lib/format/ecil_ids.{h,cpp}` (owned by
`a_stream_reader`, reachable as `reader.ids()`):

```
class a_string_arena;            // chunked (64 KiB), string_view stable
struct a_gid_entry {             // 24 bytes, index = G-id (uint32)
  uint8_t  kind;                 // none, void..opaque, func_type, record, func, var
  uint8_t  state;                // complete, is_union, has_count
  uint32_t element;              // array element G-id
  uint64_t extent;               // record: nfields; array: count; int/float: bits
  void    *payload;              // owner data; nullptr in ecil-dump
};
struct a_lid_entry {             // 16 bytes, index = base + L-id
  uint8_t  state;                // unseen, value, label, forward_label, dead
  void    *payload;
};
class a_id_tables {              // sid -> string_view; gid; lid stack;
  /* external symbol name -> owner G-id (string_view key into the arena) */
};
```

- **Ownership.** `a_id_tables` is the only owner of introduced facts. The
  validator (`a_ref_checker`, which stays in `ecil_refs.cpp` as the rule
  engine but loses all storage except the two initializer target stacks)
  and the emitter both address it by integer index. `ecil-dump` uses the
  same reader, so it gets the same tables with null payloads.
- **Payload.** `void *` to emitter-owned objects: `a_type` / `a_entity`
  from `std::deque`s (stable addresses, the MLIR handles live in them) and
  `a_local` from a per-function deque for L-ids. The emitter's `types_`,
  `entities_`, `locals_`, `record_gids_`, the session's `strings` and
  `int_widths` go away. A reference then costs one bounds-checked array
  index and one pointer load in each of the two stages, no hashing. I
  considered a templated payload type; rejected because it makes
  `a_stream_reader` a template for no gain.
- **Strings.** `STR` is copied once into the arena by the reader; the
  session handler ignores it. `string_for` returns `std::string_view`;
  `a_entity::name`, symbol maps and record names are views into the arena (a
  second arena in the emitter holds generated names). `symbol_owner_` becomes
  `unordered_map<string_view,uint32_t>` with one `try_emplace` per external
  declaration (today: find + insert + a string copy, 3 hashes).
- **Arrays.** `decode_operand` already walks every element once; it records
  "all unsigned" in `an_operand` (a flag bit), so `check_record_operands`
  stops re-decoding, and a small `an_uint_array_reader` (no generic
  `decode_scalar`) is used by the checker and the emitter. Net: 2 cheap
  walks instead of 4 generic ones. The messages are unchanged.
- **Brackets matched once.** `check_brackets` (reader) is the only matcher.
  It already owns the stack with phase and `has_else`; I extract it as
  `a_bracket_tracker` so that (a) the ref rules can ask for scope open/close
  (below) and (b) `a_record_context` carries the result for the handler:
  `bracket_event` (none/open/advance/close), `bracket_kind`,
  `bracket_depth`. The emitter's `frames_` stays as the parallel stack of
  MLIR state (op, value base, alloca block) but is pushed/popped from the
  event; the `top_frame(kind, what)` mismatch errors become asserts, as do the
  emitter's duplicates of validator errors ("TY_PTR names an unknown
  pointee", "LREF of an undeclared local", ...). No lit test matches those
  messages (checked); the bad_* fixtures already cover them in the
  validator. The session always runs the reader with `check=true`
  (`--no-check` stays a dump-only option); the emitter documents and
  asserts that precondition.
- **L-ids.** One `std::vector<a_lid_entry>` with a stack of function bases
  (nested FUNC_BEGIN for M3 lambdas works the same way); FUNC_END truncates.
  The `goto_targets` set disappears: GOTO to an unseen L-id creates a
  `forward_label` entry and FUNC_END scans the function's range. This also
  makes the "never defined by LABEL" report deterministic (lowest L-id; it
  is hash-order today). Same message.
- **New rule (ECIL 0.7).** A LOCAL or MATERIALIZE_TEMP is bound to the
  innermost open scope (BLOCK or FOR; PARAM and labels are function-level).
  `a_bracket_tracker` keeps a list of the L-ids introduced per open scope;
  closing the scope marks them `dead` (O(1) amortized). LREF of a dead L-id
  is a format error: "local id out of scope: L-id N was declared in a
  scope that has closed". Fixture `bad_ref_local_scope_closed.txt` (0.7
  header); lit test `stmt-for-init.test` (for-init LOCAL in `FOR_BEGIN`
  used in cond/step/body, executed). Question 2 below: gating on minor.
- **Unchanged order.** Per record: decode, `check_record_operands` (operand
  class, flag and enum ranges), `check_section`, brackets, refs, handler.
  Every validator-only check (one G-id per symbol, goto targets, sections,
  ranges) still runs before the emitter reads a raw `.u`.
- **Header replay.** The header's TY_* records are replayed into the
  emitter after the module exists; with shared tables the replay only
  creates payloads. Kept (the module needs the whole header first).

**Migration** (each step builds, passes `ctest` and keeps the 94 fixtures
failing the same way; after steps 3 and 5 the corpus `.s` is diffed):
1. Add `ecil_ids` with unit tests in `format_tests` (bounds, arena, lid
   stack); nothing uses it yet.
2. Move `a_ref_checker` onto it (sid, gid, lid, symbol owner, goto scan);
   array flag + fast reader; add the new fixtures.
3. Extract `a_bracket_tracker`; publish bracket events; scope-closure rule.
4. Emitter: strings, then `types_`/`entities_`/`locals_` onto payloads; drop
   the session maps; asserts for the duplicated checks.
5. Emitter frames from bracket events; delete dead state.

## 2. L2: dense storage, one arena, bounded growth

Dense `std::vector` indexed by G-id, sid and L-id, and the string arena are
above. `max_type_gid` (2^26) and `types_.resize(gid+1)` disappear.

**What the writer guarantees** (read `src/ecil_write.c` `ecil_new_gid`,
`ecil_type.c`, `ecil_const.c`, `ecil_intern_string`): ids are assigned from
a counter at the point of introduction and the introduction record is
appended to the declaration buffer at once (`ecil_ptr_type`,
`ecil_intern_simple`, record, function, variable and string-literal sites),
so G-ids and sids are dense and introduced in increasing order; a body
truncation (`ecil_frame_truncate`) keeps the declaration section whole. L-ids
(`new_lid`) are dense too, but a forward-referenced label gets its L-id at
the first GOTO and its LABEL comes later, so the order is "introduced or
GOTO-referenced". Spec 7 already says "dense from 1".

**Rule.** A newly introduced id may exceed the number of ids introduced so
far (distinct, in that space; for L-ids including GOTO-referenced ones) by
at most K = 1024; otherwise a format error ("G-id N is far above the N
introduced so far (ids must be dense)"). The table is therefore never longer
than introductions + K, i.e. proportional to the stream, whatever the ids
say (today one TY_VOID with G-id 2^26-1 forces ~6 GB). GOTO targets, STR
sids and G-id references to unintroduced ids already fail on lookup; a GOTO
forward reference is bounded by the same rule. Fixtures:
`bad_ref_gid_far.txt`, `bad_ref_sid_far.txt`, `bad_ref_lid_far.txt`, and a
dense-with-gap good one (within K). Ids above 2^32 are rejected as a
consequence.

## 3. L4: renames at finish, no silent suffixing

**Today.** `renames_` plus a per-entity `replaceAllSymbolUses` (a full module
walk each; quadratic with C++ NAME_BIND), and `unique_symbol` silently gives
`foo.1` to whichever of two same-named symbols comes second, even if that is
the external one.

**Design.** Each entity has `symbol` (the current MLIR symbol, unique among
ops) and `want` (the final name; empty until known). At declaration the
name is claimed in a `names_` map (string_view to G-id): free gives the name
itself, taken gives a generated unique placeholder `__ecil.<gid>` (probed
for uniqueness). At finish, once, in G-id order:
1. every external entity (linkage not internal/private) takes `want`; two
   externals wanting one name cannot occur (the validator rejects it:
   one G-id per symbol) and is asserted;
2. every internal entity takes `want` if no external reserved it and no
   earlier internal took it, else `want.N` from a per-base counter
   (identical to today's results when nothing external collides, so the C
   corpus `.s` stays byte-identical);
3. entities with `final != symbol` form a map `symbol -> final`, applied to
   all references in one pass: `mlir::AttrTypeReplacer` with a
   `SymbolRefAttr` replacement over `recursivelyReplaceElementsIn(module)`
   on the worker (the same mechanism `SymbolTable::replaceAllSymbolUses` uses
   at the pin, `SymbolTable.cpp:879`, so GlobalView/GlobalOffset constants and
   call/get_global callees are covered exactly as today). Substitution is
   simultaneous, so an internal `N -> N.1` and an external placeholder
   `__ecil.5 -> N` do not interfere. Then `setSymbolName` on each op.
   The walk is skipped when no referenced entity changes (always in C).

Per-entity use lists were rejected: they miss references that live inside
constant attributes (C_ADDR), which the current `calls`/`addresses` vectors
for retyping do not cover either. The stale `name_bind` comment ("renamed at
once") is rewritten. Tests: lit tests for an external colliding with an
internal (both orders, via a late NAME_BIND and via a named DECL), an
external-external clash (format error, existing fixture), and a many-rename
stream whose walk count is asserted through `EDG_CIR_FLAG_PRINT_STATS`.

## 4. Questions for the orchestrator (contract/scope)

1. **Contract text for density** (spec 7): "G-ids, sids and L-ids are dense
   from 1 and introduced in increasing order (a forward-referenced label
   counts when first referenced); a reader may reject an id more than 1024
   above the count introduced so far." Is K = 1024 acceptable, and does
   cir-writer confirm no path allocates a G-id/sid without emitting its
   introduction (I found none in the four G-id sites and the string site)?
2. **Gate the new dead-local rule on stream minor >= 7**, so 0.6 streams
   (existing goldens, `ecil 0.6` fixtures, BLOCK-wrapped for-init) still read?
   The spec sentence says "a minor-7 reader treats...". I will implement
   `minor >= 7` unless told otherwise.
3. **Emitter duplicates become asserts** (internal error if reached, not a
   user-facing format error) under the "always `check=true`" precondition.
   Confirm that is acceptable under the "strict validation" decision.

I will implement nothing until the review comes back; L3 and L5-L9 are
independent and not started.

## 5. As built (step 2) and the check table

Review verdict: APPROVE WITH FOLLOW-UPS; the REQUIRED items A, B, D, E, F and
G are implemented as below.

**Structure.** `lib/format/ecil_ids.{h,cpp}` (arena, sid/G-id/L-id tables,
declaration side table in the G-id entry, owner map), `ecil_brackets.cpp`
(`a_bracket_tracker`: brackets, FUNC bodies, the region rule table, case-label
placement), `ecil_refs.cpp` (the rules, no storage). The reader owns
`ids_` and `tracker_`; `a_record_context.scope` (an `a_scope_event`: bracket
action, depth, the list of closed scope regions, zero for header replays)
carries the tracker's result to the handler. `commit()` (locals of closed
regions die, a finished function's L-ids go) runs after the handler (H).
The emitter keeps `a_type` / `a_entity` / `a_local` in deques on the payload
slots; `types_`, `entities_`, `locals_`, `session.strings` and `int_widths`
are gone.

**Deviations from the note above.**
- The reader limit is `id <= 2 * introduced + 1024` (G), applied to every
  minor (table memory must be bounded whatever the minor; the dense writers
  of every minor satisfy it). Everything else new (dead locals, nested
  FUNC_BEGIN, case placement) is gated on minor >= 7.
- A repeated STR with identical contents is accepted (it introduces nothing);
  different contents are an error.
- Names (L4): each entity has a unique MLIR `symbol` during emission and the
  effective name in `want`; at finish `assign_final_names` picks the final
  names (externals first, internals `name.N`), queues symbol changes, and one
  `AttrTypeReplacer` walk applies them simultaneously (skipped only when the
  change list is empty). `__const.<fn>.<n>` is created under a placeholder and
  named at finish from the function's final name. `--stats` reports renames
  and walks, G-id and sid holes.

**Validator rules added so that emitter checks could be dropped** (all
format errors): CALL names a function (a variable is rejected,
`ecil_refs.cpp:796`); DECL_VAR type is not a function or void (`:263`);
one body per function (`:592`); VAR_INIT type complete (`:579`); effective
name ownership with release on rename (`apply_name`, `:218`); a record body
may not await REC_FIELD at the frame end (`end_frame`, `:367`); selector
bounds are overflow-safe (`:346`); nested FUNC_BEGIN, scope regions and case
placement (minor >= 7; `ecil_brackets.cpp:171`, `ecil_refs.cpp:659`,
`ecil_brackets.cpp:300`). Fixtures: see the list in the step-2 report.

**Converted emitter checks** (format error -> always-on internal error,
`a_emitter::internal`, status EDG_CIR_ERROR_INTERNAL; no `assert`). Emitter
line | counterpart in the validator:

| Emitter check | Validator counterpart |
| --- | --- |
| unknown / non-type G-id (`type_at` types.cpp:173; TY_PTR/ARRAY/FUNC/REC_FIELD/CAST/VA_ARG/PTR_DIFF/TERN/AGG_INIT/constant "unknown type" sites) | `need_type` ecil_refs.cpp:107,111,115 (and the TY_FUNC loop :482-489) |
| unknown sid (`string_for` types.cpp:130) | `need_sid` ecil_refs.cpp:84,89 |
| no declaration object for a GREF/C_ADDR/CALL/NAME_BIND/VAR_INIT/FUNC_BEGIN G-id (globals.cpp:71,85) | ecil_refs.cpp:673,768,793,422,571,589 |
| unknown / dead L-id (`local_at` types.cpp:210,226) | ecil_refs.cpp:659,663 |
| type id operand not unsigned (types.cpp:329) | `check_record_operands` ecil_decode.cpp (operand classes) |
| TY_RECORD_BODY out of step (types.cpp:519), REC_FIELD outside a body (:554) | ecil_refs.cpp:515,518,387,530 |
| bit-field member of a non-integer type (types.cpp:578) | ecil_refs.cpp:541 |
| frame end: record fields missing (function.cpp:122), body still open (:125) | ecil_refs.cpp:367; ecil_stream.cpp:295 |
| bracket mismatch (`top_frame` function.cpp:161; FUNC_END/FOR_* out of order :530,778-836; case outside a switch :876; AGG_SEL outside AGG_INIT :894; C_AGG_END / C_SEL outside C_AGG :850,876) | ecil_brackets.cpp:179-278,298,347,363,369; lockstep check function.cpp:108 |
| function defined twice (function.cpp:340) | ecil_refs.cpp:592 |
| PARAM / LOCAL / MATERIALIZE_TEMP of an incomplete record (function.cpp:394,465; expr.cpp:1365) | `need_object` ecil_refs.cpp:159 |
| DECL_FUNC type not a function type (globals.cpp:281), DECL_VAR type not an object type (:364), DECL_VAR definition of an incomplete record (:366), re-issue changes the kind (:307,414) | ecil_refs.cpp:258,263,159,271 |
| VAR_INIT of an incomplete record (globals.cpp:465) | ecil_refs.cpp:579 |
| FIELD type/index (expr.cpp:217,219), AGG_SEL target (:265) | ecil_refs.cpp:679,685,694 |
| ADDR_OFFSET result type not a pointer (expr.cpp:1332) | ecil_refs.cpp:775 |
| C_SEL index past the array/union/record (globals.cpp:702,733,774), C_AGG_BEGIN type (:836) | `check_selector` ecil_refs.cpp:337,346; :717 |

**Checks that stay format errors** (no validator counterpart; they depend on
CIR types or the value/constant stacks): value-stack underflow and
glvalue/prvalue class (expr.cpp:45-85), cast and arithmetic type rules, CALL
type not a function type / incomplete argument types, CALL_INDIRECT callee,
argument counts against the value stack (bounded before any allocation,
expr.cpp:570,1570), PARAM placement, count and type (function.cpp:397-421),
"a function's type changed after its body was emitted" (globals.cpp:309),
constant-stack sequencing and constant type rules (globals.cpp:665-985),
`a stack is not empty at the end of a frame` (function.cpp:128). The ctest
`bad_fixtures_edg_cir` runs every `bad_*` fixture through edg-cir and
requires status FORMAT or VERSION, never INTERNAL.
