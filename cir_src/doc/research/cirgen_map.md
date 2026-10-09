# CIRGen map: what clang does for C, what we reuse, what we write

Source: `clang/lib/CIR/CodeGen` at llvm-project 894bfff5ca9fbe8ffc4171d5f5a4e68d11537f6d
(read from `~/src/llvm-project-cir`).  Companion to `cir_api_inventory.md`.

**Reuse rule.**  Nothing in `lib/CIR/CodeGen` is installed or linkable apart from the
`clangCIR` archive that comes along transitively; and every component takes
`clang::ASTContext`, `QualType`, `Decl`, `Expr`, `GlobalDecl`.  So *code* is
re-implemented against ECIL records (with attribution where an algorithm is copied);
what we reuse as-is is the **public** dialect API, the `CIRBaseBuilderTy` helpers,
`cir::setMLIRDataLayout`, the passes, and `cir::CIRDataLayout` for size/alignment
queries.  Line counts are the upstream sizes (all of CodeGen is 66k lines, mostly
C++/OpenMP/OpenACC/CUDA/SYCL we never touch).

| Upstream component | LOC | Does for C | Reuse | We write (lib/cirgen) |
| --- | --- | --- | --- | --- |
| `CIRGenModule` | 4500 | module attrs, global var/func creation, linkage/visibility/dso_local, string literals, deferred emission, `release()` | attr names + setters (API); `cir::setMLIRDataLayout` | `ModuleEmitter` |
| `CIRGenTypes` | 941 | `convertType` (QualType -> mlir::Type), function types, record type creation | type constructors (API) | `TypeConverter` |
| `CIRGenRecordLayoutBuilder` | 1320 | record -> member list + padding + bit-field units; field index maps | algorithm (copy w/ attribution) | `RecordLowering` (over ECIL layout facts) |
| `CIRGenFunction` | 1836 | prologue (arg allocas, `__retval`), implicit return, `LexicalScope`, symbol table | allocation-point helper (API) | `FunctionEmitter` + frame stack |
| `CIRGenStmt` | 1401 | compound/if/for/while/do/switch/case/break/continue/goto/label/return | shapes only | statement handlers of the frame machine |
| `CIRGenDecl` | 1438 | local `VarDecl`: alloca, init, static locals, `emitAutoVarInit` | shapes only | `VarEmitter` |
| `CIRGenExpr` | 3273 | lvalues: decl refs, member/subscript/deref, bit-field lvalues, load/store of scalars, calls | shapes only | `LvalueEmitter` / value-category machinery |
| `CIRGenExprScalar` | 3355 | scalar rvalues: arithmetic, comparisons, casts, `?:`, `&&`/`||`, inc/dec, compound assign | cast-kind table (copy) | `ScalarEmitter` |
| `CIRGenExprAggregate` | 1407 | struct/array init lists, aggregate copy, zero-fill, aggregate `?:`/call/return | `cir.copy`/`libc.memset` recipe | `AggregateEmitter` |
| `CIRGenExprConstant` | 1702 | constant initializers -> attributes (ints, fp, strings, arrays, records, address constants) | attribute constructors (API); string/record logic (copy) | `ConstantEmitter` (attr stack) |
| `CIRGenCall` | 1252 | `FuncInfo` arrangement, argument/return coercion bookkeeping, call op | **not needed**: CallConvLowering does the ABI | `CallEmitter` (plain signatures) |
| `CIRGenBuiltin` | ~3500 | builtin lowering (huge) | ops (API) | `BuiltinEmitter` (minimal set) |
| `CIRGenCleanup` + `EHScopeStack` | 930 | cleanups for C++/`cleanup` attr; **nothing for plain C** | -- | deferred to M3 (`cir.cleanup.scope`) |
| `CIRGenerator`/`CIRGenAction` | 216/~500 | driver: init context, register dialects, run passes, backend | the recipe | `driver/` (pipeline) |
| `CIRGenCXXABI`, `ItaniumCXXABI`, `VTables`, `Class`, `Exception`, `Coroutine` | ~8000 | C++ | -- | M3/M4 |

## 1. Per component

