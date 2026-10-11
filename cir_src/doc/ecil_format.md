# ECIL — EDG CIR IL stream format

Status: **v1.0**, frozen at the M3 gate (2026-10-11). Version 1.0 is C (sections 1–10) plus core C++ (section 11). Every rule that 0.x introduced "from minor 7" applies unconditionally, and 0.x pre-release streams are rejected by the major-version check (section 9). History of the 0.x minors: (header minor 8 declares every C++ (v1) record and flag, specified in section 11; minor 7 makes the `FOR_BEGIN` init scope span the whole loop (8.3, 8.4) and adds these rules: for streams of every minor, the reader id limit and effective-name ownership (7); for streams of minor 7 or later, writer density (7), scope regions, no nested function bodies (8.3) and case-label placement (8.4); minor 6 adds the float-classification builtins and the `CMP` kinds `one`/`uno`; minor 5 adds builtin ids; minor 4: adds the clarifications made during M1, namely END n_frames, HDR_LANG std, one G-id per symbol, feature-gated records, CALL result, and the layout rules). Owner: the
orchestrator.

- **Normative source.** The definitions live in
  `cir_src/include/edg_cir/ecil_format.h`; this document explains them.
- **Precedence.** This document and the header win over anything in
  `research/`. Where a research document disagrees, follow the errata in
  `design.md` section 8.
- **Changes** go through the orchestrator and bump the version (section 9).

ECIL is a forward-only, streamable, tag-encoded serialization of the
**non-lowered** EDG IL. It carries what the CIR back end needs to emit C
(v0) and later C++ (v1, v2). It is inspired by MessagePack, but trades
compactness for decode speed. The writer is `src/ecil_write.c`; the readers
are `cir_src/lib/format` and `cir_src/lib/cirgen`.

## 1. Design rules

1. **No pointers or host addresses.** Every cross-reference is a small
   integer ID assigned by the writer.
2. **Forward-only.** The writer never seeks. Every entity is introduced
   before its first use. Two exceptions:
   - names may be bound late (`NAME_BIND`);
   - declarations may be re-issued to update them (8.2).
3. **Frames are the unit of hand-off.** A frame is length-prefixed and
   self-contained for decoding. A consumer can process frame N while the
   producer is still writing frame N+1.
4. **No recursion is needed to decode.**
   - Arrays are flat: they hold scalars only.
   - Expressions are post-order (a value stack).
   - Constructs that have control flow or regions are bracketed with
     BEGIN/…/END records (a frame stack).
5. **Optional information is skippable.**
   - Supplementary blobs (`supp`) and `EXT` records carry byte lengths.
   - Frame kinds 0x80–0xFF are skippable extensions.
   - Presence bits: ignorable stream feature bits (section 3), the frame
     flag `HAS_SUPP` (section 4), and the supp tag itself (section 6).
6. **Little-endian, fixed width.** Readers accept any integer width;
   writers should use the shortest encoding.
7. **Deterministic.** The same input and options give a byte-identical
   stream. The writer never iterates a pointer-keyed table to decide
   emission order; it uses insertion order.

## 2. Stream layout

```
stream  := preamble frame* (end_frame | abort_frame)
preamble:= "ECIL" 0x0D 0x0A 0x1A 0x0A   (8 bytes: detects text-mode damage)
           u16 major | u16 minor | u32 features          (16 bytes in total)
frame   := u32 payload_len | u8 kind | u8 flags | u16 reserved(=0) | payload
```

- The first frame must be `F_HEADER`.
- The last frame is `F_END`, or `F_ABORT` if the front end hit an error.
  After `F_ABORT` the consumer discards everything and reports failure.
- A payload contains whole records only.
- **Empty stacks (F4).** The value, constant, initialization-target and
  bracket stacks are empty at every frame end. A writer that abandons a
  partly written sequence truncates its buffer to a mark taken before the
  sequence.

## 3. Feature bits (`u32 features` in the preamble)

- **Bits 0–15 are ignorable.** A reader that does not know one proceeds.
- **Bits 16–31 are required.** A reader that does not know one rejects the
  stream with `EDG_CIR_ERROR_VERSION`.

| Bit | Name | Class | Meaning when set |
| --- | --- | --- | --- |
| 0 | `LOCATIONS` | ignorable | `LOC` records may appear |
| 1 | `SUPP` | ignorable | Some frames carry supp blobs (they are also flagged per frame) |
| 2 | `REFS` | ignorable | `F_REGION` frames end with a `REFS` record |
| 3 | `LATE_NAMES` | ignorable | `NAME_BIND` records may appear |
| 4–15 | — | ignorable | Reserved; 0 in v0 |
| 16–31 | — | required | Reserved; 0 in v0 |

A record whose feature bit is clear is a format error. That covers `LOC`
without `LOCATIONS`, `REFS` without `REFS`, and `NAME_BIND` without
`LATE_NAMES`. So is a frame with `HAS_SUPP` when the `SUPP` feature bit is
clear.

## 4. Frames

| Kind | Name | Contents |
| --- | --- | --- |
| 0x01 | `F_HEADER` | `HDR_*` records, plus any `TY_*` records they need |
| 0x02 | `F_DECLS` | Type and declaration introductions not tied to a body |
| 0x03 | `F_REGION` | One EDG function memory region: a declaration section, then a body section with one or more `FUNC_BEGIN…FUNC_END` (a function and its lambdas) |
| 0x04 | `F_GLOBALS` | File-scope variable definitions and initializers, and final re-issued declarations |
| 0x05 | `F_NAMES` | Late `NAME_BIND` records |
| 0x0E | `F_ABORT` | Front-end error; the stream is invalid |
| 0x0F | `F_END` | The `END` record |
| 0x80–0xFF | extension | Skipped if unknown |

A reader rejects unknown frame kinds below 0x80.

- **Frame flags.** Bit 0 is `HAS_SUPP`. When it is clear, the reader never
  looks for supp blobs in that frame (the fast path). Bits 1–7 are reserved
  and must be 0.
- **Declaration section.** It holds introductions only: `STR`, `TY_*`,
  `REC_FIELD`, `DECL_*`, `C_*` and `VAR_INIT`, `NAME_BIND`, `RETRACT`,
  `HDR_TYPE_ROLE`, `LOC`, `EXT`. In `F_REGION`, the body section follows
  and starts at the first `FUNC_BEGIN`. The writer keeps two buffers and
  submits them together, so every G-id a body uses is introduced earlier in
  the same frame or in an earlier frame.

## 5. Records and operands

A record is one opcode byte, then the opcode's fixed number of operands
(the `ECIL_OPCODES` table), then, only in a frame with `HAS_SUPP`, an
optional supp blob.

| Tag | Format | Payload |
| --- | --- | --- |
| 0x00–0x7F | fixuint | value 0–127 |
| 0x80–0x9F | fixbin | 0–31 bytes |
| 0xA0–0xBF | fixarray | 0–31 scalar elements |
| 0xC0 | nil | — |
| 0xC1 | poison | never valid (desync detector) |
| 0xC2 / 0xC3 | false / true | — |
| 0xC4 / 0xC5 / 0xC6 | bin8 / bin16 / bin32 | u8/u16/u32 length, then bytes |
| 0xC7 / 0xC8 / 0xC9 | supp8 / supp16 / supp32 | u8/u16/u32 length, then the supp payload (section 6) |
| 0xCA / 0xCB | f32 / f64 | IEEE-754 bits |
| 0xCC–0xCF | u8 / u16 / u32 / u64 | — |
| 0xD0–0xD3 | i8 / i16 / i32 / i64 | — |
| 0xD4 / 0xD5 | u128 / i128 | 16 bytes |
| 0xD6 | fbits | u8 n, then the n-byte target float bit pattern as a little-endian integer (x87 80-bit: n=10; binary128: n=16) |
| 0xDC / 0xDD | arr16 / arr32 | u16/u32 element count, then scalar elements |
| 0xE0–0xFF | negfixint | −32 … −1 |
| others | — | reserved; invalid |

