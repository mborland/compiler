/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
globals.cpp -- DECL_FUNC/DECL_VAR/VAR_INIT/NAME_BIND, the constant stack
(C_* records), re-issued declarations (retyping with per-entity use lists, as
CIRGenModule::replaceUsesOfNonProtoTypeWithRealFunction does but without any
symbol-table walk) and the finish-time policy (late names, record layout
metadata, inline-only bodies, unsupported entities).
*/

#include <algorithm>
#include <cstring>

#include "emitter.h"

namespace edg_cir {

/* Entities. */

std::string a_emitter::unique_symbol(const std::string &base)
/*
Return base if no symbol of that name was created, else base.N.
*/
{
  unsigned &count = symbol_names_[base];
  std::string result = count == 0 ? base : base + "." + std::to_string(count);

  ++count;
  if (result != base)
    ++symbol_names_[result];
  return result;
}  /* a_emitter::unique_symbol */

a_entity *a_emitter::entity_for(uint64_t gid, bool is_function)
/*
Find the entity for gid (of the given kind), or nullptr.
*/
{
  a_entity *result = nullptr;
  auto found = entities_.find(gid);

  if (found != entities_.end() && found->second.is_function == is_function)
    result = &found->second;
  return result;
}  /* a_emitter::entity_for */

void a_emitter::apply_function_attributes(a_entity &e, cir::FuncOp fn)
/*
Set the function's attributes from the latest DECL_FUNC.
*/
{
  bool ok = true;
  cir::GlobalLinkageKind kind = compat::linkage_kind(e.linkage, ok);
  const a_type *t = type_at(e.type);

  fn.setLinkageAttr(cir::GlobalLinkageKindAttr::get(ctx_, kind));
  fn.setNoProto(t != nullptr && t->no_proto);
  if (e.flags & ECIL_DECL_FUNC_NORETURN)
    fn->setAttr(cir::CIRDialect::getNoReturnAttrName(),
                builder_.getUnitAttr());
  else
    fn->removeAttr(cir::CIRDialect::getNoReturnAttrName());
  if (e.flags & ECIL_DECL_FUNC_INLINE)
    fn.setInlineKind(cir::InlineKind::InlineHint);
  mlir::SymbolTable::setSymbolVisibility(
      fn, compat::symbol_visibility(kind, !e.has_body));
}  /* a_emitter::apply_function_attributes */

mlir::Value a_emitter::function_address(a_entity &e)
/*
cir.get_global of a function (a pointer to its function type).
*/
{
  cir::FuncOp fn = mlir::cast<cir::FuncOp>(e.op);
  cir::GetGlobalOp op = cir::GetGlobalOp::create(
      builder_, loc(), cir::PointerType::get(fn.getFunctionType()),
      fn.getSymNameAttr(), /*threadLocal=*/false);

  e.addresses.push_back(op);
  e.referenced = true;
  return op;
}  /* a_emitter::function_address */

mlir::Value a_emitter::variable_address(a_entity &e)
/*
cir.get_global of a variable.
*/
{
  cir::GlobalOp g = mlir::cast<cir::GlobalOp>(e.op);
  cir::GetGlobalOp op = builder_.createGetGlobal(
      loc(), g, g.getTlsModel().has_value());

  e.addresses.push_back(op);
  e.referenced = true;
  return op;
}  /* a_emitter::variable_address */

/* Re-issued declarations. */

namespace {

void fix_address_types(compat::a_builder &builder,
                       std::vector<cir::GetGlobalOp> &addresses,
                       mlir::Type new_pointer)
/*
Give every recorded get_global the symbol's new pointer type and bitcast its
result back to the type its users were built for (clang's replace-and-bitcast
strategy).
*/
{
  for (cir::GetGlobalOp op : addresses) {
    mlir::Value result = op.getAddr();
    mlir::Type old_type = result.getType();

    if (old_type != new_pointer) {
      mlir::OpBuilder::InsertionGuard guard(builder);
      result.setType(new_pointer);
      builder.setInsertionPointAfter(op.getOperation());
      mlir::Value cast = cir::CastOp::create(builder, op.getLoc(), old_type,
                                             cir::CastKind::bitcast, result);
      result.replaceAllUsesExcept(cast, cast.getDefiningOp());
    }
  }
}  /* fix_address_types */

}  /* anonymous namespace */

void a_emitter::convert_call_to_variadic(a_entity &e, cir::CallOp call)
/*
A direct call whose callee type no longer matches becomes get_global +
bitcast to a zero-fixed-parameter variadic function pointer type + indirect
call (spec 8.8).
*/
{
  mlir::OpBuilder::InsertionGuard guard(builder_);
  mlir::Type result_type = call.getNumResults() == 0
      ? static_cast<mlir::Type>(compat::void_type(ctx_))
      : call.getResult().getType();
  mlir::Type call_type = compat::function_type({}, result_type, true);
  mlir::Value address;
  mlir::Value cast;
  llvm::SmallVector<mlir::Value> operands(call.getArgOperands().begin(),
                                          call.getArgOperands().end());
  cir::CallOp replacement;

  builder_.setInsertionPoint(call);
  address = function_address(e);
  cast = builder_.createBitcast(call.getLoc(), address,
                                compat::pointer_to(call_type));
  replacement = builder_.createIndirectCallOp(
      call.getLoc(), cast, mlir::cast<cir::FuncType>(call_type), operands);
  for (mlir::NamedAttribute attr : call->getAttrs()) {
    if (attr.getName() == cir::CIRDialect::getNoThrowAttrName())
      replacement->setAttr(attr.getName(), attr.getValue());
  }
  call->replaceAllUsesWith(replacement);
  call->erase();
}  /* a_emitter::convert_call_to_variadic */

void a_emitter::retype_function(a_entity &e, mlir::Type new_type)
/*
A re-issued DECL_FUNC changed the type: retype the symbol and fix the earlier
uses it recorded.
*/
{
  cir::FuncOp fn = mlir::cast<cir::FuncOp>(e.op);
  cir::FuncType new_func = mlir::cast<cir::FuncType>(new_type);
  std::vector<cir::CallOp> calls;

  calls.swap(e.calls);
  fn.setFunctionTypeAttr(mlir::TypeAttr::get(new_func));
  fix_address_types(builder_, e.addresses,
                    compat::pointer_to(new_func));
  for (cir::CallOp call : calls) {
    bool matches = !call.isIndirect() &&
        (new_func.hasVoidReturn() ? call.getNumResults() == 0
             : (call.getNumResults() == 1 &&
                call.getResult().getType() == new_func.getReturnType()));
    mlir::OperandRange args = call.getArgOperands();

    matches = matches && (new_func.isVarArg()
                            ? args.size() >= new_func.getNumInputs()
                            : args.size() == new_func.getNumInputs());
    for (unsigned i = 0; matches && i < new_func.getNumInputs(); ++i)
      matches = args[i].getType() == new_func.getInput(i);
    if (matches)
      e.calls.push_back(call);
    else
      convert_call_to_variadic(e, call);
  }
}  /* a_emitter::retype_function */

void a_emitter::retype_variable(a_entity &e, mlir::Type new_type)
/*
A re-issued DECL_VAR changed the type (a completed array): retype the global
and fix earlier get_globals.  The initializer is reset; a VAR_INIT follows.
*/
{
  cir::GlobalOp g = mlir::cast<cir::GlobalOp>(e.op);

  g.setSymTypeAttr(mlir::TypeAttr::get(new_type));
  fix_address_types(builder_, e.addresses, compat::pointer_to(new_type));
  if (g.getInitialValue().has_value())
    g.setInitialValueAttr(zero_attribute(new_type));
}  /* a_emitter::retype_variable */

/* DECL_FUNC / DECL_VAR / VAR_INIT / NAME_BIND. */

bool a_emitter::decl_function(const ecil::a_record &r)
/*
Create or update a function entity.
*/
{
  bool ok = true;
  uint64_t gid = 0;
  uint64_t tgid = 0;
  uint64_t linkage = 0;
  uint64_t flags = 0;
  bool good = true;
  std::string name;
  a_type *t;

  uint_operand(r.ops[0], gid);
  uint_operand(r.ops[1], tgid);
  uint_operand(r.ops[2], linkage);
  uint_operand(r.ops[3], flags);
  name = string_for(r.ops[4], good);
  t = type_at(tgid);
  auto found = entities_.find(gid);
  if (!good) {
    ok = format_error("DECL_FUNC uses an unknown string id");
  } else if (t == nullptr || t->kind != a_type_kind::function_type) {
    ok = format_error("DECL_FUNC type is not a function type");
  } else if (found == entities_.end()) {
    bool known = true;
    cir::GlobalLinkageKind kind = compat::linkage_kind(
                                      static_cast<unsigned>(linkage), known);
    a_entity e;
    cir::FuncOp fn;

    e.is_function = true;
    e.gid = gid;
    e.type = tgid;
    e.linkage = static_cast<unsigned>(linkage);
    e.flags = static_cast<uint32_t>(flags);
    e.named = r.ops[4].kind != ecil::an_operand_kind::nil;
    e.name = e.named ? name : "__ecil." + std::to_string(gid);
    if (!compat::linkage_is_local(kind) || e.named)
      e.name = unique_symbol(e.name);
    e.placeholder = e.name;
    fn = compat::create_function(builder_, module_, loc(), e.name, t->cir);
    e.op = fn.getOperation();
    apply_function_attributes(e, fn);
    entity_order_.push_back(gid);
    entities_.emplace(gid, std::move(e));
  } else {
    a_entity &e = found->second;
    cir::FuncOp fn;
    if (!e.is_function) {
      ok = format_error("DECL_FUNC re-issues a variable's id");
    } else if (e.type != tgid && e.has_body) {
      ok = format_error("a function's type changed after its body was "
                        "emitted");
    } else {
      fn = mlir::cast<cir::FuncOp>(e.op);
      e.linkage = static_cast<unsigned>(linkage);
      e.flags = static_cast<uint32_t>(flags);
      ++e.reissue_count;
      if (r.ops[4].kind != ecil::an_operand_kind::nil && !e.named) {
        e.named = true;
        renames_.push_back({gid, name});
      }
      if (e.type != tgid) {
        a_type *old_type = type_at(e.type);
        if (old_type == nullptr || old_type->cir != t->cir)
          retype_function(e, t->cir);
        e.type = tgid;
      }
      apply_function_attributes(e, fn);
    }
  }
  return ok;
}  /* a_emitter::decl_function */

bool a_emitter::decl_variable(const ecil::a_record &r)
/*
Create or update a variable entity (a cir.global).
*/
{
  bool ok = true;
  uint64_t gid = 0;
  uint64_t tgid = 0;
  uint64_t linkage = 0;
  uint64_t flags = 0;
  uint64_t align = 0;
  bool good = true;
  std::string name;
  a_type *t;
  bool is_definition;

  uint_operand(r.ops[0], gid);
  uint_operand(r.ops[1], tgid);
  uint_operand(r.ops[2], linkage);
  uint_operand(r.ops[3], flags);
  uint_operand(r.ops[4], align);
  name = string_for(r.ops[5], good);
  t = type_at(tgid);
  is_definition = (flags & ECIL_DECL_VAR_DEFINITION) != 0;
  auto found = entities_.find(gid);
  if (!good) {
    ok = format_error("DECL_VAR uses an unknown string id");
  } else if (t == nullptr || t->kind == a_type_kind::function_type ||
             t->kind == a_type_kind::void_type) {
    ok = format_error("DECL_VAR type is not an object type");
  } else if (found == entities_.end()) {
    bool known = true;
    cir::GlobalLinkageKind kind = compat::linkage_kind(
                                      static_cast<unsigned>(linkage), known);
    a_entity e;
    cir::GlobalOp g;
    bool is_string = (flags & ECIL_DECL_VAR_STRING_LITERAL) != 0;

    e.gid = gid;
    e.type = tgid;
    e.linkage = static_cast<unsigned>(linkage);
    e.flags = static_cast<uint32_t>(flags);
    e.align = static_cast<uint32_t>(align);
    e.named = r.ops[5].kind != ecil::an_operand_kind::nil;
    e.name = e.named ? name : (is_string ? ".str"
                                         : "__ecil." + std::to_string(gid));
    e.name = unique_symbol(e.name);
    e.placeholder = e.name;
    g = compat::create_global(builder_, module_, loc(), e.name, t->cir,
                              (flags & ECIL_DECL_VAR_CONST) != 0 || is_string,
                              kind);
    e.op = g.getOperation();
    if (is_definition) {
      g.setInitialValueAttr(zero_attribute(t->cir));
      compat::set_dso_local(g.getOperation(), true);
    }
    if (align != 0)
      g.setAlignmentAttr(builder_.getI64IntegerAttr(
                              static_cast<int64_t>(align)));
    if (flags & ECIL_DECL_VAR_TLS)
      g.setTlsModel(cir::TLSModel::GeneralDynamic);
    mlir::SymbolTable::setSymbolVisibility(
        g, compat::symbol_visibility(kind, !is_definition));
    entity_order_.push_back(gid);
    entities_.emplace(gid, std::move(e));
  } else if (found->second.is_function) {
    ok = format_error("DECL_VAR re-issues a function's id");
  } else {
    a_entity &e = found->second;
    cir::GlobalOp g;
    bool known = true;
    cir::GlobalLinkageKind kind = compat::linkage_kind(
                                      static_cast<unsigned>(linkage), known);

    g = mlir::cast<cir::GlobalOp>(e.op);
    e.linkage = static_cast<unsigned>(linkage);
    e.flags = static_cast<uint32_t>(flags);
    if (align != 0) {
      e.align = static_cast<uint32_t>(align);
      g.setAlignmentAttr(builder_.getI64IntegerAttr(
                              static_cast<int64_t>(align)));
    }
    ++e.reissue_count;
    if (r.ops[5].kind != ecil::an_operand_kind::nil && !e.named) {
      e.named = true;
      renames_.push_back({gid, name});
    }
    if (e.type != tgid) {
      a_type *old_type = type_at(e.type);
      if (old_type == nullptr || old_type->cir != t->cir)
        retype_variable(e, t->cir);
      e.type = tgid;
    }
    g.setLinkageAttr(cir::GlobalLinkageKindAttr::get(ctx_, kind));
    g.setConstant((flags & ECIL_DECL_VAR_CONST) != 0 ||
                  (flags & ECIL_DECL_VAR_STRING_LITERAL) != 0);
    if (is_definition && !g.getInitialValue().has_value()) {
      g.setInitialValueAttr(zero_attribute(t->cir));
      compat::set_dso_local(g.getOperation(), true);
    } else if (!is_definition && g.getInitialValue().has_value() &&
               (flags & ECIL_DECL_VAR_UNSUPPORTED)) {
      g->removeAttr("initial_value");
    }
    mlir::SymbolTable::setSymbolVisibility(
        g, compat::symbol_visibility(kind, !is_definition));
  }
  return ok;
}  /* a_emitter::decl_variable */

bool a_emitter::var_init(const ecil::a_record &r)
/*
VAR_INIT: the constant on top of the constant stack initializes the variable.
*/
{
  bool ok = true;
  uint64_t gid = 0;
  a_entity *e;

  uint_operand(r.ops[0], gid);
  e = entity_for(gid, false);
  if (e == nullptr) {
    ok = format_error("VAR_INIT for an unknown variable");
  } else if (consts_.empty() || !const_aggs_.empty()) {
    ok = format_error("VAR_INIT without a constant");
  } else {
    cir::GlobalOp g = mlir::cast<cir::GlobalOp>(e->op);
    a_const c = consts_.back();
    consts_.pop_back();
    if (mlir::cast<mlir::TypedAttr>(c.attr).getType() != g.getSymType()) {
      ok = format_error("VAR_INIT constant type differs from the variable "
                        "type");
    } else {
      g.setInitialValueAttr(c.attr);
      e->has_init = true;
    }
  }
  return ok;
}  /* a_emitter::var_init */

bool a_emitter::name_bind(const ecil::a_record &r)
/*
NAME_BIND: bind a late name.  Unreferenced entities are renamed at once;
others at finish (the rename needs a symbol-use rewrite).
*/
{
  bool ok = true;
  uint64_t gid = 0;
  uint64_t linkage = 0;
  bool good = true;
  std::string name;
  auto found = entities_.end();

  uint_operand(r.ops[0], gid);
  name = string_for(r.ops[1], good);
  uint_operand(r.ops[2], linkage);
  found = entities_.find(gid);
  if (!good) {
    ok = format_error("NAME_BIND uses an unknown string id");
  } else if (found == entities_.end()) {
    ok = format_error("NAME_BIND for an unknown entity");
  } else {
    a_entity &e = found->second;
    bool known = true;
    cir::GlobalLinkageKind kind = compat::linkage_kind(
                                      static_cast<unsigned>(linkage), known);

    e.linkage = static_cast<unsigned>(linkage);
    if (e.is_function)
      mlir::cast<cir::FuncOp>(e.op).setLinkageAttr(
          cir::GlobalLinkageKindAttr::get(ctx_, kind));
    else
      mlir::cast<cir::GlobalOp>(e.op).setLinkageAttr(
          cir::GlobalLinkageKindAttr::get(ctx_, kind));
    if (!e.named) {
      e.named = true;
      renames_.push_back({gid, name});
    }
  }
  return ok;
}  /* a_emitter::name_bind */

/* Constants. */

void a_emitter::push_const(mlir::Attribute attr, bool is_zero)
/*
Push a constant with its zero-ness.
*/
{
  a_const c = {attr, is_zero};
  consts_.push_back(c);
}  /* a_emitter::push_const */

mlir::Attribute a_emitter::zero_attribute(mlir::Type type)
/*
The zero constant of any (scalar or aggregate) type.
*/
{
  return builder_.getZeroInitAttr(type);
}  /* a_emitter::zero_attribute */

mlir::Attribute a_emitter::scalar_int_attribute(mlir::Type type,
                                                const ecil::an_operand &op,
                                                bool &ok)
/*
An integer constant of an integer, bool or pointer type.
*/
{
  mlir::Attribute result;
  llvm::APInt value;

  ok = true;
  if (auto int_type = mlir::dyn_cast<cir::IntType>(type)) {
    ok = int_operand_apint(op, int_type.getWidth(), value);
    if (ok)
      result = cir::IntAttr::get(type, value);
  } else if (mlir::isa<cir::BoolType>(type)) {
    ok = int_operand_apint(op, 64, value);
    if (ok)
      result = cir::BoolAttr::get(ctx_, !value.isZero());
  } else if (mlir::isa<cir::PointerType>(type)) {
    ok = int_operand_apint(op, 64, value);
    if (ok)
      result = cir::ConstPtrAttr::get(
          type, builder_.getI64IntegerAttr(value.getSExtValue()));
  } else {
    ok = false;
  }
  return result;
}  /* a_emitter::scalar_int_attribute */

mlir::Attribute a_emitter::float_attribute(mlir::Type type,
                                           const ecil::an_operand &op,
                                           bool &ok)
/*
A floating-point constant from an f32, f64 or fbits operand.
*/
{
  mlir::Attribute result;
  auto fp = mlir::dyn_cast<cir::FPTypeInterface>(type);

  ok = fp != nullptr;
  if (ok) {
    const llvm::fltSemantics &sem = fp.getFloatSemantics();
    unsigned width = llvm::APFloat::getSizeInBits(sem);

    if (op.kind == ecil::an_operand_kind::f32 ||
        op.kind == ecil::an_operand_kind::f64) {
      unsigned have = op.kind == ecil::an_operand_kind::f32 ? 32 : 64;
      ok = have == width;
      if (ok)
        result = cir::FPAttr::get(
            type, llvm::APFloat(sem, llvm::APInt(have, op.u)));
    } else if (op.kind == ecil::an_operand_kind::fbits) {
      llvm::SmallVector<uint64_t, 2> words(2, 0);
      std::memcpy(words.data(), op.data, std::min<uint32_t>(op.length, 16));
      llvm::APInt bits(128, words);
      result = cir::FPAttr::get(type, llvm::APFloat(sem, bits.trunc(width)));
    } else {
      ok = false;
    }
  }
  return result;
}  /* a_emitter::float_attribute */

mlir::Attribute a_emitter::string_attribute(const uint8_t *data,
                                            uint32_t length,
                                            mlir::Type array_type)
/*
The constant of a char array initialized from string bytes: the zero tail is
not materialized (the trailing_zeros form), an all-zero array is #cir.zero.
*/
{
  auto array = mlir::cast<cir::ArrayType>(array_type);
  uint64_t count = array.getSize();
  auto element = mlir::cast<cir::IntType>(array.getElementType());
  unsigned width = element.getWidth();
  mlir::Attribute result;

  if (width == 8) {
    std::string bytes(reinterpret_cast<const char *>(data),
                      std::min<uint64_t>(length, count));
    size_t last = bytes.find_last_not_of('\0');
    if (last == std::string::npos) {
      result = cir::ZeroAttr::get(array_type);
    } else {
      size_t trailing = count - last - 1;
      auto truncated = cir::ArrayType::get(element, count - trailing);
      auto text = mlir::StringAttr::get(
          llvm::StringRef(bytes).take_front(last + 1), truncated);
      result = cir::ConstArrayAttr::get(array, text);
    }
  } else {
    unsigned step = width / 8;
    uint64_t have = std::min<uint64_t>(length / step, count);
    uint64_t last = 0;
    llvm::SmallVector<mlir::Attribute> elements;

    for (uint64_t k = 0; k < have; ++k) {
      uint64_t v = 0;
      std::memcpy(&v, data + k * step, step);
      elements.push_back(cir::IntAttr::get(element,
                                           llvm::APInt(width, v)));
      if (v != 0)
        last = k + 1;
    }
    elements.resize(last, mlir::Attribute());
    if (last == 0) {
      result = cir::ZeroAttr::get(array_type);
    } else {
      result = cir::ConstArrayAttr::get(array,
                                        builder_.getArrayAttr(elements));
    }
  }
  return result;
}  /* a_emitter::string_attribute */

bool a_emitter::commit_pending(a_const_aggregate &agg)
/*
Attach the constant built since the last C_SEL/C_REPEAT to the aggregate.
*/
{
  bool ok = true;

  if (agg.have_pending) {
    if (consts_.size() != agg.base + 1) {
      ok = format_error("C_SEL/C_REPEAT must be followed by exactly one "
                        "constant");
    } else {
      a_const_aggregate::a_entry entry = {agg.pending_index, agg.pending_count,
                                          consts_.back()};
      consts_.pop_back();
      agg.entries.push_back(entry);
      agg.have_pending = false;
    }
  }
  return ok;
}  /* a_emitter::commit_pending */

bool a_emitter::build_constant_aggregate(a_const_aggregate &agg, a_const &out)
/*
Build the attribute of a finished C_AGG_BEGIN..C_AGG_END aggregate.
*/
{
  bool ok = true;
  a_type *t = type_at(agg.type);

  out.attr = mlir::Attribute();
  out.is_zero = true;
  for (const auto &entry : agg.entries) {
    if (!entry.value.is_zero)
      out.is_zero = false;
  }
  if (t == nullptr) {
    ok = format_error("C_AGG_BEGIN names an unknown type");
  } else if (t->kind == a_type_kind::array_type) {
    uint64_t count = t->count;
    std::vector<mlir::Attribute> elements;
    uint64_t listed = 0;
    a_type *element = type_at(t->target);

    for (const auto &entry : agg.entries) {
      if (entry.index + entry.count > count && count != 0)
        ok = format_error("C_SEL index past the end of the array");
      if (!entry.value.is_zero)
        listed = std::max(listed, entry.index + entry.count);
    }
    if (!ok) {
      /* The failure is already recorded. */
    } else if (out.is_zero) {
      out.attr = cir::ZeroAttr::get(t->cir);
    } else {
      mlir::Attribute zero = zero_attribute(element->cir);
      elements.assign(listed, zero);
      for (const auto &entry : agg.entries) {
        for (uint64_t k = 0; k < entry.count; ++k) {
          if (entry.index + k < listed)
            elements[entry.index + k] = entry.value.attr;
        }
      }
      out.attr = cir::ConstArrayAttr::get(mlir::cast<cir::ArrayType>(t->cir),
                                          builder_.getArrayAttr(elements));
    }
  } else if (t->kind == a_type_kind::record_type && t->record->complete) {
    a_record_info &rec = *t->record;
    cir::RecordType record_type = mlir::cast<cir::RecordType>(t->cir);

    if (rec.is_union) {
      if (agg.entries.size() > 1) {
        ok = format_error("a union constant has at most one member");
      } else if (agg.entries.empty() || out.is_zero) {
        out.attr = cir::ZeroAttr::get(t->cir);
        out.is_zero = true;
      } else if (agg.entries[0].index >= rec.fields.size()) {
        ok = format_error("C_SEL index past the union's fields");
      } else {
        const auto &entry = agg.entries[0];
        size_t index = static_cast<size_t>(entry.index);
        mlir::Attribute element = entry.value.attr;
        if (rec.fields[index].is_bitfield) {
          const a_bitfield_info &info = rec.bitfields[index];
          unsigned bits = static_cast<unsigned>(type_size(info.storage) * 8);
          llvm::APInt unit(bits, 0);
          if (auto iv = mlir::dyn_cast<cir::IntAttr>(entry.value.attr)) {
            llvm::APInt v = iv.getValue().zextOrTrunc(info.size).zext(bits);
            unit = v << info.offset;
          } else if (auto bv = mlir::dyn_cast<cir::BoolAttr>(
                                                    entry.value.attr)) {
            unit = llvm::APInt(bits, bv.getValue() ? 1 : 0) << info.offset;
          }
          if (auto it = mlir::dyn_cast<cir::IntType>(info.storage)) {
            element = cir::IntAttr::get(it, unit);
          } else {
            auto arr = mlir::cast<cir::ArrayType>(info.storage);
            llvm::SmallVector<mlir::Attribute> bytes;
            for (uint64_t k = 0; k < arr.getSize(); ++k)
              bytes.push_back(cir::IntAttr::get(
                  arr.getElementType(), unit.lshr(k * 8).trunc(8)));
            element = cir::ConstArrayAttr::get(arr,
                                               builder_.getArrayAttr(bytes));
          }
        }
        out.attr = cir::ConstRecordAttr::get(record_type,
                                             builder_.getArrayAttr({element}));
      }
    } else {
      std::vector<mlir::Attribute> elems(rec.members.size());
      std::vector<llvm::APInt> units(rec.members.size());
      std::vector<bool> unit_used(rec.members.size(), false);

      for (const auto &entry : agg.entries) {
        for (uint64_t k = 0; ok && k < entry.count; ++k) {
          size_t index = static_cast<size_t>(entry.index + k);
          uint32_t member = 0;
          if (index >= rec.fields.size()) {
            ok = format_error("C_SEL index past the record's fields");
            continue;
          }
          member = rec.member_index[index];
          if (rec.fields[index].is_bitfield) {
            const a_bitfield_info &info = rec.bitfields[index];
            unsigned bits = static_cast<unsigned>(type_size(info.storage) * 8);
            if (!unit_used[member]) {
              units[member] = llvm::APInt(bits, 0);
              unit_used[member] = true;
            }
            llvm::APInt v(bits, 0);
            if (auto iv = mlir::dyn_cast<cir::IntAttr>(entry.value.attr))
              v = iv.getValue().zextOrTrunc(info.size).zext(bits);
            else if (auto bv = mlir::dyn_cast<cir::BoolAttr>(entry.value.attr))
              v = llvm::APInt(bits, bv.getValue() ? 1 : 0);
            else if (!mlir::isa<cir::ZeroAttr>(entry.value.attr))
              ok = format_error("bit-field constant is not an integer");
            if (ok) {
              llvm::APInt mask = llvm::APInt::getLowBitsSet(bits, info.size);
              units[member] = (units[member] & ~(mask << info.offset)) |
                              ((v & mask) << info.offset);
            }
          } else {
            elems[member] = entry.value.attr;
          }
        }
      }
      std::vector<mlir::Attribute> stored;
      for (size_t m = 0; ok && m < rec.members.size(); ++m) {
        mlir::Type member_type = rec.members[m];
        if (!compat::member_owns_bytes(member_type))
          continue;
        mlir::Type storage = compat::member_storage_type(member_type);
        if (unit_used[m]) {
          if (auto it = mlir::dyn_cast<cir::IntType>(storage)) {
            stored.push_back(cir::IntAttr::get(it, units[m]));
          } else {
            auto arr = mlir::cast<cir::ArrayType>(storage);
            llvm::SmallVector<mlir::Attribute> bytes;
            for (uint64_t k = 0; k < arr.getSize(); ++k)
              bytes.push_back(cir::IntAttr::get(
                  arr.getElementType(), units[m].lshr(k * 8).trunc(8)));
            stored.push_back(cir::ConstArrayAttr::get(
                arr, builder_.getArrayAttr(bytes)));
          }
        } else if (elems[m]) {
          stored.push_back(elems[m]);
        } else {
          stored.push_back(zero_attribute(storage));
        }
      }
      if (!ok) {
        /* The failure is already recorded. */
      } else if (out.is_zero) {
        out.attr = cir::ZeroAttr::get(t->cir);
      } else {
        out.attr = cir::ConstRecordAttr::get(record_type,
                                             builder_.getArrayAttr(stored));
      }
    }
  } else {
    ok = format_error("C_AGG_BEGIN type is not a complete record or array");
  }
  return ok;
}  /* a_emitter::build_constant_aggregate */

bool a_emitter::const_aggregate_end()
/*
C_AGG_END: build the aggregate and push it as one constant.
*/
{
  bool ok = true;
  a_const result;

  if (const_aggs_.empty())
    ok = format_error("C_AGG_END without C_AGG_BEGIN");
  else
    ok = commit_pending(const_aggs_.back()) &&
         build_constant_aggregate(const_aggs_.back(), result);
  if (ok) {
    const_aggs_.pop_back();
    consts_.push_back(result);
  }
  return ok;
}  /* a_emitter::const_aggregate_end */

bool a_emitter::constant_record(const ecil::a_record &r)
/*
Handle a C_* record.
*/
{
  bool ok = true;
  uint64_t tgid = 0;
  a_type *t = nullptr;

  if (r.opcode == eop_c_agg_end) {
    ok = const_aggregate_end();
  } else if (r.opcode == eop_c_sel || r.opcode == eop_c_repeat) {
    uint64_t index = 0;
    uint64_t count = 1;
    if (const_aggs_.empty())
      ok = format_error("C_SEL outside C_AGG_BEGIN");
    else
      ok = commit_pending(const_aggs_.back());
    if (ok) {
      uint_operand(r.ops[0], index);
      if (r.opcode == eop_c_repeat)
        uint_operand(r.ops[1], count);
      a_const_aggregate &agg = const_aggs_.back();
      agg.have_pending = true;
      agg.pending_index = index;
      agg.pending_count = count;
    }
  } else {
    uint_operand(r.ops[0], tgid);
    t = type_at(tgid);
    if (t == nullptr)
      ok = format_error("constant names an unknown type");
    else
      ok = scalar_constant_record(r, t, tgid);
  }
  return ok;
}  /* a_emitter::constant_record */

bool a_emitter::scalar_constant_record(const ecil::a_record &r, a_type *t,
                                       uint64_t tgid)
/*
The C_INT / C_PTR / C_FLOAT / C_NULL / C_ZERO / C_STRING / C_ADDR /
C_AGG_BEGIN records, once their type is known.
*/
{
  bool ok = true;
  bool good = true;

  switch (r.opcode) {
  case eop_c_int: {
    mlir::Attribute attr = scalar_int_attribute(t->cir, r.ops[1], good);
    bool zero = true;
    if (!good) {
      ok = format_error("C_INT of a non-integer type");
      break;
    }
    if (auto ia = mlir::dyn_cast<cir::IntAttr>(attr))
      zero = ia.getValue().isZero();
    else if (auto ba = mlir::dyn_cast<cir::BoolAttr>(attr))
      zero = !ba.getValue();
    else if (auto pa = mlir::dyn_cast<cir::ConstPtrAttr>(attr))
      zero = pa.isNullValue();
    push_const(attr, zero);
    break;
  }
  case eop_c_ptr: {
    mlir::Attribute attr = scalar_int_attribute(t->cir, r.ops[1], good);
    bool zero = true;
    if (!good) {
      ok = format_error("C_PTR of a non-pointer type");
      break;
    }
    if (auto pa = mlir::dyn_cast<cir::ConstPtrAttr>(attr))
      zero = pa.isNullValue();
    else if (auto ia = mlir::dyn_cast<cir::IntAttr>(attr))
      zero = ia.getValue().isZero();
    push_const(attr, zero);
    break;
  }
  case eop_c_float: {
    mlir::Attribute attr = float_attribute(t->cir, r.ops[1], good);
    if (!good) {
      ok = format_error("C_FLOAT does not match its type");
      break;
    }
    push_const(attr, mlir::cast<cir::FPAttr>(attr).getValue().isPosZero());
    break;
  }
  case eop_c_null:
  case eop_c_zero:
    push_const(zero_attribute(t->cir), true);
    break;
  case eop_c_string: {
    if (t->kind != a_type_kind::array_type ||
        !mlir::isa<cir::IntType>(mlir::cast<cir::ArrayType>(t->cir)
                                     .getElementType())) {
      ok = format_error("C_STRING needs an integer array type");
      break;
    }
    mlir::Attribute attr = string_attribute(r.ops[1].data, r.ops[1].length,
                                            t->cir);
    push_const(attr, mlir::isa<cir::ZeroAttr>(attr));
    break;
  }
  case eop_c_addr: {
    uint64_t g = 0;
    uint64_t bytes = 0;
    int64_t offset = 0;
    a_entity *e;
    mlir::StringAttr name;
    mlir::Attribute attr;

    uint_operand(r.ops[1], g);
    ecil::operand_i64(r.ops[2], offset);
    (void)bytes;
    auto found = entities_.find(g);
    if (found == entities_.end()) {
      ok = format_error("C_ADDR names an unknown entity");
      break;
    }
    e = &found->second;
    e->referenced = true;
    name = e->is_function
        ? mlir::cast<cir::FuncOp>(e->op).getSymNameAttr()
        : mlir::cast<cir::GlobalOp>(e->op).getSymNameAttr();
    if (!mlir::isa<cir::PointerType>(t->cir) &&
        !mlir::isa<cir::IntType>(t->cir)) {
      ok = format_error("C_ADDR needs a pointer or integer type");
      break;
    }
    if (offset == 0)
      attr = cir::GlobalViewAttr::get(t->cir,
                                      mlir::FlatSymbolRefAttr::get(name));
    else
      attr = cir::GlobalOffsetAttr::get(
          t->cir, mlir::FlatSymbolRefAttr::get(name), offset);
    push_const(attr, false);
    break;
  }
  case eop_c_agg_begin: {
    a_const_aggregate agg;
    agg.type = tgid;
    agg.base = consts_.size();
    const_aggs_.push_back(std::move(agg));
    break;
  }
  default:
    ok = format_error("unexpected constant record");
    break;
  }
  return ok;
}  /* a_emitter::scalar_constant_record */

/* Finish. */

bool a_emitter::finish()
/*
Late names, record layout metadata, inline-only bodies and the unsupported
entity policy (spec 8.11).  Runs on the worker thread.
*/
{
  bool ok = true;
  bool keep_going = (session_.flags & EDG_CIR_FLAG_KEEP_GOING) != 0;
  std::vector<std::string> errors;
  std::vector<mlir::NamedAttribute> layouts;

  /* Late names. */
  for (const auto &rename : renames_) {
    auto found = entities_.find(rename.first);
    if (found == entities_.end())
      continue;
    a_entity &e = found->second;
    std::string new_name = unique_symbol(rename.second);
    mlir::StringAttr old_attr = mlir::StringAttr::get(ctx_, e.name);
    mlir::StringAttr new_attr = mlir::StringAttr::get(ctx_, new_name);

    if (e.referenced)
      (void)mlir::SymbolTable::replaceAllSymbolUses(old_attr, new_attr,
                                                    module_);
    mlir::SymbolTable::setSymbolName(e.op, new_attr);
    e.name = new_name;
  }
  /* Definitions must be named. */
  for (uint64_t gid : entity_order_) {
    a_entity &e = entities_[gid];
    bool defined = e.is_function ? e.has_body : (e.flags &
                                       ECIL_DECL_VAR_DEFINITION) != 0;
    if (defined && !e.named && !(e.flags & ECIL_DECL_VAR_STRING_LITERAL))
      errors.push_back("definition of entity " + std::to_string(gid) +
                       " has no name at the end of the stream");
  }
  /* C99 inline-only functions keep no body: they are external declarations
     (their definition lives in another translation unit). */
  for (uint64_t gid : entity_order_) {
    a_entity &e = entities_[gid];
    if (e.is_function && e.has_body &&
        (e.flags & ECIL_DECL_FUNC_INLINE_ONLY)) {
      cir::FuncOp fn = mlir::cast<cir::FuncOp>(e.op);
      fn.getBody().getBlocks().clear();
      fn.setLinkageAttr(cir::GlobalLinkageKindAttr::get(
          ctx_, cir::GlobalLinkageKind::ExternalLinkage));
      mlir::SymbolTable::setSymbolVisibility(
          fn, mlir::SymbolTable::Visibility::Private);
      e.has_body = false;
    }
  }
  /* Unsupported entities. */
  for (uint64_t gid : entity_order_) {
    a_entity &e = entities_[gid];
    bool dropped = e.is_function ? (e.flags & ECIL_DECL_FUNC_UNSUPPORTED) != 0
                                 : (e.flags & ECIL_DECL_VAR_UNSUPPORTED) != 0;
    bool local = e.linkage == elk_internal || e.linkage == elk_private;

    if (!dropped)
      continue;
    if (e.is_function ? e.has_body : e.has_init) {
      /* A later successful definition replaced the dropped one. */
      continue;
    }
    if (!keep_going) {
      errors.push_back("the definition of '" + e.name + "' was dropped: it "
                       "uses a construct the CIR back end cannot express");
    } else if (!local) {
      session_.report(EDG_CIR_SEVERITY_WARNING, edc_unsupported,
                      "'" + e.name + "' is only declared: its definition "
                      "uses a construct the CIR back end cannot express");
      if (!e.is_function) {
        mlir::cast<cir::GlobalOp>(e.op)->removeAttr("initial_value");
        mlir::SymbolTable::setSymbolVisibility(
            e.op, mlir::SymbolTable::Visibility::Private);
      }
    } else if (e.referenced) {
      errors.push_back("the internal definition of '" + e.name + "' was "
                       "dropped but is still referenced");
    } else {
      e.op->erase();
      e.op = nullptr;
    }
  }
  /* Record ABI metadata, one entry per complete record as CIRGen does:
     CallConvLowering looks every by-value record up here, and the alignment
     is the front end's (the DataLayout cannot see aligned(N)). */
  for (const a_record_info &info : records_) {
    if (!info.complete)
      continue;
    for (const a_type &t : types_) {
      if (t.kind == a_type_kind::record_type && t.record == &info) {
        layouts.push_back(mlir::NamedAttribute(
            mlir::StringAttr::get(ctx_, info.name),
            compat::record_layout(ctx_, info.align)));
        break;
      }
    }
  }
  if (!layouts.empty())
    module_->setAttr(compat::record_layouts_attr_name(),
                     mlir::DictionaryAttr::get(ctx_, layouts));
  ok = errors.empty();
  if (!ok) {
    /* fail() reports the first message and sets the status. */
    session_.fail(EDG_CIR_ERROR_UNSUPPORTED, edc_unsupported,
                  errors.front());
    for (size_t k = 1; k < errors.size(); ++k)
      session_.report(EDG_CIR_SEVERITY_ERROR, edc_unsupported, errors[k]);
  }
  return ok;
}  /* a_emitter::finish */

}  /* namespace edg_cir */
