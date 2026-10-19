# ECIL — EDG CIR IL stream format

Status: **v1.3, open** (session 4, from gate M4.1: supp key `noinline` (14) and several keys per supp blob (section 6); more 1.3 additions come at M5a, and 1.3 freezes at the M5 gate). **v1.2** (gate M4a, session 3: section 12: virtual functions, virtual bases, RTTI, exceptions and pointers to members). 1.2 is frozen at the M4 gate; additions and corrections found while implementing it (for example `NEW_ARRAY_SIZE_THROW`) are part of 1.2 until then. **v1.1** (gate M3.1, session 3: section 11.12). **v1.0** was frozen at the M3 gate (2026-10-11). Version 1.0 is C (sections 1–10) plus core C++ (section 11). Every rule that 0.x introduced "from minor 7" applies unconditionally, and 0.x pre-release streams are rejected by the major-version check (section 9). History of the 0.x minors: (header minor 8 declares every C++ (v1) record and flag, specified in section 11; minor 7 makes the `FOR_BEGIN` init scope span the whole loop (8.3, 8.4) and adds these rules: for streams of every minor, the reader id limit and effective-name ownership (7); for streams of minor 7 or later, writer density (7), scope regions, no nested function bodies (8.3) and case-label placement (8.4); minor 6 adds the float-classification builtins and the `CMP` kinds `one`/`uno`; minor 5 adds builtin ids; minor 4: adds the clarifications made during M1, namely END n_frames, HDR_LANG std, one G-id per symbol, feature-gated records, CALL result, and the layout rules). Owner: the
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
  `HDR_TYPE_ROLE`, `LOC`, `EXT`, and from 1.2 `CLASS_*`, `STRUCTOR*`,
  `TYPEINFO_DEF` (12.2, 12.3). In `F_REGION`, the body section follows
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

The keys are `an_ecil_supp_key` in `ecil_format.h`. Writers before 1.1
emit none. From 1.1 the writer emits `unsupported_reason` (13), and from
1.3 `noinline` (14); the other keys are consumed from M5 onward. Readers
skip keys they do not use.

**Several keys in one blob** (1.3). A record has at most one supp blob,
and a blob may hold several pairs. A key appears at most once per blob.
For example, a dropped `noinline` function carries both
`unsupported_reason` and `noinline`.

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
| 13 `unsupported_reason` (1.1) | bin: UTF-8 text, at most 255 bytes |
| 14 `noinline` (1.3) | true |

**`unsupported_reason`** (1.1) may follow a `DECL_FUNC` or `DECL_VAR` that
has `UNSUPPORTED` set, and only such a record; it appears at most once per
record. The value is valid UTF-8 and may be empty. It carries the writer's reason for dropping the
definition, for example `the statement "try statement"`. The consumer
appends it to the error it reports when the drop is an error (8.11), so the
reason survives even when the front end suppresses its own warning (a
declaration in a system header). A frame that carries a supp blob sets
`HAS_SUPP`. A 1.1 writer sets the `SUPP` feature bit on every stream,
because the preamble is written before any frame: the bit means "frames
may carry supp blobs", not that one does.

**`noinline`** (1.3) may follow a `DECL_FUNC`, and only a `DECL_FUNC`.
Its value is `true`.
- **Meaning.** The function must not be inlined. It is the GNU
  `noinline` attribute, written `__attribute__((noinline))` or
  `[[gnu::noinline]]`. The consumer sets the function's inline kind to
  "never" (LLVM `noinline`).
- **Sticky.** Once any issue of a G-id carries the key, the function is
  `noinline` for the rest of the stream. A re-issue without the key does
  not clear it, because GNU attributes accumulate over declarations.
- **The writer** sends the key on the first issue after the FE records
  the attribute, and on every later issue.
- **Older readers.** A 1.2 reader accepts a 1.3 stream that uses no newer
  opcode or flag (section 9) and skips key 14. It compiles the function
  without the attribute, which is the 1.2 behavior.

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
  - an unknown flag bit, or a flag bit added in a later minor than the
    stream's ("needs ECIL minor ≥ N"; for example a `CALL` with `VIRTUAL` in
    a 1.1 stream);
  - an unknown enumerated-operand value, also reported as "needs ECIL
    minor ≥ N".
- **Validation tightenings.** A minor may add a validation rule that no
  correct writer of any earlier minor could violate (a stream that breaks it
  was already malformed), for example the 1.1 `data_size` consistency check
  (11.3). Such rules apply to streams of every minor.
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

- `HDR_LANG(elang_cxx, std, flags)`. Before 1.2 the `exceptions` flag must
  be 0, and a 1.0 or 1.1 reader rejects 1. From 1.2 it may be 1, with the
  rules of 12.5 and 12.6. The rest of this section describes a stream with
  exceptions = 0.
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
  - **Static anonymous unions** (1.3 writer, gate M4.1). A static anonymous
    union has no source name. Like g++ and clang, it takes the name of its
    first named member: `_ZZ1fiE1i` for `static union { int i; … };` in
    `f(int)`. It is discriminated in the same sequence as the named locals
    of that name, in declaration order: a union then a `static int i`
    give `_ZZ1fvE1i` and `_ZZ1fvE1i_0`, as in g++ and clang.
  - **Linkage.** A local static of a function with vague linkage is
    `linkonce_odr` with `COMDAT`, and `GUARDED` when it is dynamically
    initialized. That covers inline functions, template instances,
    functions defined in their class, and the routines nested in such a
    function (next rule). Every other local static is internal, as in
    clang: the locals of a `static inline` function and of a function in
    an anonymous namespace, for example. Before gate M4.1 the writer made
    every unnamed static local internal, and the static locals of nested
    routines too. That was an ABI defect (pr39188): a g++ TU that inlines
    `f` used a different object.
- **Routines nested in a function** (1.3 writer, gate M4.1). These are the
  member functions of a local class, the call operator and static invoker
  of a lambda, and the special members of either. They have the vague
  linkage of their innermost enclosing namespace-scope function, at any
  depth of nesting: `linkonce_odr` with `COMDAT` when that function has
  vague linkage and the local class is not TU-local, internal otherwise.
  That is clang's rule. EDG gives such routines `storage_class` `static`,
  meaning "no linkage", which the writer used to read as internal.
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
  - `data_size` is the Itanium dsize; it equals `size` for C. From 1.1 an
    inconsistent `data_size` is a format error: below the end of the last
    field that is not `EMPTY_FOR_ABI`, or above `size`. A `BASE` or
    `POTENTIALLY_OVERLAPPING` field ends at the end of its data size (from
    1.2, a `BASE` or `VIRTUAL_BASE` field ends at the `nvsize` of its class:
    12.2; the two are equal for a class without virtual bases). A
    bit-field ends at the first byte after its last bit
    (`byte_off + ceil((bit_off + bit_width) / 8)`), so a zero-width bit-field
    ends at its byte offset (Itanium and clang: `struct S { S(); char c;
    int : 0; }` has dsize 4). Unions are exempt from the lower
    bound. The check applies to streams of every minor.
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
- **vptr.** Before 1.2, a class with a vptr has an unnamed
  `TY_OPAQUE(ptr_size, ptr_align)` field at the vptr offset, so it is
  ABI-opaque (8.11). From 1.2 the class's own vptr is a `TY_VPTR` field, the
  `VIRTUAL_BASE` fields follow the data members, and the class facts of 12.2
  describe it.

### 11.4 Structors

- **`STRUCTOR(g_complete, g_base, flags{dtor, has_vbases})`** (declaration
  section):
  - It pairs C1 with C2, or D1 with D2. Both are declared functions of the
    same type and linkage, and their names differ only in the variant
    digit.
  - The body is streamed under `g_base` only. `FUNC_BEGIN(g_complete)` is an
    error while `has_vbases` is 0, and `has_vbases` is 0 before 1.2
    (12.3 gives the rules for 1).
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
  - The kinds are `vbase` 0 (before 1.2 an error; from 1.2 only in C1 of a `has_vbases`
    pair, 12.3), `base` 1, `field` 2 and
    `delegation` 3. A delegation must be the only group, and its `idx` is
    0.
  - `idx` is the `REC_FIELD` index and strictly increases (from 1.2,
    within each kind, and the kinds come in the order `vbase`, `base`,
    `field`: 12.3).
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
  - `normal`/`eh`/`all` protect from `CLEANUP_BODY` (from 1.2, a
    `CLEANUP_END` deactivates a `DEACTIVATABLE` one earlier) to
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
- **With exceptions = 0,** consumers map `all` to `normal` and drop `eh`.

**`CLEANUP_END`** (1.2, 12.6) deactivates a `DEACTIVATABLE` cleanup, which
need not be the innermost. A 1.0 or 1.1 reader rejects it. Every other
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
- While a cleanup or (1.2) a `TRY` is open, the consumer routes every value
  return through the return slot.

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
  **Validation (gate M4.1; a tightening that applies to streams of every
  minor):**
  - **A glvalue made directly by `DEREF`.** This includes the same glvalue
    passed on by `DUP`, by a `STORE` that yields its target, or out of a
    `FULLEXPR`. It designates the object a pointer addresses, which may be
    an element of an array: a subscript `a[i]` is `PTR_STRIDE` then
    `DEREF`, and a `new[]` allocation is addressed by its first element.
    The writer may reach it through a reload of a local, as in the
    free-on-throw form of 12.10 (10). So `bytes` may exceed the size of
    the glvalue's type, but must be a whole multiple of it.
  - **Every other glvalue.** That covers a local, a global, a `FIELD`
    (also of a `DEREF` glvalue), an `AGG_SEL` element and the return slot.
    `bytes` must not exceed the size of its type: the range lies inside
    the object it designates.
  - **Still errors.** A bit-field, an object that holds a pointer to a data
    member (12.7), and an omitted empty subobject with `bytes` > 1 (below)
    remain errors.
  - **Not checked.** The consumer does not check the count against the
    number of elements that the pointer's array holds.
  - **Future.** A writer could zero through a `T[n]` glvalue instead, as it
    already does for types with pointers to data members. Then
    `bytes ≤ size` could hold for every glvalue (an ECIL 1.3 candidate).
    Streams of 1.2 writers keep the rule above.
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
  It is one of the two cases where `STATIC_INIT` is allowed for a `g` that
  has a `VAR_INIT`; the other is `PARTIAL` (1.1, section 11.12). The
  consumer emits a dtor-only `cir.global` region, or a `cir.local_init` with
  an empty ctor region.

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