- **Reserved opcode values.** 0x00, 0xC0, 0xC1 and 0xC7–0xC9 are never
  assigned as opcodes. A desync that lands on nil or poison is therefore
  detected, and a C7–C9 byte after a record's operands can only be that
  record's supp blob.
- **Booleans.** An operand documented as a bool is written as false/true
  (C2/C3). Readers also accept fixuint 0/1.
- **Flags.** A `flags` operand with no defined bits must be 0. A reader
  rejects unknown bits in flags operands of the current major version.

## 6. Supplementary blobs

A supp payload is a sequence of `key value` pairs. Every key has **exactly
one** value, so an unknown key can always be skipped:
- the key is a tagged unsigned integer;
- the value is one tagged scalar or bin;
- flag-like keys use `true`.

The keys are `an_ecil_supp_key` in `ecil_format.h`. v0 writers emit none,
and v0 readers skip them all (they are consumed from M5 onward).

| Key | Value |
| --- | --- |
| 1 `nothrow` | true |
| 2 `branch_hint` | 0 = unlikely, 1 = likely |
| 3 `typedef_name` | sid |
| 4 `pure` | true |
| 5 `const` | true |
| 6 `align_assumed` | uint |
| 7 `nrvo_var` | lid |
| 8 `readable_name` | sid |
| 9 `section` | sid |
| 10 `visibility` | uint |
| 11 `used` | true |
| 12 `explicit_align` | uint |

## 7. Identifiers

- **Three ID spaces.**
  - **G-ids** (types, functions, variables, records) are dense from 1 and
    global to the stream.
  - **L-ids** (parameters, locals, temporaries, labels) are dense from 1
    and reset at every `FUNC_BEGIN`.
  - **sids** (strings) are global and introduced by `STR(sid, bin)`. The
    library copies any string it keeps.
  - In every space, 0 or nil means "none". Every id is below 2^32.
- **Density (minor 7; a writer MUST).**
  - A writer introduces G-ids and sids densely and in increasing order.
    Each new id is the highest id introduced so far plus 1.
  - The introduction record (`TY_*`, `DECL_*`, `STR`) is emitted in the
    declaration section of the frame that assigns the id, at the moment the
    id is assigned.
  - L-ids are dense in the same way within a function. The one exception: a
    forward `GOTO_STMT` may name a label L-id before its `LABEL` record.
  - **Reader limit (all minors).** A reader may reject any id above
    `2 * introduced + 1024`, where `introduced` is the number of ids of that
    space introduced (or, for L-ids, referenced) so far. This is a resource
    limit, not a format rule: it bounds the reader's tables by the stream
    size, so it applies to streams of every minor. Every writer since minor
    0 has been dense.
  - **Writer check.** In CHECKING builds the writer asserts at `F_END` that
    every id it allocated was introduced.
- **Names.**
  - A declaration carries its final mangled (or C) name sid when it is
    known. Otherwise it carries nil, and `NAME_BIND(g, sid, linkage)`
    supplies the name later. Until then the consumer uses the placeholder
    `__ecil.<gid>`.
  - A definition still unnamed at `F_END` is a format error.
  - **Effective name.** An entity's *effective* name is the name of its
    latest `DECL_*` or `NAME_BIND` that carried a non-nil name.
    - A re-issue with a nil name keeps the effective name.
    - A re-issue with a different non-nil name renames the entity.
    - The consumer applies every rename at finish, so all earlier uses
      follow the entity.
  - **One G-id per symbol.** Two distinct G-ids must never have the same
    effective external (non-internal) name.
    - The writer deduplicates by name; for example, `__builtin_memcpy` sent
      as a library call reuses the G-id of `memcpy`.
    - A reader checks ownership after every `DECL_*` and `NAME_BIND`, on the
      effective name and linkage, and releases the old name when an entity
      is renamed or becomes internal. A duplicate is a format error.
    - Internal entities may share a name with each other or with an
      external entity. The consumer makes internal symbols unique at finish
      and never renames an external one.
  - Label names (`LABEL`) are hints only. The consumer makes them unique
    within the function.
- **Stretch goal (ODR).** Every non-internal entity ends up with its
  Itanium-mangled name, so G-ids from several streams can be resolved by
  name.

## 8. Opcodes (v0.2 = C subset)

The table of record is `ECIL_OPCODES(X)` in `ecil_format.h`: name, value and
arity.

**Notation**
- Operands: `t` a type gid, `g` a gid, `l` an lid, `s` a sid.
- Stack entries: "glv" a glvalue (address), "prv" a prvalue, "bool" a
  `!cir.bool` prvalue.
- Aggregate prvalues are values of the record or array type. The consumer
  may fuse `LOAD`+`STORE` into `cir.copy`.
- `C_*` records push onto a separate **constant stack**.

