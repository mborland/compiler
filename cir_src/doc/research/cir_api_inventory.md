# CIR API inventory (llvm-project 894bfff5ca9fbe8ffc4171d5f5a4e68d11537f6d)

Scope: the public ClangIR API that `cir_src` (EDG-CIRGen) builds against, for
the v0 C subset. Everything here was read from the pinned sources in the
sparse reference checkout `~/src/llvm-project-cir` (paths are relative to it).
Nothing was compiled: the toolchain was not yet installed when this was
written.  Items marked **(unverified)** are inferred, not read.

## 0. What is installed vs. lib-private

The consumer sees only what `clang`'s and `mlir`'s `install(DIRECTORY include/...)`
rules ship (`clang/CMakeLists.txt:446-463`: source `include/clang/**.h|.def` plus
generated `build/tools/clang/include/clang/**.inc|.h`).

| Header | Installed | Notes |
| --- | --- | --- |
| `clang/CIR/Dialect/Builder/CIRBaseBuilder.h` | yes | `cir::CIRBaseBuilderTy`; **header-only** |
| `clang/CIR/Dialect/IR/{CIRDialect,CIRAttrs,CIRTypes,CIROpsEnums,CIRDataLayout}.h` + generated `CIROps.h.inc` etc. | yes | the op/type/attr classes |
| `clang/CIR/CIRToCIRPasses.h`, `LowerToLLVM.h`, `CIRDataLayoutSpec.h`, `InitAllDialects.h`, `MissingFeatures.h` | yes | pipeline entry points |
| `clang/CIR/Dialect/Passes.h` | yes | individual pass factories |
| `lib/CIR/CodeGen/*.h` (`CIRGenBuilder.h`, `CIRGenModule.h`, ...) | **no** | lib-private; needs `ASTContext`; cannot be reused, only copied with attribution |

Consequence for the plan: `EcilBuilder : cir::CIRBaseBuilderTy` is right, but
`CIRGenBuilderTy` helpers (`getString`, `getConstRecordOrZeroAttr`,
`createGetBitfield`/`createSetBitfield` wrappers, `getCompleteNamedRecordType`,
`getIncompleteRecordTy`, `getUniqueRecordName`, `getZeroInitAttr` for records is in
base) must be re-implemented in `EcilBuilder`.

`CIRBaseBuilder.h` itself includes `clang/AST/CharUnits.h`,
`clang/Basic/AddressSpaces.h`, `clang/Basic/LangOptions.h` and `llvm/IR/FPEnv.h`,
so even header-only use needs clang's include dirs (and its generated `.inc`
headers) on the include path.  It does not need any clang object to exist at
run time, except see the `TargetInfo` note in section 9.

## 1. Context and dialect registration

```cpp
#include "clang/CIR/InitAllDialects.h"
mlir::MLIRContext ctx(mlir::MLIRContext::Threading::DISABLED);   // see below
cir::registerAllDialects(ctx);                  // CIR, DLTI, LLVM, OpenMP, OpenACC
ctx.loadDialect<mlir::DLTIDialect, cir::CIRDialect>();   // as CIRGenerator::Initialize
```

`cir::registerAllDialects(DialectRegistry&)` / `(MLIRContext&)`
(`lib/CIR/RegisterAllDialects.cpp`): inserts `BuiltinDialect, CIRDialect,
DLTIDialect, LLVM::LLVMDialect, omp::OpenMPDialect, acc::OpenACCDialect` plus the
OpenMP/OpenACC CIR extensions.  The LLVM dialect is required to *parse* CIR data
layouts.  `CIRGenerator` additionally loads OpenACC/OpenMP dialects eagerly.

**Threading finding (new, not in plan).**  The MLIR pass manager runs
function-level passes on the context's `ThreadPool`.  Those pool threads use the
default stack size, so a big-stack *driver* thread does not cover deep
verifier/pass recursion on pool threads.  Create the context with
`Threading::DISABLED` (or `ctx.disableMultithreading()`), then everything
(builder, verifier, all passes, lowering) runs on the one big-stack worker.
`CIRGenerator` leaves multithreading on; we should not.

## 2. Types (all in namespace `cir`)

| Type | Creation |
| --- | --- |
| `IntType` | `IntType::get(ctx, width, isSigned)` (+ optional `is_bit_int`); `!s8i ... !u128i` |
| `BoolType` | `BoolType::get(ctx)`; bools stay `!cir.bool` **in memory too** (`emitToMemory/FromMemory` are identity) |
| `SingleType/DoubleType/FP80Type/FP128Type/FP16Type/BF16Type` | `T::get(ctx)`; C `long double` is `LongDoubleType::get(ctx, FP80Type)` (x86) |
| `VoidType` | `VoidType::get(ctx)`; `void*` is `PointerType::get(VoidType)` |
| `PointerType` | `PointerType::get(pointee)` / `(pointee, addrSpaceAttr)` ; builder `getPointerTo(ty)` |
| `ArrayType` | `ArrayType::get(elt, size)`; size 0 = flexible array member |
| `FuncType` | `FuncType::get(inputs, returnType, isVarArg=false)`; a `void` return becomes "no result". Function pointer = `PointerType::get(FuncType)` |
| `StructType` / `UnionType` | see section 6; `cir::RecordType` is a *view class* over both |
| `BitFieldType` | member type of a bit-field access unit, see section 6 |
| `ComplexType`, `VectorType` | exist (complex has full CIRGen support at this SHA) but are v1+ for us |

Unsized/incomplete: `StructType::get(ctx, nameAttr, isClass)` makes an identified
incomplete type; the type is MLIR-"mutable" so `complete()` later fills it.

## 3. `cir::CIRBaseBuilderTy` (public builder)

`class CIRBaseBuilderTy : public mlir::OpBuilder` -- constructor takes `MLIRContext&`
or an `OpBuilder&`.  Public helpers actually useful to us:

