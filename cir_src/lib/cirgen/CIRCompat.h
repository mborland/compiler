/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
CIRCompat.h -- the single churn point for the upstream CIR API.  Every call
whose spelling or shape has a realistic chance of changing between llvm-project
pins (builders with callbacks, record type creation, case/switch/ternary
creation, bit-field ops, module attribute names) goes through a function in
namespace edg_cir::compat; the rest of cir_src uses the plain, stable parts of
the dialect (types, attributes, simple ops through cir::CIRBaseBuilderTy).

Derived in part from llvm-project clang/lib/CIR (Apache-2.0 WITH
LLVM-exception); the pinned revision is cir_src/LLVM_REVISION.
*/

#ifndef EDG_CIR_COMPAT_H
#define EDG_CIR_COMPAT_H 1

#include <cstdint>
#include <string>
#include <vector>

#include "clang/CIR/CIRDataLayoutSpec.h"
#include "clang/CIR/Dialect/Builder/CIRBaseBuilder.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"

namespace edg_cir {
namespace compat {

/* The builder: all region-opening helpers below take it. */
typedef cir::CIRBaseBuilderTy a_builder;

inline mlir::Type void_type(mlir::MLIRContext *ctx)
/* Return !cir.void. */
{ return cir::VoidType::get(ctx); }

inline mlir::Type int_type(mlir::MLIRContext *ctx, unsigned bits,
                           bool is_signed)
/* Return an integer type. */
{ return cir::IntType::get(ctx, bits, is_signed); }

inline mlir::Type bool_type(mlir::MLIRContext *ctx)
/* Return !cir.bool. */
{ return cir::BoolType::get(ctx); }

inline mlir::Type pointer_to(mlir::Type pointee)
/* Return !cir.ptr<pointee>. */
{ return cir::PointerType::get(pointee); }

inline mlir::Type array_of(mlir::Type element, uint64_t count)
/* Return !cir.array<element x count>. */
{ return cir::ArrayType::get(element, count); }

inline mlir::Type function_type(llvm::ArrayRef<mlir::Type> inputs,
                                mlir::Type result, bool variadic)
/* Return a function type (a void result becomes "no result"). */
{ return cir::FuncType::get(inputs, result, variadic); }

inline mlir::Type float_type(mlir::MLIRContext *ctx, unsigned format,
                             bool &ok)
/*
Map an ECIL float format (an_ecil_float_format value) to a CIR type; ok is
false for formats CIR cannot represent.
*/
{
  mlir::Type result;

  ok = true;
  switch (format) {
  case 0: result = cir::FP16Type::get(ctx); break;
  case 1: result = cir::BF16Type::get(ctx); break;
  case 2: result = cir::SingleType::get(ctx); break;
  case 3: result = cir::DoubleType::get(ctx); break;
  case 4:
    result = cir::LongDoubleType::get(ctx, cir::FP80Type::get(ctx));
    break;
  case 5: result = cir::FP128Type::get(ctx); break;
  default: ok = false; break;
  }
  return result;
}

inline cir::RecordType incomplete_struct(mlir::MLIRContext *ctx,
                                         llvm::StringRef name, bool is_class)
/* Create an identified, incomplete struct/class type. */
{
  return cir::StructType::get(ctx, mlir::StringAttr::get(ctx, name),
                              is_class);
}

inline cir::RecordType incomplete_union(mlir::MLIRContext *ctx,
                                        llvm::StringRef name)
/* Create an identified, incomplete union type. */
{ return cir::UnionType::get(ctx, mlir::StringAttr::get(ctx, name)); }

inline void complete_record(cir::RecordType record,
                            llvm::ArrayRef<mlir::Type> members, bool packed,
                            mlir::Type union_padding,
                            llvm::ArrayRef<cir::RecordMemberKind> kinds)
/* Give an incomplete identified record its body. */
{ record.complete(members, packed, union_padding, kinds); }

inline mlir::Type record_union_padding(cir::RecordType record)
/* The tail-padding slot of a union (null for a struct). */
{
  mlir::Type result;

  if (auto u = mlir::dyn_cast<cir::UnionType>(record))
    result = u.getPadding();
  return result;
}

inline cir::RecordType lowered_record(mlir::MLIRContext *ctx,
                                      cir::RecordType like,
                                      llvm::ArrayRef<mlir::Type> members)
/*
A complete identified record "<like>.lowered" of the same kind, packing,
member kinds and union padding as like, with other member types (the lowered
{i64, i64} form of the pointers to member functions in it).
*/
{
  std::string name = like.getName().getValue().str() + ".lowered";
  cir::RecordType result = like.isUnion()
      ? incomplete_union(ctx, name)
      : incomplete_struct(ctx, name, like.isClass());

  result.complete(members, like.getPacked(), record_union_padding(like),
                  like.getMemberKinds());
  return result;
}

inline mlir::Type bitfield_unit(mlir::MLIRContext *ctx, mlir::Type storage,
                                llvm::ArrayRef<cir::BitFieldDeclAttr> fields)
/* Return the member type of a bit-field access unit (storage may be null). */
{ return cir::BitFieldType::get(ctx, storage, fields); }

inline cir::BitFieldDeclAttr bitfield_decl(mlir::Type declared,
                                           uint64_t width, bool unnamed)
/* Return the declaration attribute of one bit-field. */
{ return cir::BitFieldDeclAttr::get(declared, width, unnamed); }

inline bool member_owns_bytes(mlir::Type member)
/* A zero-width bit-field unit owns no bytes. */
{ return cir::memberOwnsBytes(member); }

inline mlir::Type member_storage_type(mlir::Type member)
/* The stored type of a member (the storage type of a bit-field unit). */
{ return cir::memberStorageType(member); }

inline mlir::Type union_storage_type(const mlir::DataLayout &layout,
                                     llvm::ArrayRef<mlir::Type> members)
/* The union variant used as storage when lowering. */
{ return cir::UnionType::getUnionStorageType(layout, members); }

inline cir::GlobalLinkageKind linkage_kind(unsigned linkage, bool &ok)
/* Map an an_ecil_linkage value to the CIR linkage kind. */
{
  cir::GlobalLinkageKind result = cir::GlobalLinkageKind::ExternalLinkage;

  ok = true;
  switch (linkage) {
  case 0: result = cir::GlobalLinkageKind::ExternalLinkage; break;
  case 1: result = cir::GlobalLinkageKind::InternalLinkage; break;
  case 2: result = cir::GlobalLinkageKind::PrivateLinkage; break;
  case 3: result = cir::GlobalLinkageKind::LinkOnceODRLinkage; break;
  case 4: result = cir::GlobalLinkageKind::WeakODRLinkage; break;
  case 5: result = cir::GlobalLinkageKind::WeakAnyLinkage; break;
  case 6: result = cir::GlobalLinkageKind::AvailableExternallyLinkage; break;
  case 7: result = cir::GlobalLinkageKind::ExternalWeakLinkage; break;
  case 8: result = cir::GlobalLinkageKind::CommonLinkage; break;
  default: ok = false; break;
  }
  return result;
}

inline bool linkage_is_local(cir::GlobalLinkageKind linkage)
/* True for internal and private linkage. */
{
  return linkage == cir::GlobalLinkageKind::InternalLinkage ||
         linkage == cir::GlobalLinkageKind::PrivateLinkage;
}

inline mlir::SymbolTable::Visibility
symbol_visibility(cir::GlobalLinkageKind linkage, bool is_declaration)
/* MLIR visibility CIRGen uses: private for declarations and local linkage. */
{
  return is_declaration || linkage_is_local(linkage)
           ? mlir::SymbolTable::Visibility::Private
           : mlir::SymbolTable::Visibility::Public;
}

inline void set_dso_local(mlir::Operation *op, bool value)
/* Set dso_local through the global-value interface. */
{ mlir::cast<cir::CIRGlobalValueInterface>(op).setDSOLocal(value); }

inline void set_comdat(mlir::Operation *op, llvm::StringRef name)
/* Put a function or global in the comdat group name ("" is its own name). */
{
  if (auto fn = mlir::dyn_cast<cir::FuncOp>(op))
    fn.setComdat(name);
  else
    mlir::cast<cir::GlobalOp>(op).setComdat(name);
}

inline cir::GlobalOp create_global(a_builder &b, mlir::ModuleOp module,
                                   mlir::Location loc, llvm::StringRef name,
                                   mlir::Type type, bool is_constant,
                                   cir::GlobalLinkageKind linkage)
/* Create a global at the end of the module (the builder's point is kept). */
{
  mlir::OpBuilder::InsertionGuard guard(b);

  b.setInsertionPointToEnd(module.getBody());
  return cir::GlobalOp::create(b, loc, name, type, is_constant,
                               mlir::ptr::MemorySpaceAttrInterface(), linkage);
}

inline void set_alias(cir::FuncOp fn, llvm::StringRef target)
/* Make fn an alias of the function named target (fn has no body). */
{
  fn.setAliaseeAttr(mlir::FlatSymbolRefAttr::get(fn.getContext(), target));
}

inline cir::FuncOp create_function(a_builder &b, mlir::ModuleOp module,
                                   mlir::Location loc, llvm::StringRef name,
                                   mlir::Type function_type)
/* Create a function declaration at the end of the module. */
{
  mlir::OpBuilder::InsertionGuard guard(b);
  b.setInsertionPointToEnd(module.getBody());
  return cir::FuncOp::create(b, loc, name,
                             mlir::cast<cir::FuncType>(function_type));
}

/* Structured-op helpers: create the op with empty regions' blocks and return
   it; the caller moves the insertion point into the regions. */

inline auto &no_op_body()
/* A callback that does nothing (the builder still creates the blocks). */
{
  static auto fn = [](mlir::OpBuilder &, mlir::Location) {};
  return fn;
}

inline cir::ScopeOp open_scope(a_builder &b, mlir::Location loc)
/* Create a cir.scope (its single block is empty). */
{ return cir::ScopeOp::create(b, loc, no_op_body()); }

inline cir::IfOp open_if(a_builder &b, mlir::Location loc, mlir::Value cond)
/* Create a cir.if with only a then block (the else region stays empty). */
{
  return cir::IfOp::create(b, loc, cond, /*withElseRegion=*/false,
                           no_op_body(), nullptr);
}

inline cir::CleanupScopeOp open_cleanup_scope(
    a_builder &b, mlir::Location loc,
    cir::CleanupKind kind = cir::CleanupKind::Normal)
/*
Create a cir.cleanup.scope of the kind with an empty body block and an empty
cleanup block (each is terminated when its region is finished).
*/
{
  return cir::CleanupScopeOp::create(b, loc, kind, no_op_body(),
                                     no_op_body());
}

inline cir::LocalInitOp open_local_init(a_builder &b, mlir::Location loc,
                                        cir::GlobalOp global, bool tls)
/* Create cir.local_init static_local @global with no regions yet. */
{ return cir::LocalInitOp::create(b, loc, global.getSymNameAttr(), tls); }

inline cir::RegisterExitDtorOp open_register_exit_dtor(a_builder &b,
                                                       mlir::Location loc,
                                                       cir::GlobalOp object)
/* Create cir.register_exit_dtor @object with no body block yet. */
{
  return cir::RegisterExitDtorOp::create(b, loc,
                                         object.getSymNameAttr().getValue());
}

inline void mark_static_local(mlir::Value address)
/* Mark a get_global as the address of a guarded function-local static. */
{ mlir::cast<cir::GetGlobalOp>(address.getDefiningOp()).setStaticLocal(true); }

inline void set_dynamic_init(cir::GlobalOp global, llvm::StringRef guard,
                             bool is_local, int tls_kind, bool is_inline,
                             bool template_instance)
/*
Mark a global as guarded dynamically initialized: the guard variable name and
the facts LoweringPrepare needs (tls_kind: 0 none, 1 static, 2 dynamic).
*/
{
  mlir::MLIRContext *ctx = global.getContext();
  cir::TLSKind tls = tls_kind == 0 ? cir::TLSKind::None
                     : tls_kind == 1 ? cir::TLSKind::Static
                                     : cir::TLSKind::Dynamic;

  global.setDynamicInitGuardAttr(cir::DynamicInitGuardAttr::get(
      ctx, mlir::StringAttr::get(ctx, guard)));
  global.setDynamicInitInfoAttr(cir::DynamicInitInfoAttr::get(
      ctx, is_local, tls, is_inline,
      template_instance
          ? cir::TemplateSpecializationKind::ImplicitInstantiation
          : cir::TemplateSpecializationKind::Undeclared));
}

inline void delete_array(a_builder &b, mlir::Location loc,
                         mlir::Value address, cir::FuncOp delete_fn,
                         bool size_param, uint64_t align_param,
                         cir::FuncOp element_dtor, uint64_t element_align,
                         bool dtor_may_throw)
/*
Create cir.delete_array: the usual operator delete parameters (size, and the
alignment when align_param is not zero) only when it takes any, the element
destructor when the elements have one.  The element alignment is the one the
new[] used to size the cookie.  dtor_may_throw makes the lowering keep
destroying the remaining elements and free the storage when an element
destructor throws.
*/
{
  mlir::MLIRContext *ctx = b.getContext();
  cir::UsualDeleteParamsAttr params;

  if (size_param || align_param != 0)
    params = cir::UsualDeleteParamsAttr::get(
        ctx, size_param,
        align_param != 0 ? std::optional<uint64_t>(align_param)
                         : std::nullopt,
        false, false);
  cir::DeleteArrayOp::create(
      b, loc, address, mlir::FlatSymbolRefAttr::get(delete_fn.getSymNameAttr()),
      params,
      element_dtor ? mlir::FlatSymbolRefAttr::get(element_dtor.getSymNameAttr())
                   : mlir::FlatSymbolRefAttr(),
      dtor_may_throw,
      b.getI64IntegerAttr(static_cast<int64_t>(element_align)));
}

inline cir::ForOp open_for(a_builder &b, mlir::Location loc)
/* Create a cir.for with empty cond/body/step blocks. */
{
  return b.createFor(loc, no_op_body(), no_op_body(), no_op_body());
}

inline cir::WhileOp open_while(a_builder &b, mlir::Location loc)
/* Create a cir.while with empty cond/body blocks. */
{ return b.createWhile(loc, no_op_body(), no_op_body()); }

inline cir::DoWhileOp open_do(a_builder &b, mlir::Location loc)
/* Create a cir.do with empty body/cond blocks. */
{ return b.createDoWhile(loc, no_op_body(), no_op_body()); }

inline cir::SwitchOp open_switch(a_builder &b, mlir::Location loc,
                                 mlir::Value cond)
/* Create a cir.switch whose body region has one empty block. */
{
  return cir::SwitchOp::create(
      b, loc, cond,
      [](mlir::OpBuilder &, mlir::Location, mlir::OperationState &) {});
}

inline mlir::Operation *open_ternary(a_builder &b, mlir::Location loc,
                                     mlir::Value cond, mlir::Type result)
/*
Create a cir.ternary with two empty regions (no blocks) and the given result
type (null for void).  Its builder infers the result type from yields that do
not exist yet, so the op is built through OperationState.
*/
{
  mlir::OperationState state(loc, cir::TernaryOp::getOperationName());

  state.addOperands(cond);
  if (result)
    state.addTypes(result);
  state.addRegion();
  state.addRegion();
  return b.create(state);
}

inline mlir::Operation *array_ctor_dtor(a_builder &b, mlir::Location loc,
                                        bool is_dtor, mlir::Value address,
                                        mlir::Value count)
/*
Create cir.array.ctor (or .dtor) over address: a pointer to a fixed-size array
(count is null) or to the first element (count is the runtime element count).
The body region gets its single block; the caller adds the element pointer
argument and the code.
*/
{
  mlir::Operation *result;
  auto body = [](mlir::OpBuilder &, mlir::Location) {};
  llvm::function_ref<void(mlir::OpBuilder &, mlir::Location)> noop(body);

  if (is_dtor) {
    result = count ? cir::ArrayDtor::create(b, loc, address, count, noop)
                         .getOperation()
                   : cir::ArrayDtor::create(b, loc, address, noop)
                         .getOperation();
  } else {
    result = count ? cir::ArrayCtor::create(b, loc, address, count, noop,
                                            nullptr).getOperation()
                   : cir::ArrayCtor::create(b, loc, address, noop, nullptr)
                         .getOperation();
  }
  return result;
}

inline cir::CaseOp create_marker_case(a_builder &b, mlir::Location loc,
                                      cir::CaseOpKind kind,
                                      llvm::ArrayRef<mlir::Attribute> values)
/*
Create a marker cir.case (an empty region ending in cir.yield) at the
builder's insertion point; the point ends up just after the case.
*/
{
  mlir::OpBuilder::InsertPoint inside;
  cir::CaseOp result = cir::CaseOp::create(b, loc, b.getArrayAttr(values),
                                           kind, inside);
  mlir::OpBuilder::InsertionGuard guard(b);

  b.restoreInsertionPoint(inside);
  b.createYield(loc);
  return result;
}

inline mlir::Value get_bitfield(a_builder &b, mlir::Location loc,
                                mlir::Type result_type, mlir::Value unit_addr,
                                mlir::Type storage, llvm::StringRef name,
                                unsigned size, unsigned offset,
                                bool is_signed, bool is_volatile,
                                unsigned align)
/* Create cir.get_bitfield. */
{
  return cir::GetBitfieldOp::create(b, loc, result_type, unit_addr, storage,
                                    name, size, offset, is_signed,
                                    is_volatile, align);
}

inline mlir::Value set_bitfield(a_builder &b, mlir::Location loc,
                                mlir::Type result_type, mlir::Value unit_addr,
                                mlir::Type storage, mlir::Value src,
                                llvm::StringRef name, unsigned size,
                                unsigned offset, bool is_signed,
                                bool is_volatile, unsigned align)
/* Create cir.set_bitfield (its result is the value now held). */
{
  return cir::SetBitfieldOp::create(b, loc, result_type, unit_addr, storage,
                                    src, name, size, offset, is_signed,
                                    is_volatile, align);
}

inline mlir::Value memset_zero(a_builder &b, mlir::Location loc,
                               mlir::Value address, mlir::Type size_type,
                               uint64_t bytes, unsigned align)
/* Zero bytes bytes at address (any pointer type) with cir.libc.memset. */
{
  mlir::Value dst = b.createBitcast(address, b.getVoidPtrTy());
  mlir::Value zero = b.getConstantInt(loc, b.getUIntNTy(8), 0);
  mlir::Value len = b.getConstantInt(
      loc, size_type, static_cast<int64_t>(bytes));

  cir::MemSetOp::create(b, loc, dst, b.getAlignmentAttr(align), zero, len);
  return dst;
}

inline void memcpy_bytes(a_builder &b, mlir::Location loc, mlir::Value dst,
                         mlir::Value src, uint64_t bytes, unsigned dst_align,
                         unsigned src_align)
/* Copy bytes bytes between objects (any pointer types): cir.libc.memcpy. */
{
  mlir::Value to = b.createBitcast(dst, b.getVoidPtrTy());
  mlir::Value from = b.createBitcast(src, b.getVoidPtrTy());
  mlir::Value len = b.getConstantInt(loc, b.getUIntNTy(64),
                                     static_cast<int64_t>(bytes));

  cir::MemCpyOp::create(b, loc, to, from, len, b.getAlignmentAttr(dst_align),
                        b.getAlignmentAttr(src_align));
}

inline mlir::Value class_address(a_builder &b, mlir::Location loc,
                                 bool to_derived, mlir::Value pointer,
                                 mlir::Type result_pointer, uint64_t offset,
                                 bool assume_not_null)
/*
The address of a direct non-virtual base (cir.base_class_addr) or, with
to_derived, of the derived object (cir.derived_class_addr) for the base at
offset bytes.
*/
{
  llvm::APInt bytes(64, offset);
  mlir::Value result;

  if (to_derived)
    result = cir::DerivedClassAddrOp::create(b, loc, result_pointer, pointer,
                                             bytes, assume_not_null);
  else
    result = cir::BaseClassAddrOp::create(b, loc, result_pointer, pointer,
                                          bytes, assume_not_null);
  return result;
}

inline cir::LoweringLangOptionsAttr
lowering_lang_options(mlir::MLIRContext *ctx, bool exceptions,
                      bool threadsafe_statics, int32_t abi_compat)
/* Build #cir.lowering_lang_options (C/C++, no offloading). */
{
  return cir::LoweringLangOptionsAttr::get(
      ctx, exceptions, threadsafe_statics, false, false, false, false, false,
      false, abi_compat);
}

inline cir::RecordLayoutAttr record_layout(mlir::MLIRContext *ctx,
                                           uint64_t align,
                                           bool passed_indirectly,
                                           bool has_trivial_dtor)
/*
#cir.record_layout: the ABI facts CallConvLowering needs.  A NONTRIVIAL_ABI
record cannot be passed in registers.
*/
{
  return cir::RecordLayoutAttr::get(
      ctx, passed_indirectly ? cir::ArgPassingKind::CannotPassInRegs
                             : cir::ArgPassingKind::CanPassInRegs,
      has_trivial_dtor, align);
}

/* Builtin operations whose op classes and builders have churned upstream. */

enum class a_overflow_kind : uint8_t { add, sub, mul };

inline void overflow_op(a_builder &b, mlir::Location loc, a_overflow_kind kind,
                        mlir::Type result_type, mlir::Value x, mlir::Value y,
                        mlir::Value &value, mlir::Value &overflow)
/* Create cir.add/sub/mul.overflow: the wrapped value and the overflow bit. */
{
  switch (kind) {
  case a_overflow_kind::add: {
    auto op = cir::AddOverflowOp::create(b, loc, result_type, x, y);
    value = op.getResult();
    overflow = op.getOverflow();
    break;
  }
  case a_overflow_kind::sub: {
    auto op = cir::SubOverflowOp::create(b, loc, result_type, x, y);
    value = op.getResult();
    overflow = op.getOverflow();
    break;
  }
  default: {
    auto op = cir::MulOverflowOp::create(b, loc, result_type, x, y);
    value = op.getResult();
    overflow = op.getOverflow();
    break;
  }
  }
}

enum class a_bit_op : uint8_t {
  byte_swap, count_leading_zeros, count_trailing_zeros, population_count,
  parity, find_first_set, redundant_sign_bits
};

inline mlir::Value bit_op(a_builder &b, mlir::Location loc, a_bit_op kind,
                          mlir::Value x)
/*
Create the cir bit operation on x (the result type is the operand type; the
zero-undefined flag of clz/ctz is set, as for the GNU builtins).
*/
{
  mlir::Value result;

  switch (kind) {
  case a_bit_op::byte_swap:
    result = cir::ByteSwapOp::create(b, loc, x);
    break;
  case a_bit_op::count_leading_zeros:
    result = cir::BitClzOp::create(b, loc, x, true).getResult();
    break;
  case a_bit_op::count_trailing_zeros:
    result = cir::BitCtzOp::create(b, loc, x, true).getResult();
    break;
  case a_bit_op::population_count:
    result = cir::BitPopcountOp::create(b, loc, x).getResult();
    break;
  case a_bit_op::parity:
    result = cir::BitParityOp::create(b, loc, x).getResult();
    break;
  case a_bit_op::find_first_set:
    result = cir::BitFfsOp::create(b, loc, x).getResult();
    break;
  default:
    result = cir::BitClrsbOp::create(b, loc, x).getResult();
    break;
  }
  return result;
}

inline mlir::Value cmp3way(a_builder &b, mlir::Location loc,
                            unsigned result_bits, mlir::Value lhs,
                            mlir::Value rhs, bool partial, int64_t lt,
                            int64_t eq, int64_t gt, int64_t unordered)
/*
Create cir.cmp3way: the built-in <=>.  The result is a signed integer of
result_bits bits holding lt, eq or gt (and unordered when partial); the
operands have the same arithmetic or pointer type.  LoweringPrepare turns it
into compares and selects.
*/
{
  cir::CmpThreeWayInfoAttr info = partial
      ? cir::CmpThreeWayInfoAttr::get(b.getContext(), lt, eq, gt, unordered)
      : cir::CmpThreeWayInfoAttr::get(b.getContext(), cir::CmpOrdering::Strong,
                                      lt, eq, gt);

  return cir::CmpThreeWayOp::create(b, loc, b.getSIntNTy(result_bits), lhs,
                                    rhs, info);
}

inline mlir::Value sign_bit(a_builder &b, mlir::Location loc, mlir::Value x)
/* Create cir.signbit (a bool). */
{ return cir::SignBitOp::create(b, loc, b.getBoolTy(), x); }

inline mlir::Value is_fp_class(a_builder &b, mlir::Location loc,
                               mlir::Value x, uint64_t mask)
/* Create cir.is_fp_class with an llvm.is.fpclass test mask (a bool). */
{
  return cir::IsFPClassOp::create(b, loc, b.getBoolTy(), x,
                                  static_cast<cir::FPClassTest>(mask));
}

inline void clear_initial_value(cir::GlobalOp global)
/* Turn a definition back into a declaration: remove its initializer. */
{ global->removeAttr("initial_value"); }

inline llvm::StringRef record_layouts_attr_name()
/* The module attribute that carries the per-record ABI metadata. */
{ return cir::CIRDialect::getRecordLayoutsAttrName(); }

/* Virtual functions, vtables and typeinfo (ECIL 1.2, M4). */

inline cir::RecordType anonymous_record(mlir::MLIRContext *ctx,
                                        llvm::ArrayRef<mlir::Type> members)
/* An anonymous struct of data members (the type of a vtable or a typeinfo). */
{
  return cir::StructType::get(ctx, members, /*packed=*/false,
                              /*is_class=*/false,
                              cir::RecordType::getAllDataKinds(members));
}

inline mlir::Attribute vtable_attr(cir::RecordType type,
                                   mlir::ArrayAttr arrays)
/* #cir.vtable<{array, array...}> of the anonymous record type. */
{ return cir::VTableAttr::get(type, arrays); }

inline mlir::Attribute typeinfo_attr(cir::RecordType type,
                                     mlir::ArrayAttr fields)
/* #cir.typeinfo<{...}> of the anonymous record type. */
{ return cir::TypeInfoAttr::get(type, fields); }

inline mlir::Attribute global_view(mlir::Type type, mlir::StringAttr symbol,
                                   mlir::ArrayAttr indices,
                                   bool address_point)
/* #cir.global_view<@symbol, [indices], address_point>. */
{
  return cir::GlobalViewAttr::get(type, mlir::FlatSymbolRefAttr::get(symbol),
                                  indices, address_point);
}

inline mlir::Operation *vtable_address_point(a_builder &b, mlir::Location loc,
                                             mlir::StringAttr symbol,
                                             unsigned index, unsigned offset)
/* cir.vtable.address_point(@symbol, address_point = <index, offset>). */
{
  mlir::MLIRContext *ctx = b.getContext();

  return cir::VTableAddrPointOp::create(
             b, loc, cir::VPtrType::get(ctx),
             mlir::FlatSymbolRefAttr::get(symbol),
             cir::AddressPointAttr::get(ctx, index, offset)).getOperation();
}

inline void set_vtable_address_point(mlir::Operation *op,
                                     mlir::StringAttr symbol, unsigned index,
                                     unsigned offset)
/* Choose the vtable and the address point of an address_point op later. */
{
  auto point = mlir::cast<cir::VTableAddrPointOp>(op);
  mlir::MLIRContext *ctx = op->getContext();

  point.setNameAttr(mlir::FlatSymbolRefAttr::get(symbol));
  point.setAddressPointAttr(cir::AddressPointAttr::get(ctx, index, offset));
}

inline mlir::Value vtable_get_vptr(a_builder &b, mlir::Location loc,
                                   mlir::Value object)
/* cir.vtable.get_vptr: the address of the vptr of the object. */
{ return cir::VTableGetVPtrOp::create(b, loc, object); }

inline mlir::Value vtable_function_slot(a_builder &b, mlir::Location loc,
                                        mlir::Type result_type,
                                        mlir::Value vptr, uint64_t index)
/* cir.vtable.get_virtual_fn_addr: the address of slot `index`. */
{
  return cir::VTableGetVirtualFnAddrOp::create(b, loc, result_type, vptr,
                                               index);
}

inline mlir::Operation *vtt_address_point(a_builder &b, mlir::Location loc,
                                          mlir::Type result_type,
                                          mlir::StringAttr symbol,
                                          mlir::Value vtt, uint32_t offset)
/* cir.vtt.address_point: the element at `offset` of the VTT global `symbol`
   (the complete-object structors) or of the VTT the pointer `vtt` is (the
   structors that take the VTT as a parameter). */
{
  return cir::VTTAddrPointOp::create(
             b, loc, result_type,
             symbol ? mlir::FlatSymbolRefAttr::get(symbol)
                    : mlir::FlatSymbolRefAttr(),
             vtt, offset).getOperation();
}

inline void set_vtt_address_point(mlir::Operation *op, mlir::StringAttr symbol,
                                  uint32_t offset)
/* Choose the VTT symbol (when the op has one) and the offset later. */
{
  auto point = mlir::cast<cir::VTTAddrPointOp>(op);

  if (symbol)
    point.setNameAttr(mlir::FlatSymbolRefAttr::get(symbol));
  point.setOffset(offset);
}

inline void set_signed_constant(mlir::Operation *op, int64_t value)
/* Choose the value of a 64-bit signed cir.const later. */
{
  auto constant = mlir::cast<cir::ConstantOp>(op);

  constant.setValueAttr(cir::IntAttr::get(constant.getType(), value));
}

/* Pointers to members and RTTI (ECIL 1.2, M4 slice S3). */

inline mlir::Type data_member_type(mlir::Type member, mlir::Type record)
/* !cir.data_member<member in record>. */
{
  return cir::DataMemberType::get(member, mlir::cast<cir::RecordType>(record));
}

inline mlir::Type method_type(cir::FuncType with_this, mlir::Type record)
/* !cir.method<func in record>: the function type with its `this`, as
   CIRGen's arrangeCXXMethodType makes it. */
{
  return cir::MethodType::get(with_this, mlir::cast<cir::RecordType>(record));
}

inline mlir::Attribute data_member_attr(mlir::Type type,
                                        llvm::ArrayRef<int32_t> path,
                                        bool null)
/* #cir.data_member<[path]> or #cir.data_member<null>. */
{
  auto member = mlir::cast<cir::DataMemberType>(type);

  return null ? cir::DataMemberAttr::get(member)
              : cir::DataMemberAttr::get(member, path);
}

inline mlir::Attribute data_member_offset_attr(mlir::Type type,
                                               uint64_t offset)
/* #cir.data_member_offset<N>: a member that has no CIR field. */
{
  return cir::DataMemberOffsetAttr::get(mlir::cast<cir::DataMemberType>(type),
                                        offset);
}

inline mlir::Attribute method_attr_null(mlir::Type type)
/* #cir.method<null>. */
{ return cir::MethodAttr::get(mlir::cast<cir::MethodType>(type)); }

inline mlir::Attribute method_attr_symbol(mlir::Type type,
                                          mlir::StringAttr symbol)
/* #cir.method<@symbol>. */
{
  return cir::MethodAttr::get(mlir::cast<cir::MethodType>(type),
                              mlir::FlatSymbolRefAttr::get(symbol));
}

inline mlir::Attribute method_attr_virtual(mlir::Type type, uint64_t offset)
/* #cir.method<vtable_offset = N>. */
{ return cir::MethodAttr::get(mlir::cast<cir::MethodType>(type), offset); }

inline mlir::Value get_runtime_member(a_builder &b, mlir::Location loc,
                                      mlir::Value object, mlir::Value member)
/* cir.get_runtime_member: the address of the member `member` designates in
   the object (the .* and ->* operators). */
{
  auto type = mlir::cast<cir::DataMemberType>(member.getType());

  return cir::GetRuntimeMemberOp::create(
      b, loc, cir::PointerType::get(type.getMemberTy()), object, member);
}

inline mlir::Value convert_member_pointer(a_builder &b, mlir::Location loc,
                                          bool to_derived, bool is_method,
                                          mlir::Type result, mlir::Value src,
                                          uint64_t offset)
/* cir.derived_* and cir.base_* of a data member or method pointer: the
   offset of the base subobject in the derived class is added or
   subtracted. */
{
  mlir::IntegerAttr offset_attr = b.getIndexAttr(offset);
  mlir::Value value;

  if (is_method) {
    value = to_derived
        ? cir::DerivedMethodOp::create(b, loc, result, src, offset_attr)
              .getResult()
        : cir::BaseMethodOp::create(b, loc, result, src, offset_attr)
              .getResult();
  } else {
    value = to_derived
        ? cir::DerivedDataMemberOp::create(b, loc, result, src, offset_attr)
              .getResult()
        : cir::BaseDataMemberOp::create(b, loc, result, src, offset_attr)
              .getResult();
  }
  return value;
}

inline mlir::Value member_pointer_to_bool(a_builder &b, mlir::Location loc,
                                          mlir::Value value)
/* The !cir.bool of a member pointer (non-null). */
{
  return b.createCast(loc, cir::CastKind::member_ptr_to_bool, value,
                      b.getBoolTy());
}

inline mlir::Value vtable_get_type_info(a_builder &b, mlir::Location loc,
                                        mlir::Type result_type,
                                        mlir::Value vptr)
/* cir.vtable.get_type_info: the address of the RTTI slot of the vtable. */
{ return cir::VTableGetTypeInfoOp::create(b, loc, result_type, vptr); }

inline mlir::Attribute dyn_cast_info(mlir::Attribute src_rtti,
                                     mlir::Attribute dest_rtti,
                                     mlir::StringAttr runtime_func,
                                     mlir::StringAttr bad_cast_func,
                                     mlir::Type ptrdiff, int64_t hint)
/* #cir.dyn_cast_info<...>. */
{
  return cir::DynamicCastInfoAttr::get(
      mlir::cast<cir::GlobalViewAttr>(src_rtti),
      mlir::cast<cir::GlobalViewAttr>(dest_rtti),
      mlir::FlatSymbolRefAttr::get(runtime_func),
      mlir::FlatSymbolRefAttr::get(bad_cast_func),
      cir::IntAttr::get(ptrdiff, hint));
}

inline void set_dyn_cast_hint(mlir::Operation *op, int64_t hint)
/* Choose the offset hint of a cir.dyn_cast later. */
{
  auto cast = mlir::cast<cir::DynamicCastOp>(op);
  cir::DynamicCastInfoAttr info = cast.getInfoAttr();

  cast.setInfoAttr(cir::DynamicCastInfoAttr::get(
      info.getSrcRtti(), info.getDestRtti(), info.getRuntimeFunc(),
      info.getBadCastFunc(),
      cir::IntAttr::get(info.getOffsetHint().getType(), hint)));
}

inline mlir::Value dynamic_cast_op(a_builder &b, mlir::Location loc,
                                   mlir::Value src, mlir::Type result,
                                   bool is_ref, mlir::Attribute info)
/* cir.dyn_cast ptr|ref; with no info, the cast to the complete object
   (void *). */
{
  return cir::DynamicCastOp::create(
      b, loc, result,
      is_ref ? cir::DynamicCastKind::Ref : cir::DynamicCastKind::Ptr, src,
      info ? mlir::cast<cir::DynamicCastInfoAttr>(info)
           : cir::DynamicCastInfoAttr(),
      /*relative_layout=*/false);
}

/* Exceptions (ECIL 1.2, M4 slice S4). */

inline void mark_catch_copy_thunk(cir::FuncOp fn)
/* The unit attribute that tags a catch copy thunk. */
{
  fn->setAttr(cir::CIRDialect::getCatchCopyThunkAttrName(),
              mlir::UnitAttr::get(fn->getContext()));
}

inline void set_hidden(cir::FuncOp fn)
/* ELF hidden visibility of a function. */
{
  fn.setGlobalVisibility(cir::VisibilityKind::Hidden);
}

inline void construct_catch_param(a_builder &b, mlir::Location loc,
                                  uint64_t kind, mlir::Value token,
                                  mlir::Value param, mlir::Attribute copy_fn)
/* cir.construct_catch_param (kinds 5 and 6): before cir.begin_catch. */
{
  cir::ConstructCatchParamOp::create(
      b, loc, token, param,
      cir::InitCatchKindAttr::get(b.getContext(),
                                  static_cast<cir::InitCatchKind>(kind)),
      copy_fn ? mlir::cast<mlir::FlatSymbolRefAttr>(
                    mlir::FlatSymbolRefAttr::get(
                        mlir::cast<mlir::StringAttr>(copy_fn)))
              : mlir::FlatSymbolRefAttr());
}

inline void begin_catch(a_builder &b, mlir::Location loc, mlir::Value token,
                        mlir::Type exception_pointer,
                        mlir::Value &catch_token, mlir::Value &exception)
/* cir.begin_catch: the catch token and the pointer to the exception. */
{
  cir::BeginCatchOp op = cir::BeginCatchOp::create(
      b, loc, cir::CatchTokenType::get(b.getContext()), exception_pointer,
      token);

  catch_token = op.getCatchToken();
  exception = op.getExnPtr();
}

inline void init_catch_param(a_builder &b, mlir::Location loc, uint64_t kind,
                             mlir::Value exception, mlir::Value param)
/* cir.init_catch_param: the catch parameter from the exception object. */
{
  cir::InitCatchParamOp::create(
      b, loc, exception, param,
      cir::InitCatchKindAttr::get(b.getContext(),
                                  static_cast<cir::InitCatchKind>(kind)));
}

inline mlir::Value alloc_exception(a_builder &b, mlir::Location loc,
                                   mlir::Type pointer, uint64_t size)
/* cir.alloc.exception: __cxa_allocate_exception of size bytes. */
{
  return cir::AllocExceptionOp::create(b, loc, pointer, b.getI64IntegerAttr(
                                           static_cast<int64_t>(size)));
}

inline void throw_op(a_builder &b, mlir::Location loc, mlir::Value object,
                     mlir::StringAttr type_info, mlir::StringAttr dtor)
/* cir.throw object, @type_info[, @dtor]; no operands: a rethrow. */
{
  cir::ThrowOp::create(
      b, loc, object,
      type_info ? mlir::FlatSymbolRefAttr::get(type_info)
                : mlir::FlatSymbolRefAttr(),
      dtor ? mlir::FlatSymbolRefAttr::get(dtor) : mlir::FlatSymbolRefAttr());
}

inline void eh_terminate(a_builder &b, mlir::Location loc, mlir::Value token)
/* cir.eh.terminate: a handler that ends the program. */
{ cir::EhTerminateOp::create(b, loc, token); }

inline void eh_unexpected(a_builder &b, mlir::Location loc, mlir::Value token)
/* cir.eh.unexpected: the exception violated the specification. */
{ cir::EhUnexpectedOp::create(b, loc, token); }

inline void set_must_tail(cir::CallOp call)
/* Mark a call musttail (the variadic thunks). */
{
  call->setAttr(cir::CIRDialect::getMustTailAttrName(),
                mlir::UnitAttr::get(call->getContext()));
}

inline void set_unwind_tables(cir::FuncOp fn)
/* #cir.uwtable<async> on a function definition (every function: unwinding
   passes through frames of C code too). */
{ fn.setUwtable(cir::UnwindTableKind::Async); }

}  /* namespace compat */
}  /* namespace edg_cir */

#endif /* EDG_CIR_COMPAT_H */
