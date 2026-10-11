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

inline cir::CleanupScopeOp open_cleanup_scope(a_builder &b, mlir::Location loc)
/*
Create a cir.cleanup.scope normal with an empty body block and an empty
cleanup block (each is terminated when its region is finished).
*/
{
  return cir::CleanupScopeOp::create(b, loc, cir::CleanupKind::Normal,
                                     no_op_body(), no_op_body());
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
                         cir::FuncOp element_dtor, uint64_t element_align)
/*
Create cir.delete_array: the usual operator delete parameters (size, and the
alignment when align_param is not zero) only when it takes any, the element
destructor when the elements have one.  The element alignment is the one the
new[] used to size the cookie.
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
      false, b.getI64IntegerAttr(static_cast<int64_t>(element_align)));
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

}  /* namespace compat */
}  /* namespace edg_cir */

#endif /* EDG_CIR_COMPAT_H */