| Group | Helpers |
| --- | --- |
| constants | `getConstant(loc, TypedAttr)`, `getConstantInt(loc, ty, int64)`, `getConstAPInt(loc, ty, APInt)`, `getSignedInt/getUnsignedInt(loc, v, bits)`, `getNullValue(ty, loc)`, `getZeroInitAttr(ty)` (int/fp/complex/array/vector/pointer/record/bool -> attr; **records/arrays give `#cir.zero`**), `getBool/getTrue/getFalse(loc)`, `getCIRBoolAttr`, `getConstPtrAttr(ptrTy, int64)`, `getConstNullPtrAttr(ptrTy)` |
| types | `getBoolTy`, `getVoidTy`, `getUIntNTy(n)`, `getSIntNTy(n)`, `getPointerTo(ty[, as])`, `getVoidPtrTy()` |
| memory | `createAlloca(loc, ptrTy, name, IntegerAttr align[, dynSize])`, `createLoad(loc, ptr, isVolatile, align)`, `createAlignedLoad`, `createStore(loc, val, dst, isVolatile, isNontemporal, IntegerAttr align, scope, order)` (auto-bitcasts dst if pointee differs), `createCopy(dst, src, isVolatile, skipTailPadding)`, `createGetMember(loc, resultPtrTy, base, name, idx)`, `createPtrStride(loc, base, stride)`, `createGetGlobal(globalOp[, threadLocal])`, `createGlobal(module, loc, name, type, isConstant, linkage, addrSpace)` (inserts at start of module body), `getGlobalViewAttr(...)` (4 overloads), `getAlignmentAttr(int64 / Align / CharUnits)` |
| arithmetic | `createAdd/Sub/Mul(loc, l, r, OverflowBehavior)` + `createNSW*/NUW*`; `createDiv`, `createRem` (integer; fp is `createFAdd/FSub/FMul/FDiv/FRem/FNeg`); `createAnd/Or/Xor` (**bitwise**; `cir.and` etc.), `createNot`, `createInc/Dec/Minus(loc, v, nsw)`, `createShift(loc, v, amt, isLeft)`/`createShiftLeft/Right`, `createCompare(loc, CmpOpKind, l, r)` -> `cir.cmp` (`!cir.bool`), `createSelect(loc, cond, t, f)`, `createLogicalAnd/Or` (as select; CIRGen does not use them for C `&&`, it uses `cir.ternary`), `createMax/Min` |
| casts | `createCast(loc, CastKind, src, ty)` (identity if same type), `createIntCast`, `createBoolToInt`, `createPtrToBoolCast`, `createIntToPtr`, `createPtrToInt`, `createBitcast`, `createPtrBitcast(src, pointeeTy)`, `createBoolIntToIntCast` |
| calls | `createCallOp(loc, SymbolRefAttr/FuncOp, [retTy,] operands, attrs, argAttrs, resAttrs, callingConv)`; `createIndirectCallOp(loc, calleeValue, FuncType, operands, ...)` |
| control flow | `createFor/While/DoWhile(loc, condCb, bodyCb[, stepCb])` (callbacks run **immediately** inside the builder), `createCondition(bool)`, `createYield(loc, values)`, `createBreak(loc)`, `createContinue(loc)`, `getBestAllocaInsertPoint(block)` (static) |

`OverflowBehavior` flags: `None`, `NoSignedWrap`, `NoUnsignedWrap`, `Saturated`.

Call-site gotchas: `createAlloca` overloads with `CharUnits` vs `IntegerAttr`; pass
`getAlignmentAttr(n)` to avoid dragging in `CharUnits`.  `createStore`'s alignment
parameter is an `IntegerAttr`, not an int.

## 4. Region-carrying ops and the non-recursion problem

Every structured op's builder takes **callbacks that run inside the builder**
(`IfOp::create(b, loc, cond, withElse, thenCb, elseCb)`,
`ScopeOp::create(b, loc, cb)`, `ForOp/WhileOp/DoWhileOp::create(b, loc, condCb, bodyCb, stepCb)`,
`SwitchOp::create(b, loc, cond, cb(b, loc, OperationState&))`,
`TernaryOp::create(b, loc, cond, trueCb, falseCb)`).  The callbacks create the
blocks and may emit arbitrary code, which is how clang recurses.  We want to open
a region, return to the driver loop, and close it later.  Verified techniques:

1. **No-op callbacks.**  The builders still `createBlock` for every region
   (`CIRDialect.cpp:1889-1927` `IfOp`, `1929-1955` `ScopeOp`, `ForOp::build` in
   `CIROps.td:2465`, `SwitchOp:2275`).  After the call, set the insertion point
   into `op.getThenRegion().front()` etc. and keep going.  Upstream itself does this
   for compound statements (`CIRGenStmt.cpp:148-164`: a `ScopeOp` callback only
   does `scopeInsPt = b.saveInsertionPoint()`, then `restoreInsertionPoint`).
   Terminators are *not* added by the no-op callback; the frame close must add
   `cir.yield`.
2. **`cir.if` else region** is created lazily: with `withElseRegion=false` the else
   region is empty; at ECIL `ELSE` do `builder.createBlock(&ifOp.getElseRegion())`.
3. **`cir.ternary` cannot use its builder**: it infers the result type from the
   `cir.yield` operands *after* the callbacks ran, so no-op callbacks give a
   void ternary.  Build with `mlir::OperationState` instead:
   `OperationState st(loc, cir::TernaryOp::getOperationName()); st.addOperands(cond);
   st.addTypes(resultTy); st.addRegion(); st.addRegion(); Operation *op = b.create(st);`
   then `createBlock` each region, emit, `cir.yield %v`.  Same trick works for any
   op whose builder is callback-only.