### CIRGenModule
For C: sets the attributes of section 7 of the inventory in the constructor
(`CIRGenModule.cpp:85-230`) and `cir.module_asm`/`cir.record_layouts` in `release()`.
Functions are created as declarations at the end of the module (`createCIRFunction`) with
`private` visibility and external linkage; `getOrCreateCIRFunction` creates them lazily on
first reference (so a callee seen before its definition is a declaration that later
gets a body; type mismatch handling replaces it: `replaceUsesOfNonProtoTypeWithRealFunction`
for K&R).  Globals: `getOrCreateCIRGlobal` (declaration) then
`emitGlobalVarDefinition` (initializer attr, `constant`, linkage, `dso_local`, alignment,
`common` linkage rule, section).  String literals are uniqued by content
(`constantStringMap`), named `.str`, `.str.N` (`getUniqueGlobalName`), private constant.
Deferred decl emission (inline functions emitted only when used) is a classic-clang
mechanism; **we do not need it**: the FE decides what to stream (REFS/MAINTAIN_NEEDED
discussion belongs to the writer).

**We write `ModuleEmitter`**: owns the `MLIRContext`, module, `EcilBuilder`, symbol maps
(G-id -> `cir.func`/`cir.global`, name -> op, `NAME_BIND` rename at `F_END`), the unique-name
generator, the record-layout table that becomes `cir.record_layouts`, and error/diagnostic
collection.  Rules to copy: public/private visibility from linkage
(`getMLIRVisibilityFromCIRLinkage`), `dso_local` (`shouldAssumeDSOLocal`: local linkage or
non-default visibility), global declarations private, `nothrow` on C definitions,
`no_proto` for K&R, `inline_kind` mapping.

### CIRGenTypes
`convertType`: `void`->`!cir.void`; `_Bool`->`!cir.bool`; `char` -> `!s8i`/`!u8i`
(signedness from the target); integer widths -> `!s16i/!s32i/!s64i/!s128i` and
unsigned; `float/double` -> `!cir.float/!cir.double`; `long double` ->
`!cir.long_double<!cir.f80>`; pointer -> `!cir.ptr<T>` (incomplete record pointee is
fine); `void*` -> `!cir.ptr<!cir.void>`; array -> `!cir.array<T x N>` (flexible array `x 0`);
function -> `!cir.func<(...) -> R>` (`isVarArg`); enum -> its underlying int type;
records -> identified `StructType/UnionType` (see below).  In memory `_Bool` stays
`!cir.bool`.  CIRGenTypes also keeps a `recordDeclTypes` map and defers laying out a record
until all its by-value field types are complete (`isSafeToConvert`) to cope with recursive
types; with ECIL `TY_RECORD_DECL` (incomplete, G-id) then `TY_RECORD_BODY` (members), the
FE ordering already guarantees completeness so we create the incomplete type at
`TY_RECORD_DECL` and `complete()` it at `TY_RECORD_BODY` in a single pass (no recursion:
member types are G-ids already converted).  Type naming: tag name, typedef name for
anonymous records, else `anon`, uniqued with `.N` (`getRecordTypeName`).

**We write `TypeConverter`** as a flat `std::vector<mlir::Type>` indexed by G-id, filled
in stream order; `TY_*` introduction records are processed in a loop, never by recursion.

