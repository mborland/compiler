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
                                           uint64_t align)
/* #cir.record_layout for a trivially copyable C record. */
{
  return cir::RecordLayoutAttr::get(ctx, cir::ArgPassingKind::CanPassInRegs,
                                    /*has_trivial_dtor=*/true, align);
}

inline llvm::StringRef record_layouts_attr_name()
/* The module attribute that carries the per-record ABI metadata. */
{ return cir::CIRDialect::getRecordLayoutsAttrName(); }

}  /* namespace compat */
}  /* namespace edg_cir */

#endif /* EDG_CIR_COMPAT_H */
