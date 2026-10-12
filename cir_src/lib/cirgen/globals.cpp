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
#include <unordered_map>
#include <unordered_set>

#include "mlir/IR/AttrTypeSubElements.h"

#include "emitter.h"

namespace edg_cir {

namespace {

std::string reason_suffix(const a_entity &e)
/*
": <reason>" when the writer gave a reason for dropping the definition (the
unsupported_reason supp key), else nothing.
*/
{
  std::string result;

  if (!e.reason.empty()) {
    result = ": ";
    result += e.reason;
  }
  return result;
}  /* reason_suffix */

}  /* anonymous namespace */

/* Entities. */

std::string_view a_emitter::unsupported_reason(const ecil::a_record &r)
/*
The writer's reason for a declaration it marked UNSUPPORTED (the supp key
unsupported_reason, ECIL 1.1), or an empty view.  The validator has checked
the value.
*/
{
  std::string_view reason;

  if (r.supp != nullptr) {
    std::vector<std::pair<uint64_t, ecil::an_operand>> pairs;

    ecil::supp_pairs(r, pairs);
    for (const auto &pair : pairs) {
      if (pair.first == esk_unsupported_reason &&
          pair.second.kind == ecil::an_operand_kind::bin)
        reason = keep_string(std::string_view(
            reinterpret_cast<const char *>(pair.second.data),
            pair.second.length));
    }
  }
  return reason;
}  /* a_emitter::unsupported_reason */

std::string_view a_emitter::keep_string(std::string_view text)
/*
Copy text into the string arena (views of it stay valid for the session).
*/
{
  return ids_.store_string(text);
}  /* a_emitter::keep_string */

std::string_view a_emitter::claim_symbol(std::string_view want, uint64_t gid)
/*
Claim an MLIR symbol name that no op has (want must live in the arena).  When
it is taken, a unique placeholder takes its place; the final name is chosen
at finish (assign_final_names), which is also where every name collision is
resolved.
*/
{
  std::string_view result = want;

  if (!symbols_.insert(result).second) {
    std::string base = "__ecil." + std::to_string(gid);
    unsigned n = 0;

    for (;;) {
      std::string candidate = n == 0 ? base : base + "." + std::to_string(n);
      result = keep_string(candidate);
      if (symbols_.insert(result).second)
        break;
      ++n;
    }
  }
  return result;
}  /* a_emitter::claim_symbol */

a_entity *a_emitter::entity_at(uint64_t gid)
/*
The function or variable entity of a G-id.  The validator checked that the
G-id was declared, so a miss is an internal error.
*/
{
  a_entity *result = nullptr;
  const ecil::a_gid_entry *entry = ids_.find_gid(gid);

  if (entry == nullptr || entry->payload == nullptr ||
      (entry->kind != ecil::gk_func && entry->kind != ecil::gk_var))
    internal("G-id " + std::to_string(gid) + " has no declaration object");
  else
    result = static_cast<a_entity *>(entry->payload);
  return result;
}  /* a_emitter::entity_at */

a_entity *a_emitter::entity_for(uint64_t gid, bool is_function)
/*
Find the entity for gid, which must be of the given kind.
*/
{
  a_entity *result = entity_at(gid);

  if (result != nullptr && result->is_function != is_function) {
    internal("G-id " + std::to_string(gid) + " is not a " +
             (is_function ? "function" : "variable"));
    result = nullptr;
  }
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

  fn.setLinkageAttr(linkage_attr(kind));
  fn.setNoProto(t != nullptr && t->no_proto);
  if (e.flags & ECIL_DECL_FUNC_NORETURN)
    fn->setAttr(noreturn_name_, unit_attr_);
  else
    fn->removeAttr(noreturn_name_);
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

  /* Every address of a guarded function-local static says so. */
  if ((e.flags & ECIL_DECL_VAR_STATIC_LOCAL) &&
      (e.flags & ECIL_DECL_VAR_GUARDED))
    compat::mark_static_local(op);
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

/* DECL_FUNC / DECL_VAR / VAR_INIT / NAME_BIND / STRUCTOR. */

bool a_emitter::structor_record(const ecil::a_record &r)
/*
STRUCTOR: pair a complete-object variant (C1/D1) with its base-object variant
(C2/D2).  The body is streamed under the base variant; at finish the complete
variant becomes an alias of it, or is redirected to it (ECIL spec 11.4).
*/
{
  bool ok = true;
  uint64_t g_complete = 0;
  uint64_t g_base = 0;
  uint64_t flags = 0;
  a_entity *c;
  a_entity *b;

  uint_operand(r.ops[0], g_complete);
  uint_operand(r.ops[1], g_base);
  uint_operand(r.ops[2], flags);
  c = entity_for(g_complete, true);
  b = entity_for(g_base, true);
  if (c == nullptr || b == nullptr) {
    ok = false;
  } else if (c == b || c->in_structor || b->in_structor) {
    ok = internal("STRUCTOR out of step with the validator");
  } else {
    a_structor_pair pair = {c, b, (flags & ECIL_STRUCTOR_DTOR) != 0};
    c->in_structor = true;
    b->in_structor = true;
    structors_.push_back(pair);
  }
  return ok;
}  /* a_emitter::structor_record */

bool a_emitter::decl_function(const ecil::a_record &r)
/*
Create or update a function entity.  A re-issue with another non-nil name
renames the entity (spec 7); the new name is applied at finish.
*/
{
  bool ok = true;
  uint64_t gid = 0;
  uint64_t tgid = 0;
  uint64_t linkage = 0;
  uint64_t flags = 0;
  bool good = true;
  std::string_view name;
  a_type *t;
  ecil::a_gid_entry *entry;

  uint_operand(r.ops[0], gid);
  uint_operand(r.ops[1], tgid);
  uint_operand(r.ops[2], linkage);
  uint_operand(r.ops[3], flags);
  name = string_for(r.ops[4], good);
  t = type_at(tgid);
  entry = ids_.find_gid(gid);
  if (!good || t == nullptr || entry == nullptr ||
      entry->kind != ecil::gk_func) {
    ok = good && t != nullptr ? internal("DECL_FUNC out of step with the "
                                         "validator") : false;
  } else if (t->kind != a_type_kind::function_type) {
    ok = internal("DECL_FUNC type is not a function type");
  } else if (entry->payload == nullptr) {
    entity_store_.emplace_back();
    a_entity &e = entity_store_.back();
    cir::FuncOp fn;

    e.is_function = true;
    e.gid = gid;
    e.type = tgid;
    e.linkage = static_cast<unsigned>(linkage);
    if (linkage == elk_linkonce_odr)
      e.had_linkonce = true;
    e.flags = static_cast<uint32_t>(flags);
    if (std::string_view why = unsupported_reason(r); !why.empty())
      e.reason = why;
    e.named = r.ops[4].kind != ecil::an_operand_kind::nil;
    if (e.named)
      e.want = name;
    e.symbol = claim_symbol(e.named ? name
                                    : keep_string("__ecil." +
                                                  std::to_string(gid)), gid);
    fn = compat::create_function(builder_, module_, loc(), e.symbol, t->cir);
    e.op = fn.getOperation();
    apply_function_attributes(e, fn);
    entity_order_.push_back(&e);
    entry->payload = &e;
  } else {
    a_entity &e = *static_cast<a_entity *>(entry->payload);
    cir::FuncOp fn;
    if (!e.is_function) {
      ok = internal("DECL_FUNC re-issues a variable's id");
    } else if (e.type != tgid && e.has_body) {
      std::string was, now;
      llvm::raw_string_ostream ws(was), ns(now);
      ws << type_at(e.type)->cir;
      ns << t->cir;
      ok = format_error("the type of function '" + std::string(e.symbol) +
                        "' changed after its body was emitted (" + ws.str() +
                        " to " + ns.str() + ")");
    } else {
      fn = mlir::cast<cir::FuncOp>(e.op);
      e.linkage = static_cast<unsigned>(linkage);
      if (linkage == elk_linkonce_odr)
        e.had_linkonce = true;
      e.flags = static_cast<uint32_t>(flags);
      if (std::string_view why = unsupported_reason(r); !why.empty())
        e.reason = why;
      if (r.ops[4].kind != ecil::an_operand_kind::nil &&
          (!e.named || e.want != name)) {
        e.named = true;
        e.want = name;
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
  std::string_view name;
  a_type *t;
  bool is_definition;
  ecil::a_gid_entry *entry;

  uint_operand(r.ops[0], gid);
  uint_operand(r.ops[1], tgid);
  uint_operand(r.ops[2], linkage);
  uint_operand(r.ops[3], flags);
  uint_operand(r.ops[4], align);
  name = string_for(r.ops[5], good);
  t = type_at(tgid);
  is_definition = (flags & ECIL_DECL_VAR_DEFINITION) != 0;
  entry = ids_.find_gid(gid);
  if (!good || t == nullptr) {
    ok = false;
  } else if (entry == nullptr || entry->kind != ecil::gk_var) {
    ok = internal("DECL_VAR out of step with the validator");
  } else if (t->kind == a_type_kind::function_type ||
             t->kind == a_type_kind::void_type) {
    ok = internal("DECL_VAR type is not an object type");
  } else if (is_definition && !type_is_complete(t->cir)) {
    ok = internal("DECL_VAR definition of an incomplete record type");
  } else if (entry->payload == nullptr) {
    bool known = true;
    cir::GlobalLinkageKind kind = compat::linkage_kind(
                                      static_cast<unsigned>(linkage), known);
    entity_store_.emplace_back();
    a_entity &e = entity_store_.back();
    cir::GlobalOp g;
    bool is_string = (flags & ECIL_DECL_VAR_STRING_LITERAL) != 0;

    e.gid = gid;
    e.type = tgid;
    e.linkage = static_cast<unsigned>(linkage);
    if (linkage == elk_linkonce_odr)
      e.had_linkonce = true;
    e.flags = static_cast<uint32_t>(flags);
    if (std::string_view why = unsupported_reason(r); !why.empty())
      e.reason = why;
    e.align = static_cast<uint32_t>(align);
    e.named = r.ops[5].kind != ecil::an_operand_kind::nil;
    /* An unnamed string literal is named ".str" (made unique at finish). */
    if (e.named)
      e.want = name;
    else if (is_string)
      e.want = ".str";
    e.symbol = claim_symbol(e.want.empty()
                                ? keep_string("__ecil." + std::to_string(gid))
                                : e.want, gid);
    g = compat::create_global(builder_, module_, loc(), e.symbol, t->cir,
                              (flags & ECIL_DECL_VAR_CONST) != 0 || is_string,
                              kind);
    e.op = g.getOperation();
    if (is_definition) {
      g.setInitialValueAttr(zero_attribute(t->cir));
      compat::set_dso_local(g.getOperation(), true);
    }
    if (align != 0)
      g.setAlignmentAttr(alignment_attr(align));
    if (flags & ECIL_DECL_VAR_TLS)
      g.setTlsModel(cir::TLSModel::GeneralDynamic);
    mlir::SymbolTable::setSymbolVisibility(
        g, compat::symbol_visibility(kind, !is_definition));
    entity_order_.push_back(&e);
    entry->payload = &e;
  } else {
    a_entity &e = *static_cast<a_entity *>(entry->payload);
    cir::GlobalOp g;
    bool known = true;
    cir::GlobalLinkageKind kind = compat::linkage_kind(
                                      static_cast<unsigned>(linkage), known);

    if (e.is_function) {
      ok = internal("DECL_VAR re-issues a function's id");
    } else {
      g = mlir::cast<cir::GlobalOp>(e.op);
      e.linkage = static_cast<unsigned>(linkage);
      if (linkage == elk_linkonce_odr)
        e.had_linkonce = true;
      e.flags = static_cast<uint32_t>(flags);
      if (std::string_view why = unsupported_reason(r); !why.empty())
        e.reason = why;
      if (align != 0) {
        e.align = static_cast<uint32_t>(align);
        g.setAlignmentAttr(alignment_attr(align));
      }
      if (r.ops[5].kind != ecil::an_operand_kind::nil &&
          (!e.named || e.want != name)) {
        e.named = true;
        e.want = name;
      }
      if (e.type != tgid) {
        a_type *old_type = type_at(e.type);
        if (old_type == nullptr || old_type->cir != t->cir)
          retype_variable(e, t->cir);
        e.type = tgid;
      }
      g.setLinkageAttr(linkage_attr(kind));
      g.setConstant((flags & ECIL_DECL_VAR_CONST) != 0 ||
                    (flags & ECIL_DECL_VAR_STRING_LITERAL) != 0);
      if (is_definition && !g.getInitialValue().has_value()) {
        g.setInitialValueAttr(zero_attribute(t->cir));
        compat::set_dso_local(g.getOperation(), true);
      } else if (!is_definition && g.getInitialValue().has_value() &&
                 (flags & ECIL_DECL_VAR_UNSUPPORTED)) {
        compat::clear_initial_value(g);
      }
      mlir::SymbolTable::setSymbolVisibility(
          g, compat::symbol_visibility(kind, !is_definition));
    }
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
    ok = false;
  } else if (!type_is_complete(type_at(e->type)->cir)) {
    ok = internal("VAR_INIT of an incomplete record type");
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

bool a_emitter::retract_record(const ecil::a_record &r)
/*
RETRACT(g): the entity is not needed after all (an instance made only to
deduce a return type, one covered by extern template).  A defined internal or
linkonce_odr entity is discardable already: GlobalDCE drops it when nothing
uses it.  The mark clears the entity's unsupported obligation (spec 8.11), so
a dropped body does not become an error.  For a structor pair it names the
base variant and applies to both.
*/
{
  bool ok = true;
  uint64_t gid = 0;
  a_entity *e;

  uint_operand(r.ops[0], gid);
  e = entity_at(gid);
  if (e == nullptr) {
    ok = false;
  } else {
    e->retracted = true;
    for (const a_structor_pair &pair : structors_) {
      if (pair.base == e)
        pair.complete->retracted = true;
    }
  }
  return ok;
}  /* a_emitter::retract_record */

bool a_emitter::name_bind(const ecil::a_record &r)
/*
NAME_BIND: bind a late name or rename the entity (its effective name, spec
7).  The entity keeps its symbol until finish, which applies all names in one
pass.
*/
{
  bool ok = true;
  uint64_t gid = 0;
  uint64_t linkage = 0;
  bool good = true;
  std::string_view name;
  a_entity *e;

  uint_operand(r.ops[0], gid);
  name = string_for(r.ops[1], good);
  uint_operand(r.ops[2], linkage);
  e = good ? entity_at(gid) : nullptr;
  if (e == nullptr) {
    ok = false;
  } else {
    bool known = true;
    cir::GlobalLinkageKind kind = compat::linkage_kind(
                                      static_cast<unsigned>(linkage), known);

    e->linkage = static_cast<unsigned>(linkage);
    if (e->is_function)
      mlir::cast<cir::FuncOp>(e->op).setLinkageAttr(linkage_attr(kind));
    else
      mlir::cast<cir::GlobalOp>(e->op).setLinkageAttr(linkage_attr(kind));
    if (!e->named || e->want != name) {
      e->named = true;
      e->want = name;
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

mlir::Attribute a_emitter::constant_for_member(mlir::Attribute attr,
                                               mlir::Type member_type)
/*
The constant of a base or potentially-overlapping member: the value is the
complete record's constant, the member is its ".base" record, which has the
same members except for the tail padding.
*/
{
  mlir::Attribute result = attr;
  auto typed = mlir::dyn_cast<mlir::TypedAttr>(attr);
  auto record = mlir::dyn_cast<cir::RecordType>(member_type);

  if (typed && record && typed.getType() != member_type) {
    if (mlir::isa<cir::ZeroAttr>(attr)) {
      result = cir::ZeroAttr::get(member_type);
    } else if (auto agg = mlir::dyn_cast<cir::ConstRecordAttr>(attr)) {
      llvm::SmallVector<mlir::Attribute> kept;
      size_t keep = record.getMembers().size();

      for (mlir::Attribute element : agg.getMembers()) {
        if (kept.size() < keep)
          kept.push_back(element);
      }
      result = cir::ConstRecordAttr::get(record, builder_.getArrayAttr(kept));
    }
  }
  return result;
}  /* a_emitter::constant_for_member */

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
    ok = false;
  } else if (t->kind == a_type_kind::array_type) {
    uint64_t count = t->count;
    std::vector<mlir::Attribute> elements;
    uint64_t listed = 0;
    a_type *element = type_at(t->target);

    for (const auto &entry : agg.entries) {
      if (entry.index + entry.count > count && count != 0)
        ok = internal("C_SEL index past the end of the array");
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
        ok = internal("C_SEL index past the union's fields");
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
            ok = internal("C_SEL index past the record's fields");
            continue;
          }
          member = rec.member_index[index];
          if (member == no_member)
            continue;  /* An empty base or member: nothing to store. */
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
            elems[member] = constant_for_member(entry.value.attr,
                                                rec.members[member]);
            auto typed = mlir::dyn_cast<mlir::TypedAttr>(elems[member]);
            if (typed && typed.getType() != rec.members[member])
              ok = format_error("the constant of field " +
                                std::to_string(index) + " of record '" +
                                rec.name + "' does not have the type of the "
                                "field");
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
    ok = internal("C_AGG_BEGIN type is not a complete record or array");
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
    ok = internal("C_AGG_END without C_AGG_BEGIN");
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
      ok = internal("C_SEL outside C_AGG_BEGIN");
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
      ok = false;
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
    int64_t offset = 0;
    a_entity *e;
    mlir::StringAttr name;
    mlir::Attribute attr;

    uint_operand(r.ops[1], g);
    ecil::operand_i64(r.ops[2], offset);
    e = entity_at(g);
    if (e == nullptr) {
      ok = false;
      break;
    }
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

/*
Names.  During emission every entity has a unique MLIR symbol of its own
(a_entity::symbol: its name if it was free, else a placeholder) and all uses
refer to it.  The final names are chosen here, once, from the effective names
(spec 7): an external entity always keeps its name, an internal one gives way
(name.N) when an external or an earlier internal entity has the name.  The
symbols that differ from their final names are then substituted in one pass
over the module.  Everything derived from a final name (the library-generated
__const.<function>.<n> globals; C++ comdat and guard names later) is computed
here, after the final names, never at the point of creation.
*/

void a_emitter::assign_final_names()
/*
Choose the final name of every entity and generated symbol and queue the
changes (symbol to final name) for apply_substitution.
*/
{
  std::unordered_set<std::string_view> used;
  std::unordered_map<std::string_view, unsigned> counters;
  std::vector<std::string_view> finals(entity_order_.size());

  auto unique_name = [&](std::string_view base) {
    std::string_view result = base;
    if (!used.insert(result).second) {
      unsigned &k = counters[base];
      do {
        ++k;
        result = keep_string(std::string(base) + "." + std::to_string(k));
      } while (!used.insert(result).second);
    }
    return result;
  };
  auto is_local = [](const a_entity &e) {
    return e.linkage == elk_internal || e.linkage == elk_private;
  };

  /* External entities first: their names are never changed. */
  for (size_t i = 0; i < entity_order_.size(); ++i) {
    const a_entity &e = *entity_order_[i];
    if (!is_local(e)) {
      finals[i] = e.named ? e.want : e.symbol;
      if (!used.insert(finals[i]).second)
        internal("two external entities have the final name '" +
                 std::string(finals[i]) + "'");
    }
  }
  /* Then the internal ones, in creation order. */
  for (size_t i = 0; i < entity_order_.size(); ++i) {
    const a_entity &e = *entity_order_[i];
    if (is_local(e))
      finals[i] = unique_name(e.want.empty() ? e.symbol : e.want);
  }
  for (size_t i = 0; i < entity_order_.size(); ++i) {
    a_entity &e = *entity_order_[i];
    if (finals[i] != e.symbol) {
      symbol_changes_.push_back({e.op,
                                 mlir::StringAttr::get(ctx_, e.symbol),
                                 mlir::StringAttr::get(ctx_, finals[i])});
      e.symbol = finals[i];
    }
  }
  /* Library-generated names derive from the final names. */
  std::unordered_map<uint64_t, std::string_view> final_of;
  if (!generated_.empty()) {
    for (size_t i = 0; i < entity_order_.size(); ++i)
      final_of[entity_order_[i]->gid] = finals[i];
  }
  for (const a_generated_symbol &g : generated_) {
    std::string_view function = "fn";
    std::string_view name;
    auto found = final_of.find(g.function_gid);
    if (found != final_of.end() && g.function_gid != 0)
      function = found->second;
    name = unique_name(keep_string("__const." + std::string(function) + "." +
                                   std::to_string(g.index)));
    cir::GlobalOp op = g.op;
    symbol_changes_.push_back({op.getOperation(), op.getSymNameAttr(),
                               mlir::StringAttr::get(ctx_, name)});
  }
}  /* a_emitter::assign_final_names */

void a_emitter::apply_substitution()
/*
Apply the queued symbol changes: one walk over the module replaces every
symbol reference at once (simultaneously, so a name that is both a source and
a target is safe), then the symbol names are set.  The walk is skipped only
when there is nothing to substitute.
*/
{
  if (!symbol_changes_.empty()) {
    llvm::DenseMap<mlir::StringAttr, mlir::StringAttr> map;
    mlir::AttrTypeReplacer replacer;

    for (const a_symbol_change &change : symbol_changes_)
      map[change.from] = change.to;
    replacer.addReplacement(
        [&](mlir::SymbolRefAttr attr) -> std::pair<mlir::Attribute,
                                                    mlir::WalkResult> {
          if (attr.getNestedReferences().empty()) {
            auto found = map.find(attr.getRootReference());
            if (found != map.end())
              return {mlir::FlatSymbolRefAttr::get(found->second),
                      mlir::WalkResult::skip()};
          }
          return {attr, mlir::WalkResult::skip()};
        });
    replacer.recursivelyReplaceElementsIn(module_.getOperation(),
                                          /*replaceAttrs=*/true,
                                          /*replaceLocs=*/false,
                                          /*replaceTypes=*/false);
    ++walks_done_;
    for (const a_symbol_change &change : symbol_changes_) {
      if (change.op != nullptr)
        mlir::SymbolTable::setSymbolName(change.op, change.to);
    }
    renames_applied_ += symbol_changes_.size();
    symbol_changes_.clear();
  }
}  /* a_emitter::apply_substitution */


void a_emitter::apply_structors()
/*
The C1 strategy (ECIL spec 11.4), for the pairs whose base variant ended up
defined (not unsupported, not retracted, not merely declared); otherwise C1
stays an external declaration.  External linkage and weak_odr: C1 becomes an
alias of C2 (the comdat of the weak_odr case is set by apply_comdats).
Discardable linkage: every reference to C1 is redirected to C2 by the
finish-time substitution, and C1 goes (finish_structors).  It runs after the
final names are known, before the one substitution walk: an aliasee names
C2's current symbol, so the walk gives it C2's final name.
*/
{
  for (const a_structor_pair &pair : structors_) {
    a_entity &c = *pair.complete;
    a_entity &b = *pair.base;
    bool defined = b.has_body && !c.has_body &&
                   !(b.flags & ECIL_DECL_FUNC_INLINE_ONLY);

    if (!defined || c.op == nullptr || b.op == nullptr)
      continue;
    if (c.linkage == elk_external || c.linkage == elk_weak_odr) {
      compat::set_alias(mlir::cast<cir::FuncOp>(c.op),
                        mlir::cast<cir::FuncOp>(b.op).getSymName());
      c.is_alias = true;
    } else if (c.linkage == elk_linkonce_odr || c.linkage == elk_internal ||
               c.linkage == elk_private) {
      mlir::StringAttr from = mlir::cast<cir::FuncOp>(c.op).getSymNameAttr();
      symbol_changes_.push_back({nullptr, from,
                                 mlir::StringAttr::get(ctx_, b.symbol)});
      redirected_.push_back(&c);
    }
  }
}  /* a_emitter::apply_structors */

void a_emitter::finish_structors()
/*
Remove the complete variants whose references were redirected to the base
variant.
*/
{
  for (a_entity *c : redirected_) {
    c->op->erase();
    c->op = nullptr;
  }
  redirected_.clear();
}  /* a_emitter::finish_structors */

void a_emitter::apply_comdats()
/*
Put every defined entity with the COMDAT flag in a comdat group of its own
name; the base variant of a weak_odr constructor or destructor pair whose
complete variant is an alias goes in the comdat C5 (D5): its name with the
variant digit replaced.  Names are the final ones.
*/
{
  for (a_entity *entity : entity_order_) {
    a_entity &e = *entity;
    bool defined = e.is_function ? e.has_body
        : e.has_init || (e.flags & ECIL_DECL_VAR_DEFINITION) != 0;
    uint32_t comdat_flag = e.is_function ? ECIL_DECL_FUNC_COMDAT
                                         : ECIL_DECL_VAR_COMDAT;
    std::string name;

    if (e.op == nullptr || !defined || (e.flags & comdat_flag) == 0)
      continue;
    for (const a_structor_pair &pair : structors_) {
      if (pair.base == &e && pair.complete->is_alias &&
          pair.complete->linkage == elk_weak_odr) {
        std::string_view base_name = e.symbol;
        std::string_view complete_name = pair.complete->symbol;
        if (base_name.size() == complete_name.size()) {
          for (size_t k = 0; k < base_name.size(); ++k) {
            if (base_name[k] != complete_name[k]) {
              name = std::string(base_name);
              name[k] = '5';
              break;
            }
          }
        }
      }
    }
    compat::set_comdat(e.op, name);
  }
}  /* a_emitter::apply_comdats */

void a_emitter::apply_dynamic_init()
/*
Mark every guarded, dynamically initialized variable with its guard name and
dynamic_init_info (they must be present together).  The guard name derives
from the final name (ECIL spec 11.2): _ZGV plus the name without its _Z when
the name is mangled, else _ZGV<length><name>.
*/
{
  for (a_entity *entity : entity_order_) {
    a_entity &e = *entity;

    if (e.is_function || e.op == nullptr || !e.dyn_init ||
        !(e.flags & ECIL_DECL_VAR_GUARDED))
      continue;
    std::string name(e.symbol);
    std::string guard = name.compare(0, 2, "_Z") == 0
        ? "_ZGV" + name.substr(2)
        : "_ZGV" + std::to_string(name.size()) + name;
    int tls = !(e.flags & ECIL_DECL_VAR_TLS) ? 0
              : e.init_exit_only ? 1 : 2;

    compat::set_dynamic_init(mlir::cast<cir::GlobalOp>(e.op), guard,
                             (e.flags & ECIL_DECL_VAR_STATIC_LOCAL) != 0, tls,
                             e.init_inline, e.init_template);
  }
}  /* a_emitter::apply_dynamic_init */

bool a_emitter::is_discardable(const a_entity &e) const
/*
Whether every translation unit that needs the entity defines it itself:
internal linkage, or vague (linkonce_odr) linkage.  The writer re-issues the
declaration of a dropped inline function with external linkage, so for an
entity that carries UNSUPPORTED the linkage it once had counts too (one that
later got a supported definition is judged by its current linkage).
*/
{
  bool dropped = e.is_function ? (e.flags & ECIL_DECL_FUNC_UNSUPPORTED) != 0
                               : (e.flags & ECIL_DECL_VAR_UNSUPPORTED) != 0;

  return e.linkage == elk_internal || e.linkage == elk_private ||
         e.linkage == elk_linkonce_odr || (e.had_linkonce && dropped);
}  /* a_emitter::is_discardable */

bool a_emitter::is_dropped_definition(const a_entity &e) const
/*
An entity whose definition the writer dropped as unsupported (and that no
later definition replaced).
*/
{
  bool dropped = e.is_function ? (e.flags & ECIL_DECL_FUNC_UNSUPPORTED) != 0
                               : (e.flags & ECIL_DECL_VAR_UNSUPPORTED) != 0;

  return dropped && e.op != nullptr && !e.retracted &&
         !(e.is_function ? e.has_body : e.has_init);
}  /* a_emitter::is_dropped_definition */

void a_emitter::entity_liveness(
    std::unordered_set<const a_entity *> &live,
    std::unordered_map<const a_entity *, std::vector<a_entity *>> &uses)
/*
Which entities can be needed.  The roots are the definitions that cannot be
discarded (external, weak, ... linkage), definitions with dynamic
initialization, and uses from ops that belong to no entity; a use is any
symbol reference (a call, an address, a global_view in an initializer, a
register_exit_dtor, a delete function) or an alias.  A STRUCTOR pair lives or
dies together: a use of C1 is a use of C2.  Runs at finish, on the worker.
*/
{
  std::unordered_map<mlir::Operation *, a_entity *> by_op;
  std::unordered_map<std::string_view, a_entity *> by_name;
  std::vector<a_entity *> work;
  auto mark = [&](a_entity *e) {
    if (e != nullptr && live.insert(e).second)
      work.push_back(e);
  };

  for (a_entity *entity : entity_order_) {
    if (entity->op == nullptr)
      continue;
    by_op[entity->op] = entity;
    by_name[std::string_view(
        mlir::cast<mlir::SymbolOpInterface>(entity->op).getName())] =
        entity;
  }
  auto uses_range = mlir::SymbolTable::getSymbolUses(&module_.getBodyRegion());
  if (uses_range) {
    for (const mlir::SymbolTable::SymbolUse &use : *uses_range) {
      mlir::Operation *top = use.getUser();
      auto target = by_name.find(std::string_view(
          use.getSymbolRef().getRootReference().getValue()));

      while (top != nullptr && top->getParentOp() != module_.getOperation())
        top = top->getParentOp();
      if (target == by_name.end())
        continue;
      auto from = by_op.find(top);
      if (from == by_op.end())
        mark(target->second);
      else
        uses[from->second].push_back(target->second);
    }
  }
  for (const a_structor_pair &pair : structors_) {
    uses[pair.complete].push_back(pair.base);
    uses[pair.base].push_back(pair.complete);
  }
  for (a_entity *entity : entity_order_) {
    a_entity &e = *entity;
    bool discardable = is_discardable(e);
    bool defined = e.is_function
        ? e.has_body || e.is_alias
        : e.has_init || (e.flags & ECIL_DECL_VAR_DEFINITION) != 0;

    if (e.op != nullptr && defined && (!discardable || e.dyn_init))
      mark(&e);
  }
  while (!work.empty()) {
    a_entity *e = work.back();
    work.pop_back();
    for (a_entity *target : uses[e])
      mark(target);
  }
}  /* a_emitter::entity_liveness */

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

  /* Final names, and their one-pass substitution. */
  assign_final_names();
  apply_structors();
  apply_substitution();
  finish_structors();
  apply_dynamic_init();
  /* Definitions must be named. */
  for (a_entity *entity : entity_order_) {
    a_entity &e = *entity;
    bool defined = e.is_function ? e.has_body : (e.flags &
                                       ECIL_DECL_VAR_DEFINITION) != 0;
    if (defined && !e.named && !(e.flags & ECIL_DECL_VAR_STRING_LITERAL))
      errors.push_back("definition of entity " + std::to_string(e.gid) +
                       " has no name at the end of the stream");
  }
  /* A variable of a record type that never got its body (a class the writer
     does not describe, e.g. one with virtual bases) has no layout: it is
     dropped when nothing uses it, an error otherwise. */
  for (a_entity *entity : entity_order_) {
    a_entity &e = *entity;

    if (e.is_function || e.op == nullptr || e.retracted)
      continue;
    /* A dropped definition of such a variable is the unsupported policy's:
       an error unless it is discardable and unused (see below). */
    if (is_dropped_definition(e) && !keep_going)
      continue;
    cir::GlobalOp g = mlir::cast<cir::GlobalOp>(e.op);
    if (type_is_complete(g.getSymType()))
      continue;
    if (mlir::SymbolTable::symbolKnownUseEmpty(g.getSymNameAttr(), module_)) {
      e.op->erase();
      e.op = nullptr;
    } else {
      errors.push_back("the variable '" + std::string(e.symbol) +
                       "' has a record type without a body and is used");
    }
  }
  /* C99 inline-only functions keep no body: they are external declarations
     (their definition lives in another translation unit). */
  for (a_entity *entity : entity_order_) {
    a_entity &e = *entity;
    if (e.is_function && e.has_body &&
        (e.flags & ECIL_DECL_FUNC_INLINE_ONLY)) {
      cir::FuncOp fn = mlir::cast<cir::FuncOp>(e.op);
      /* Uses may outlive their definitions across blocks: drop them first. */
      fn.getBody().dropAllReferences();
      fn.getBody().getBlocks().clear();
      fn.setLinkageAttr(
          linkage_attr(cir::GlobalLinkageKind::ExternalLinkage));
      mlir::SymbolTable::setSymbolVisibility(
          fn, mlir::SymbolTable::Visibility::Private);
      e.has_body = false;
    }
  }
  /* Declarations without a definition: weak ones are extern_weak, every
     other kind of declaration is external (llvm.func and llvm.mlir.global
     accept nothing else). */
  for (a_entity *entity : entity_order_) {
    a_entity &e = *entity;
    bool declared_only = e.is_function ? !e.has_body : !e.has_init &&
        !(e.flags & ECIL_DECL_VAR_DEFINITION);
    if (declared_only && e.op != nullptr && !e.is_alias) {
      /* Only a C weak declaration is extern_weak.  A C++ vague-linkage
         entity whose definition is not in this module (dropped, or the
         complete variant of a structor pair that has no body) is an ordinary
         external declaration. */
      bool weak = e.linkage == elk_weak || e.linkage == elk_extern_weak;
      auto kind = weak ? cir::GlobalLinkageKind::ExternalWeakLinkage
                       : cir::GlobalLinkageKind::ExternalLinkage;
      auto attr = linkage_attr(kind);
      if (e.is_function)
        mlir::cast<cir::FuncOp>(e.op).setLinkageAttr(attr);
      else
        mlir::cast<cir::GlobalOp>(e.op).setLinkageAttr(attr);
    }
  }
  /* Unsupported entities (spec 8.11 F5). */
  {
    std::unordered_set<const a_entity *> live;
    std::unordered_map<const a_entity *, std::vector<a_entity *>> uses;
    std::unordered_set<a_entity *> erase_set;
    std::vector<a_entity *> dropped_list;
    bool any_dropped = false;

    for (a_entity *entity : entity_order_) {
      if (is_dropped_definition(*entity))
        any_dropped = true;
    }
    if (any_dropped)
      entity_liveness(live, uses);
    for (a_entity *entity : entity_order_) {
      a_entity &e = *entity;
      bool discardable = is_discardable(e);

      if (!is_dropped_definition(e))
        continue;
      if (discardable && live.count(&e) == 0 &&
          (e.is_function || keep_going)) {
        /* Nothing can use it: it is erased with a warning.  A variable is
           erased only under KEEP_GOING: in the default mode its dropped
           initializer may have had effects (a constructor) whether or not
           anything names it. */
        erase_set.insert(&e);
        dropped_list.push_back(&e);
      } else if (!keep_going) {
        errors.push_back("the definition of '" + std::string(e.symbol) +
                         "' was dropped: it uses a construct the CIR back "
                         "end cannot express" + reason_suffix(e) +
                         (live.count(&e) != 0 ? " (and it is referenced)"
                                              : ""));
      } else if (e.linkage != elk_internal && e.linkage != elk_private) {
        session_.report(EDG_CIR_SEVERITY_WARNING, edc_unsupported,
                        "'" + std::string(e.symbol) + "' is only declared: "
                        "its definition uses a construct the CIR back end "
                        "cannot express" + reason_suffix(e));
        if (!e.is_function) {
          compat::clear_initial_value(mlir::cast<cir::GlobalOp>(e.op));
          mlir::SymbolTable::setSymbolVisibility(
              e.op, mlir::SymbolTable::Visibility::Private);
        }
      } else {
        errors.push_back("the internal definition of '" +
                         std::string(e.symbol) + "' was dropped but is "
                         "still referenced" + reason_suffix(e));
      }
    }
    if (!erase_set.empty()) {
      /* A dead definition that uses an erased entity goes too (to a fixed
         point; each pass is O(entities x uses), the number of passes is the
         length of the longest chain of dead users, usually one or two): its own body would name a symbol that is gone.  A chain of
         dead discardable entities therefore vanishes together. */
      bool grew = true;
      while (grew) {
        grew = false;
        for (a_entity *entity : entity_order_) {
          a_entity &e = *entity;
          bool discardable = is_discardable(e);

          if (!discardable || e.op == nullptr || e.retracted ||
              erase_set.count(&e) != 0 || live.count(&e) != 0)
            continue;
          for (a_entity *target : uses[&e]) {
            if (erase_set.count(target) != 0) {
              erase_set.insert(&e);
              grew = true;
              break;
            }
          }
        }
      }
      for (a_entity *entity : dropped_list) {
        session_.report(EDG_CIR_SEVERITY_WARNING, edc_unsupported,
                        "the unused definition of '" +
                        std::string(entity->symbol) + "' was dropped and "
                        "erased: it uses a construct the CIR back end "
                        "cannot express" + reason_suffix(*entity));
      }
      for (a_entity *entity : erase_set) {
        entity->op->erase();
        entity->op = nullptr;
      }
    }
  }
  apply_comdats();
  /* Record ABI metadata, one entry per complete record as CIRGen does:
     CallConvLowering looks every by-value record up here, and the alignment
     is the front end's (the DataLayout cannot see aligned(N)). */
  for (const a_record_info &info : records_) {
    if (info.complete)
      layouts.push_back(mlir::NamedAttribute(
          mlir::StringAttr::get(ctx_, info.name),
          compat::record_layout(ctx_, info.align, info.nontrivial_abi,
                                !info.nontrivial_dtor)));
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