### 11.12 ECIL 1.1 additions (gate M3.1)

- **`CMP3WAY(t_result, lt, eq, gt, unordered|nil)`** (0x9D): the built-in
  `<=>`.
  - It pops two prvalues of the same arithmetic, enumeration or pointer
    type, already converted by the writer, and pushes a prvalue of the
    integer type `t_result` (at most 64 bits). `lt`, `eq`, `gt` and
    `unordered` are integer constants representable in `t_result`'s width
    (for an unsigned `t_result`, as its bit pattern).
  - The value is `lt`, `eq` or `gt` by the usual comparison. `unordered` is
    present if and only if the operands are floating (partial ordering); an
    unordered pair gives `unordered`. Floating operands without `unordered`,
    or `unordered` with non-floating operands, are errors.
  - The values are the comparison category's constants as the standard
    library defines them; the writer reads them from the category type. The
    writer stores the result into the category object (a record with one
    integer field) itself.
  - Consumer: `cir.cmp3way` with `#cir.cmp3way_info<strong|partial, …>`.
- **`STATIC_INIT_BEGIN` flag `PARTIAL` (0x04)**: partial static
  initialization, as g++ does it ([basic.start.static] allows it). `g` has a
  `VAR_INIT` that holds the constant elements of its initializer, with
  `C_ZERO` (or zero) for the elements that are initialized dynamically. The
  `STATIC_INIT` stores only those dynamic elements, with no zero-fill and no
  re-store of the constant ones. This is the second case, after the exit-only
  case of 11.7, where a `STATIC_INIT` is allowed for a `g` with a `VAR_INIT`.
  The consumer keeps the constant initial value and adds the dynamic
  initialization (a `cir.global` ctor region, or the `cir.local_init` region of
  a guarded variable). Example: `int ar[2] = { 42, i };` keeps `ar[0] == 42`
  before any dynamic initialization runs (`cpp0x/constexpr-static4`).
- **`unsupported_reason`** supp key (13): section 6.
- **`data_size`** consistency is checked (11.3).

## 12. C++ (M4): ECIL 1.2

ECIL 1.2 adds virtual functions, virtual bases, RTTI, exceptions and
pointers to members. It follows the minor rules of section 9: new opcodes in
reserved ranges, new flags and enumerated values, and validity for three
things that 1.0 reserved (`HDR_LANG.exceptions` = 1, `MEMINIT` kind `vbase`,
`STRUCTOR.has_vbases`) and for `CLEANUP_END`. A 1.1 reader rejects the new
records and flags ("stream needs ECIL minor ≥ 2"), and gives a format error
for the reserved uses; a 1.2 reader rejects a 1.2 record in a stream that
says minor 1 or lower. Background:
- `research/m4_ecil_design.md` (the design and its history);
- `research/m4_cir_shapes.md` (spikes K1–K7 and K9 at the pin, gaps
  G1–G8);