4. **`cir.case`** has `CaseOp::create(b, loc, ArrayAttr values, CaseOpKind kind,
   InsertPoint &out)`: creates the region block and returns its insertion point
   (the builder's own point is restored).  No callback.
5. **`cir.global`** has optional `ctor/dtor` callbacks; with none the initial value
   goes in the `initial_value` attribute (`setInitialValueAttr`).

## 5. v0 op set

Notation: `Op::create(b, loc, ...)` is the generated static creator;
"builder" means a `CIRBaseBuilderTy` helper.  Shapes are what clang's CIRGen
emits for C (checked in `clang/test/CIR/CodeGen/*.c|.cpp`).

### Functions, globals, calls

| Op | Create / attrs | Notes |
| --- | --- | --- |
| `cir.func` | `FuncOp::create(b, loc, name, FuncType[, linkage[, callingConv]])`; then `addEntryBlock()` (adds block args from the type). Setters: `setLinkageAttr(GlobalLinkageKindAttr)`, `setNoProto(true)`, `setDSOLocal(true)`, `setInlineKind(InlineKind::{NoInline,AlwaysInline,InlineHint})`, `setAlignment(n)`, `setGlobalVisibility(VisibilityKind)`, `SymbolTable::setSymbolVisibility(f, Public/Private)`. Discardable attr `nothrow` via `f->setAttr(CIRDialect::getNoThrowAttrName(), UnitAttr)` | CIRGen creates every function as **declaration with `private` MLIR visibility** (`createCIRFunction`), and switches to `public` when it gets a body or is external-linkage definition (`getMLIRVisibilityFromCIRLinkage`: internal/private -> private, others -> public). C definitions get `nothrow` when exceptions are off. Varargs = `FuncType` `isVarArg`. K&R unprototyped = `no_proto` (calls through unprototyped types are NYI upstream; ECIL uses the variadic-bitcast rule of spec 8.8, which avoids the NYI path). |
| `cir.global` | builder `createGlobal(module, loc, name, ty, isConst, linkage, addrSpace)` then `setInitialValueAttr(attr)`, `setAlignmentAttr(i64)`, `setConstant`, `setDSOLocal`, `setVisibility(SymbolTable::Visibility)`, `setSection`, `setTlsModel` | no initial value + external linkage = declaration (private visibility). Definitions: public for external linkage, private for internal/private. `common` linkage (tentative defs) with non-null init is demoted to `weak` by CIRGen. |
| `cir.get_global` | `builder.createGetGlobal(globalOp)` or `GetGlobalOp::create(b, loc, ptrTy, symName, tls)`; works for **functions** too (function address = `get_global @f : !cir.ptr<!cir.func<...>>`) | static locals: same, but do **not** set `static_local` for unguarded C statics (the verifier requires a guard; spec 8.2) |
| `cir.call` | `builder.createCallOp(loc, FuncOp, operands, ...)`; indirect: `createIndirectCallOp(loc, fnPtrValue, FuncType, operands)`; result via `.getResult()` (optional) | call attrs `nothrow`/`nounwind`. By-value structs are passed as loaded record values; `CallConvLowering` rewrites them. Variadic: callee type keeps only declared params; extra operands trail. |
| `cir.return` | `ReturnOp::create(b, loc, {value})` | emitted **directly** at the return statement (no shared return block): store to `__retval` alloca, `load`, `cir.return`, then open a fresh block |
| `cir.unreachable` / `cir.trap` | `UnreachableOp::create(b, loc)`, `TrapOp::create(b, loc)` | terminators; `__builtin_unreachable`, `__builtin_trap` |

### Memory and addresses

| Op | Create | Notes |
| --- | --- | --- |
| `cir.alloca` | `builder.createAlloca(loc, ptrTy, name, alignAttr)`; unit attrs `init`, `const` | `alignment` is mandatory. Locals are created at the entry block of the **innermost scope region** (`CIRGenFunction::emitAlloca`, `CIRGenExpr.cpp:2744`); the `HoistAllocas` pass (runs in lowering) hoists to the function entry. Params get an alloca + store in the prologue; non-void functions get `__retval`. |
| `cir.load` / `cir.store` | `createLoad/createStore`; `is_volatile`, `alignment`, `is_nontemporal`; atomic ordering attrs exist | `cir.load` result type = pointee |
| `cir.const` | `getConstant(loc, attr)` with `IntAttr`, `FPAttr`, `BoolAttr`, `ConstPtrAttr`, `ZeroAttr`, `UndefAttr`, `PoisonAttr` | |
| `cir.get_member` | `createGetMember(loc, ptrToMemberTy, base, name, idx)` | `idx` is the **member index in the CIR record** (not the source field index) except unions, where CIRGen passes the field index. For a bit-field the result type is `!cir.ptr<storage int>`. |
| `cir.get_bitfield` / `cir.set_bitfield` | `GetBitfieldOp::create(b, loc, resTy, addr, storageTy, name, size, offset, isSigned, isVolatile, align)`; `SetBitfieldOp::create(b, loc, resTy, addr, storageTy, src, name, size, offset, isSigned, isVolatile, align)` (returns the stored value as `resTy`) | wraps `#cir.bitfield_info<name, storage_type, size, offset, is_signed>` (`BitfieldInfoAttr`). `addr` is the `get_member` of the access unit. `offset` is the bit offset inside the unit from the LSB (reversed for big-endian). The result type is the *declared* field type (int/enum/bool). |
| `cir.ptr_stride` | `builder.createPtrStride(loc, ptr, intIdx)` | result type = base type. `a[i]`: `cast array_to_ptrdecay` then `ptr_stride`. Pointer +/- int uses it (negate for `-`). |
| `cir.ptr_diff` | `PtrDiffOp::create(b, loc, resIntTy, lhs, rhs)` | lowering divides by pointee size |
| `cir.copy` | `builder.createCopy(dst, src, isVolatile, skipTailPadding)`; set `dst_alignment/src_alignment` | aggregate assignment and by-value record copy. `cir.libc.memcpy/memmove/memset` are separate ops (`MemSetOp::create(b, loc, dst, align, val(u8), len)`); zero-init of aggregates (`emitNullInitialization`) is `libc.memset` of the object |

### Operators

| Op | Create | Notes |
| --- | --- | --- |
| `cir.cast` | `createCast(loc, kind, src, ty)` | kinds used by C: `integral`, `int_to_bool`, `bool_to_int`, `int_to_float`, `float_to_int`, `float_to_bool`, `floating`, `ptr_to_int`, `int_to_ptr`, `ptr_to_bool`, `array_to_ptrdecay`, `bitcast`, `address_space`. Not casts: lvalue-to-rvalue (= load), function-to-pointer (= get_global). Float cast kinds take `fenv`. |
| `cir.cmp` | `createCompare(loc, CmpOpKind::{lt,le,gt,ge,eq,ne}, l, r)` | result `!cir.bool`; a C `int` result is `bool_to_int` of it. Operands: int, fp, pointer, bool |
| `cir.add/sub/mul` | `createAdd/Sub/Mul(..., OverflowBehavior)` | `nsw` when the C type is signed int (no `-fwrapv`); `nuw` is not set by clang for C |
| `cir.div/rem` | `createDiv/createRem` | integer only |
| `cir.fadd/fsub/fmul/fdiv/frem/fneg` | `createFAdd` ... | |
| `cir.and/or/xor/not` | `createAnd/Or/Xor/Not` | bitwise; `cir.not` also works on bool |
| `cir.shift` | `createShift(loc, value, amount, isLeft)` | |
| `cir.inc/dec/minus` | `createInc/Dec/Minus(loc, v, nsw)` | `++`/`--` produce inc then store |
| `cir.select` | `createSelect(loc, cond, t, f)` | CIRGen uses it for `?:` when both arms are "cheap" (`isCheapEnoughToEvaluateUnconditionally`) |
| `cir.ternary` | OperationState (section 4) | `&&`/`||` are *also* `cir.ternary` over `bool` with a `true/false` constant arm (`VisitBinLAnd/LOr`), then `bool_to_int` |
| `cir.va_start/va_end/va_copy/va_arg` | `VAStartOp::create(b, loc, ptr)`, `VAEndOp`, `VACopyOp::create(b, loc, dst, src)`, `VAArgOp::create(b, loc, resTy, ptr)` | `ptr` is `!cir.ptr<!rec___va_list_tag>` obtained by `array_to_ptrdecay` of the `va_list` array; `va_arg` is expanded to the SysV algorithm by CallConvLowering (x86_64 only) |
| `cir.expect`, `cir.is_constant`, `cir.stacksave/stackrestore`, bit ops (`clz/ctz/popcount/...`) | builtin-specific | minimal builtin set only |

### Control flow

| Op | How to open | Terminators / shape |
| --- | --- | --- |
| `cir.scope` | `ScopeOp::create(b, loc, [&](OpBuilder&, Location){})`; scope body block is `getScopeRegion().front()` | closed with `cir.yield` (or `yield %v` for a value-producing scope, e.g. statement expr, NYI for us). Every C compound statement and `if`/`for`/`switch` statement gets an enclosing `cir.scope` upstream. |
| `cir.if` | `IfOp::create(b, loc, boolCond, withElse, noop, noop)` | then/else regions end with `cir.yield` (`cir::buildTerminatedBody`) |
| `cir.for` | `ForOp::create(b, loc, noop, noop, noop)` | regions `cond` (ends `cir.condition(%bool)`), `body` (ends `cir.yield`), `step` (ends `cir.yield`); optional `cleanup` region |
| `cir.while` | `WhileOp::create(b, loc, noop, noop)` | `cond` / `body` |
| `cir.do` | `DoWhileOp::create(b, loc, noop, noop)` | **region order is body then cond** |
| `cir.switch` | `SwitchOp::create(b, loc, cond, [](OpBuilder&, Location, OperationState&){})` | cond is an int (bool is coerced to `u1` first); single body region |
| `cir.case` | `CaseOp::create(b, loc, ArrayAttr, CaseOpKind::{Default,Equal,Anyof,Range}, InsertPoint&)` | `Equal`: `[#cir.int<v>]`; `Range` (GNU `case a ... b`): `[lo, hi]`; `Default`: `[]`. Case regions end with `cir.yield` (**fallthrough to the next sibling case**) or `cir.break`/`return`/`goto`. Cases normally are siblings in the switch body block; cases nested inside other statements (Duff's device) are nested `cir.case` ops. Statements before the first case sit in the switch body block before a `cir.br` to a new block. `setAllEnumCasesCovered` is optional. |
| `cir.break` / `cir.continue` | `createBreak/createContinue` | terminators; legal inside loop/switch (any depth in regions); open a fresh block after |
| `cir.goto` / `cir.label` | `GotoOp::create(b, loc, name)` (terminator), `LabelOp::create(b, loc, name)` | label emission: if the current block is non-empty, create a new block, `cir.br` to it, `cir.label "L"` at its start. `FuncOp::verify` checks every goto has a label in the function; resolved later by `FlattenCFG` + `GotoSolver`. Indirect goto (`cir.indirect_goto`, `cir.block_address`) exists but is out of v0. |
| `cir.br` / `cir.brcond` | `BrOp::create(b, loc, block)` | used only for block structure (labels, switch pre-case block) |
| `cir.yield` / `cir.condition` | `createYield(loc, values)`, `createCondition(cond)` | |

Dead-code policy used upstream: after every terminator (return/break/continue/goto)
create a new empty block; at region close (`terminateStructuredRegionBody`,
`CIRGenStmt.cpp:532`) add `cir.yield` to blocks without a terminator and erase empty
blocks without predecessors.

## 6. Constants and records

### Constant attributes

* `cir::IntAttr::get(type, APInt)` / `(type, int64)`; `FPAttr::get(type, APFloat)`;
  `BoolAttr::get(ctx, bool)`; `ConstPtrAttr::get(ptrTy, i64Attr)` (null = 0);
  `ZeroAttr::get(type)` (any aggregate or scalar), `UndefAttr`, `PoisonAttr`.
* `ConstArrayAttr::get(ArrayType, Attribute elts [, trailingZerosNum])` where `elts`
  is an `ArrayAttr` of typed attrs **or an `mlir::StringAttr`** for character data.
  C strings: elements are all non-zero-prefix chars; trailing zeros are *not*
  materialized (`trailing_zeros`), e.g. `char s[5]="1234"` is
  `#cir.const_array<"1234" : !cir.array<!s8i x 4>, trailing_zeros> : !cir.array<!s8i x 5>`;
  an all-zero string is `#cir.zero`.  CIRGenBuilder's `getString` does this (we copy it).
* `ConstRecordAttr::get(RecordType, ArrayAttr members)`: **one element per member
  that owns bytes**, in member order, *including pad members* (as zero arrays) and
  with a bit-field access unit holding the packed integer of all its fields; a
  zero-width bit-field has no element.  Unions: exactly one element, typed as one of
  the members (bit-field variant: the unit's storage type).  Flexible array member
  may carry any number of elements.  If every element is null, use `#cir.zero`
  (`getConstRecordOrZeroAttr`).  `collectStoredInitializers`
  (`CIRGenExprConstant.cpp:49`) fills missing members with `getZeroInitAttr`.
* Address constants: `GlobalViewAttr::get(ptrTy, FlatSymbolRefAttr, indices
  ArrayAttr?, addressPoint=false)` -- `&g`, `&g.f`, `&a[3]` (indices index
  *sub-elements*, no leading zero), `g + 4 bytes` via `GlobalOffsetAttr`
  (`#cir.global_offset<@g, N>`), function addresses via `GlobalViewAttr` on the
  function symbol.  An address cast to integer uses an int-typed `GlobalViewAttr`.
  A global holding a pointer-to-string: `global_view<@.str>` of type `!cir.ptr<!s8i>`.
* String literals: private constant global (`createGlobal(..., isConstant=true,
  PrivateLinkage)`, name `.str`, `.str.1`, ... -- **CIR does not uniquify names**, the
  producer must), alignment set, `dso_local`; uses are `get_global` +
  `cast array_to_ptrdecay`, or `global_view` in constants.

### Record types

At this SHA records are `cir::StructType` and `cir::UnionType`; `cir::RecordType` is
a hand-written view class with `getMembers/getMemberKinds/getPacked/getPadded/
isComplete/complete/...`.  Anything generic over both takes `cir::RecordType`.

```
StructType::get(ctx, nameAttr, isClass)                        // identified, incomplete
StructType::get(ctx, members, nameAttr, packed, isClass, kinds)// complete
rec.complete(members, packed, /*padding(union only)*/ Type{}, kinds)
UnionType::get(ctx, nameAttr)                                  // incomplete
UnionType::get(ctx, members, nameAttr, packed, paddingTy, kinds)
```

`RecordMemberKind` per member: `Data`, `Pad`, `Empty`, `BitField`.
* `Pad` = compiler-inserted byte array (`!cir.array<!u8i x N>`, or `!u8i` for N=1); a
  *trailing* pad is reusable tail padding.
* `Empty` = a data/base/vptr that is empty for the ABI (zero-size field, GNU empty
  struct), or a bit-field unit holding only unnamed bit-fields.
* `BitField` = an access unit with at least one named bit-field.
* `Data` otherwise.
A struct has **no separate padding slot**; padding is a `Pad` member.  A **union**
carries one optional tail-padding *type* (`padding = {...}`) separate from member
kinds; the union members are the variants.

Bit-field access units: a run of bit-fields is **one member** of type
`!cir.bitfield<storageTy, [#cir.bitfield_decl<declaredTy, width[, unnamed]>...]>`
(declared type and width per field).  `storageTy` is `!u8i/u16/u32/u64` or a byte
array (when the unit is "clipped" by the record's end/next field).  A zero-width
bit-field is a member `!cir.bitfield<[#cir.bitfield_decl<T, 0, unnamed>]>` with no
storage; it owns no bytes and becomes no LLVM field.  `get_member` addresses the unit
as `!cir.ptr<storageTy>`; `cir.get_bitfield` extracts.  This replaces the old incubator
`!cir.struct<..., !u32i>` plain-int representation; **`memberOwnsBytes()` and
`memberStorageType()`** (`CIRTypes.h`) give the byte-owning view.

#### What CIRGenRecordLayoutBuilder computes from ASTRecordLayout

(`lib/CIR/CodeGen/CIRGenRecordLayoutBuilder.cpp`, `CIRRecordLowering`.)
1. Accumulate non-bit-field members: `(offset, convertTypeForMem(fieldTy), kind)`
   with offset = `getFieldOffset(i)/8`; for records in C only `Data`/`Empty`
   (zero-size) kinds occur.
2. Accumulate bit-fields in **access units** with the non-discrete algorithm
   (`accumulateBitFields`, ~230 lines, mirrors classic `CGRecordLayoutBuilder`):
   greedy spans widened up to `RegisterWidth`, cheap-unaligned-access test
   (`TargetInfo::hasCheapUnalignedBitFieldAccess`, true for x86), natural size
   `getUIntNType(bits)` vs "clipped" byte array, limited by the next field with
   storage or `DataSize`; zero-width bit-fields are barriers.  A *discrete*
   algorithm exists for MSVC/AAPCS-like ABIs (`isDiscreteBitFieldABI`) -- not needed for
   x86-64 SysV.
3. Sort by offset (stable; bit-fields keep their unit's offset), append a sentinel
   `u8` at `size` (full `getSize()` or `getNonVirtualSize()` for base subobjects).
4. `determinePacked`: packed if any member offset is not a multiple of its
   alignment, or the record size is not a multiple of the max alignment.
5. `insertPadding`: for each byte-owning member, if `offset != alignTo(size,
   packed ? 1 : align(member))` add a `Pad` byte-array of `offset - size`; a
   zero-width bit-field splits padding at its offset.
6. Output: member types + kinds + `packed`; per-field maps: `fieldIdx -> CIR member
   index`, `bit-field -> {name, offset-in-unit, size, isSigned, storageSize,
   storageOffset, storageType}`; `zeroInitializable`; `cir.record_layouts` entry
   `{arg_passing_kind, has_trivial_dtor, record_align = ASTRecordLayout::getAlignment()}`.
7. Union: `lowerUnion` -- members are the variants (largest alignment variant chosen
   for storage by lowering); tail padding as `padding` type if size exceeds the
   largest member; bit-field variants are single-field units.

**What we must supply from EDG** (the producer-side facts; put in `TY_RECORD_BODY`):
total size and alignment in bytes (including `aligned(N)`); data size (size before
tail alignment, `getDataSize()`; needed as `limitOffset` when no later field exists);
per field: byte offset or bit offset (bit-fields), declared type, bit width, name,
`is_unnamed`, and an "empty for ABI" flag (zero-size field); record-level `packed`
is **derived**, not sent, but a `#pragma pack`/`packed` attribute changes offsets
only, which we observe through offsets; also `arg_passing_kind`, `has_trivial_dtor`
for `cir.record_layouts` (C: `CanPassInRegs`, trivial).  The register width (64) and
"cheap unaligned bitfield access" come from the stream target block.

## 7. Required module attributes

Set by `CIRGenModule`'s constructor (`CIRGenModule.cpp:85-230`) and `release()`;
which of them the pipeline actually needs, read from the consumers:

| Attribute (C++ accessor `CIRDialect::get...AttrName()`) | CIRGen value for x86-64 C | Consumed by | Needed? |
| --- | --- | --- | --- |
| `cir.triple` (`getTripleAttrName`) | `StringAttr("x86_64-unknown-linux-gnu")` | `runCIRToCIRPasses` (**hard error if missing**), `createLowerModule`, lowered to `llvm.target_triple` | **mandatory** |
| `cir.target_abi` (`getTargetABIAttrName`) | `StringAttr("")` (x86-64: `""`, or `"avx"/"avx512"`) | `runCIRToCIRPasses` (**hard error if missing**; picks X86 AVX ABI level) | **mandatory** |
| `dlti.dl_spec` (`DLTIDialect::kDataLayoutAttrName`) | `cir::setMLIRDataLayout(module, llvm::DataLayout)` | CallConvLowering (**error if missing**), every `mlir::DataLayout`-based size query, FlattenCFG, LLVM lowering | **mandatory** (and must include the `!cir.ptr` `#cir.ptr_spec` entry; use `cir::setMLIRDataLayout`, which adds it. `mlir::translateDataLayout` alone gives 64-bit pointers by default) |
| `cir.lowering_lang_options` (`getLoweringLangOptionsAttrName`) | `#cir.lowering_lang_options<exceptions, threadsafe_statics, cuda, cuda_is_device, hip, gpu_rdc, openmp, openmp_is_target_device, clang_abi_compat:i32>`; C: all false except `threadsafe_statics=true`; `clang_abi_compat = (int32) clang::LangOptions::ClangABI::Latest` (=16 at this SHA; `Ver23`=15) | `createLowerModule`, CallConvLowering (`clang_abi_compat` selects the ABI-compat quirks) | optional (defaults = `LangOptions` default), set it |
| `cir.lang` (`getSourceLanguageAttrName`) | `cir::SourceLanguageAttr::get(ctx, SourceLanguage::C)` (`C`=1, `CXX`=2) | printing / tools; no pass consumer found | optional |
| `cir.size_type_width`, `cir.int_type_width` | `I32IntegerAttr(64)`, `I32IntegerAttr(32)` | only the `cir.libc.memchr` verifier | optional for v0, set anyway |
| `cir.opt_info` (`getOptInfoAttrName`) | `OptInfoAttr::get(ctx, level, size)` **only if** `-O>0` | `createLowerModule` -> `CodeGenOptions` | optional |
| `cir.default_tls_model` | `TLSModelAttr::get(ctx, TLSModel::GeneralDynamic)` | `LoweringPrepare` for TLS guards | optional (v0 has no TLS) |
| `cir.module_asm` (`getModuleLevelAsmAttrName`) | `ArrayAttr` of strings (empty array set at `release()`) | lowered to `llvm.module_asm` | optional |
| `cir.record_layouts` (`getRecordLayoutsAttrName`) | `DictionaryAttr` name -> `#cir.record_layout<arg_passing_kind=..., has_trivial_dtor=..., record_align=N>` (set at `release()`) | CallConvLowering/ABI rewrite: `tryGetRecordLayout`, **missing entry degrades gracefully** (defaults: can_pass_in_regs, ABI alignment of the type) | optional, but needed for `aligned(N)` record fidelity and for C++ non-trivial records |
| `sym_name` (module `SymbolName`) + `loc` | main file path | LLVM module identifier | optional |
| `cir.target-cpu`, `cir.tune-cpu`, `cir.target-features`, `cir.sdk_version` | optional | | optional |

`dlti.dl_spec` source of truth: the data layout string comes from the stream target
block, e.g. `e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128`
for `x86_64-unknown-linux-gnu`; in the spike we take it from
`TargetMachine::createDataLayout()` so it matches the backend exactly.

## 8. Pipeline API (exact)

```cpp
#include "clang/CIR/CIRToCIRPasses.h"   // lib: clangCIRLoweringCommon
mlir::LogicalResult cir::runCIRToCIRPasses(
    mlir::ModuleOp theModule, mlir::MLIRContext &mlirCtx,
    bool enableVerifier, bool enableIdiomRecognizer, bool enableCIRSimplify,
    bool enableLibOpt, llvm::StringRef libOptOptions, bool enableCallConvLowering);

#include "clang/CIR/LowerToLLVM.h"      // lib: clangCIRLoweringDirectToLLVM
std::unique_ptr<llvm::Module> cir::direct::lowerDirectlyFromCIRToLLVMIR(
    mlir::ModuleOp mlirModule, llvm::LLVMContext &llvmCtx, bool enableOpenMP,
    llvm::StringRef mlirSaveTempsOutFile = {}, llvm::vfs::FileSystem *fs = nullptr);
```

* There is **no `clang::ASTContext`** parameter on either (the plan's premise holds).
* clang's own defaults (`CIRGenConsumer`): verifier = on; idiom recognizer off;
  `enableCIRSimplify = (-O > 0)`; libopt off (`-clangir-lib-opt`); `enableCallConvLowering`
  = **on by default** (`-fno-clangir-call-conv-lowering` disables).
* `runCIRToCIRPasses` pipeline: `CIRCanonicalize` -> (`CIRSimplify`) -> (`IdiomRecognizer`)
  -> (`LibOpt`) -> `TargetLowering` -> `CXXABILowering` -> `LoweringPrepare` ->
  `CallConvLowering` (x86_64 non-Windows only; any other triple is left unchanged).
* `lowerDirectlyFromCIRToLLVMIR` runs `HoistAllocas`, `FlattenCFG`, `EHABILowering`,
  `GotoSolver`, then `ConvertCIRToLLVM`; it registers the LLVM/OpenMP/CIR translations
  itself; **failures call `report_fatal_error`** (`LowerToLLVM.cpp:6238-6264`) rather than
  returning null.  The returned module has the triple (copied from `cir.triple`) and
  `dlti` data layout applied; clang additionally calls
  `setDataLayout(TargetInfo data layout string)`; we should `setDataLayout` from our
  `TargetMachine`.
* `CallConvLowering` is what makes by-value structs, sret and `va_arg` ABI-correct;
  turning it off yields incorrect SysV code.
* All of this recurses (verifier, pattern rewriter, walkers): run on a big-stack
  thread with multithreading disabled in the context (section 1).

Other verifier hooks: `mlir::verify(module)` (what clang's `verifyModule` calls); on
failure the diagnostics go through the context's diagnostic engine (stderr by
default).

### Minimal LLVM steps to `.o`/`.s` (x86_64-unknown-linux-gnu)

```
llvm::InitializeAllTargetInfos(); InitializeAllTargets(); InitializeAllTargetMCs();
InitializeAllAsmPrinters();                        // llvm/Support/TargetSelect.h
llvm::Triple tt("x86_64-unknown-linux-gnu");
const llvm::Target *t = llvm::TargetRegistry::lookupTarget(tt, err);   // takes Triple at this SHA
std::unique_ptr<llvm::TargetMachine> tm(t->createTargetMachine(
    tt, "generic", "", llvm::TargetOptions(), llvm::Reloc::PIC_,
    std::nullopt, llvm::CodeGenOptLevel::Default));
mod->setTargetTriple(tt);                           // Module::setTargetTriple(Triple)
mod->setDataLayout(tm->createDataLayout());
// optional optimization: PassBuilder PB(tm.get()); ... PB.buildPerModuleDefaultPipeline(O2)
llvm::legacy::PassManager pm;                       // llvm/IR/LegacyPassManager.h
tm->addPassesToEmitFile(pm, os, nullptr, llvm::CodeGenFileType::ObjectFile /*or AssemblyFile*/);
pm.run(*mod);
```
(Signatures read from `llvm/include/llvm/{IR/Module.h,MC/TargetRegistry.h,Target/TargetMachine.h,Support/CodeGen.h}`
at the pinned SHA.)  `llvm::thread(std::optional<unsigned> stackBytes, fn, args...)`
(`llvm/Support/thread.h`) must be passed an explicit `std::optional<unsigned>` --
a bare `unsigned` binds to the other constructor template and does not compile.
`CrashRecoveryContext::RunSafelyOnThread(fn, RequestedStackSize)` is the alternative.

## 9. CMake link set

`clang/lib/CIR/**/CMakeLists.txt` and `clang/tools/cir-translate/CMakeLists.txt` are the
authority.  `find_package(Clang CONFIG)` loads LLVM too; `find_package(MLIR CONFIG)` is
separate.  Prefix layout: `$PREFIX/lib/cmake/{llvm,mlir,clang}`.

Targets to link (minimal, mirrors `cir-translate`):

```
clangCIR  clangCIRLoweringDirectToLLVM  clangCIRLoweringCommon  CIRRegisterAllDialects
MLIRCIR   MLIRCIRTransforms
```
plus MLIR (via `MLIR*` exported targets): `MLIRIR MLIRPass MLIRDLTIDialect
MLIRLLVMDialect MLIRFuncDialect MLIRLLVMToLLVMIRTranslation
MLIRBuiltinToLLVMIRTranslation MLIROpenMPToLLVMIRTranslation MLIRTargetLLVMIRExport
MLIRTransforms MLIRTransformUtils MLIRAnalysis MLIRSupport` and LLVM components
`core support targetparser passes analysis codegen asmprinter mc x86codegen
x86asmparser x86desc x86info` (CMake: `llvm_map_components_to_libnames`).

Transitive facts (important for the isolation story):

* **`clangCIR` (the whole CIRGen library) is pulled in transitively**:
  `clangCIRLoweringCommon -> clangCIR` (`Lowering/CMakeLists.txt`).  Its dependencies:
  `clangAST clangBasic clangCodeGenUtils clangLex CIRRegisterAllDialects CIROpenACCSupport
  CIROpenMPSupport MLIR{Arith,DLTI,LLVM,OpenACC,OpenMP,OpenMPUtils,PtrMemorySpaceInterfaces,
  TargetLLVMIRImport}Dialect` and LLVM components `Core FrontendOpenMP Support`.
  `clangCodeGenUtils` itself needs the LLVM `CodeGen` component and `Demangle`.
  So the final link includes `clangAST`/`clangBasic`/`clangLex` (static archives; only
  referenced objects are pulled).  It also contains the `cir::setMLIRDataLayout` we call.
* `MLIRCIR` itself links `clangAST` (AST attribute interfaces) and `MLIRCIRTransforms`
  links `clangBasic`, `LLVMABI` (the new `llvm/lib/ABI` library), `MLIRABI`.
* **Run-time AST-free but not clang-free**: `cir::createLowerModule` calls
  `clang::targets::AllocateTarget(triple, TargetOptions)` and builds `clang::LangOptions`
  and `clang::CodeGenOptions` objects (`LowerModule.cpp:84-126`), used by TargetLowering,
  CXXABILowering and LoweringPrepare.  No `ASTContext`/`SourceManager`/`FileManager` is
  needed.  `AllocateTarget` is in `clangBasic` and independent of
  `LLVM_TARGETS_TO_BUILD`.
* Compile flags: LLVM/MLIR are built `-fno-rtti -fno-exceptions`; consumers must match
  (`LLVM_ENABLE_RTTI` from `LLVMConfig`).  Assertions are ON in the toolchain: keep
  `-UNDEBUG` consistent for headers that contain inline asserts.
* The installed prefix must contain `include/clang/CIR/Dialect/IR/*.inc`, `include/mlir/**`
  (including generated `.inc`); `clang/CMakeLists.txt:446-463` installs them as part of the
  `clang-headers` component, and `mlir` installs its own (verify on the box:
  `test -f $PREFIX/include/clang/CIR/Dialect/IR/CIROps.h.inc`).
* `CLANG_ENABLE_CIR=ON` requires a non-standalone, in-tree build with `mlir` in
  `LLVM_ENABLE_PROJECTS` (`clang/CMakeLists.txt:193-202`).
* If cir-config builds shared libs (`LLVM_LINK_LLVM_DYLIB`/`CLANG_LINK_CLANG_DYLIB`),
  the target names are unchanged but the archives become `libclang-cpp.so`/`libMLIR.so`
  **(unverified; ask cir-config which)**.

## 10. MissingFeatures / NYI relevant to C

`include/clang/CIR/MissingFeatures.h` (344 lines) is the machine-checked "not done yet"
list: the relevant entries for C codegen are `vlas`, `vaArgABILowering` (still
listed, but CallConvLowering now does it on x86_64), `opGlobalThreadLocal`,
`opGlobalUnnamedAddr`, `opGlobalVisibility`, `opFuncParameterAttributes` (only a subset of
parameter attributes; CIRGen does emit `arg_attrs` dictionaries with
`llvm.noundef`, `llvm.noalias` (restrict), `llvm.nonnull` -- see
`test/CIR/CodeGen/{restrict-noalias,nonnull}.c`; these are optional for us and via
`FuncOp::setArgAttrs`/`mlir::function_interface_impl::setArgAttrs`), `opFuncMultipleReturnVals`, `opCallCallConv`,
`asmGoto`, `atomic*` (partly), `opAllocaStaticLocal`, `emitLifetimeMarkers`,
`generateDebugInfo` (CIR emits no debug info), `sanitizers`, `pgoUse`/`coverageMapping`,
`opLoadStoreTbaa`/`opTBAA` (no TBAA metadata), `constantFoldSwitchStatement`,
`fastMathFlags`.

`errorNYI` sites (diagnosed "ClangIR code gen Not Yet Implemented") that bite C:

* VLAs and variably-modified types (`emitVariablyModifiedType`, `variably modified types in
  varargs`, `emitNullInitialization for zero size VariableArrayType`);
* calls through a function type *without* a prototype (`call to function without a
  prototype`);
* `__attribute__((cleanup))` (`emitAutoVarCleanups: CleanupAttr`), `naked`, `weakref`,
  `target_clones`/multiversion, `section` on static locals, `alias` partially;
* statement expressions as lvalues (`emitLValue: StmtExpr`), `va_arg` as lvalue,
  `compound literal` at file scope (`emitCompoundLiteralLValue: FileScope`), flexible
  array member initializers in globals, designated-init-update, array range designators
  (`ArrayRangeDesignator`);
* `_Atomic` args/returns (`passing or returning atomic types`), atomic inc/dec of
  non-power-of-2 float;
* wide/unusual constant lvalues (`ConstantLValueEmitter: call expr/block expr/...`);
* strict-vtable / sanitizer / overflow-check / signed-bool paths.
* **Not NYI** (supported): `_Complex` (full `CIRGenExprComplex`), bit-fields incl.
  volatile (AAPCS only partially), `goto`/labels, computed goto (`indirect_goto`,
  `block_address`; with limits), `switch` ranges, `_BitInt`, GNU `__int128`, `inline asm`
  (basic), K&R function *definitions*, K&R *calls* via the spec 8.8 variadic-bitcast rule, varargs.

C constructs CIR cannot yet express (v0 should diagnose and drop the function to a
declaration): VLAs, cleanup attribute, naked
functions, and everything in the NYI list above that EDG's non-lowered IL can reach.

## 11. Divergences from the plan (flagged for the orchestrator)

1. **Record representation** is richer than "padded/packed structs": member kinds
   (`Data/Pad/Empty/BitField`), `!cir.bitfield` access-unit member types, and
   `cir.get_bitfield` addressed via the unit pointer.  The producer must reproduce
   the access-unit algorithm (needs `DataSize`, per-field bit offset/width/unnamed,
   empty-for-ABI flag) -- ECIL record bodies must carry those.
2. **Module attributes**: only `cir.triple`, `cir.target_abi`, `dlti.dl_spec` are mandatory
   for the pass pipeline; `cir.record_layouts` is optional (graceful default);
   `size_type_width`/`int_type_width`/`default_tls_model`/`module_asm` exist and are set by
   clang.  `cir.target_abi` is `""` for plain x86-64, not the triple.
3. **Link set**: `clangCIR` is a *transitive* dependency (through
   `clangCIRLoweringCommon`), together with `clangAST`, `clangBasic`, `clangLex`,
   `clangCodeGenUtils` (-> LLVM `CodeGen`), `LLVMABI`, `MLIRABI`.  No ASTContext is
   needed, but `clang::targets::AllocateTarget`/`LangOptions`/`CodeGenOptions` are used
   inside the passes.
4. **ABI**: `CallConvLowering` does all x86-64 SysV argument/return/va_arg
   classification inside CIR; it is on by default and required for correctness.  The
   producer therefore emits plain, un-coerced signatures (records by value).  Other
   triples fall through unchanged (plan's "clean unsupported target error" must be
   produced by us).
5. **Threading**: disable MLIR multithreading on the context; a big-stack driver thread is
   not enough.
6. `LowerToLLVM` aborts via `report_fatal_error`; the C API must run it where a fatal
   handler (`llvm::install_fatal_error_handler`) converts it to a status, or in a
   `CrashRecoveryContext`.
7. `clang_abi_compat` must follow `clang::LangOptions::ClangABI::Latest` of the pinned
   SHA (a bump changes the integer).

## 12. Spike confirmation (2026-10-09, box, `edgcpp/cir-env`)

`spike/spike.cpp` built and ran (see `spike/README.md`, "Spike results").

API spelling fixes needed for `CIRCompat.h`: **none** for the creators/builders
documented above.  Setup/link fixes (record in `CIRCompat.h`/CMake):
1. Load `mlir::LLVM::LLVMDialect` (`context.loadDialect`) before `cir::setMLIRDataLayout`
   (registration alone aborts with "storage uniquer isn't initialized").  Section 1
   should read `loadDialect<DLTIDialect, CIRDialect, LLVM::LLVMDialect>()`.
2. Link every LLVM target the toolchain enables if `InitializeAll*` is used
   (X86 and AArch64 here), or call `LLVMInitializeX86*` only.
3. The pruned prefix `/edg/llvm-cir` cannot be used with `find_package(LLVM)`;
   `/full` (unpruned install) can.  Static link; final binary 448 MB unstripped,
   127 MB stripped; only libc/libstdc++/libm/libgcc_s dynamic.
4. Unchanged: `cir.target_abi` is `""`; `clang_abi_compat` is 16; `setDSOLocal` works through
   `CIRGlobalValueInterface`; `FuncOp::setVisibility(Public)` works.
Contract headers (`ecil_format.h`, `edg_cir.h`, `design.md`, `ecil_format.md`) were checked
against this research: no conflicts (`TY_RECORD_BODY` carries size, data_size, align, and
`REC_FIELD` flags `unnamed`/`empty_for_abi`; target block has reg_bits and the cheap
unaligned bit-field flag; `target_abi` `""`).
