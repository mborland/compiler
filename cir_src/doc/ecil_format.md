# ECIL — EDG CIR IL stream format

Status: **v0.3** (Phase R; it addresses gate R blockers R1–R8 and follow-ups F1–F6). Owner: the
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
  - In every space, 0 or nil means "none".
- **Names.**
  - A declaration carries its final mangled (or C) name sid when it is
    known. Otherwise it carries nil, and `NAME_BIND(g, sid, linkage)`
    supplies the name later. Until then the consumer uses the placeholder
    `__ecil.<gid>`.
  - A definition still unnamed at `F_END` is a format error.
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
| `END(n_frames, status)` | Only in `F_END`. `status` 0 = ok. |
| `HDR_TARGET(s triple, s\|nil target_abi, s\|nil datalayout, ptr_bits, reg_bits, flags)` | Flags: big-endian, char signed, cheap unaligned bit-field access. `target_abi` is `""` for plain x86-64 SysV. |
| `HDR_LANG(lang, std, flags)` | `lang`: `elang_c` or `elang_cxx`; `std`: year (e.g. 2011). Flags: wrapv, exceptions, rtti, threadsafe statics. |
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

**Locals and temporaries**
- `LOCAL(l, t, s|nil, align, flags=0)` declares an automatic variable at
  that point of the body, in the current block. It may appear between
  expression records, because it does not touch the value stack.
- `MATERIALIZE_TEMP(l, t)` creates temporary `l` in the current block,
  stores the popped prvalue and pushes its glvalue.
- The consumer places allocas in the current scope; HoistAllocas moves
  them later.

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
  statements.
- **switch:** the condition is already promoted.
- **jumps:** `RETURN_STMT` pops a value that is already converted. Label
  lids share the frame's L-id space. `GOTO` may jump into or out of
  blocks; C has no cleanups, so `n_cleanups` is 0.

**Switch strategy.** A case label may sit directly in the switch body or
inside a plain block in it; the writer diagnoses Duff's device. The
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
| `BUILTIN(id, nargs, t\|nil)` | args… → result. v0 ids: `expect`, `unreachable`, `trap`. Library builtins (`__builtin_memcpy`, `__builtin_printf`, `__builtin_abort`, …) are sent as ordinary `CALL`s to the library function. |

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
- **Default.** A dropped definition (function or variable, any linkage) is
  an error, `EDG_CIR_ERROR_UNSUPPORTED`, naming each entity. Tests then
  report the unsupported construct, not a later link failure.
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
- **v0.x streams are pre-release.** Writer and reader in one tree always
  match.

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