### CIRGenRecordLayoutBuilder
See inventory section 6 for the algorithm.  Inputs from the stream per record:
size, data size, alignment, per field (type G-id, byte offset or bit offset+width,
name, unnamed, empty-for-ABI), kind (struct/union), `packed`/`aligned` already folded
into offsets.  **We write `RecordLowering`** (about 450 lines): the non-discrete
`accumulateBitFields` access-unit algorithm (needs the target's register width = 64 and
`hasCheapUnalignedBitFieldAccess` = true for x86-64 from the target block),
`determinePacked`, `insertPadding`, `lowerUnion`, `setBitFieldInfo`, plus the
`cir.record_layouts` entry.  It uses `cir::CIRDataLayout` (public) to get member sizes and
alignments exactly as upstream (so padding decisions agree with the backend's layout).
Test oracle: compare against clang's `-emit-cir` struct types for a corpus of C structs
(packed, aligned, bit-field runs, zero-width, unions, flexible arrays) in `test/cirgen`.

### CIRGenFunction
Prologue (`startFunction`/`emitFunctionProlog`): entry block from `fn.addEntryBlock()`;
for each parameter an alloca named after the parameter at the function entry, then a store
of the block argument; for a non-void return type an alloca `__retval` created *after* the
parameter allocas; `main` with implicit return zero stores 0 into `__retval`.  Body.
Epilogue: `LexicalScope::cleanup` -- if control can fall off the end (current block has
no terminator) emit the implicit return: load `__retval` and `cir.return` (C) -- for C++ a
non-void function falls to `cir.trap` (-O0) / `cir.unreachable`.  At the end of every
region: add `cir.yield` to unterminated blocks and erase empty predecessor-less blocks
(`terminateStructuredRegionBody`, `eraseEmptyAndUnusedBlocks`).

**We write `FunctionEmitter`** with the explicit frame stack described in section 2.

### CIRGenStmt
(`CIRGenStmt.cpp`)  Every C `{}` compound statement is a `cir.scope` (unless it is the
function body or a switch/loop body that already has a region); `if`, `for`, `while`, `do`
and `switch` each get an *enclosing* `cir.scope` that holds the init statement and the
condition variable (so locals declared in the `for` init live in that scope); the
loops/ifs themselves are `cir.for/while/do/if`.  `return` stores to `__retval`, loads,
`cir.return` *in place* (no return block) and opens a fresh block.  `break/continue/goto`
are terminators followed by a fresh block.  `case/default` are `cir.case` regions (see
section 2.6).  Labels: new block + `cir.br` + `cir.label`.  Statement attributes
(`[[likely]]`, `musttail`, asm) -- v0 ignores/diagnoses.

### CIRGenDecl
`emitAutoVarAlloca`: `cir.alloca "name"` with alignment `getDeclAlign` at the scope entry
block; `emitAutoVarInit`: no init -> nothing; scalar init -> `store`; aggregate init
list -> **element-wise stores into the alloca** via the aggregate emitter (there is *no*
memcpy-from-constant-global optimization at this SHA: `emitAutoVarInit: constant
aggregate` is NYI and `trivialAutoVarInit` modes are NYI); string-literal init of char
arrays -> aggregate init; `static` locals -> a `cir.global` named `fn.var` (uniqued) with
internal linkage (initializer through the constant emitter) and uses via
`cir.get_global` (`static_local`); `extern` block-scope declarations -> `get_global`.
VLAs: NYI.  `__attribute__((cleanup))`: NYI.

### CIRGenExpr / CIRGenExprScalar
`emitLValue`: decl refs to locals -> the alloca address; to globals -> `cir.get_global`;
to functions -> `cir.get_global` of the function (type `!cir.ptr<!cir.func<..>>`); `*p` ->
the pointer value itself (as an address); `s.f` -> `cir.get_member` (struct) with the CIR
member index; `p->f` -> `get_member` on the loaded pointer; `a[i]` -> pointer decay +
`cir.ptr_stride` (index sign/zero-extended to 64 bits); compound literal -> alloca +
init (file-scope: NYI); string literal -> `get_global` of `.str` (an address of
`!cir.array<..>` which decays); bit-field -> `LV::BitField` (section 2.5).
Scalar loads/stores: `cir.load` / `cir.store` with `align` and `volatile`; no TBAA.
Scalar rvalue ops: integer `+ - *` as `cir.add/sub/mul` with `nsw` for signed C types
(unless `-fwrapv`), `/ %` `cir.div/rem`, bit ops `cir.and/or/xor`, `~` `cir.not`, shifts
`cir.shift` (operand converted to the left type), comparisons `cir.cmp` then `cir.cast
bool_to_int` for the C `int` result (or consumed directly as the `!cir.bool` condition of
`cir.if/ternary/for`), `!x` -> `int_to_bool`/`cir.not` then `bool_to_int`, unary minus
`cir.minus nsw`, `++/--` `cir.inc/dec nsw` (+store), pointer `+/-` int `cir.ptr_stride`
(negated stride for `-`), pointer difference `cir.ptr_diff`, floating `cir.fadd..`,
casts per the cast-kind table (inventory section 5), conditional `?:` per the shape
rules (select if both arms cheap, otherwise `cir.ternary`), `&&`/`||` as `cir.ternary`
over `!cir.bool` (the "false"/"true" arm is a constant), comma (evaluate and discard),
assignment (store; value of the expression is the stored value for scalars, and
`cir.set_bitfield`'s result for bit-fields), compound assignment (load, op, store).
`sizeof/alignof/offsetof/enum constants` are FE-folded to `cir.const`.
**We write `ScalarEmitter` + `LvalueEmitter`** as record handlers (no `Visit*` recursion).

### CIRGenExprAggregate
Struct/array init lists: zero the object first when the init list is partial
(`emitNullInitialization` = `cir.libc.memset` with value 0 and the type size, element
stores afterwards); copy of record values (assignment, by-value pass, return) =
`cir.copy` (with alignments); union init = initialize the active member; array
`[N]` with repeated filler = loop (NYI upstream for non-constant: `VisitArrayInitLoopExpr`)
-> v0: emit element stores, zero-fill the rest.  Aggregate `?:` and aggregate
call results go through a destination slot (alloca) in upstream; for ECIL we model
**aggregate values as addresses** (section 2.4).
**We write `AggregateEmitter`.**

### CIRGenExprConstant
`tryEmitAbstract`: builds `cir::IntAttr/FPAttr/BoolAttr/ConstPtrAttr`, string literals
(`getString`: `ConstArrayAttr` with a `StringAttr` and `trailing_zeros`), arrays
(`ConstArrayAttr` of element attrs; bulk-zero tail via `trailingZerosNum`; all-zero ->
`ZeroAttr`), records (`buildRecordHelper`: one element per byte-owning member
including pads as `ZeroAttr`, bit-fields packed into the unit integer by
`updateBitfieldInit`, union = single element of the active member), address constants
(`ConstantLValueEmitter` -> `GlobalViewAttr` with indices / `GlobalOffsetAttr`),
integer-cast of an address.  NYI at this SHA: constant `call expr`/`block expr` lvalues,
flexible-array-member initializers in globals, designated-init-update.
**We write `ConstantEmitter`** over an **attribute stack** separate from the value stack:
ECIL constant records (`C_INT`, `C_FP`, `C_STR`, `C_ARRAY_BEGIN/END`, `C_RECORD_BEGIN/END`,
`C_GADDR(gid, path)`, `C_NULL`, `C_ZERO`) push/pop attrs; global definitions and static
locals consume the top attribute.

### CIRGenCall
`arrangeFunctionCall` builds a `CIRGenFunctionInfo` (ABI arg infos) for classic
parity; at this SHA CIRGen emits **plain** signatures (the record type as the parameter
type, by value) and `CallConvLowering` later rewrites declarations, definitions and call
sites for x86-64 SysV (coerced eightbytes, `byval`/sret, variadic `al` count, `va_arg`
expansion).  Therefore the call emitter is small: convert each argument value to the
callee parameter type (integer widening is NYI upstream because the AST already inserts
the casts; we receive explicit cast records from the FE), for a direct call
`createCallOp(loc, fnOp, args)`; indirect `createIndirectCallOp(loc, fnPtr, funcType,
args)`; void result -> no result; record result -> call returns the record value, we
spill it to a temp alloca and treat it as an address; add `nothrow` when exceptions are
off.  K&R unprototyped calls: NYI upstream -> diagnose.  `noreturn`: unreachable after
the call (`emitUnreachable`).  Parameter attrs (`llvm.noundef/noalias/nonnull`,
sign/zero-ext) are optional.
**We write `CallEmitter`** (about 200 lines).

### CIRGenBuiltin (minimal set)
The 3.5k-line file is dominated by x86/ARM/AMDGPU intrinsics.  The set a C
run-pass corpus needs: `__builtin_va_start/va_end/va_copy` (-> `cir.va_start/end/copy` on
the decayed `va_list` pointer), `__builtin_trap` (`cir.trap`), `__builtin_unreachable`
(`cir.unreachable`), `__builtin_expect` (`cir.expect`, or pass-through),
`__builtin_memcpy/memmove/memset` (`cir.libc.memcpy/memmove/memset`),
`__builtin_abs/labs`, `__builtin_clz/ctz/popcount/bswap/ffs/parity` (dedicated ops),
`__builtin_constant_p` (`cir.is_constant` or FE-folded), `__builtin_alloca` (supported through
`emitBuiltinAlloca`, a dynamic-size `cir.alloca`; `_with_align` is NYI), `__builtin_object_size`, `__builtin_huge_val/inf/nan` (FE-folded constants),
`__builtin_fabs/sqrt/...` (libm-style ops), `__builtin_offsetof` (folded).  Everything
else falls back to an **external call to the library function** (clang's behavior when
no inline expansion exists) or a diagnostic.  The FE should send classified builtins
(an enum in the stream) so cirgen does not match on names.
**We write `BuiltinEmitter`** as one switch over the builtin enum, growing on demand.

### CIRGenCleanup
Not applicable to C beyond `__attribute__((cleanup))` (NYI upstream).  For C++ (M3) the
design is in `clang/docs/CIR/CleanupAndEHDesign.md`: cleanups are **regions**
(`cir.cleanup.scope` with a body and a cleanup region, `cir.begin_cleanup/end_cleanup`),
not blocks, and branches through cleanups are resolved during `FlattenCFG`; this maps
directly onto our frame stack as a `CLEANUP_SCOPE` frame kind.  Reserve the kind now;
implement in M3.

## 2. Non-recursive frame-stack emitter

### 2.1 Machinery

```
a_value        { mlir::Value v; a_value_kind kind; a_type_id ty; bitfield_info bf; }
a_frame        { a_frame_kind kind; mlir::Operation *op; mlir::Block *alloca_block;
                 unsigned phase; size_t value_base; mlir::OpBuilder::InsertPoint outer_ip; ... }
state          { std::vector<a_frame> frames; std::vector<a_value> values;
                 std::vector<mlir::Attribute> attrs; per-function id maps; ...}
driver loop    while ((rec = cursor.next())) dispatch(rec.opcode, rec);
```
Every handler runs in constant stack: it pops/pushes `values`, may push/pop `frames`, creates
ops with the builder, and returns.  Heavy MLIR work (op verification at the end of each
function, passes) is separate and on the big-stack worker thread.  `value_base` lets a frame
check at close that the value stack has the expected height (checking builds).
Allocation: `frames`, `values` reserved to typical depth and grow on demand
(heap).  Unwinding on error: pop frames to the function frame, erase the half-built
`cir.func` body, and turn the function into a declaration (clean diagnostic).

### 2.2 Frame kinds and how regions open/close

All structured ops are created **with no-op callbacks** (so the builder still creates the
region blocks) or via `OperationState`, then the insertion point is moved into the region;
the frame close adds the terminator and moves the insertion point after the op.

| Frame kind | Opened by (ECIL) | Op created | Phases / close |
| --- | --- | --- | --- |
| `FUNCTION` | `FUNC_BEGIN` | `cir.func` (+ `addEntryBlock`, prologue allocas/stores) | body; at `FUNC_END` implicit return if the block is unterminated, erase dead blocks, verify the function |
| `SCOPE` | `BLOCK_BEGIN` (block with decls or the enclosing scope of if/for/switch) | `ScopeOp::create(b, loc, noop)`, IP saved inside | `BLOCK_END`: `cir.yield` (terminate region), IP after op. Blocks without declarations/labels may skip the scope (v0: always emit; `CIRSimplify` folds trivial scopes: `ScopeOp::fold`) |
| `IF` | `IF_BEGIN` (pops the condition, converted to `!cir.bool`) | `IfOp::create(b, loc, cond, /*withElse=*/false, noop, noop)` | phase THEN until `IF_ELSE`: terminate then-region, `createBlock(&ifOp.getElseRegion())`, phase ELSE; `IF_END`: terminate current region, IP after op |
| `TERNARY` | `TERN_BEGIN` (pops cond; result type id in the record, void allowed) | `OperationState` + `b.create` (inventory sec. 4) | `TERN_ELSE` pops the true value, `cir.yield %v`, opens region 1; `TERN_END` pops the false value, yields, IP after op, **pushes the op result** |
| `LAND` / `LOR` | `LAND_BEGIN`/`LOR_BEGIN` (lhs already a `!cir.bool`) | `cir.ternary` over bool: LAND true-arm = rhs, false-arm = `false` const; LOR true-arm = `true` const, false-arm = rhs | `*_END` pops rhs bool, yields, pushes `bool_to_int` result (C `int`) |
| `FOR` | `FOR_BEGIN` | `createFor(loc, noop, noop, noop)`; the init statement was emitted by the enclosing `SCOPE` before it | `FOR_COND`... `FOR_COND_END` pops cond -> `cir.condition`; `FOR_BODY`; `FOR_STEP`... `FOR_STEP_END` yield; `FOR_END`: IP after op |
| `WHILE` / `DO` | `WHILE_BEGIN` / `DO_BEGIN` | `createWhile` / `createDoWhile` (region order **body, cond** for `do`) | as FOR |
| `SWITCH` | `SWITCH_BEGIN` (pops integer cond; bool -> `u1`) | `SwitchOp::create(b, loc, cond, noop-with-state)` | see 2.6 |
| `CASE` | `CASE_BEGIN(kind, values)` | `CaseOp::create(b, loc, ArrayAttr, kind, ip)` | see 2.6 |
| `CLEANUP_SCOPE` | (M3) | `cir.cleanup.scope` | reserved |

Non-region constructs (loops' `break/continue`, `return`, `goto`, `label`) are plain
records handled inside whichever frame is current.  Insertion-point bookkeeping uses
`OpBuilder::InsertionGuard` only inside a single handler; across handlers the builder's own
point is the state, and frames store the `outer_ip` they must restore.

### 2.3 Value stack and control-flow expressions
Post-order records push/pop `a_value`s.  Each conditional value-producing construct
(`TERNARY`, `LAND`, `LOR`) checks `values.size() == frame.value_base + 1` at its close:
anything pushed inside an arm must be consumed by its yield.  Because arms are separate
regions, a value pushed in one arm is never legally used in the other; after `*_ELSE` the
emitter *forgets* arm-local values (pop to `value_base`).

### 2.4 Lvalues (addresses) vs rvalues
`a_value_kind`: `RVALUE` (scalar SSA value), `ADDR` (a glvalue: SSA pointer to the object,
also used for **aggregate-valued** expressions, which are "in memory"), `BITFIELD`
(an address of the access unit + bit info), `VOID`.
* `LREF(lid)` pushes `ADDR` (the local's alloca); `GREF(gid)` pushes `ADDR` = `cir.get_global`
  (functions: the function pointer value as `RVALUE`, since `&f`/decay is not an lvalue
  load); `DEREF` retags an `RVALUE` pointer as `ADDR` (no op); `ADDR_OF` retags `ADDR` as
  `RVALUE` pointer (no op).
* `FIELD(field)` on `ADDR` of a record pushes `ADDR` (`cir.get_member`); on a bit-field
  pushes `BITFIELD`.
* `LOAD` on `ADDR`: scalar type -> `cir.load` -> `RVALUE`; aggregate type -> **no load**,
  the value stays `ADDR` (consumers copy); `LOAD` on `BITFIELD` -> `cir.get_bitfield`.
* `STORE`/`ASSIGN`: scalar -> `cir.store` (value pushed back as `RVALUE` for expressions);
  aggregate -> `cir.copy(dst, src)` (src `ADDR`); `BITFIELD` -> `cir.set_bitfield`.
* Aggregate rvalues from calls/ternaries/casts are materialized in a temp alloca and
  pushed as `ADDR`.  For an aggregate `?:`: arms yield `!cir.ptr<record>` (addresses) and the
  result is an `ADDR`.  Passing a record argument: `cir.load` of the address immediately
  before the call (value semantics are what `CallConvLowering` expects).
* Side-effect ordering is exactly record order; the FE chooses C's unspecified order.

### 2.5 Bit-field lvalues
`FIELD` carries the field id; the emitter looks it up in the record's `RecordLowering`
result: CIR member index of the access unit (`fieldIdxMap`; **union: the source field
index**), unit storage type, `offset` (bit offset inside the unit, from LSB;
`fieldBitOffset - unitStartBits`, reversed `storageSize - (offset+size)` for big-endian),
`size` (clamped to the unit width), `isSigned` (declared type signedness), name.
Steps: `get_member base[unitIdx] {name = field}` with result `!cir.ptr<storageTy>`
(bitcast if the element type differs) -> `BITFIELD{addr, info, align = alignment of the
unit within the record}` on the stack.  `LOAD` -> `cir.get_bitfield` typed as the declared
(converted) field type (`int`, `unsigned`, `_Bool`, enum underlying).  `STORE` ->
`cir.set_bitfield`, whose result is the value the field now holds (the assignment's
value); `++/--/op=` are get, op, set.  Volatile bit-fields set `is_volatile`; AAPCS
"volatile access width" variants are ARM-only (skip).  Zero-width and unnamed
bit-fields have no `FIELD` references.

### 2.6 Switch, case, fallthrough, break
Upstream shape (`emitSwitchStmt`, `emitSwitchBody`, `emitCaseDefaultCascade`): a
`cir.scope` holds the switch (its init/condition variable); inside it `cir.switch(%cond)`
has one body block containing **sibling `cir.case` ops**; each case region runs
until its terminator: `cir.yield` = fall through to the next case in the switch body,
`cir.break` = leave the switch, `return`/`goto` as usual.  Algorithm for the
frame machine:
* `SWITCH_BEGIN`: pop cond, create `cir.switch` (callback no-op creates the body block),
  frame keeps `body_block`, `case_ip` (insertion point at the end of the most recent
  top-level case region, initially unset), `saw_case` = false.  IP := body block.
* Statements *before* the first case label (legal C, unreachable unless jumped to) are
  emitted into the body block; at the first `CASE_BEGIN` a new block is created in the
  body region and a `cir.br` links to it (upstream does this for pre-case statements).
* `CASE_BEGIN(kind, values)` where the **parent frame is the switch's own statement
  list** (a top-level case): terminate nothing; restore IP to the body block end; create
  `CaseOp::create(b, loc, ArrayAttr, kind, ip)`; kind `Equal` with `[IntAttr(condTy, v)]`,
  `Range` with `[lo, hi]` (GNU `a ... b`), `Default` with `[]`, `Anyof` unused by CIRGen
  but legal for merged labels; IP := the case region.  Consecutive labels
  (`case 1: case 2: stmt`) become consecutive sibling cases (the first is yielded empty,
  falling into the next: "simple form").
* `CASE_END` (the labelled substatement has been emitted): at top level, **keep emitting
  into the same case region** (statements following until the next label belong to it:
  the fallthrough structure), i.e. save `case_ip` and leave the builder there.
  For a **nested** case (a label inside an `if`/loop/block within the switch: Duff's
  device), `CaseOp::create` is emitted at the current IP in that inner region and only the
  labelled substatement lives inside; `CASE_END` restores the outer IP.
* `SWITCH_END`: for every `cir.case` (collect them with `SwitchOp::collectCases`) and for
  the body region, add `cir.yield` to unterminated blocks and erase empty unreachable
  blocks (`terminateStructuredRegionBody`); set `all_enum_cases_covered` if known; IP after
  the switch op; the enclosing `SCOPE` closes normally.
* `cir.break` is created in whatever region is current (inside a case, nested `if`, etc.)
  and is verified to be inside a loop/switch (`AncestorOneOf`).  `continue` likewise
  needs an enclosing loop ancestor.
The ECIL must therefore bracket the labelled substatement (`CASE_BEGIN ... CASE_END`)
and tell the emitter whether the label is directly in the switch body (derivable from the
frame stack: parent frame kind == SWITCH or SCOPE-directly-under-SWITCH).

### 2.7 Block-scope allocas and `HoistAllocas`
Each region-owning frame records `alloca_block` = the first block of its region.
`LVAR_DECL` creates `cir.alloca "name" align(n)` at
`getBestAllocaInsertPoint(frame.alloca_block)` (after the last alloca/label in that block)
inside an `InsertionGuard`: exactly `CIRGenFunction::emitAlloca`.  Parameters and `__retval`
go to the function entry block.  Temporaries (aggregate call results, compound literals)
use the innermost region frame.  The `HoistAllocas` pass (first in
`populateCIRPreLoweringPasses`, i.e. inside `lowerDirectlyFromCIRToLLVMIR`) moves every
non-dynamic alloca to the function entry block ahead of the control flow so that SROA/
mem2reg can promote them; so we get scoped CIR **and** an LLVM-friendly entry block with
no work of our own.  `static` locals are not allocas but globals.  `cir.alloca`
`init`/`const` unit attributes are optional documentation.

### 2.8 goto and labels
`GOTO(label_id)` -> `cir.goto "name"` (a terminator) followed by a fresh empty block;
`LABEL(label_id)` -> if the current block is non-empty (or is the function entry),
create a new block in the same region, `cir.br` to it, set IP there, then
`cir.label "name"`.  Label names are function-scoped strings (`FuncOp::verify` checks that
every goto has a label; duplicates are an error), so use the source name when available else
`L<id>`.  Gotos may cross region boundaries (into/out of nested `cir.scope`, `if`, loop
bodies); `FlattenCFG` + `GotoSolver` turn them into branches after the structured ops are
flattened; **jumping *into* a scope past a declaration with non-trivial lifetime** is not
checked (C has no destructors; VLAs are NYI).  `cir.indirect_goto`/`cir.block_address`
(computed goto) can come later.  After any terminator the emitter keeps a fresh block so
subsequent (dead) code has an insertion point; dead empty blocks are erased at region
close.

### 2.9 Declarations, globals and function bodies in the stream
`F_DECLS`/`F_GLOBALS` records create types, function declarations (`cir.func` with no
body, private), and global definitions (constant attr from the attribute stack, or zero)
before bodies use them (each frame is declaration section then body section, per the
plan).  A function referenced before its definition is a declaration; the definition adds
the body to the same op and switches visibility to public.  `NAME_BIND` renames at `F_END`
(`replaceAllSymbolUses`), as `CIRGenModule` does for mangling replacements.

## 3. Proposed lib/cirgen components (responsibilities)

| File / class | Responsibility |
| --- | --- |
| `CIRCompat.h` | the only place that spells churn-prone upstream names (create-signature wrappers, enum names, interface casts) |
| `EcilBuilder.h/.cpp` | `: cir::CIRBaseBuilderTy`; local copies of `getString`, `getConstRecordOrZeroAttr`, `getCompleteNamedRecordType`, `getIncompleteRecordTy`, `createGetBitfield/SetBitfield`, region-open helpers (`open_scope`, `open_if`, `open_ternary`, ...) |
| `ModuleEmitter` | context + module, module attrs, G-id tables, global/function creation, linkage/visibility policy, unique names, `NAME_BIND`, `cir.record_layouts`, `release()` equivalent, diagnostics |
| `TypeConverter` | `TY_*` records -> `mlir::Type` table (G-id indexed); function types; incomplete->complete records |
| `RecordLowering` | the layout algorithm, `a_record_layout` result (member index, bit-field infos) |
| `ConstantEmitter` | attribute stack handlers: ints/fp/strings/arrays/records/address constants; zero-fill |
| `FunctionEmitter` | `FUNC_BEGIN/END`, prologue/epilogue, frame stack, per-function maps (L-id -> address/value), dead-block cleanup |
| `StmtHandlers` | scope/if/loops/switch/case/break/continue/goto/label/return handlers |
| `ExprHandlers` | value-stack handlers: lref/gref/field/deref/load/store/ops/casts/ternary/land/lor/call/va_*; bit-field lvalues |
| `AggregateHandlers` | init lists, copies, zero-fill, aggregate temporaries |
| `CallHandlers` | direct/indirect calls, plain signatures, varargs |
| `BuiltinHandlers` | minimal builtin enum switch |
| `ItaniumABI` | M3+ (vtables, ctors/dtors variants); empty in v0 |
| `driver/Pipeline` | big-stack worker, `runCIRToCIRPasses`, lowering, `TargetMachine` emission, C API |

## 4. Open issues for the orchestrator (ECIL v0)

1. Record bodies must carry: size, **data size**, alignment, per-field offset/bit-offset,
   width, `unnamed`, `empty_for_abi`, type; the record-level `arg_passing_kind`/
   `has_trivial_dtor` (C: constants).
2. Target block: pointer/int/long/size_t widths, endianness, data layout string, register
   width, `hasCheapUnalignedBitFieldAccess`, `char` signedness, `long double` format,
   `va_list` shape (x86-64: `[1 x __va_list_tag]` with the 4-member record; the FE must
   introduce that record type), triple, `target_abi`.
3. `CASE` must bracket its substatement; label ids -> strings; switch condition type
   after integral promotion; `_Bool` switch conditions.
4. Explicit conversion records (integral promotion, usual arithmetic conversions, bool
   conversions, array-to-pointer, function-to-pointer, default argument promotions) --
   CIRGen relies on the AST having made them explicit.
5. Aggregate-valued expression convention (addresses), `?:` over aggregates.
6. `nsw` vs wrapping arithmetic per operation (type signedness and `-fwrapv`),
   shift-operand conversions, pointer-arithmetic element sizes are implicit in
   `cir.ptr_stride`'s pointee type (the FE must not send byte offsets).
7. Builtin enum list for the minimal builtin set.