- `research/m4_upstream_drift.md` (K8: upstream after the pin);
- `research/m4_fe_facts.md` (the FE's facts against clang);
- `research/il_to_ecil_map_m4.md` (the writer's IL map).

### 12.1 Principles

- **Facts and mechanics.** The FE sends authoritative facts:
  - slot numbers and final overriders, per class and subobject;
  - the primary base, `nvsize` and `nvalign`, base offsets and class
    manglings;
  - emission and linkage decisions, the operator delete of D0, typeinfo
    linkage and the EH facts.

  The library ports clang's Itanium mechanics (`cir_src/lib/abi`). It
  derives everything else:
  - dynamic-ness, emptiness and clang's `vbases()` order;
  - the functions each function overrides (from `CLASS_OVERRIDES`),
    covariant return adjustments, thunks, vtable groups, the VTT,
    construction vtables and the RTTI layout;
  - the `_ZTV/_ZTT/_ZTI/_ZTS/_ZTC/_ZTh/_ZTv/_ZTc` names.

  The stream carries no fact that the library can derive: two sources of
  truth drift.
- **Strict.** Every rule below that a consumer can check is a format error
  or, where noted, a fatal finish-time diagnostic, with a `bad_*` fixture.
  Rules stated as writer duties (`sig`, the provenance of `NOTHROW`,
  devirtualization, the emission and linkage rules of 12.8) cannot be
  checked by the consumer; they are reviewed and tested.
- **Non-recursive** on both sides. Inheritance depth scales with the input.
- **Clang tiebreak.** Where clang and g++ differ on an ABI-visible detail,
  ECIL follows clang. Two exceptions (user decisions): object layout is the
  FE's (`vbase13`, `m4_fe_facts.md`), and the typeinfo name of a TU-local
  type has g++'s `*` prefix (12.3).

### 12.2 Class facts (declaration sections)

| Op | Record | Rules |
| --- | --- | --- |
| 0x1B | `TY_VPTR(g)` | Pointer size and alignment. At most one per record, only as an unnamed field 0 at offset 0: the class's own vptr. A class whose vptr is its primary base's has none of its own. A record with a `TY_VPTR` field has its layout facts right after its body (the ordering rules below). |
| — | `REC_FIELD` flag `VIRTUAL_BASE` 0x40 | One field per virtual base, direct or indirect, after the data members, at its complete-object offset. The set equals the direct virtual bases plus the virtual bases of every direct base. The writer sends them in the FE's virtual-base order (postorder: a virtual base comes after its own virtual bases, so `N : virtual V1, virtual V2` with `V2 : virtual V1` lists V1, then V2), which is also the order of its `MEMINIT(vbase)` groups; the order of the fields is not significant to the library. `EMPTY_FOR_ABI` means no storage of its own: it is set iff the virtual base's class is empty, or it is nearly empty and is the primary virtual base of the class or of one of its bases, direct or indirect (clang's `hasOwnStorage`; it then lives inside that base's storage). **Data size** (amends the 1.1 rule of 11.3): a `BASE` or `VIRTUAL_BASE` field that is not `EMPTY_FOR_ABI` ends at its offset plus the `nvsize` of its class (from `CLASS_INFO`; the class's `data_size` when it has none), as clang's layout builder ends a non-empty base. For a class with virtual bases, the writer's `data_size` is the largest end of any field, these included (clang's dsize of the complete object): `N : virtual V1, virtual V2 { char n; }` with V2 (nvsize 12) at 16 has dsize 28, not 32. `FIELD`, `EXTRACT`, `AGG_SEL` and `C_SEL` of a `VIRTUAL_BASE` field are errors: a virtual base is reached with `VBASE_CAST`. |
| 0x1E | `CLASS_INFO(t, nvsize, nvalign, primary_idx\|nil, nslots)` | Right after the record body for a dynamic class (an own `TY_VPTR` field, a `BASE` field of a dynamic class, or a `VIRTUAL_BASE` field); for any other class only at need (the ordering rules below): a dynamic class, a class whose typeinfo is declared (and every base of it), and a class named by `CATCH_PARAM` or `DYNAMIC_CAST`. At most once per `t`, after its `TY_RECORD_BODY` and `CLASS_MANGLE`. Until its `CLASS_INFO`, a class is laid out with `nvsize` = `data_size` (its `.base` is `data_size` bytes; the library places fields by their offsets, so a later, larger `nvsize` only adds tail bytes). `nvsize ≤ size`, `nvsize` is at least the end of the last field that is not a virtual base, and without `VIRTUAL_BASE` fields `data_size ≤ nvsize` (a displaced empty base makes `nvsize` larger). A class without a vptr has `nslots` 0. `nvalign` is a power of two no larger than `align`. `primary_idx` is a `BASE` or `VIRTUAL_BASE` field at offset 0 of a dynamic class. A dynamic class has either its own `TY_VPTR` field or a `primary_idx`, never both and never neither. `nslots` is the number of function slots of the primary vtable: the FE's `highest_virtual_function_number + 1`, or 0 for its 65535 sentinel (a class whose only dynamic feature is a virtual base has a vptr but no slots). `nslots` is at least the primary base's `nslots` and at least the largest own slot + 1 (+2 for a destructor). The `.base` type is `nvsize` bytes, and distinct from the complete type iff `nvsize != size`. |
| 0x1F | `CLASS_BASES(t, arr[(field_idx, flags)…])` | Follows every `CLASS_INFO`; empty for a class without bases. The direct bases in **declaration order** (FE `direct_base_number`), virtual and non-virtual interleaved. Flag `PUBLIC` 0x01. Whether a base is virtual comes from its field. No duplicates. The `BASE` entries appear in increasing field order and are exactly the `BASE` fields. The virtual entries are `VIRTUAL_BASE` fields. |
| 0x20 | `CLASS_VFUNCS(t, arr[(g, slot, flags, sig)…])` | Sent for every dynamic class at its first need, after `CLASS_BASES` (an error for any other class); empty for a dynamic class that declares no virtual function. Every virtual function declared in `t`, in declaration order, implicit ones last in slot order. `g` is a `DECL_FUNC` whose `this` points to `t`. A destructor is sent as its D1 (the complete variant of a dtor `STRUCTOR`), at `slot`, with D0 at `slot+1`. A `consteval` virtual function has no slot and appears in neither `CLASS_VFUNCS` nor `CLASS_OVERRIDES`. Flags: `PURE` 0x01, `DELETED` 0x02, `IMPLICIT` 0x04, `REF_RESULT` 0x08 (the function returns a reference: ECIL sends references as `TY_PTR`, and a covariant return adjustment of a reference must not be null-checked, while a pointer result stays null). `sig` (u32) is a non-zero id that the writer assigns, and 0 exactly for destructors. Two functions have the same `sig` iff they have the same name, identical parameter types as the FE compares them (top-level cv removed, arrays and functions decayed) and the same cv-qualifiers of `this`; ref-qualifiers are ignored, as in clang's `HasSameVirtualSignature`. It drives vcall-offset sharing; the library never compares parameter types. **Covariant returns.** The library derives covariance and the return adjustment by comparing each function's return type with that of the functions it overrides; any class may be a covariant result, polymorphic or not (`struct D2 : R1, R2 {}` returned for `R2*` needs +offset). So the vtable facts of a class need, for each listed function whose return type is a pointer or reference to a class, that class's layout facts (sent at need for a class that is not dynamic), and its vtable facts too when the path to the overridden return class goes through a virtual base. The library's vtable layout must put every listed function at its slot (`check_slots_against_builder`): a difference, a slot collision or a gap in `[0, nslots)` of the primary chain is a fatal finish-time diagnostic, because `CALL VIRTUAL` uses the FE's slot. |
| 0x21 | `CLASS_OVERRIDES(t, arr[(g_fn, sub_off, g_ovr, ovr_off)…])` | Follows `CLASS_VFUNCS` for every dynamic class, at its first need, including a class seen only as a base (construction vtables use the base's own lists); empty when nothing is overridden; an error for a class that is not dynamic. In `t`'s complete layout, the subobject of `g_fn`'s class at `sub_off` has the final overrider `g_ovr`, declared in the subobject at `ovr_off` (for a virtual base, its offset in `t`). Only entries with `g_ovr ≠ g_fn`: no entry means `g_fn` is final. The format checks ids and stride; the existence of the subobjects is checked at finish (`validate_deep`, a fatal diagnostic). |
| 0x23 | `TY_DATA_MEMBER(g, t_member, t_class)` | `!cir.data_member`. Its null is −1 (12.7). |
| 0x24 | `TY_METHOD(g, t_func, t_class)` | `t_func` is the member function's `TY_FUNC`, whose first parameter points to `t_class`. `!cir.method`. |
| 0x25 | `CLASS_MANGLE(t, sid)` | The Itanium `<type>` mangling of `t`, what follows `_ZTV`: `1D`, `N1N1CE`, or the deterministic internal form of 11.2 (`N12_GLOBAL__N_12AnE`). Required before `CLASS_INFO`. It may be re-issued until `F_NAMES`, for a late typedef name for linkage (`typedef struct {…} TN;` mangles first as `Ut_`, then `2TN`); the latest wins. The library derives every ABI name at finish from the final mangling only. |

**Ordering.** The facts of a class come in this order: `TY_RECORD_BODY`
and its fields; `CLASS_MANGLE`, `CLASS_INFO` and `CLASS_BASES` (the
*layout facts*); then, for a dynamic class, `CLASS_VFUNCS` and
`CLASS_OVERRIDES` (the *vtable facts*). **Bases first, within each
group:** a class's `CLASS_BASES` needs the layout facts of each dynamic
base (they were sent right away), and a dynamic class's `CLASS_VFUNCS`
needs the vtable facts of each of its dynamic bases. The other facts of a
base are needed only by the records that use them (for example a class
typeinfo, 12.3), and are sent before those. A class that is not dynamic
has no virtual bases, so where the library needs the bases of one that has
no `CLASS_BASES` (the `__vmi` flags of a derived class's typeinfo), it
walks its `BASE` fields.
- **Layout facts, right away.** The layout facts of a *dynamic* class
  follow its `TY_RECORD_BODY` directly. A class is dynamic when its record
  has a `TY_VPTR` field, a `VIRTUAL_BASE` field, or a `BASE` field of a
  dynamic class (its vptr is its primary base's: `struct B : A {}` with A
  polymorphic). The consumer knows this at the body, because the bases'
  bodies come first.
  A `TY_RECORD_BODY` with a `BASE`, `VIRTUAL_BASE` or
  `POTENTIALLY_OVERLAPPING` field of such a class needs that class's
  `CLASS_INFO` first. The library needs them to build the record (`.base`
  is `nvsize` bytes; a primary virtual base has no storage of its own):
  `std::stringstream` has a vptr and a virtual base even in a TU that never
  calls a virtual function.
- **Everything else at first need.** The layout facts of other classes, the
  vtable facts, the `DECL_FUNC`s they name and `STRUCTOR_DELETING` are sent
  before the first record that needs them, never at class completion. A
  need record for a dynamic class whose vtable facts have not been sent is
  an error.
- The records that need a class's facts:
  - `VPTR_INIT` and `VBASE_CAST` in the class;
  - `VTT_ADDR`;
  - a `CALL VIRTUAL` through a function of the class or of a class derived
    from it;
  - `DYNAMIC_CAST`, and `CONST_METHOD`/`C_METHOD` with `VIRTUAL`;
  - `C_AGG_BEGIN` of the class (in a constant for `VAR_INIT` or
    `INIT_CONST`), and of each dynamic class that such a constant contains
    as a member or base, because the library fills in their vptrs;
  - `DECL_ABI_DATA` of the class;
  - `CATCH_PARAM`.
- Every G-id that `CLASS_VFUNCS` and `CLASS_OVERRIDES` name is introduced
  before them (rule 1.2). The writer declares every virtual function of a
  class at the class's first need.

### 12.3 ABI data and structors (declaration sections)

| Op | Record | Rules |
| --- | --- | --- |
| 0x34 | `STRUCTOR_DELETING(g_d0, g_d1, g_delete, flags)` | One per virtual destructor: `g_d1` is the complete variant of a dtor `STRUCTOR` whose destructor is virtual (a non-virtual destructor has no D0). `g_d0` is introduced by its own `DECL_FUNC` immediately before this record (its name has `D0`; D1's type, linkage and flags). The class of `g_d1` is complete (its `TY_RECORD_BODY` has been sent) before this record, and before a `STRUCTOR` with `has_vbases`. `g_delete` is the FE's `assoc_operator_delete_routine`. Flags `SIZED` 0x01 and `ALIGNED` 0x02 must match the number of parameters of `g_delete`'s type (1 + `SIZED` + `ALIGNED`, + 1 for the empty `std::destroying_delete_t` tag with `DESTROYING`; ECIL cannot tell `size_t` from `align_val_t`). `DESTROYING` 0x04: `g_delete` is a destroying operator delete, and D0 calls it with `this` (and size and alignment as flagged) and runs no D1, as clang does. D0 is defined iff D1 is defined here. The library synthesizes D0: `cleanup.scope all { D1(this) } cleanup { operator delete }`, or, with `DESTROYING`, one call of the destroying operator delete and no D1 (below). A destroying operator delete chosen as the deallocation function of a `new` expression is diagnosed by the writer. |
| 0x35 | `DECL_ABI_DATA(g, kind, t, linkage, flags, sid\|nil)` | Kinds `vtable` 0 (needs a dynamic class), `vtt` 1 (needs a class with virtual bases), `typeinfo` 2. Flags `DEFINED_HERE` 0x01, `COMDAT` 0x02 (iff the linkage is `linkonce_odr` or `weak_odr`) and `TU_LOCAL` 0x04 (typeinfo only; implies internal linkage): the type is TU-local in the sense of 11.2, its pointee chain included. **Class kinds** (a vtable, a VTT, the typeinfo of a class): at most one per (kind, `t`); the name is derived from `CLASS_MANGLE`, and `sid` is nil or equal to it. **Non-class typeinfo** is unique by name and needs `sid` (`_ZTIPKi`: ECIL types carry no cv), plus a `TYPEINFO_DEF`. A `DECL_ABI_DATA` is introduced at its first use (a typeinfo in a body record, with the linkage and flags the writer knows then) and **re-issued in the `F_GLOBALS` declaration section** with its final linkage, flags and `DEFINED_HERE`; the latest wins, as for `DECL_VAR`. Vtable and VTT G-ids occur in no body, so the writer sends them once, at the end of the TU. A class typeinfo with `DEFINED_HERE` needs the typeinfo of each direct base introduced first, on every issue. The typeinfo of an **incomplete class** (`typeid(I*)`) is a plain `__class_type_info` without bases or `CLASS_INFO`; it is internal. A pointer or member-pointer type whose pointee chain contains an incomplete class (clang's `ContainsIncompleteClassType`: `I*`, `I**`, `int I::*`) has internal typeinfo; each pointer level of that chain has the `INCOMPLETE` mask (0x08), and a member pointer whose class is incomplete has `INCOMPLETE_CLASS` (0x10). ABI-data G-ids appear only in `C_ADDR` and the typeinfo operands of the records below, never in `GREF`. `_ZTS` (the name and its string) is derived. The string of a typeinfo with `TU_LOCAL` starts with `*`, as g++ writes it (user decision 2026-10-12; clang does not); internal linkage alone does not imply it (the typeinfo of `I*` with `I` incomplete is internal but its name is `P1I` in every TU): libstdc++ then compares such names by address, so two TU-local types with the same mangling in different TUs never compare equal, and `type_info::name()` skips the `*`. The name pointer never has the non-unique bit (bit 63): on x86-64, clang's `shouldRTTIBeUnique()` holds, and libstdc++ dereferences the pointer. |
| 0x36 | `TYPEINFO_DEF(g_ti, kind, flags, g_a\|nil, g_b\|nil)` | The content of a non-class typeinfo `g_ti`. Kinds: `fundamental` 0 (an external declaration, never defined here: `_ZTIi`, `_ZTIDn`); `pointer` 1 (`g_a` is the pointee's typeinfo); `member_pointer` 2 (`g_a` the pointee's, `g_b` the class's); `function` 3; `array` 4; `enum` 5. Flags for 1 and 2 are the `__pbase_type_info` masks: `CONST` 0x01, `VOLATILE` 0x02, `RESTRICT` 0x04, `INCOMPLETE` 0x08, `INCOMPLETE_CLASS` 0x10, `TRANSACTION_SAFE` 0x20, `NOEXCEPT` 0x40; 0 for the other kinds. A class typeinfo has no `TYPEINFO_DEF`: the library derives `__class`, `__si` or `__vmi`. |

**`STRUCTOR` with `has_vbases` = 1.** It must equal "the class has
`VIRTUAL_BASE` fields". Amends 11.4:
- Both bodies, `g_complete` and `g_base`, are streamed, each once. The
  alias and redirect strategies for C1 are off; each variant has its own
  `COMDAT`, as in clang.
- **Constructors.** C1 has the `MEMINIT(vbase, idx)` groups (`idx` is the
  `VIRTUAL_BASE` field index); C2 does not. Delegating constructors: C1
  calls the target's C1, and C2 calls the target's C2 with `VTT_ADDR(self)`.
- **Destructors.** D2 is the FE body without the virtual-base destructions.
  D1 is a body that the writer synthesizes:
  1. a `CLEANUP_BEGIN(all)` for each virtual base with a non-trivial
     destructor, in the order of the `MEMINIT(vbase)` groups of C1, so that
     they run in reverse construction order; each calls the base's D2 on
     `VBASE_CAST COMPLETE`;
  2. a `CALL` of D2 with `VTT_ADDR(self)`.

  D1 has no `VPTR_INIT`. Clang emits D1 as a second copy of the body;
  "D2, then the virtual bases" behaves the same but is not byte-equal (a
  deliberate difference).
- **The VTT parameter.** The library adds the hidden VTT parameter to C2 and
  D2 and to calls of them. `TY_FUNC` stays the source type. The `CALL` flag
  `VTT` appears only on calls of a base variant of a `has_vbases` pair.
- **Writer duties.**
  - A lambda in the body is streamed once.
  - CHECKING asserts that the second walk introduces no new G-ids.
  - A static local may have a `STATIC_INIT` in both bodies (K5a).
- **`MEMINIT` order** (amends 11.4): kinds appear in the order `vbase`,
  `base`, `field`, and `idx` increases strictly *within each kind*.

### 12.4 Function headers and body records

| Op | Record | Stack | Rules and CIR |
| --- | --- | --- | --- |
| 0x6F | `FUNC_EH_SPEC(kind, arr g_ti)` | — | A header record, after the `PARAM`s and before any body record, at most once. Only with exceptions = 1. Kinds: `noexcept` 1 wraps the whole function (member and base initializers, `VPTR_INIT`, a destructor's cleanup prologue and the body, outside any `FUNCTION_TRY`) in `try {…} catch all {terminate}` (clang's order: the inner cleanups run before `terminate`); `dynamic` 2 is `throw(T…)`: a filter on the listed types, then `__cxa_call_unexpected`. An empty list with `dynamic` is pre-C++17 `throw()`; the writer maps `throw()` to `noexcept` from C++17 on. The writer sends it for every definition whose exception specification is `noexcept(true)`, implicit destructors and deallocation functions included. `noexcept` without `DECL_FUNC.NOTHROW` is an error. |
| 0x6E | `VPTR_INIT` | — | Exactly once in each constructor and destructor body of a dynamic class (the pointee of `this`). In a **constructor**, after every `vbase` and `base` `MEMINIT` group (and the `CLEANUP_BEGIN(eh)` that follows each), before the first `field` `MEMINIT_BEGIN` or statement. In a **destructor** (D2, or the only body), after the `CLEANUP_BEGIN(all)` prologue. Forbidden in a delegating constructor and in a synthesized D1. Library: clang's `InitializeVTablePointers` order; the `vtable.address_point` of the class's vtable group in C1, D1 and every structor of a class without virtual bases, and loads of `vtt.address_point` from the VTT parameter in C2 and D2. |
| 0x9E | `VBASE_CAST(t_der, t_vbase, flags)` | prv ptr → prv ptr | `NONNULL` 0x01; `COMPLETE` 0x02 uses the static complete-object offset, and is required for the construction and destruction of virtual bases in C1 and D1. Otherwise the virtual-base offset is loaded from the vtable (clang's shape), with a null check unless `NONNULL`. A path through a virtual base is `VBASE_CAST` and then `BASE_CAST`s. |
| 0x9F | `VTT_ADDR(kind, idx\|nil)` | → prv | `self` 0 (nil `idx`), `base` 1 (`idx` is a `BASE` field), `vbase` 2 (`idx` is a `VIRTUAL_BASE` field). Only as the VTT operand of a `CALL` with `VTT`. The library computes it from the class's `_ZTT` in C1 and D1, and from the VTT parameter in C2 and D2. |
| 0xA0 | `DYNAMIC_CAST(g_src_ti, g_dst_ti\|nil, t_result, flags)` | prv ptr → prv ptr | `REF` 0x01 throws `bad_cast` on failure. A nil `g_dst_ti` is `dynamic_cast<void*>`. `cir.dyn_cast` with `#cir.dyn_cast_info`; the library computes clang's offset hint. An error with rtti = 0. |
| 0xA1 | `TYPEID(g_ti\|nil, t_result, flags)` | [glv] → glv | With `g_ti`, the static form: the object of `g_ti`, and nothing is popped. With nil, it pops a glvalue of a polymorphic class and reads the typeinfo from its vtable. `NULL_CHECK` 0x01 is set iff the operand is an indirection through a pointer (`typeid(*p)`), looking through parentheses, the right operand of a comma and either arm of a conditional, as clang's `isGLValueFromPointerDeref` does (`typeid((x, *p))` checks); the null check (`__cxa_bad_typeid`) precedes the vptr load. `t_result` is the `std::type_info` record. An error with rtti = 0. |
| 0xA2 | `PM_FIELD(t_dm)` | prv obj*, prv dm → glv | `cir.get_runtime_member`. |
| 0xA3 | `PM_CALL(t_call, nargs, flags)` | prv obj*, prv method, args → result | The object comes first, since C++17 evaluates the object expression first. `nargs` counts the parameters of `t_call`, the object pointer included; the method pointer is not counted, so it pops `nargs + 1` values: the object pointer, the method pointer, then the other arguments. `cir.get_method` and an indirect call. Flags and copy elision as for `CALL`. |
| 0xA4 | `PM_CAST(kind, t_from, t_to, offset)` | prv → prv | `to_derived` 0 is clang's `CK_BaseToDerivedMemberPointer` (the implicit `int B::*` → `int D::*`), which adds `offset`; `to_base` 1 is `CK_DerivedToBaseMemberPointer` (`static_cast`), which subtracts it. `offset` is the non-negative offset of the base in the derived class; virtual paths are ill-formed. Null is preserved. |
| 0xA5 | `CONST_DATA_MEMBER(t, offset\|nil)` | → prv | `t` is a `TY_DATA_MEMBER`. `offset` is the byte offset, in the complete class of `t`, of the designated member: the member's own offset plus the offset of the non-virtual base subobject that contains it. nil is the null member pointer. The library finds the field path of `#cir.data_member` by descending the `BASE` fields; the member type of `t` selects among members at one offset (union members at the same offset give the same value). No match is an error. A member of a virtual base cannot be designated this way (its pointer type is the virtual base's own). |
| 0xA6 | `CONST_METHOD(t, g\|nil, adj, flags)` | → prv | `t` is a `TY_METHOD`; `g` is the member function, and nil is null (`{0, 0}`). `adj` is the non-negative byte offset added to `this`. `VIRTUAL` 0x01: `g` is virtual, and the value is its vtable offset (from its slot in `CLASS_VFUNCS`). A non-zero `adj` becomes a `cir.const` plus a runtime `cir.derived_method` (gap G2). |
| 0xA7 | `ARRAY_PARTIAL_DTOR(g_dtor)` | — | Directly after an `ARRAY_CTOR_BEGIN`, with exceptions = 1 and an element type with a non-trivial destructor (`g_dtor` is its D1): `cir.array.ctor … partial_dtor`. An aggregate-initialized array (`new T[n]{…}`, `S s = {A(1), A(2)}`) uses a `CLEANUP_BEGIN(eh, DEACTIVATABLE)` per element and `CLEANUP_END` instead. |
| 0xA8 | `INIT_ZERO_BASE` | pops a glv | Zero-initializes a base subobject: the first `nvsize` bytes of the class (`data_size` without `CLASS_INFO`), with the null constants of 12.7 for members that are not zero-initializable. It writes nothing for an `EMPTY_FOR_ABI` base. `INIT_ZERO` of a base subobject would write `size` bytes. |
| 0xA9 | `NEW_ARRAY_SIZE_THROW(t_elem, cookie)` | prv count → prv `size_t` | As `NEW_ARRAY_SIZE` (11.8), but a negative or overflowing count calls `__cxa_throw_bad_array_new_length` instead of yielding `SIZE_MAX`. The writer uses it for a `new T[n]` that is not a nothrow allocation, from C++11 on (clang's rule); a nothrow `new[]` and C++98/03 keep `NEW_ARRAY_SIZE` (EDG and clang return null for a nothrow allocation; g++ throws). |

**Member-pointer constants.** 0xEB `C_DATA_MEMBER(t, offset|nil)` and 0xEC
`C_METHOD(t, g|nil, adj, flags)` have the operands and meaning of
`CONST_DATA_MEMBER` and `CONST_METHOD`. They push on the constant stack and
are valid in `VAR_INIT`, `INIT_CONST` and `C_AGG`. In a `VAR_INIT`, any
`adj` is valid: the library emits the global in its lowered `{i64, i64}`
form (G2, K6b), also when the method pointer is inside a record, an array
or a union (the variable then has a lowered type of the same layout). In a function body, a `C_METHOD` with a non-zero `adj`
(alone or inside a `C_AGG`) is an error: the writer uses `CONST_METHOD` and
a `STORE` instead.

### 12.5 Exceptions

The records below are valid only with exceptions = 1.

| Op | Record | Rules |
| --- | --- | --- |
| 0xD7 | `TRY_BEGIN(flags)` | Opens the try body, a scope region. `FUNCTION_TRY` 0x01 is valid only in a constructor or destructor body (C1, C2, D2 or the only body). It comes right after the header (`PARAM`s, `FUNC_EH_SPEC`), and the `MEMINIT`/`VPTR_INIT` order and the destructor prologue continue inside the try body: a destructor's function-try encloses its cleanup prologue (clang's `EmitDestructorBody`). Other functions use an ordinary `TRY` around the body. |
| 0xD8 | `CATCH_BEGIN(g_ti\|nil)` | Closes the try body or the previous handler, and opens a handler (a scope region). nil is `catch (...)`, which must come last. Every `TRY` has at least one. It may be followed by `LOCAL l` and then `CATCH_PARAM`. The library defers the handler head (`construct_catch_param` or `begin_catch`, and the `cleanup.scope … cleanup all {end_catch}`) until it sees `CATCH_PARAM` or a record that is neither that `LOCAL` nor `CATCH_PARAM`: one record of lookahead. Without a catch-all, it adds `unwind {resume}`. |
| 0xD9 | `CATCH_PARAM(l, kind, g_copy\|nil)` | The kinds of `cir.init_catch_param`, chosen by the writer from the parameter's type: `reference_to_record_pointer` 7 (a reference to a pointer to a record); `reference_to_pointer` 6 (a reference to any other pointer); `reference` 0 (any other reference); `pointer` 1; `scalar` 2 (arithmetic, enumeration, `bool`, `nullptr_t`, a pointer to data member or to member function, as CIRGen classifies them); `trivial_copy` 4 (a class with a bitwise-copy initialization); `non_trivial_copy` 5 (a class initialized by a copy constructor). 3 is invalid. `g_copy` is required for 5 (the C1 copy constructor) and nil otherwise. The type of `l` is a record for 4 and 5, and a pointer to a pointer for 6 and 7 (the pin's verifier requires it). `construct_catch_param` starts the handler for kinds 5 and 6 only (6 without a copy function). Kind 7 binds the reference to a temporary copy of the pointer, so assigning through the reference does not change the exception object (upstream `EHABILowering`; a documented departure). An unnamed parameter still has its `LOCAL` and `CATCH_PARAM`. A destructible parameter gets the usual `CLEANUP_BEGIN(all)`, nested inside the `end_catch` scope. The writer diagnoses a catch copy with default arguments or temporaries. |
| 0xDB | `TRY_END` | Closes the last handler. The shape is `TRY_BEGIN … CATCH_BEGIN … [CATCH_BEGIN …] TRY_END`, like `IF_ELSE`/`IF_END`; 0xDA is unassigned. The value stack is empty at all three. `TRY`, `CATCH` and `RETHROW` are statement brackets for the switch rule. A constructor or destructor function-try handler ends with an explicit `RETHROW` (the FE's implicit rethrow). |
| 0xDC | `THROW_BEGIN(t)` | Pushes the glvalue of the new exception object (`cir.alloc.exception`) and begins an implicit `DEACTIVATABLE` `eh` cleanup that frees it (`__cxa_free_exception`, gap G1). It is not a region: the cleanups of the initializer's temporaries are ordinary cleanups of the enclosing `FULLEXPR` or scope region, so on the normal path they run during unwinding, as clang does (user decision 2026-10-12; g++ destroys them before the throw). If the initialization throws, they run first, then the object is freed. The initializer consumes the glvalue (`STORE INIT`, or as `this`), so the value stack at `THROW_END` equals that at `THROW_BEGIN`. Every `DEACTIVATABLE` cleanup begun after `THROW_BEGIN` must be deactivated (`CLEANUP_END`) before `THROW_END`, and `CLEANUP_END` never matches the implicit cleanup. Valid wherever an expression is (a `TERN` arm, the right operand of `LAND`/`LOR`, `MEMINIT`, `STATIC_INIT`), except in cleanup code. In a conditional region, a `THROW_BEGIN` needs an enclosing `FULLEXPR` in the same function or `STATIC_INIT` (as conditional cleanups do, 11.5), and the library guards the implicit cleanup as it guards a conditional `new` (12.6). |
| 0xDD | `THROW_END(g_ti, g_dtor\|nil)` | Deactivates the implicit free cleanup of its `THROW_BEGIN`, then `cir.throw %exn, @ti[, @dtor]` followed by `cir.unreachable`. `g_dtor` is the D1 of a class with a non-trivial destructor. `THROW_BEGIN`/`THROW_END` pairs nest like brackets. |
| 0xDE | `RETHROW` | `cir.throw` without operands, then `cir.unreachable`. Valid anywhere except in cleanup code (single-block). |

**Terminated blocks.** `THROW_END` and `RETHROW` terminate the current
block, as `UNREACHABLE` does. Records may follow, as after `UNREACHABLE`
(1.1 is unchanged): the consumer continues in a new block without
predecessors. A `TERN` arm or a `LAND`/`LOR` operand whose last record is
`THROW_END`, `RETHROW` or `UNREACHABLE`, with the value stack as it was at
the start of the arm, contributes no value, and the other arm decides the
result. If both arms are terminated, the `TERN` is void.

**Returns** go through the return slot while any cleanup or `TRY` is open
(amends 11.5).

### 12.6 Flags

| Record | Addition |
| --- | --- |
| `HDR_LANG` | `exceptions` = 1 is valid: functions and calls may throw unless nothrow, `eh` and `all` cleanups are live, the records of 12.5 are allowed, and the library runs with exceptions on. With exceptions = 0, the 1.0 rule stays (every function and call is nothrow) and the records of 12.5 are errors. The supp key `esk_nothrow` is a hint only. |
| `HDR_LANG` | `rtti` = 0: the RTTI slot of each vtable is null, typeinfo objects exist only for EH, and `TYPEID` and `DYNAMIC_CAST` are errors. `CLASS_MANGLE` is still required. |
| `DECL_FUNC` | `NOTHROW` 0x80, from the FE's `is_non_throwing_routine` only. A wrong `NOTHROW` silently skips cleanups. A re-issue never clears it. |
| `CALL`, `CALL_INDIRECT`, `PM_CALL` | `NOTHROW` 0x08: the call type is non-throwing (`is_nothrow_type`). A call is nothrow iff it has the flag or its callee's `DECL_FUNC` has `NOTHROW`. |
| `CALL` | `VIRTUAL` 0x10: argument 0 is already converted to `g`'s class, and the slot comes from `CLASS_VFUNCS`. The writer devirtualizes as clang does: it looks through the compiler-generated base-class conversion of the object, and the call is plain when the object is a complete object that is neither a reference nor an indirection (`local.f()`), when the object's class is `final`, or when the function is `final`. A plain call calls the final overrider of the named function in the object's class (from the FE's override list of that class), which is not the named function when a using-declaration names a base's function (`struct C : B { using A::f; }; C c; c.f();` calls `B::f`). When that final overrider's return type differs from the named function's (a covariant return), the call stays `VIRTUAL`, as in clang. |
| `CALL` | `VTT` 0x20: a `VTT_ADDR` operand is inserted between argument 0 and argument 1, and `nargs` still counts the parameters of `t_call`. Required iff the callee is the base variant of a `has_vbases` pair. |
| `DELETE` | `VIRTUAL` 0x08: a null check, then D0 through the vtable (the slot after D1's). The class must have a `STRUCTOR_DELETING`, and `g_delete_fn` is that record's `g_delete`. `GLOBAL` 0x10, only with `VIRTUAL`, is `::delete` of a polymorphic object: the library computes the complete object's address through the offset-to-top slot, calls D1 through the vtable, and then calls `g_delete_fn` (the global operator delete the FE chose) on that address, freeing it even if the destructor throws, as clang's `emitVirtualObjectDelete` does; the size operand of a sized global delete is the one clang passes. `DESTROYING` 0x20, never with `VIRTUAL`, is a destroying operator delete (C++20) of a class without a virtual destructor: a null check, then one call `g_delete_fn(ptr[, size][, align])` (the `std::destroying_delete_t` tag is empty and not passed); no destructor runs, so `g_dtor` is nil. With a virtual destructor, `delete p` stays `VIRTUAL` and D0 calls the destroying delete (`STRUCTOR_DELETING` flag `DESTROYING`). A destroying delete chosen as the deallocation function of a `new` expression is diagnosed by the writer. |
| `CLEANUP_BEGIN` | `DEACTIVATABLE` 0x01, on `eh` and `all` only. A deactivatable cleanup must be deactivated by its `CLEANUP_END` before its range ends (otherwise a later throw would run it a second time). Valid in function bodies and in `STATIC_INIT`, where the range ends with the `STATIC_INIT` region at the latest (`U g[3] = {U(1), U(2), U(9)};`, `T *p = new T(…);` at namespace scope). Cleanup code may read `LOCAL`s declared before the `CLEANUP_BEGIN`, as M3's `this`. The writer keeps what the free-on-exception and partial-aggregate cleanups need (the `operator new` result, an element pointer, the operands of a sized or placement delete) in `LOCAL`s, evaluated once. |
| `CLEANUP_END` | Deactivates the innermost *active deactivatable* cleanup begun in the same function, the same `FULLEXPR` (or none, if the cleanup was begun outside any) and the same conditional arm. It disables both the normal and the eh path; the cleanup's range still ends with its region. Without a match it is an error. Library: CIRGen's flag and `cir.if`. An unconditional `new` begins its delete cleanup right after the allocation call, outermost by LIFO (K4d). A conditional `new` begins it inside the arm, and the library hoists the guarded scope around the `FULLEXPR`, with function-level slots (`k4d2`). |
| `MEMINIT_BEGIN` | Kind `vbase` is valid in C1 of a `has_vbases` pair (12.3). |

### 12.7 The null data-member pointer

A null pointer to data member is −1, so a type that contains one anywhere
(a field, a base, an array element) is **not zero-initializable**. CIRGen
expands its zero into explicit null constants (`EmitNullConstant`); g++ does
the same, static zero-initialization included.
- **Library.**
  - It caches a `zero_initializable(t)` predicate.
  - For a type that is not zero-initializable, `C_ZERO`, `INIT_ZERO`, a
    definition without `VAR_INIT`, the unlisted parts of `C_AGG` and
    `INIT_CONST`, and the padding of `C_REPEAT` all produce the expanded
    null constant. Above 256 bytes, it is copied from a private constant.
  - `#cir.zero` stays valid for method pointers (null is `{0, 0}`) and for
    vptrs.
- **Format.** `ZERO_BYTES` of a glvalue whose type is not
  zero-initializable is an error.
- **Writer.** It uses `INIT_ZERO` on the typed glvalue of a complete object
  or member, and `INIT_ZERO_BASE` (0xA8) on the glvalue of a base subobject:
  it writes only the class's `nvsize` bytes (`data_size` when the class has
  no `CLASS_INFO`), as clang's `EmitNullBaseClassInitialization` does, so a
  virtual base or a field placed in the base's tail padding is not
  overwritten. `new S[n]{}` uses the shape of `new S[n]()`.
- `TO_BOOL` and `CMP eq`/`ne` accept method and data-member prvalues.
  Relational comparisons of them are errors.

**vptrs in constants.** No record names a vptr in a constant: the FE's
constants of dynamic classes have none (`throw Der()` is `{{}}`). A `C_AGG`,
`INIT_CONST` or `VAR_INIT` of a dynamic class never selects a `TY_VPTR`
field (`C_SEL` of one is an error). The library stores every vptr of the
object, as clang's `ConstStructBuilder` does: for a base subobject, from the
vtable group of the enclosing complete object; for a member, from the
member's own class. The writer sends the facts of every dynamic class whose
`C_AGG_BEGIN` it streams, and the constant uses the vtable of the complete
object's class and of each member's class (12.8); a base subobject's
`C_AGG_BEGIN` does not use the base's own vtable. `C_ZERO` of a dynamic
class, as a `VAR_INIT`, an `INIT_CONST` source or the unlisted part of a
`C_AGG`, is an error: a constant of a dynamic class lists its members. A
definition without `VAR_INIT` is zero-initialized as before (`#cir.zero`
for the vptr), and its constructor then sets the vptrs. A class with
virtual bases is never a literal type.

### 12.8 Emission and linkage (writer rules)

- **Key function** (clang's rule, decided at the end of the TU): the first
  virtual function declared in the class, in declaration order, that is not
  pure, not compiler-generated, not declared `inline` or `constexpr`, and
  not defined inside the class definition (the FE's
  `inline_in_class_definition`; a function defaulted or deleted in the class
  counts as defined inside). There is none if that function is later defined
  inline, and none for an implicit or explicit instantiation of a class
  template (an explicit specialization is an ordinary class).
- **Vtable linkage:**
  - key function defined here → external, defined;
  - key function defined elsewhere → an external declaration;
  - no key function, vtable used here → `linkonce_odr` with `COMDAT`;
  - explicit instantiation definition → `weak_odr` with `COMDAT`;
  - a TU-local class → internal.

  "Used" means a `VPTR_INIT`, a `VTT_ADDR`, a `C_AGG_BEGIN` of the class as
  a complete object or a member (not as a base subobject: 12.7), or a
  reference from a construction vtable or VTT. A virtual call alone does not
  make a vtable used. The writer decides it, and sends the vtable's
  `DECL_ABI_DATA` with that linkage.
- **`extern template`.** An instance of a class covered by an explicit
  instantiation declaration, and not explicitly instantiated here, gets an
  external vtable declaration and no typeinfo definition (clang).
- **The VTT and the construction vtables** follow the vtable. A class with
  virtual bases whose vtable is defined or referenced (`VTT_ADDR`, a
  structor of the class) has its VTT `DECL_ABI_DATA` with the vtable's
  linkage and flags. A construction vtable's RTTI slot is the typeinfo of
  its base class, so when the vtable of a class with virtual bases is
  defined here, the typeinfo of every base class that gets a construction
  vtable (an indirect base included) is declared. The **class
  typeinfo** of a dynamic class has the linkage that its vtable would have
  (key function defined here: external, defined; key function elsewhere: an
  external declaration; no key function: `linkonce_odr` with `COMDAT`,
  defined; explicit instantiation: `weak_odr`; TU-local: internal), whether
  or not the vtable itself is used or emitted here.
- **A defined vtable odr-uses every function in it** that is not pure or
  deleted. The writer declares each one, and streams the implicit
  definitions that the FE made (it makes them where a vtable is defined) and
  the inline ones of a `linkonce_odr` vtable. A function that is neither
  defined here, nor declared for another TU to define, nor pure or deleted
  makes the writer diagnose.
- **A dropped virtual function** (8.11).
  - A `linkonce_odr` or `weak_odr` vtable that would refer to a dropped
    definition is not defined here: it becomes an external declaration,
    together with its VTT and construction vtables, and the writer warns as
    for any dropped definition. The class typeinfo refers to no function
    and is unchanged.
  - An internal or external vtable is defined, and the library's rule for
    unsupported definitions applies to its references: a referenced dropped
    function is an error.
- **Typeinfo linkage** (clang's `getTypeInfoLinkage`):
  - fundamental types, and pointers to cv-unqualified or const fundamental
    types → external (the C++ runtime defines them);
  - a dynamic class → the linkage its vtable would have (above);
  - a TU-local type → internal;
  - a pointer or member pointer whose pointee chain contains an incomplete
    class → internal (masks in 12.3);
  - anything else → `linkonce_odr` with `COMDAT`, where used.

  The typeinfo objects of a TU are the closure over the thrown, caught and
  `typeid` types and their bases.
- **Thunks** are emitted with the overrider's definition, with its linkage
  and `COMDAT`. A thunk for a variadic function uses `musttail`.

### 12.9 Library-owned names

- **Names that the library defines or declares:**
  - the derived ABI names: `_ZTS`, `_ZTC<D><off>_<C>`, the thunks
    `_ZTh`/`_ZTv`/`_ZTc`, and the D0 bodies;
  - `__cxa_pure_virtual` and `__cxa_deleted_virtual`;
  - the `__cxxabiv1` vtables of the typeinfo classes;
  - `__clang_call_terminate` and `__gxx_personality_v0`;
  - `__cxa_allocate_exception`, `__cxa_free_exception`, `__cxa_throw`,
    `__cxa_rethrow`, `__cxa_begin_catch`, `__cxa_end_catch`,
    `__cxa_get_exception_ptr`, `__cxa_bad_cast`, `__cxa_bad_typeid`,
    `__cxa_call_unexpected` and `__dynamic_cast`;
  - the catch-copy thunk `__clang_cir_catch_copy_<mangling of the type>`:
    `linkonce_odr`, hidden (internal when the type is TU-local: 11.2,
    "internal wins"), `(ptr, ptr) -> void`, with the body `C1(dst, src)`;
    one per `g_copy` in a TU.
- **Clashes.** Each of these names reuses a stream entity of the same name
  and type. A different type uses replace-and-bitcast (R8). Only a clash with
  a stream *definition* is an error.

### 12.10 Worked encodings

These follow the notation of `research/m3_ecil_v1_design.md` §2. `pX` is
`X*`, `fv_X` is `void(X*)`, and `nae` is cleanup kind `all`. Records of the
declaration section are shown only where they matter.

**(1) A virtual call**

`struct B { virtual ~B(); virtual int f(int); int x; };`
`int g(B *p) { return p->f(1); }`
```
decl: TY_RECORD_DECL gB class "B"   TY_VPTR gVp
      TY_RECORD_BODY gB 16 12 8 NONTRIVIAL_ABI|NONTRIVIAL_DTOR 2
        REC_FIELD gVp 0 nil nil nil UNNAMED
        REC_FIELD int 8 nil nil "x" 0
      CLASS_MANGLE gB "1B"
      CLASS_INFO gB 12 8 nil 3
      CLASS_BASES gB []
      DECL_FUNC gBd1 … "_ZN1BD1Ev"  DECL_FUNC gBd2 … "_ZN1BD2Ev"  STRUCTOR gBd1 gBd2 DTOR
      DECL_FUNC gBd0 … "_ZN1BD0Ev"  STRUCTOR_DELETING gBd0 gBd1 g_ZdlPvm SIZED
      DECL_FUNC gBf fi_Bi external 0 "_ZN1B1fEi"
      CLASS_VFUNCS gB [gBd1 0 0 0, gBf 2 0 1]
      CLASS_OVERRIDES gB []
FUNC_BEGIN gg 0  PARAM l1 pB pB "p" 0
  LREF l1 LOAD  CONST_INT int 1  CALL gBf fi_Bi 2 VIRTUAL
  RETURN_STMT true
FUNC_END 0
```
B has a vptr of its own, so its layout facts (`CLASS_MANGLE`, `CLASS_INFO`,
`CLASS_BASES`) follow its body at once; the vtable facts and the
destructors come at the first need, the `CALL VIRTUAL`. The library loads
the vptr and calls through slot 2 (`cir.vtable.get_vptr`,
`cir.vtable.get_virtual_fn_addr`). Nothing makes B's vtable used here, so
no `DECL_ABI_DATA` is needed.

**(2) A constructor with `VPTR_INIT`**

`struct D : B { int y; D(); int f(int) override; };`
`D::D() : y(5) {}`, with `D::f` defined in this TU (its key function).
D shares B's vptr, so it has no `TY_VPTR` field. Its fields are [0] B
`BASE` @0 and [1] y @12.
```
decl: CLASS_MANGLE gD "1D"  CLASS_INFO gD 16 8 0 3  CLASS_BASES gD [0 PUBLIC]
      DECL_FUNC gDd0 … "_ZN1DD0Ev"  STRUCTOR_DELETING gDd0 gDd1 g_ZdlPvm SIZED
      CLASS_VFUNCS gD [gDf 2 0 1, gDd1 0 IMPLICIT 0]
      CLASS_OVERRIDES gD [gBf 0 gDf 0, gBd1 0 gDd1 0]
      DECL_ABI_DATA gTIB typeinfo gB external 0 nil
      DECL_ABI_DATA gTID typeinfo gD external DEFINED_HERE nil
      DECL_ABI_DATA gTVD vtable gD external DEFINED_HERE nil
FUNC_BEGIN gDc2 0  PARAM l1 pD pD "this" THIS
  MEMINIT_BEGIN base 0 compiler_generated
    LREF l1 LOAD BASE_CAST D B 0 NONNULL CALL gBc2 fv_B 1 0
  MEMINIT_END
  CLEANUP_BEGIN eh nil 0  LREF l1 LOAD BASE_CAST D B 0 NONNULL CALL gBd2 fv_B 1 0  CLEANUP_BODY
  VPTR_INIT
  MEMINIT_BEGIN field 1 0  LREF l1 LOAD DEREF FIELD D 1 CONST_INT int 5 STORE INIT none  MEMINIT_END
  BLOCK_BEGIN BLOCK_END
FUNC_END reachable
```
`VPTR_INIT` stores `vtable.address_point(@_ZTV1D, 0, 2)`. C1 is an alias of
C2, as in M3 (D has no virtual bases).

**(3) Virtual bases: C1, C2, D1 and D2**

`struct V { V(); ~V(); int a; };`
`struct L : virtual V { int l; L(); ~L(); };` and `R` likewise;
`struct X : L, R { int x; X(); ~X(); };`

X's fields are [0] L `BASE` @0, [1] R `BASE` @16, [2] x @28 and
[3] V `VIRTUAL_BASE` @32; `TY_RECORD_BODY gX 40 36 8 …`,
`CLASS_INFO gX 32 8 0 0`, `STRUCTOR gXc1 gXc2 HAS_VBASES`,
`STRUCTOR gXd1 gXd2 DTOR|HAS_VBASES`. X has no key function, so its
vtable, VTT and typeinfo are `linkonce_odr` with `COMDAT`
(`DECL_ABI_DATA gTVX vtable gX linkonce_odr DEFINED_HERE|COMDAT nil`, and
`gTTX vtt`). `VB` abbreviates `LREF l1 LOAD VBASE_CAST X V NONNULL|COMPLETE`.
```
FUNC_BEGIN gXc1 0  PARAM l1 pX pX "this" THIS
  MEMINIT_BEGIN vbase 3 compiler_generated  VB CALL gVc2 fv_V 1 0  MEMINIT_END
  CLEANUP_BEGIN eh nil 0  VB CALL gVd2 fv_V 1 0  CLEANUP_BODY
  MEMINIT_BEGIN base 0 compiler_generated
    LREF l1 LOAD BASE_CAST X L 0 NONNULL  VTT_ADDR base 0  CALL gLc2 fv_L 1 VTT
  MEMINIT_END
  CLEANUP_BEGIN eh nil 0  LREF l1 LOAD BASE_CAST X L 0 NONNULL VTT_ADDR base 0 CALL gLd2 fv_L 1 VTT  CLEANUP_BODY
  MEMINIT_BEGIN base 1 compiler_generated
    LREF l1 LOAD BASE_CAST X R 16 NONNULL  VTT_ADDR base 1  CALL gRc2 fv_R 1 VTT
  MEMINIT_END
  CLEANUP_BEGIN eh nil 0  LREF l1 LOAD BASE_CAST X R 16 NONNULL VTT_ADDR base 1 CALL gRd2 fv_R 1 VTT  CLEANUP_BODY
  VPTR_INIT
  BLOCK_BEGIN BLOCK_END
FUNC_END reachable
```
C2 (`gXc2`) is the same without the `vbase` group and its cleanup. The
library adds C2's VTT parameter: there, `VTT_ADDR base 0` is an address
inside the caller's VTT, and `VPTR_INIT` loads the vptrs from it. In C1,
`VTT_ADDR base 0` points into `_ZTT1X`, and `VPTR_INIT` uses the address
points of `_ZTV1X`.

D2 is the FE's body without the destruction of V. D1 is synthesized:
```
FUNC_BEGIN gXd2 0  PARAM l1 pX pX "this" THIS
  CLEANUP_BEGIN nae nil 0  LREF l1 LOAD BASE_CAST X L 0 NONNULL VTT_ADDR base 0 CALL gLd2 fv_L 1 VTT  CLEANUP_BODY
  CLEANUP_BEGIN nae nil 0  LREF l1 LOAD BASE_CAST X R 16 NONNULL VTT_ADDR base 1 CALL gRd2 fv_R 1 VTT  CLEANUP_BODY
  VPTR_INIT
  BLOCK_BEGIN … BLOCK_END
FUNC_END reachable           -- ~R(), then ~L()
FUNC_BEGIN gXd1 0  PARAM l1 pX pX "this" THIS
  CLEANUP_BEGIN nae nil 0  VB CALL gVd2 fv_V 1 0  CLEANUP_BODY
  LREF l1 LOAD  VTT_ADDR self nil  CALL gXd2 fv_X 1 VTT
FUNC_END reachable           -- D2, then ~V()
```

**(3b) A delegating constructor in a class with virtual bases**

`X::X(int) : X() {}`
```
FUNC_BEGIN gXic1 0  PARAM l1 pX pX "this" THIS  PARAM l2 int int nil 0
  MEMINIT_BEGIN delegation 0 0  LREF l1 LOAD CALL gXc1 fv_X 1 0  MEMINIT_END
  CLEANUP_BEGIN eh nil 0  LREF l1 LOAD CALL gXd1 fv_X 1 0  CLEANUP_BODY
  BLOCK_BEGIN BLOCK_END
FUNC_END reachable
FUNC_BEGIN gXic2 0  PARAM l1 pX pX "this" THIS  PARAM l2 int int nil 0
  MEMINIT_BEGIN delegation 0 0  LREF l1 LOAD VTT_ADDR self nil CALL gXc2 fv_X 1 VTT  MEMINIT_END
  CLEANUP_BEGIN eh nil 0  LREF l1 LOAD VTT_ADDR self nil CALL gXd2 fv_X 1 VTT  CLEANUP_BODY
  BLOCK_BEGIN BLOCK_END
FUNC_END reachable
```
Neither body has a `VPTR_INIT`; in the base variant, `VTT_ADDR self` is the
VTT parameter.

**(4) A thunk**

`struct A { virtual void f(); }; struct P { virtual void g(); };`
`struct C : A, P { void g() override; };`

No record names the thunk. `CLASS_OVERRIDES gC [gPg 8 gCg 0]` says that the
P subobject at offset 8 is overridden by `C::g` at offset 0. The library
emits `_ZThn8_N1C1gEv` (this − 8, then `C::g`) with `C::g`'s definition,
with its linkage and `COMDAT`, and puts it in P's slot of C's secondary
vtable.

**(5) `typeid`**

`typeid(int)`, and `typeid(*p)` with `p` a `B*`:
```
decl: DECL_ABI_DATA gTIi typeinfo int external 0 "_ZTIi"
      TYPEINFO_DEF gTIi fundamental 0 nil nil
TYPEID gTIi gTI 0                              [-> glv std::type_info]
LREF lp LOAD DEREF  TYPEID nil gTI NULL_CHECK  [glv B -> glv std::type_info]
```

**(6) `dynamic_cast`**
```
LREF lp LOAD  DYNAMIC_CAST gTIB gTID pD 0      -- dynamic_cast<D*>(p)
LREF lr LOAD  DYNAMIC_CAST gTIB gTID pD REF    -- dynamic_cast<D&>(r), then DEREF
LREF lp LOAD  DYNAMIC_CAST gTIB nil pv 0       -- dynamic_cast<void*>(p)
```

**(7) `throw T(A())`**

`struct A { A(); ~A(); }; struct T { T(const A&); ~T(); };`
```
FULLEXPR_BEGIN
  THROW_BEGIN gT                                 [-> glv exn; implicit free cleanup]
    ADDR
    LOCAL l2 gA nil 1 TEMPORARY  LREF l2 ADDR CALL gAc1 fv_A 1 0
    CLEANUP_BEGIN nae nil 0  LREF l2 ADDR CALL gAd1 fv_A 1 0  CLEANUP_BODY
    LREF l2 ADDR CALL gTc1 fv_TcA 2 0           [consumes exn]
  THROW_END gTIT gTd1                            -- deactivates the free, throws
FULLEXPR_END
```
The library allocates the object and protects it with a flag-guarded free
cleanup. If `T(const A&)` throws, `~A()` runs, then the object is freed.
Otherwise `THROW_END` clears the flag and throws with `@_ZTI1T` and
`@_ZN1TD1Ev`, and `~A()` runs during unwinding
(`std::uncaught_exceptions()` is 1 there, as with clang; g++ gives 0).

**(8) try, catch and rethrow**

`try { g(); } catch (const E &e) { h(e); } catch (S s) { use(s); } catch (...) { throw; }`,
where S has a copy constructor and a destructor.
```
TRY_BEGIN 0
  BLOCK_BEGIN CALL gg fv 0 0 BLOCK_END
CATCH_BEGIN gTIE
  LOCAL l3 pE "e" 8 0  CATCH_PARAM l3 reference nil
  BLOCK_BEGIN LREF l3 LOAD CALL gh fv_pE 1 0 BLOCK_END
CATCH_BEGIN gTIS
  LOCAL l4 gS "s" 4 0  CATCH_PARAM l4 non_trivial_copy gScc1
  CLEANUP_BEGIN nae nil 0  LREF l4 ADDR CALL gSd1 fv_S 1 0  CLEANUP_BODY
  BLOCK_BEGIN LREF l4 ADDR CALL guse fv_pS 1 0 BLOCK_END
CATCH_BEGIN nil
  BLOCK_BEGIN RETHROW BLOCK_END
TRY_END
```
S's handler starts with `construct_catch_param` through the copy thunk
`__clang_cir_catch_copy_1S`, and `~S()` runs before `__cxa_end_catch`.

**(9) A function-try-block constructor**

`struct S2 : Q { M m; S2() try : Q(1) {} catch (...) { log(); } };`, with Q
and M not dynamic.
```
FUNC_BEGIN gS2c2 0  PARAM l1 pS2 pS2 "this" THIS
TRY_BEGIN FUNCTION_TRY
  MEMINIT_BEGIN base 0 0  LREF l1 LOAD BASE_CAST S2 Q 0 NONNULL CONST_INT int 1 CALL gQc2 fv_Qi 2 0  MEMINIT_END
  CLEANUP_BEGIN eh nil 0  LREF l1 LOAD BASE_CAST S2 Q 0 NONNULL CALL gQd2 fv_Q 1 0  CLEANUP_BODY
  MEMINIT_BEGIN field 1 compiler_generated  LREF l1 LOAD DEREF FIELD S2 1 ADDR CALL gMc1 fv_M 1 0  MEMINIT_END
  CLEANUP_BEGIN eh nil 0  LREF l1 LOAD DEREF FIELD S2 1 ADDR CALL gMd1 fv_M 1 0  CLEANUP_BODY
  BLOCK_BEGIN BLOCK_END
CATCH_BEGIN nil
  BLOCK_BEGIN CALL glog fv 0 0  RETHROW BLOCK_END   -- the FE's implicit rethrow ends the handler block
TRY_END
FUNC_END reachable
```

**(10) `new T(A())` with a deactivatable free**

`p = new T(A());`, where `T(const A&)` may throw.
```
FULLEXPR_BEGIN
  LREF lp
  LOCAL l9 size_t nil 8 TEMPORARY  LREF l9 CONST_INT size_t 1 STORE INIT none
  LOCAL l5 pv nil 8 TEMPORARY
  LREF l5  LREF l9 LOAD  CALL g_Znwm fpv_z 1 0  STORE INIT none
  CLEANUP_BEGIN eh nil DEACTIVATABLE  LREF l5 LOAD LREF l9 LOAD CALL g_ZdlPvm fv_pvz 2 0  CLEANUP_BODY
  LREF l5 LOAD CAST bitcast pT
  DUP
  LOCAL l6 gA nil 1 TEMPORARY  LREF l6 ADDR CALL gAc1 fv_A 1 0
  CLEANUP_BEGIN nae nil 0  LREF l6 ADDR CALL gAd1 fv_A 1 0  CLEANUP_BODY
  LREF l6 ADDR CALL gTc1 fv_TcA 2 0
  CLEANUP_END                                    -- the free is deactivated
  STORE 0 none
FULLEXPR_END                                     -- ~A()
```
If `T(const A&)` throws, `~A()` runs, then `operator delete`. The free
cleanup is begun right after the allocation, so it is the outermost of the
full-expression (K4d: g++ and clang free the storage last). In this
configuration the free is the sized `operator delete(void*, size_t)` with
the size given to `operator new`, kept in a local. An unsized `operator
delete` (a class's `operator delete(void*)`, or sized deallocation off) has
no size operand, an aligned one also takes the alignment, and a placement
`operator delete` takes the placement operands, kept in locals.

**(11) A member-function-pointer call**

`int (S::*pm)(int) = &S::f;  (obj.*pm)(3);`, with `S::f` non-virtual.
```
LOCAL l7 gPMf "pm" 8 0
LREF l7  CONST_METHOD gPMf gSf 0 0  STORE INIT none
LREF lobj ADDR  LREF l7 LOAD  CONST_INT int 3  PM_CALL fi_Si 2 0
```
`gPMf` is `TY_METHOD gPMf fi_Si gS`. The object pointer comes first; the
library emits `cir.get_method` and an indirect call.

**(12) A constant of a dynamic class**

`struct K { constexpr K() {} virtual int f() const; int x = 1; };`
`struct KD : K { constexpr KD() {} int f() const override { return 2; } };`
`constexpr KD kd;` at namespace scope. K's fields are [0] its vptr and
[1] x; KD's is [0] K `BASE`.
```
decl: CLASS_* of K and KD (first need: the C_AGG_BEGIN below)
      DECL_ABI_DATA gTVKD vtable gKD linkonce_odr DEFINED_HERE|COMDAT nil
      DECL_ABI_DATA gTIK typeinfo gK external 0 nil       -- K's key function f is elsewhere
      DECL_ABI_DATA gTIKD typeinfo gKD linkonce_odr DEFINED_HERE|COMDAT nil
DECL_VAR gkd gKD internal DEFINITION|CONST 8 "_ZL2kd"
C_AGG_BEGIN gKD
  C_SEL 0  C_AGG_BEGIN gK  C_SEL 1 C_INT int 1  C_AGG_END
C_AGG_END
VAR_INIT gkd
```
`KD::f` is defined in the class, so KD has no key function: its vtable is
`linkonce_odr`, and this constant uses it. No record selects K's vptr
field. The library stores the address point of
`_ZTV2KD` into it: a base subobject takes the vtable group of the complete
object, and K's own vtable is not used by this constant.

**(13) A conditional `new`**

`q = c ? new T(A()) : nullptr;`
```
FULLEXPR_BEGIN
  LREF lq  LREF lc LOAD TO_BOOL  TERN_BEGIN pT
    <the records of (10) from the size LOCAL to CLEANUP_END, leaving pT>
  TERN_ELSE  CONST_NULL pT
  TERN_END  STORE 0 none
FULLEXPR_END
```
The `CLEANUP_BEGIN(eh, DEACTIVATABLE)` and its `CLEANUP_END` are both in
the arm. The library keeps the allocation's address and the cleanup's flag in
function-level slots, and hoists the guarded scope around the `FULLEXPR`,
so that on a throw `~A()` runs before the storage is freed, as g++ does
(`k4d2_cond_new_order`).

**(14) Zero-initialization with a data-member pointer**

`struct H { int S::*pm; int n; };`
```
H h{};               -- a local; the FE gives a partial aggregate constant {}
LOCAL l8 gH "h" 8 0
LREF l8  C_AGG_BEGIN gH C_AGG_END  INIT_CONST
H *p = new H();      -- value-initialization; the new pointer is on the stack
DUP DEREF INIT_ZERO
```
In both, the library stores `{-1, 0}`, not zero bytes: the unlisted members
of the constant, and `INIT_ZERO` of the typed glvalue, take the expanded
null constant (12.7). `ZERO_BYTES` on `h` is an error.