**Alignment.** Accesses carry no alignment operand; the consumer derives
it. A `LREF`/`GREF` base takes the `LOCAL`/`DECL_VAR` alignment (or the
type's). `DEREF` uses the pointee type's alignment. `FIELD` reduces it by
the member offset and the record's alignment, which handles packed
records.

### 8.1 Meta and header

| Record | Meaning |
| --- | --- |
| `STR(s, bin)` | Interns a string. |
| `LOC(s\|nil file, line, col)` | Sets the location for the records that follow (optional). |
| `NAME_BIND(g, s, linkage)` | Binds a late name. |
| `RETRACT(g)` | Drops a streamed definition if it is unreferenced (C++). |
| `EXT(kind, bin)` | An opaque extension, skipped if unknown. |
| `REFS(array g)` | The G-ids this frame references (optional). |
| `END(n_frames, status)` | Only in `F_END`. `n_frames` is the number of frames before `F_END`, `F_HEADER` included. `status` 0 = ok. |
| `HDR_TARGET(s triple, s\|nil target_abi, s\|nil datalayout, ptr_bits, reg_bits, flags)` | Flags: big-endian, char signed, cheap unaligned bit-field access. `target_abi` is `""` for plain x86-64 SysV. |
| `HDR_LANG(lang, std, flags)` | `lang`: `elang_c` or `elang_cxx`; `std`: the `__STDC_VERSION__` or `__cplusplus` value the FE would predefine (e.g. 201112, 201703). Flags: wrapv, exceptions, rtti, threadsafe statics. |
| `HDR_PRODUCER(s producer, s\|nil source_file)` | |
| `HDR_TYPE_ROLE(role, t)` | Allowed in any declaration section. Roles: `size_t`, `ptrdiff_t`, `int`, `va_list`, `wchar_t`, `char`. On x86-64 `va_list` is an array of 1 `__va_list_tag` record with exactly 4 fields. |

### 8.2 Types and declarations (declaration sections only)

Each G-id is introduced once.

| Record | Notes |
| --- | --- |
| `TY_VOID(g)` | |
| `TY_BOOL(g, size, align)` | |
| `TY_INT(g, bits, signed, size, align)` | Enums use their underlying type. |
| `TY_FLOAT(g, fmt, size, align)` | `fmt` is an `an_ecil_float_format`. |
| `TY_PTR(g, t)` | The pointee may be an incomplete record. ECIL types have no qualifiers. |
| `TY_ARRAY(g, t, count\|nil, flags{incomplete, zero_length})` | |
| `TY_FUNC(g, t ret, flags{variadic, no_proto}, array t params)` | For a prototyped type, `params` are the declared (adjusted) parameter types. For `no_proto`: empty for a declaration, or the default-promoted parameter types of a K&R definition. |
| `TY_RECORD_DECL(g, kind, s\|nil)` | An incomplete identified record. |
| `TY_RECORD_BODY(g, size, data_size, align, flags{packed, flexible_array}, n)` followed by `n` × `REC_FIELD(t, byte_off, bit_off\|nil, bit_width\|nil, s\|nil, flags{unnamed, empty_for_abi, bitfield_signed, flexible_array})` | Completes the record, in any later frame but before the first use that needs it complete. The FE values are authoritative; the consumer builds CIR padding and bit-field storage units from them. `FIELD`, `EXTRACT`, `AGG_SEL` and `C_SEL` use the position in this list. |
| `TY_OPAQUE(g, size, align)` | A type ECIL v0 cannot represent (`_Complex`, vector, `_BitInt`, …). It is laid out as `size` bytes with alignment `align`, so records containing it keep their layout. Any value-level use is diagnosed by the writer (8.11). |
| `DECL_FUNC(g, t, linkage, flags{defined_here, inline, noreturn, is_main, inline_only, unsupported}, s\|nil)` | |
| `DECL_VAR(g, t, linkage, flags{definition, const, tls, string_literal, static_local, tentative, unsupported}, align, s\|nil)` | |
| `VAR_INIT(g)` | Pops one constant (built by the `C_*` records just before it) as the initializer. A definition with no `VAR_INIT` is zero-initialized. |

**Layout rules the writer must follow** (needed to reproduce clang's CIR
record layout; verified on 69 C records):
- **Zero-width bit-fields.** A zero-width bit-field (`int : 0;`) is sent as
  an unnamed `REC_FIELD` with `bit_width` 0 at its byte offset. It ends the
  current bit-field storage unit.
- **Zero-size members.** Zero-length arrays and zero-size record members
  carry `empty_for_abi`.

- **Re-issued declarations (R8).** A `DECL_FUNC`/`DECL_VAR` whose `g` is
  already known updates that entity's type, linkage, flags and (if non-nil)
  name.
  - **Writer:** it keeps a G-id-indexed table of the last emitted
    (type, linkage, flags, name). It re-issues before any record that
    depends on the new value. In particular it re-issues `DECL_FUNC` before
    `FUNC_BEGIN` when the definition's type or flags differ from the first
    reference (unprototyped use, then a prototyped or K&R definition). It
    also re-issues `DECL_VAR` before the definition or `VAR_INIT` of a
    completed array.
  - **Consumer:** it retypes the symbol and fixes every earlier use. A
    `get_global` result type must equal the symbol type, so each use gets a
    `get_global` of the new type plus a pointer `cast bitcast` to the old
    type, or is rewritten. This is clang's replace-and-bitcast strategy.
  - **Direct calls (F3).** An earlier direct `cir.call @g` whose callee
    type no longer matches is converted to the variadic-bitcast form of
    8.8.
  - **No recursive symbol walks during consume (F3).** The consumer keeps a
    per-G-id list of the ops it created that use the symbol, or it defers
    the fixups to finish, which runs on the worker. It never calls
    `SymbolTable::getSymbolUses` or `replaceAllSymbolUses` on the calling
    thread.
- **Static locals and string literals** are defined (`DECL_VAR` with
  `definition`, plus `VAR_INIT`) in the declaration section of the frame
  that first needs them. `static_local` is informational: the consumer does
  not put the CIR `static_local` attribute on unguarded C statics. String
  literals are `private` constants with unique names, deduplicated by
  (type, bytes) in insertion order.
- **Tentative definitions** get `DECL_VAR(definition|tentative)` in
  `F_GLOBALS`, without `VAR_INIT` (zero-initialized; no `common` in v0).

### 8.3 Function bodies (R1)

```
FUNC_BEGIN(g, flags=0)
  PARAM(l, t_abi, t_decl, s|nil, flags=0)*   -- the consumer allocas t_decl
                                                and stores cast(arg); for K&R
                                                t_abi is the promoted type
  <statements>
FUNC_END(flags{end_reachable})
```

When `end_reachable` is set and control falls off the end:

| Case | Consumer emits |
| --- | --- |
| `is_main` | `return 0` |
| `void` function | `return` |
| C (`HDR_LANG` is C), non-void | Load the uninitialized `__retval` and `cir.return` it. C11 6.9.1p12 only makes this undefined if the caller uses the value. Never `unreachable`. |
| C++, non-void | `cir.trap` + `unreachable`, as clang does |

A `RETURN_STMT(false)` in a non-void C++ function (`return;`, a GNU
extension the front end accepts) returns the uninitialized `__retval`, as in
C.

**Locals and temporaries**
- `LOCAL(l, t, s|nil, align, flags=0)` declares an automatic variable at
  that point of the body, in the current block. It may appear between
  expression records, because it does not touch the value stack.
- `MATERIALIZE_TEMP(l, t)` creates temporary `l` in the current block,
  stores the popped prvalue and pushes its glvalue.
- **Placement.** The consumer may place each alloca at function entry, and
  the v0 library does so. Alloca placement is the consumer's choice, but the
  scope rule below is normative.
- **Scope regions (minor 7).** A `LOCAL` or `MATERIALIZE_TEMP` belongs to
  the innermost open *scope region*:
  - the function body;
  - a `BLOCK_BEGIN … BLOCK_END`;
  - the `FOR_BEGIN … FOR_END` init scope;
  - an `IF_BEGIN` or `IF_ELSE` arm;
  - a loop body (`WHILE_BODY … WHILE_END`, `DO_BEGIN … DO_COND`,
    `FOR_BODY … FOR_END`);
  - the `SWITCH_BEGIN … SWITCH_END` body.

  Referencing it after its scope region has closed is a format error. In
  C++ (v1) the scope region is also where its destruction happens; v1 adds
  more scope regions (11.5).
- **Function bodies are never nested (minor 7).** A `FUNC_BEGIN` before the
  previous `FUNC_END` is a format error. An `F_REGION` with several
  functions (a function and its lambdas) has their bodies one after
  another.
- **For-init variables (minor 7).** `FOR_BEGIN` opens a scope that lasts
  until the matching `FOR_END`. The init statements, including `LOCAL`s,
  follow `FOR_BEGIN` directly, with no `BLOCK` wrapper. Their variables are
  in scope in the condition, the step and the body. Before minor 7 the writer
  wrapped them in a `BLOCK` that closed before `FOR_COND`. A minor-7 reader
  treats a `LOCAL` declared in a closed `BLOCK` and used after it as a format
  error. C++ (M3) relies on this rule, because the end of the init scope is
  where for-init objects are destroyed.

### 8.4 Statements

The value stack is empty between statements. An expression statement is
`<expr>` followed by `DROP` if the expression produced a value.

| Construct | Records |
| --- | --- |
| block | `BLOCK_BEGIN … BLOCK_END` |
| if | `<cond> TO_BOOL IF_BEGIN(has_else) <then> [IF_ELSE <else>] IF_END` |
| while | `WHILE_BEGIN <cond> TO_BOOL WHILE_BODY <body> WHILE_END` |
| do | `DO_BEGIN <body> DO_COND <cond> TO_BOOL DO_END` |
| for | `FOR_BEGIN <init stmts> FOR_COND(has) [<cond> TO_BOOL] FOR_STEP(has) [<step expr> DROP?] FOR_BODY <body> FOR_END` |
| switch | `<cond> SWITCH_BEGIN <body> SWITCH_END`, with `CASE_VALUE(v)`, `CASE_RANGE(lo, hi)` and `CASE_DEFAULT` inline at label positions |
| jumps | `BREAK_STMT`, `CONTINUE_STMT`, `RETURN_STMT(has_value)`, `GOTO_STMT(l, n_cleanups)`, `LABEL(l, s\|nil)`, `UNREACHABLE` |

Notes on the rows:
- **for:** `FOR_BEGIN` opens an implicit scope that holds the init
  statements. The scope stays open over `FOR_COND`, `FOR_STEP` and
  `FOR_BODY` and closes at `FOR_END` (minor 7).
- **switch:** the condition is already promoted.
- **jumps:** `RETURN_STMT` pops a value that is already converted. Label
  lids share the frame's L-id space. `GOTO` may jump into or out of
  blocks; C has no cleanups, so `n_cleanups` is 0.

**Switch strategy.** A case label may sit directly in the switch body or
inside nested `BLOCK`s in it, but never inside another statement bracket
(`IF`, a loop). A case label inside a nested `SWITCH` belongs to that inner
switch. The writer diagnoses Duff's device. Readers enforce this from
minor 7. The
consumer emits each label as a *marker* case: a `cir.case` with an empty
region ending in `cir.yield`, at the current insertion point inside the
`cir.switch` body. The labelled statements follow in the enclosing region.
FlattenCFG turns the case region's `yield` into a branch to the op after
the case (`FlattenCFG.cpp:418-433` at the pin), which gives C fallthrough.
cir-lib validates this in M2 with switch, fallthrough, default, range and
nested-block tests. If it is rejected, cir-lib falls back to sibling
top-level cases and reports the result to the orchestrator.

### 8.5 Object initialization

| Record | Effect |
| --- | --- |
| `INIT_CONST` | Pops one constant and the target glv. Stores the constant; for aggregates, unlisted parts are zero. |
| `INIT_ZERO` | Pops the target glv and zero-fills it. |
| `AGG_INIT_BEGIN(t, flags{zero_fill})` | Pops the target glv and makes it the current initialization target (a target stack). |
| `AGG_SEL(idx)` | Pushes the glv of member or element `idx` of the current target. It is followed by a value and `STORE(…, none)`, or by `INIT_CONST`, `INIT_ZERO`, or a nested `AGG_INIT_BEGIN`. |
| `AGG_INIT_END` | Pops the target stack. |

### 8.6 References, memory and values

| Record | Stack effect |
| --- | --- |
| `LREF(l)`, `GREF(g)` | → glv. A `GREF` of a function is a function designator; it may also be used as a prv function pointer. |
| `LOAD(flags{volatile})` | glv → prv. On a bit-field glv this becomes `cir.get_bitfield`. |
| `STORE(flags{volatile}, result)` | glv prv → nothing, or the glv, or the stored prv (truncated for bit-fields). |
| `FIELD(t_rec, idx)` | glv → glv. The result is a bit-field glv when the member is a bit-field. `p->f` is `<p> DEREF FIELD`. |
| `EXTRACT(t_rec, idx)` | prv record → prv member |
| `DEREF` | prv ptr → glv |
| `ADDR` | glv → prv ptr. This is `&x`; the writer always emits it for address-of. |
| `ADDR_OFFSET(t_ptr, bytes)` | prv ptr → prv `t_ptr`, offset by `bytes` bytes |
| `DUP`, `DROP` | |
| `CONST_INT(t, int)`, `CONST_FLOAT(t, float)`, `CONST_NULL(t)`, `CONST_PTR(t, int)` | → prv |

**Float values.** `float` operands are f32 (IEEE single), f64 (IEEE double)
or `FBITS` (every other format, e.g. x87 80-bit). The writer produces
`FBITS` from the FE's internal value. It diagnoses a `long double` constant
it cannot convert exactly on its host, which can happen when host
`long double` is not the target format and softfloat is off.

### 8.7 Operators (operands already converted to a common type)

**Arithmetic and bitwise**
- `ADD SUB MUL DIV REM SHL SHR BIT_AND BIT_OR BIT_XOR` with
  `flags{nsw, nuw, exact}` [prv prv → prv]. Signedness and floating point
  come from the operand type.
- `nsw` is set only for signed integer operations of rank `int` or wider
  when `-fwrapv` is off.

**Comparisons and unary operators**
- `CMP(kind)` [prv prv → bool]. In C, the writer follows it with
  `CAST(bool_to_int, int)`. Peephole: when a `CMP`, `LOG_NOT` or `LAND`/
  `LOR` result feeds a `TO_BOOL` directly, the writer drops both the cast
  and the `TO_BOOL`.
- `NEG(flags)`, `BIT_NOT` [prv → prv]; `LOG_NOT` [bool → bool].
- `TO_BOOL` [scalar → bool]. It is a no-op on a bool.

**Pointers**
- `PTR_STRIDE(flags{negate, index_unsigned})` [prv ptr, prv int → prv ptr],
  scaled by the pointee.
- `PTR_DIFF(t_result)` [ptr ptr → prv], divided by the pointee size.

**Increment and decrement (R5)**
- `INC_DEC(kind, flags{nsw, volatile})` [glv → prv]. The result is the new
  value (pre) or the old value (post) in the operand's type.
- On a pointer it moves by one element.
- On an integer narrower than `int` the operation wraps (C does it in
  `int` and converts back), so the writer never sets `nsw` there.
- On a bit-field glv: get, operate, set.
- `_Bool` operands are diagnosed unsupported in v0.

**Compound assignment** is expanded by the writer:
`<addr> DUP LOAD CAST… <rhs> CAST… <op> CAST… STORE(volatile?, result)`.

### 8.8 Calls, casts, varargs and builtins (R2)

| Record | Stack effect |
| --- | --- |
| `CALL(g, t_call, nargs, flags{variadic_callee, noreturn, result_unused})` | args… → result (nothing if void) |
| `CALL_INDIRECT(t_call, nargs, flags)` | callee prv ptr, args… → result |
| `CAST(kind, t)` | prv → prv (`an_ecil_cast_kind`) |
| `VA_START` | pops one prv `ap` (`!cir.ptr<__va_list_tag>`, the decayed `va_list`); there is no "last parameter" operand |
| `VA_ARG(t)` | prv ap → prv |
| `VA_END` | prv ap → |
| `VA_COPY` | prv dst, prv src → |
| `BUILTIN(id, nargs, t\|nil)` | args… → result. v0 ids: `expect`, `unreachable`, `trap`, plus (minor 5) `alloca`, `bswap`, `clz`, `ctz`, `popcount`, `parity`, `ffs`, `clrsb`, and `add`/`sub`/`mul_overflow`, which take operands of any integer types and a result pointer (C23/GCC semantics). Signatures are in `an_ecil_builtin`. `__builtin_c23_va_start(ap, …)` is sent as `VA_START`. Minor 6 adds:
- **`is_fp_class(x, mask)`** → bool, with a constant mask of
  `ECIL_FP_CLASS_*` bits (LLVM FPClassTest). The writer expands `isnan`,
  `isinf`, `isfinite`, `isnormal`, `issignaling` and `fpclassify` onto it,
  using `TERN` for `fpclassify`.
- **`signbit(x)`** → bool.
- **`CMP` kinds `one` and `uno`.** `isless`/`isgreater`/`islessequal`/
  `isgreaterequal` use the ordered `lt`/`gt`/`le`/`ge`, `islessgreater`
  uses `one`, and `isunordered` uses `uno`. Library builtins (`__builtin_memcpy`, `__builtin_printf`, `__builtin_abort`, …) are sent as ordinary `CALL`s to the library function. |

**Call-site type.** `t_call` is the function type at the call site, taken
from the callee expression's type, not from the callee declaration. The
consumer's rule:
- If `t_call` is prototyped and equals the `cir.func` type of `g`, emit a
  direct `cir.call`.
- Otherwise, i.e. for a `no_proto` call type, an arity mismatch, or a
  callee declared differently: `get_global @g` (or the indirect callee),
  `cast bitcast` to `!cir.ptr<!cir.func<(...) -> ret>>`, a variadic type
  with zero fixed parameters, then an indirect `cir.call` with all
  arguments as variadic extras. This is clang's approach on x86-64. It
  makes CallConvLowering classify the call from its operands and set `%al`.
  It never sends a `no_proto` callee type to the pass, which would fail or
  assert at the pin.

A non-void `CALL` always pushes its result, whether or not `result_unused` is set; the writer emits `DROP` when the value is unused. `result_unused` is only a hint.

Arguments arrive already default-promoted and cast. Record arguments and
results are record prvalues; CallConvLowering applies the SysV ABI.

### 8.9 Bracketed expressions

- `<cond bool> TERN_BEGIN(t|nil) <then> TERN_ELSE <else> TERN_END` → result.
  `t` is nil for void, never the `TY_VOID` gid. Each arm leaves exactly one
  value (none for void).
- `<lhs bool> LAND_BEGIN <rhs bool> LAND_END` → **bool** (likewise
  `LOR_*`). The consumer never adds `bool_to_int`. In C the writer emits
  `CAST(bool_to_int, int)` after `*_END`, subject to the 8.7 peephole.

### 8.10 Constant initializers (constant stack)

| Record | Effect |
| --- | --- |
| `C_INT(t, int)`, `C_NULL(t)`, `C_ZERO(t)`, `C_PTR(t, int)` | push |
| `C_FLOAT(t, float)` | push; same encoding as 8.6 |
| `C_STRING(t, bin)` | Bytes for array type `t`; any tail is zero-padded. |
| `C_ADDR(t, g, bytes)` | `&global + bytes` or a function address (`#cir.global_view` / `#cir.global_offset`). |
| `C_AGG_BEGIN(t) { C_SEL(idx) <one constant> \| C_REPEAT(idx, count) <one constant> }* C_AGG_END` | Pushes one aggregate. Unlisted members and elements are zero; a union has at most one entry. For bit-field members the consumer packs the bits into storage units. |

`C_*` records may appear in declaration sections (feeding `VAR_INIT`) and in
bodies (feeding `INIT_CONST`).

### 8.11 Unsupported constructs (R4, F1, F2, F5)

**Function bodies.** When the writer meets a construct v0 cannot express
inside a body:
- **Body dropped, declarations kept.** It discards only that function's
  body records, from its `FUNC_BEGIN` through its `FUNC_END`. The frame's
  declaration section is always submitted, so every G-id and sid assigned
  so far reaches the consumer and writer and consumer stay in step.
- **Declaration re-issued.** It re-issues `DECL_FUNC` without
  `defined_here` and with `unsupported` set.
- **Warning.** The FE issues a warning naming the function and the
  construct.

**Variable initializers (F1).** When a variable's initializer cannot be
expressed:
- **No zero-init fallback.** The writer never omits `VAR_INIT` and lets the
  variable be zero-initialized.
- **File-scope variable.** The writer re-issues `DECL_VAR` without
  `definition` and with `unsupported`, and warns.
- **Static local.** The owning function becomes unsupported, as for a body.

**Opaque types (F2).** A type v0 cannot express (`_Complex`, vectors,
`_BitInt`, …) is introduced as `TY_OPAQUE`, so records containing it keep
their layout. A record or union that contains opaque storage at any depth
is **ABI-opaque**. Any by-value use of it as a call argument or result, a
parameter or return type, or a `VA_ARG` type is diagnosed, because
CallConvLowering would classify the raw bytes wrongly. Copying such a
record (`LOAD`+`STORE`) is allowed. Every value-level use of an opaque
scalar is diagnosed.

**Consumer, at finish (F5).** These rules apply when any entity carries
`unsupported`:
- **Default.** A dropped definition is an error,
  `EDG_CIR_ERROR_UNSUPPORTED`, naming each entity, so tests report the
  unsupported construct rather than a later link failure. One exception
  (user decision, 2026-10-11): a dropped *function* definition with
  discardable linkage (internal or `linkonce_odr`) that nothing references at
  finish is erased with a warning. Dropped variables stay errors, because
  their initialization may have side effects even when nothing refers to
  them. No TU can ever need it, so failing would only
  reject code that is never used: an unused `static inline` header function,
  or an uncalled library inline function. A referenced one, or one with
  external or `weak_odr` linkage, is still an error. "Referenced" counts
  every use: calls, addresses, `C_ADDR` constants and aliases.
  - A dropped C++ inline function is re-issued as external (11.2), so
    "discardable" is decided by the linkage it was first introduced with.
    The consumer remembers `linkonce_odr`.
  - A dead, supported, discardable definition that uses an erased one is
    erased with it, until nothing more changes.
  - A STRUCTOR pair is live if either variant is referenced.
- **With `EDG_CIR_FLAG_KEEP_GOING`.**
  - External-linkage entities become declarations, with a warning.
  - An internal-linkage entity that is referenced is still an error.
  - An unreferenced internal-linkage entity is erased.
  - Internal-linkage declarations are never emitted.

## 9. Versioning

- **Major version** changes on removal or renumbering of any opcode or
  frame kind, any arity change, or any change of meaning. A reader rejects
  any major version but its own.
- **Minor version** may only add:
  - supp keys, `EXT` kinds, frame kinds 0x80–0xFF and ignorable feature
    bits;
  - opcodes in reserved ranges;
  - new values of enumerated operands (cast kinds, linkages, builtin ids,
    type roles, float formats, …);
  - new flag bits (F6).
- **Reader acceptance.** A reader accepts any minor version. It fails only
  on:
  - an unknown opcode ("stream needs ECIL minor ≥ N");
  - an unknown required feature bit;
  - an unknown frame kind below 0x80;
  - an unknown flag bit;
  - an unknown enumerated-operand value, also reported as "needs ECIL
    minor ≥ N".
- **v0.x streams were pre-release.** Writer and reader in one tree always
  matched. **1.0** (M3) is the first stable version: later additions are
  1.x minors under the rules above, for example exceptions (M4). A change of
  meaning needs major 2.

## 10. Worked example

`int f(int a,int b){ return a<b ? a+b : 0; }` as one `F_REGION` frame.
Parameter names are omitted. The `CMP`→`TO_BOOL` peephole (8.7) applies.

```
45 00 00 00 03 00 00 00     frame header: payload 69 bytes, F_REGION
-- declaration section --
01 01 81 66                    STR s1 "f"
12 01 20 C3 04 04              TY_INT g1 bits=32 signed=true size=4 align=4
16 02 01 00 A2 01 01           TY_FUNC g2 ret=g1 flags=0 params=[g1,g1]
30 03 02 00 01 01              DECL_FUNC g3 type=g2 external defined_here s1
-- body section --
40 03 00                       FUNC_BEGIN g3 flags=0
42 01 01 01 C0 00              PARAM l1 abi=g1 decl=g1 name=nil flags=0
42 02 01 01 C0 00              PARAM l2 abi=g1 decl=g1 name=nil flags=0
70 01 72 00                    LREF l1  LOAD
70 02 72 00                    LREF l2  LOAD
8A 00                          CMP lt                  [-> bool]
D0 01                          TERN_BEGIN g1           [pops bool]
70 01 72 00 70 02 72 00 80 01  LREF l1 LOAD LREF l2 LOAD ADD nsw
D1                             TERN_ELSE
7B 01 00                       CONST_INT g1 0
D2                             TERN_END                [-> int]
5C C3                          RETURN_STMT has_value=true
41 00                          FUNC_END flags=0
```

`ecil-asm` regenerates this example in M1 as a golden test.

## 11. C++ (v1)

ECIL 1.0 contains every record and flag in this section (`ecil_format.h`),
frozen at the M3 gate. During development these were 0.8–0.x minors.
Background is in `research/m3_ecil_v1_design.md` (§8 has the spike verdicts),
`research/cxx_cir_shapes.md` (CIR shapes at the pin) and
`research/il_to_ecil_map_cxx.md` (IL facts).

### 11.1 Language header and exceptions

- `HDR_LANG(elang_cxx, std, flags)`. The `exceptions` flag must be 0 in v1;
  a v1 reader rejects 1. Exception support (M4) adds it in a later version.
- **No-exceptions stream.** The front end may compile with exceptions on,
  which is the cpfe-cir default. The writer then sends a no-exceptions
  stream:
  - Every function and call is `nothrow`.
  - A function the writer would emit that contains `try`, `throw` or a
    function-try-block is diagnosed "unsupported" (8.11). Its body is
    dropped and its declaration kept.
  - Destructions that run only when an exception propagates (for example
    freeing storage when a constructor throws, or destroying a partially
    constructed aggregate) are sent as `CLEANUP_BEGIN(eh)`. A v1 consumer
    decodes them and drops them. These cases arise:
    - In a constructor, every base or member with a non-trivial destructor,
      and the target object of a delegating constructor, gets
      `CLEANUP_BEGIN(eh)` right after its `MEMINIT_END`, at function scope.
      It therefore protects the later initializers and the body.
    - Not sent in v1: freeing storage when the constructor in a `new`
      throws, and destroying a partially constructed array. M4 designs
      them with deactivatable cleanups (CIRGen deactivates rather than
      closes them, so they do not nest with the initializer's temporaries).
  - `olk_try_block` lifetimes are reached only through `try`, so they are
    never sent.
  - Exception specifications are ignored in v1.
- **Late names.** A C++ stream sets `LATE_NAMES` and has an `F_NAMES` frame,
  possibly empty, just before `F_END`.

### 11.2 Names and linkage (writer rules)

- **Mangling.** Every non-internal entity carries its Itanium-mangled name.
- **Internal and individuated entities** use Itanium names with no module id
  derived from file times or the directory, so names are deterministic
  (rule 1.7). This covers:
  - statics and anonymous-namespace members (`_ZL…`,
    `_ZN12_GLOBAL__N_1…`);
  - every entity the FE would otherwise individuate with a module id, such
    as members of unnamed classes (`Ut_`) and entities whose types involve
    anonymous-namespace types.
  These entities have internal linkage.
  - That also covers entities whose mangled name involves an internal or
    individuated entity, for example a template instance with an argument
    that is a local type or lambda of an internal function.
  - **Internal wins.** Internal or individuated linkage takes precedence
    over every vague-linkage rule below, including inline functions and
    the local statics of inline functions. Otherwise Itanium names, which
    are equal in every TU, would merge distinct internal entities across
    TUs.
- **Function-local statics** carry the local-entity name, for example
  `_ZZ1fvE1a` and `_ZZ1fvE1a_0`.
  - Linkage: a local static of a function that may exist in several TUs
    (inline, template instance, defined in its class) is `linkonce_odr`
    with `COMDAT`, and `GUARDED` when dynamically initialized. Otherwise it
    is internal.
- **Guard and lifetime-extension names** are derived from the variable's
  name. The consumer derives guard names and the writer derives `_ZGR`
  names.
  - If the name is mangled (it starts with `_Z`), the guard is `_ZGV`
    followed by the name without its `_Z` (`_ZZ1fvE1a` → `_ZGVZ1fvE1a`).
  - Otherwise the guard is `_ZGV<length><name>` (`iv` → `_ZGV2iv`).
  - Lifetime-extension temporaries use the Itanium rule
    `_ZGR <object name> [<seq-id>] _`: `_ZGR2sr_` for the first temporary of
    `sr`, then `_ZGR2sr0_`, `_ZGR2sr1_`, …, and `_ZGRZ2h2vE2lr_` for a local.
- **Late names.**
  - A declaration whose name may still change is sent with a nil name and
    bound by `NAME_BIND` in `F_NAMES`. Examples: a member of an unnamed class
    that later gets a typedef name for linkage purposes; a lambda in a
    default argument; members of nested anonymous unions.
  - The writer resolves such names only when it hands off the file scope.
  - CHECKING builds re-mangle every named non-internal entity at that point
    and assert that the name has not changed.
- **Definitions only.** `linkonce_odr`, `weak_odr` and `COMDAT` describe
  definitions. A declaration that is not defined in this TU, or whose
  definition is dropped as unsupported (8.11), is external and has no
  `COMDAT`.
- **COMDAT** follows the entity's final linkage, after any re-issue or
  `NAME_BIND`; a re-issue updates it. If a late name also changes the
  linkage to or from vague linkage, the writer re-issues the `DECL_*` with
  the new linkage and `COMDAT` bit in the `F_GLOBALS` declaration section
  before the `NAME_BIND`. `F_NAMES` itself holds only `NAME_BIND` records.
- **Vague linkage.** A routine that may exist in several TUs
  (`routine_might_exist_in_multiple_copies`, not `static`) is
  `linkonce_odr` with `COMDAT`. An explicit instantiation definition is
  `weak_odr` with `COMDAT`. Inline variables and template static data
  members are `linkonce_odr` with `COMDAT`, and `GUARDED` when they are
  dynamically initialized.

### 11.3 Types

- **References** are `TY_PTR`. Binding is `ADDR`; use is `LOAD DEREF`.
- **Non-static member functions.** `TY_FUNC` lists the `T*` object
  parameter first. `PARAM(l, T*, T*, "this", THIS)` comes first.
- **`TY_RECORD_BODY`:**
  - `data_size` is the Itanium dsize; it equals `size` for C.
  - `NONTRIVIAL_ABI`: the record is passed and returned indirectly. That
    holds if it is not bitwise copyable, if it has a deleted or non-trivial
    destructor, or if all its copy and move constructors are deleted
    (Itanium).
  - `NONTRIVIAL_DTOR`.
- **`REC_FIELD`:**
  - `BASE` marks a direct non-virtual base. Field order is fixed: the vptr
    field first (if any), then the bases in declaration order, then the data
    members. Every base is listed, so `FIELD` indexes stay stable. Only
    *direct* bases appear; indirect bases are reached through their direct
    base.
  - `empty_for_abi` is set on these only:
    - empty bases;
    - empty potentially-overlapping members (`[[no_unique_address]]`,
      together with `POTENTIALLY_OVERLAPPING`);
    - C zero-size members.

    Any other empty C++ data member is an ordinary field of size 1.
  - `POTENTIALLY_OVERLAPPING` marks a member laid out with the
    base-subobject type (dsize bytes).
- **Base and overlapping fields.** `FIELD`, `EXTRACT`, `AGG_SEL` and
  `C_SEL` on a `BASE` or `POTENTIALLY_OVERLAPPING` field yield the complete
  field type T, not its base-subobject type. The consumer casts from its
  `.base` storage, and computes the address of an omitted empty base from
  its offset.
- **Base-subobject types.** The consumer's `.base` record (dsize bytes) is a
  plain struct even for a class, as in clang. A union with reusable tail
  padding used as a base or as a potentially overlapping member is
  diagnosed "unsupported" in v1.
- **vptr.** A class with a vptr has an unnamed
  `TY_OPAQUE(ptr_size, ptr_align)` field at the vptr offset, so it is
  ABI-opaque (8.11) until v2.

### 11.4 Structors

- **`STRUCTOR(g_complete, g_base, flags{dtor, has_vbases})`** (declaration
  section):
  - It pairs C1 with C2, or D1 with D2. Both are declared functions of the
    same type and linkage, and their names differ only in the variant
    digit.
  - The body is streamed under `g_base` only. `FUNC_BEGIN(g_complete)` is an
    error while `has_vbases` is 0, and `has_vbases` is 0 in v1.
  - **Declaring C1.** When `g_base` is defined here, `g_complete` is sent
    with the same linkage and flags as `g_base`, including
    `DEFINED_HERE` and `COMDAT`, although it has no body of its own: it is
    defined through `g_base`. If `g_base`'s body is later dropped as
    unsupported, the writer re-issues both declarations accordingly.
  - Call sites use `g_complete` for complete objects and `g_base` for base
    subobjects. Array elements are complete objects, so `ARRAY_CTOR` and
    `ARRAY_DTOR` bodies call `g_complete` (C1/D1). Clang uses C2/D2 there,
    which is equivalent while `has_vbases` is 0; the difference shows only
    in a byte-compare. M4 relies on the complete variant.
- **Consumer: C1 strategy.** It applies only when `g_base` ends up defined:
  not unsupported, not retracted, and not merely declared (for example
  `extern template`). Otherwise C1 stays an external declaration.
  - External linkage: C1 is an alias of C2.
  - `weak_odr`: C2 goes in comdat C5 (D5 for destructors), and C1 is a
    `weak_odr` alias.
  - Discardable linkage: C1 references are redirected to C2 by the
    finish-time substitution.
- **`MEMINIT_BEGIN(kind, idx, flags{compiler_generated, default_member_init})`
  … `MEMINIT_END`:**
  - The groups come only at the start of a constructor body.
  - The kinds are `vbase` 0 (an error in v1), `base` 1, `field` 2 and
    `delegation` 3. A delegation must be the only group, and its `idx` is
    0.
  - `idx` is the `REC_FIELD` index and strictly increases.
  - The writer sends one group per entry of the FE's constructor-initializer
    list. Trivially constructible members without an initializer have no
    entry and no group.
  - Compiler-generated entries have `compiler_generated`, and default member
    initializers have `default_member_init`.
  - For an implicit copy or move constructor, the writer builds the source
    subobject (the same member of the parameter, or a base cast of it).
- **Destructors.** A destructor body starts with one `CLEANUP_BEGIN(all)`
  per member and base with a non-trivial destructor, in construction order.
  Destruction then runs in reverse order, including on early returns.
  The FE's list for a destructor is already in destruction order, so the
  writer reverses it.

### 11.5 Object lifetimes and cleanups

**Scope regions** are those of 8.3, plus `FULLEXPR`, `STATIC_INIT`,
`MEMINIT` and the `ARRAY_*` loop bodies. **Conditional regions** are `TERN`
arms and the right operand of `LAND`/`LOR`.

**`CLEANUP_BEGIN(kind, g|nil, flags)` … code … `CLEANUP_BODY`**
- **Kinds:** `normal` 1, `eh` 2, `all` 3 (= `cir::CleanupKind`), `exit` 4.
- **Placement.** It is emitted right after the object is completely
  constructed.
- **The code** is stack-neutral and never consumes values pushed before it.
  It contains no `LOCAL`, `LABEL`, jumps, nested `CLEANUP` or `FULLEXPR`.
- **Range.**
  - `normal`/`eh`/`all` protect from `CLEANUP_BODY` to `CLEANUP_END`, or to
    the end of the innermost scope region. Cleanups close innermost first.
  - `exit` is allowed only inside `STATIC_INIT`, requires `g`, and protects
    nothing. It registers the code for program exit.
- **Conditional cleanups.** A `CLEANUP_BEGIN` in a conditional region
  requires an enclosing `FULLEXPR` in the same function or `STATIC_INIT`. It protects until
  that `FULLEXPR` ends, and destruction is in reverse construction order
  across the full-expression.
  - The consumer opens a flag-guarded cleanup scope before the outermost
    conditional record.
  - Unconditional cleanups begun later in the same full-expression nest
    inside that scope. Otherwise the destruction order reverses.
- **v1 consumers** map `all` to `normal` and drop `eh`.

**`CLEANUP_END`** is reserved for M4: deactivating a cleanup that need not be
the innermost. v1 writers never emit it, and a v1 reader rejects it. Every v1
cleanup ends with its scope region or `FULLEXPR`.

**`FULLEXPR_BEGIN` … `FULLEXPR_END`** closes every cleanup begun inside it.
It carries out at most one scalar prvalue or glvalue, never a record
prvalue. Values pushed before `FULLEXPR_BEGIN` are not consumed inside.

**Locals in a FULLEXPR.** `FULLEXPR` is a scope region for cleanups only. A
`LOCAL` or `MATERIALIZE_TEMP` introduced inside a `FULLEXPR` belongs to the
enclosing scope region, so its L-id stays valid after `FULLEXPR_END`. The
other v1 scope regions (`STATIC_INIT`, `MEMINIT`, `ARRAY_*`) end their locals
too.

**Lifetime extension.** A temporary bound to a local reference is
constructed inside the `FULLEXPR`, and its `CLEANUP_BEGIN` follows
`FULLEXPR_END`. A temporary that is extended only conditionally (for
example `const A& r = c ? A() : a;`) cannot be expressed in v1 and is
diagnosed "unsupported".

**Jumps.**
- `GOTO_STMT.n_cleanups` must be 0. The consumer finds the exits itself.
- While a cleanup is open, the consumer routes every value return through
  the return slot.

### 11.6 Class prvalues and copy elision

- **Consuming a `NONTRIVIAL_ABI` prvalue.** It is consumed only by one of:
  - `STORE(INIT)` into a destination glvalue pushed before the call's
    arguments;
  - `MATERIALIZE_TEMP`;
  - `DROP`, and only if the record lacks `NONTRIVIAL_DTOR`. Otherwise the
    writer materializes the prvalue and pushes a cleanup;
  - a `STORE` performing a bitwise copy, when the prvalue is a `LOAD`.
- **Arguments.** As a call argument, such a value is `LOAD` of a `LREF` that
  names a `LOCAL`, `PARAM` or temporary: the load of that storage itself.
  To pass a subobject (`FIELD` … `LOAD`) or `*p`, the writer copies it into
  a temporary first.
- **Errors.** `DUP`, a `TERN` result and `RETURN_STMT(true)` of such a type
  are errors.
- **Returns** of a `NONTRIVIAL_ABI` record use `RETURN_SLOT` …
  `RETURN_FROM_SLOT`. A trivially copyable record may use them too. The
  writer does so when the returned expression has temporaries (a
  `FULLEXPR`), so the value is complete before the cleanups run.
- **NRVO.** `LOCAL` with `RETURN_SLOT` names the NRVO variable.
  - Like any local with a non-trivial destructor, it gets a `normal`
    cleanup.
  - The consumer guards that cleanup with an `nrvo` flag: false at entry,
    set true by `RETURN_FROM_SLOT`. The object is therefore destroyed when
    control leaves its scope any other way (a `goto` out, the end of the
    scope), and not when it is returned.
- **`STORE` flags:** `INIT` (fresh storage) and `DSIZE` (copy `data_size`
  bytes).
- **`ZERO_BYTES(bytes)`** pops a glvalue and zero-fills `bytes` bytes starting
  at its address. The glvalue is not a bit-field. The writer uses it for:
  - the data size of an overlapping subobject (a base, or a non-empty
    `[[no_unique_address]]` member);
  - value-initialization of a whole record or array, padding included;
  - a constructor delegating to a trivial default constructor;
  - `new T[n]()` with a constant count: `n × sizeof(T)` bytes from the first
    element.

  The writer guarantees that the range lies inside one complete object.
  The consumer does not check the count against the glvalue's type, because
  for `new[]` the range covers more than the element the glvalue
  designates.
- **Omitted empty subobjects.** The writer writes nothing to an empty
  subobject that the consumer omits (an empty base, or an empty
  `[[no_unique_address]]` member, at an offset it shares with another
  member). As a second line of defence, the consumer treats a `STORE`,
  `INIT_*` or zero-fill through such a glvalue as writing nothing.
  `ZERO_BYTES` of more than one byte through it is a format error.
- **Casts.** `BASE_CAST`/`DERIVED_CAST(t_from, t_to, offset,
  flags{nonnull})` map prv pointer to prv pointer. Without `nonnull`, null
  stays null. Each cast is to or from a *direct* base; a conversion through
  several levels is a chain of casts.
- **Bitwise copies.** A record whose copy is trivial but whose destructor is
  not is still `NONTRIVIAL_ABI` (Itanium 3.1.2.3: a non-trivial destructor
  makes the class non-trivial for calls). When the FE copies it bitwise, the
  copy is a `LOAD` consumed directly by `STORE` (`STORE(DSIZE)` for an
  overlapping subobject).
- **By-value arguments.** The caller destroys them, as Itanium requires: a
  callee never pushes a cleanup for a by-value parameter.
- **Conditional class results.** A `TERN` whose result is a
  `NONTRIVIAL_ABI` record is encoded as a void `TERN`. Each arm pushes its
  own glvalue of the same destination (a value pushed before `TERN_BEGIN`
  cannot be consumed by both arms) and ends with a `STORE(INIT)` into it.

### 11.7 Static storage duration

**`STATIC_INIT_BEGIN(g, flags{inline_var, template_instance})` …
`STATIC_INIT_END`**
- **In a function body:** `g` is a `static_local|GUARDED` definition with no
  `VAR_INIT` (except the exit-only case below).
- **In the `F_GLOBALS` body section** (from its first `STATIC_INIT_BEGIN`):
  initialization order is record order, and `GUARDED` variables are
  initialized under the guard.
  - Each `STATIC_INIT_BEGIN` there opens a fresh L-id space, dense from 1
    as in a function, and `STATIC_INIT_END` closes it.
  - `LOCAL` and `MATERIALIZE_TEMP` are allowed inside. Their storage
    belongs to the initializer, and the consumer places the allocas in the
    ctor region.
  - In a function body, `STATIC_INIT` uses the function's L-id space.
- **Restrictions.** The content is stack-neutral, with no jumps or labels:
  the region stays single-block.
- **Consumer facts.** For `cir.global`/`cir.local_init`,
  `dynamic_init_info` is derived as follows:
  - the template kind is "implicit instantiation" if `template_instance` is
    set, otherwise "undeclared";
  - the TLS kind comes from `DECL_VAR_TLS` and whether the variable is
    dynamically initialized.
- **Destruction** uses `CLEANUP_BEGIN(exit, g)`. An `exit` cleanup for
  another variable (a `_ZGR…` temporary) registers that object's
  destruction.
- **Constant initializer with a non-trivial destructor.** Any variable of
  static storage duration with a constant initializer and a non-trivial
  destructor has three parts:
  - it is defined with `VAR_INIT`;
  - it has a `STATIC_INIT` that contains only its `exit` cleanup;
  - it is `GUARDED` if it is a function-local static or has vague linkage,
    so the registration happens exactly once.

  This applies at namespace and class scope as well as to local statics.
  It is the one case where `STATIC_INIT` is allowed for a `g` that has a
  `VAR_INIT`. The consumer emits a dtor-only `cir.global` region, or a
  `cir.local_init` with an empty ctor region.

### 11.8 Arrays, new and delete

- **`ARRAY_CTOR_BEGIN`/`_END`, `ARRAY_DTOR_BEGIN`/`_END(t_elem,
  flags{fixed})`.** With `fixed` they pop a glv of an array type; otherwise
  a prv `elem*` and a prv `size_t` count. The element pointer is pushed for
  the body, which is stack-neutral. For a multi-dimensional array, `t_elem`
  is the innermost element type and the count is the total number of
  elements.
- **`NEW_ARRAY_SIZE(t_elem, cookie)`**: count → `size_t`. Overflow or a
  negative count gives `SIZE_MAX`.
- **`NEW_ARRAY_COOKIE(t_elem, cookie)`**: (void*, count) → `elem*`. As
  Itanium requires, the count is stored at `ptr + cookie - sizeof(size_t)`,
  immediately before the elements, which start at `ptr + cookie`.
  `DELETE(array)` reads it from the same place. This matters for
  over-aligned elements, where the cookie is larger than `sizeof(size_t)`.
- **The cookie size** is computed by the FE: 0, or max(`sizeof(size_t)`,
  `alignof(t_elem)`). ARM variant cookies are rejected in v1.
- **`DELETE(g_delete_fn, g_dtor|nil, t_obj, flags{array, sized,
  aligned})`** includes the null check.
- **Switch on a 128-bit integer.** A case label that is negative or does
  not fit in 64 bits is diagnosed "unsupported" by the consumer. The CIR
  lowering at the pin reads case values as unsigned 64-bit.
- **Destroying and type-aware `operator delete`** (C++20/26) cannot be
  expressed in v1 and are diagnosed "unsupported".
- **Implicit operators.** The writer always names the allocation or
  deallocation function. The IL leaves it implicit (null) for array `new` of
  a class with a constructor and for scalar `delete` of a class with a
  non-trivial destructor, both of which use the global operator. In those
  cases the writer looks the function up itself.

### 11.9 Lambdas

- A closure type is an ordinary record.
- Its call operator is a member function whose body is a function frame.
  For a lambda in a function body, that frame is in the enclosing function's
  `F_REGION`, after the function's body. Lambdas at namespace scope, and
  instances of generic lambdas, get their own `F_REGION` like any other
  routine. Bodies are never nested (8.3).
- **Trailing region.** A synthesized invoker that is needed only after
  `F_GLOBALS` (for example a namespace-scope constant initialized with a
  converted lambda) is defined in one more `F_REGION` after `F_GLOBALS`.
  Frame order is otherwise free (4): any number of `F_REGION`/`F_GLOBALS`
  frames may precede `F_NAMES` and `F_END`, and the consumer's finish-time
  rules apply to all of them.
- **Static invoker.** For a lambda without captures, the writer synthesizes
  the static invoker (the conversion to a function pointer) when it is
  used, for generic lambdas too. The invoker's body calls the call operator
  with a null object pointer and forwards the parameters. Its linkage and
  `COMDAT` are those of the call operator.

### 11.10 Templates and retraction

- **Filters.** The writer emits only instances that are needed. Prototype
  instantiations, uninstantiated templates, deduction guides and consteval
  routines are never sent.
- **`RETRACT(g)`** may appear in `F_GLOBALS`, only for internal or
  `linkonce_odr` entities that were already streamed. The writer evaluates
  the retraction rule itself when it hands off the file scope: an instance
  that was instantiated only to deduce a return type, or that is covered by
  `extern template`. The consumer marks the definition discardable, and
  GlobalDCE removes it if it is unreferenced.
  - RETRACT also clears the entity's "unsupported" obligation (8.11). A
    G-id whose definition was dropped as unsupported may be retracted
    whatever its current linkage: an instance that is no longer needed
    must not turn an unsupported body into an error.
  - For a structor pair, RETRACT names `g_base`. It applies to the pair.

### 11.11 Worked encodings

The ten encodings are in `research/m3_ecil_v1_design.md` §2, with the §8
corrections:
- the `DROP` rule;
- conditional-cleanup placement;
- the C1 strategy;
- `empty_for_abi`.

They are checked into `cir_src/test/cirgen` as lit tests during M3b.
