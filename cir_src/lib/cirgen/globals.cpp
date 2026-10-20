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
#include <iterator>
#include <unordered_map>
#include <unordered_set>

#include "mlir/IR/AttrTypeSubElements.h"

#include "emitter.h"
#include "symbol_uses.h"

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

bool a_emitter::has_supp_flag(const ecil::a_record &r, uint64_t key) const
/*
Whether the record's supp blob carries a flag-like key (value true).  The
validator has checked the keys it knows.
*/
{
  bool result = false;

  if (r.supp != nullptr) {
    std::vector<std::pair<uint64_t, ecil::an_operand>> pairs;

    ecil::supp_pairs(r, pairs);
    for (const auto &pair : pairs) {
      if (pair.first == key && pair.second.u == 1)
        result = true;
    }
  }
  return result;
}  /* a_emitter::has_supp_flag */

uint64_t a_emitter::supp_uint(const ecil::a_record &r, uint64_t key) const
/*
The unsigned value of a supp key of the record, 0 when it is absent.  The
validator has checked the keys it knows.
*/
{
  uint64_t result = 0;

  if (r.supp != nullptr) {
    std::vector<std::pair<uint64_t, ecil::an_operand>> pairs;

    ecil::supp_pairs(r, pairs);
    for (const auto &pair : pairs) {
      if (pair.first == key && pair.second.kind == ecil::an_operand_kind::uint)
        result = pair.second.u;
    }
  }
  return result;
}  /* a_emitter::supp_uint */

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
The function or variable entity of a G-id (the global of a DECL_ABI_DATA too:
C_ADDR names one).  The validator checked that the G-id was declared, so a
miss is an internal error.
*/
{
  a_entity *result = nullptr;
  const ecil::a_gid_entry *entry = ids_.find_gid(gid);

  if (entry != nullptr && entry->payload != nullptr &&
      entry->kind == ecil::gk_abi_data)
    result = static_cast<a_abi_entity *>(entry->payload)->entity;
  else if (entry == nullptr || entry->payload == nullptr ||
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

bool a_emitter::inline_hint(const a_entity &e) const
/*
Whether clang gives the function `inlinehint`: with the feature bit
INLINE_HINTS the writer sends it exactly (the inline keyword, or a lambda's
call operator) as DECL_FUNC INLINE_HINT; without it (a writer before 1.3
final) INLINE, which also covers in-class definitions, constexpr and implicit
members, is the approximation.  Only when inlining is on (-O1 and up): at -O0
clang marks the functions noinline instead.
*/
{
  bool exact = (session_.reader->preamble().features &
                ECIL_FEATURE_INLINE_HINTS) != 0;

  return (e.flags & (exact ? ECIL_DECL_FUNC_INLINE_HINT
                           : ECIL_DECL_FUNC_INLINE)) != 0;
}  /* a_emitter::inline_hint */

unsigned a_emitter::supp_memory(const ecil::a_record &r) const
/*
The memory behavior the stream gives a function: 2 for supp const, 1 for supp
pure, else 0.
*/
{
  unsigned result = 0;

  if (has_supp_flag(r, esk_const))
    result = 2;
  else if (has_supp_flag(r, esk_pure))
    result = 1;
  return result;
}  /* a_emitter::supp_memory */

void a_emitter::add_memory_attributes(const a_entity &e,
                                      llvm::SmallVectorImpl<
                                          mlir::NamedAttribute> &attrs)
/*
The call-site attributes of a pure or const callee, as CIRGen's
ConstructAttributeList gives them from the GNU attributes: memory effects
(none for const, read-only for pure), nounwind and willreturn.  The
attribute is `nounwind` (the LLVM attribute), not `nothrow` (the call cannot
throw): a pure function called in a try body is still an invoke, as in
clangir, so a handler catches what it throws (torture/pr100382).
*/
{
  if (e.memory != 0) {
    cir::MemoryEffectsAttr effects = e.memory == 2
        ? cir::MemoryEffectsAttr::none(ctx_)
        : cir::MemoryEffectsAttr::readOnly(ctx_);

    attrs.push_back(mlir::NamedAttribute(memory_effects_name_, effects));
    attrs.push_back(mlir::NamedAttribute(nounwind_name_, unit_attr_));
    attrs.push_back(mlir::NamedAttribute(willreturn_name_, unit_attr_));
  }
}  /* a_emitter::add_memory_attributes */

void a_emitter::add_temperature_attributes(
    const a_entity &e, llvm::SmallVectorImpl<mlir::NamedAttribute> &attrs)
/*
The call-site `cold` and `hot` of a callee that has them (CIRGen's
ConstructAttributeList takes both from the callee's declaration).
*/
{
  if (e.cold && !e.hot)
    attrs.push_back(mlir::NamedAttribute(
        mlir::StringAttr::get(ctx_, "cold"), unit_attr_));
  if (e.hot && !e.cold)
    attrs.push_back(mlir::NamedAttribute(
        mlir::StringAttr::get(ctx_, "hot"), unit_attr_));
}  /* a_emitter::add_temperature_attributes */

void a_emitter::set_memory_attributes(const a_entity &e, mlir::Operation *op)
/*
The function-level form of add_memory_attributes.
*/
{
  llvm::SmallVector<mlir::NamedAttribute, 4> attrs;

  add_memory_attributes(e, attrs);
  for (mlir::NamedAttribute &attr : attrs)
    op->setAttr(attr.getName(), attr.getValue());
}  /* a_emitter::set_memory_attributes */

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
  /* The front end's non-throwing predicate (noexcept, throw(), implicit
     noexcept of destructors, nothrow library functions), on declarations and
     definitions alike, as CIRGen takes it from the function's prototype:
     LLVM nounwind.  (A definition without exceptions gets it in func_begin.)
   */
  if (e.flags & ECIL_DECL_FUNC_NOTHROW)
    fn->setAttr(nothrow_name_, unit_attr_);
  set_memory_attributes(e, fn);
  /* GNU always_inline and noinline, as CIRGen: neither overrides the other,
     and a function with both gets neither. */
  if (e.always_inline && !e.no_inline)
    fn.setInlineKind(cir::InlineKind::AlwaysInline);
  else if (e.no_inline && !e.always_inline)
    fn.setInlineKind(cir::InlineKind::NoInline);
  else if (!e.always_inline && session_.opt_level >= 1 && inline_hint(e))
    fn.setInlineKind(cir::InlineKind::InlineHint);
  /* (Both cold and hot: neither; LLVM's verifier rejects the pair.) */
  if (e.cold && !e.hot)
    fn->setAttr(mlir::StringAttr::get(ctx_, "cold"), unit_attr_);
  if (e.hot && !e.cold)
    fn->setAttr(mlir::StringAttr::get(ctx_, "hot"), unit_attr_);
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
  mlir::Value result = op;

  /* Every address of a guarded function-local static says so. */
  if ((e.flags & ECIL_DECL_VAR_STATIC_LOCAL) &&
      (e.flags & ECIL_DECL_VAR_GUARDED))
    compat::mark_static_local(op);
  e.addresses.push_back(op);
  e.referenced = true;
  /* A variable emitted in another form than its declared type (lowered
     method pointers) is cast back. */
  if (e.lowered_form) {
    a_type *declared = type_at(e.type);

    if (declared != nullptr &&
        op.getType() != compat::pointer_to(declared->cir))
      result = builder_.createBitcast(loc(), result,
                                      compat::pointer_to(declared->cir));
  }
  return result;
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
    bool has_vbases = (flags & ECIL_STRUCTOR_HAS_VBASES) != 0;
    a_structor_pair pair = {c, b, (flags & ECIL_STRUCTOR_DTOR) != 0,
                            has_vbases};
    c->in_structor = true;
    b->in_structor = true;
    structors_.push_back(pair);
    if (has_vbases) {
      /* The base variant takes the VTT after `this` (12.3). */
      if (b->body_arrived) {
        ok = internal("STRUCTOR has_vbases after the body of its base "
                      "variant");
      } else {
        b->has_vtt = true;
        retype_function(*b, with_vtt_parameter(mlir::cast<cir::FuncOp>(b->op)
                                                   .getFunctionType()));
      }
    }
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
    e.no_inline = has_supp_flag(r, esk_noinline);
    e.used = has_supp_flag(r, esk_used);
    e.always_inline = has_supp_flag(r, esk_always_inline);
    e.cold = has_supp_flag(r, esk_cold);
    e.hot = has_supp_flag(r, esk_hot);
    e.memory = std::max<unsigned>(e.memory, supp_memory(r));
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
    } else if (e.type != tgid && e.body_arrived) {
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
      if (has_supp_flag(r, esk_noinline))
        e.no_inline = true;     /* Sticky: GNU attributes accumulate. */
      if (has_supp_flag(r, esk_used))
        e.used = true;
      if (has_supp_flag(r, esk_always_inline))
        e.always_inline = true;
      if (has_supp_flag(r, esk_cold))
        e.cold = true;
      if (has_supp_flag(r, esk_hot))
        e.hot = true;
      e.memory = std::max<unsigned>(e.memory, supp_memory(r));
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
          retype_function(e, e.has_vtt
              ? with_vtt_parameter(mlir::cast<cir::FuncType>(t->cir))
              : t->cir);
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
    e.used = has_supp_flag(r, esk_used);
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
      if (has_supp_flag(r, esk_used))
        e.used = true;
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
    if (c.lowered_type) {
      /* Pointers to member functions with a this-adjustment: the variable
         is emitted in the lowered form (G2); its uses are cast back. */
      if (c.lowered_type != g.getSymType())
        retype_variable(*e, c.lowered_type);
      e->lowered_form = true;
    }
    if (mlir::cast<mlir::TypedAttr>(c.attr).getType() != g.getSymType()) {
      std::string have;
      std::string want;
      llvm::raw_string_ostream have_stream(have);
      llvm::raw_string_ostream want_stream(want);

      mlir::cast<mlir::TypedAttr>(c.attr).getType().print(have_stream);
      g.getSymType().print(want_stream);
      ok = format_error("VAR_INIT constant type differs from the variable "
                        "type (constant " + have_stream.str() + ", variable " +
                        want_stream.str() + ")");
    } else {
      resolve_vptr_markers(c, g.getSymType());
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
The constant of the value zero of any (scalar or aggregate) type: its null
constant when a pointer to a data member (null: -1) is inside (12.7).
*/
{
  return zero_initializable(type) ? builder_.getZeroInitAttr(type)
                                  : null_attribute(type);
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
Build the attribute of a finished C_AGG_BEGIN..C_AGG_END aggregate.  When a
member is a pointer to member function with a this-adjustment (in the lowered
{i64, i64} form, G2) the aggregate is built in the lowered form of its type:
the other members that hold pointers to member functions are lowered too, and
the record is built from a view of its type with the lowered member types.
*/
{
  bool ok = true;
  a_type *t = type_at(agg.type);
  a_type view;
  a_record_info view_record;

  out.attr = mlir::Attribute();
  out.lowered_type = mlir::Type();
  /* A type with a pointer to a data member is never all-zero bytes. */
  out.is_zero = t == nullptr || zero_initializable(t->cir);
  for (const auto &entry : agg.entries) {
    if (!entry.value.is_zero)
      out.is_zero = false;
  }
  bool any_lowered = false;

  for (const auto &entry : agg.entries) {
    if (entry.value.lowered_type)
      any_lowered = true;
  }
  if (t != nullptr && any_lowered &&
      t->kind == a_type_kind::record_type && t->record->complete) {
    /* The lowered view of the record: its CIR type and member types. */
    a_record_info &original = *t->record;

    view_record = original;
    view_record.members.clear();
    for (mlir::Type member : original.members)
      view_record.members.push_back(lowered_type_of(member));
    view = *t;
    view.cir = lowered_type_of(t->cir);
    view.record = &view_record;
    /* Every entry whose member changes is in the lowered form. */
    for (auto &entry : agg.entries) {
      for (uint64_t k = 0; k < entry.count; ++k) {
        size_t index = static_cast<size_t>(entry.index + k);
        uint32_t member = index < original.fields.size()
                              ? original.member_index[index] : no_member;

        if (member != no_member && !entry.value.lowered_type &&
            !original.fields[index].is_bitfield &&
            lowered_type_of(original.members[member]) !=
                original.members[member]) {
          entry.value.attr = lower_constant(entry.value.attr,
                                            original.members[member]);
          entry.value.lowered_type = view_record.members[member];
        }
        if (member != no_member && original.is_union)
          break;
      }
    }
    t = &view;
  }
  if (t == nullptr) {
    ok = false;
  } else if (any_lowered && t->kind == a_type_kind::array_type) {
    /* An array whose elements hold pointers to member functions, one with an
       adjustment: all elements go in the lowered form. */
    a_type *element = type_at(t->target);
    mlir::Type lowered_element = lowered_type_of(element->cir);
    mlir::Attribute null_element = lower_constant(
        zero_attribute(element->cir), element->cir);
    uint64_t listed = 0;
    std::vector<mlir::Attribute> elements;

    for (const auto &entry : agg.entries)
      listed = std::max(listed, entry.index + entry.count);
    elements.assign(listed, null_element);
    for (const auto &entry : agg.entries) {
      mlir::Attribute value = entry.value.lowered_type
          ? entry.value.attr : lower_constant(entry.value.attr, element->cir);

      for (uint64_t k = 0; k < entry.count; ++k) {
        if (entry.index + k < listed)
          elements[entry.index + k] = value;
      }
    }
    out.lowered_type = compat::array_of(lowered_element, t->count);
    out.is_zero = false;
    out.attr = cir::ConstArrayAttr::get(
        mlir::cast<cir::ArrayType>(out.lowered_type),
        builder_.getArrayAttr(elements));
  } else if (t->kind == a_type_kind::array_type) {
    uint64_t count = t->count;
    std::vector<mlir::Attribute> elements;
    uint64_t listed = 0;
    a_type *element = type_at(t->target);

    a_type *leaf = element;

    while (leaf != nullptr && leaf->kind == a_type_kind::array_type)
      leaf = type_at(leaf->target);
    for (const auto &entry : agg.entries) {
      if (entry.index + entry.count > count && count != 0)
        ok = internal("C_SEL index past the end of the array");
      if (!entry.value.is_zero)
        listed = std::max(listed, entry.index + entry.count);
      /* An element is a complete object: the vptrs inside it use its
         class's vtable group. */
      if (leaf != nullptr && leaf->kind == a_type_kind::record_type) {
        for (uint32_t mk = entry.value.mark_begin; mk < entry.value.mark_end;
             ++mk) {
          if (vptr_markers_[mk].vtable_class == 0)
            vptr_markers_[mk].vtable_class = leaf->record->gid;
        }
      }
    }
    if (!ok) {
      /* The failure is already recorded. */
    } else if (out.is_zero) {
      out.attr = cir::ZeroAttr::get(t->cir);
    } else {
      mlir::Attribute zero = zero_attribute(element->cir);

      /* The unlisted elements of a type with pointers to data members hold
         the null constant, not zero bytes: they are all listed. */
      if (!zero_initializable(element->cir))
        listed = count;
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
        a_type *member_type = type_at(rec.fields[index].type);

        /* The member is a complete object of its own class. */
        if (member_type != nullptr &&
            member_type->kind == a_type_kind::record_type) {
          for (uint32_t mk = entry.value.mark_begin;
               mk < entry.value.mark_end; ++mk) {
            if (vptr_markers_[mk].vtable_class == 0)
              vptr_markers_[mk].vtable_class = member_type->record->gid;
          }
        }
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
          if (k == 0) {
            const a_field &mf = rec.fields[index];
            a_type *mt = type_at(mf.type);

            /* The vptrs inside the member: a base subobject moves them by
               its offset (the complete object is not known yet), a member is
               a complete object of its class. */
            for (uint32_t mk = entry.value.mark_begin;
                 mk < entry.value.mark_end; ++mk) {
              a_vptr_marker &marker = vptr_markers_[mk];

              if (marker.vtable_class != 0)
                continue;
              if (mf.is_base)
                marker.offset += static_cast<int64_t>(mf.byte_offset);
              else if (mt != nullptr && mt->kind == a_type_kind::record_type)
                marker.vtable_class = mt->record->gid;
            }
          }
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
      /* The class's own vptr: a placeholder until the complete object is
         known (a #cir.global_view of the vtable, address_point). */
      for (size_t i = 0; ok && i < rec.fields.size(); ++i) {
        a_type *ft = type_at(rec.fields[i].type);

        if (ft != nullptr && ft->kind == a_type_kind::vptr_type &&
            rec.member_index[i] != no_member) {
          a_vptr_marker marker = {agg.type, 0, 0};
          mlir::MLIRContext *mctx = ctx_;
          std::string name = "__ecil.vptr." +
                             std::to_string(vptr_markers_.size());

          elems[rec.member_index[i]] = compat::global_view(
              cir::VPtrType::get(mctx), mlir::StringAttr::get(mctx, name),
              builder_.getArrayAttr({builder_.getI32IntegerAttr(0),
                                     builder_.getI32IntegerAttr(0)}),
              true);
          vptr_markers_.push_back(marker);
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
      if (vptr_markers_.size() > agg.marker_base)
        out.is_zero = false;
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
  if (ok && any_lowered && t == &view) {
    out.lowered_type = t->cir;
    out.is_zero = false;
  }
  out.mark_begin = agg.marker_base;
  out.mark_end = static_cast<uint32_t>(vptr_markers_.size());
  return ok;
}  /* a_emitter::build_constant_aggregate */

void a_emitter::resolve_vptr_markers(const a_const &c, mlir::Type object_type)
/*
A constant is used to initialize an object of the given type (a variable, an
INIT_CONST target): the vptrs inside it that no member boundary claimed belong
to that class's vtable group.
*/
{
  while (auto array = mlir::dyn_cast<cir::ArrayType>(object_type))
    object_type = array.getElementType();
  const a_record_info *rec = record_info_of(object_type);

  if (rec != nullptr) {
    for (uint32_t mk = c.mark_begin; mk < c.mark_end; ++mk) {
      if (vptr_markers_[mk].vtable_class == 0)
        vptr_markers_[mk].vtable_class = rec->gid;
    }
  }
}  /* a_emitter::resolve_vptr_markers */

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
    push_const(zero_attribute(t->cir), zero_initializable(t->cir));
    break;
  case eop_c_data_member: {
    mlir::Attribute attr = data_member_constant(*t, r.ops[1], good);

    if (!good)
      ok = false;
    else
      push_const(attr, false);
    break;
  }
  case eop_c_method: {
    bool lowered = false;
    mlir::Attribute attr = method_constant(*t, r, false, good, lowered);

    if (!good) {
      ok = false;
    } else {
      a_const c = {attr, r.ops[1].kind == ecil::an_operand_kind::nil};
      if (lowered)
        c.lowered_type = lowered_method_type();
      consts_.push_back(c);
    }
    break;
  }
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
    agg.marker_base = static_cast<uint32_t>(vptr_markers_.size());
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

bool a_emitter::calls_own_symbol(cir::FuncOp fn) const
/*
Whether the body of the function names the function's own symbol (final
names are in place, so an asm label that names it counts): a direct call, and
also every other use, such as the address and indirect call that a call whose
arguments do not match the function's type becomes (clang's
getReferencedDeclOfCallee sees through those, and through `(*f)(x)`).
*/
{
  bool found = false;
  mlir::StringAttr name = fn.getSymNameAttr();

  for_each_symbol_use(fn.getOperation(), [&](mlir::Operation *,
                                             mlir::StringAttr symbol) {
    if (symbol == name)
      found = true;
  });
  return found;
}  /* a_emitter::calls_own_symbol */

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
    /* An available_externally definition belongs to no group (LLVM: a
       declaration may not be in a comdat). */
    if (e.is_function &&
        mlir::cast<cir::FuncOp>(e.op).getLinkage() ==
            cir::GlobalLinkageKind::AvailableExternallyLinkage)
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

bool is_replaceable_operator_name(std::string_view name, bool *is_new)
/*
Whether the (final) symbol is one of the replaceable global allocation and
deallocation functions that LLVM's TargetLibraryInfo knows by name (the 32-bit
`j` spellings, the `__hot_cold_t` ones and the __size_returning_new family
included).  *is_new tells an allocation function (`new`, not
`__size_returning_new`) from a deallocation function.
*/
{
  static const char *const names[] = {
    "_ZdaPv", "_ZdaPvRKSt9nothrow_t", "_ZdaPvSt11align_val_t",
    "_ZdaPvSt11align_val_tRKSt9nothrow_t", "_ZdaPvj",
    "_ZdaPvjSt11align_val_t", "_ZdaPvm", "_ZdaPvmSt11align_val_t",
    "_ZdlPv", "_ZdlPvRKSt9nothrow_t", "_ZdlPvSt11align_val_t",
    "_ZdlPvSt11align_val_tRKSt9nothrow_t", "_ZdlPvj",
    "_ZdlPvjSt11align_val_t", "_ZdlPvm", "_ZdlPvmSt11align_val_t",
    "_Znaj", "_ZnajRKSt9nothrow_t", "_ZnajSt11align_val_t",
    "_ZnajSt11align_val_tRKSt9nothrow_t", "_Znam", "_Znam12__hot_cold_t",
    "_ZnamRKSt9nothrow_t", "_ZnamRKSt9nothrow_t12__hot_cold_t",
    "_ZnamSt11align_val_t", "_ZnamSt11align_val_t12__hot_cold_t",
    "_ZnamSt11align_val_tRKSt9nothrow_t",
    "_ZnamSt11align_val_tRKSt9nothrow_t12__hot_cold_t",
    "_Znwj", "_ZnwjRKSt9nothrow_t", "_ZnwjSt11align_val_t",
    "_ZnwjSt11align_val_tRKSt9nothrow_t", "_Znwm", "_Znwm12__hot_cold_t",
    "_ZnwmRKSt9nothrow_t", "_ZnwmRKSt9nothrow_t12__hot_cold_t",
    "_ZnwmSt11align_val_t", "_ZnwmSt11align_val_t12__hot_cold_t",
    "_ZnwmSt11align_val_tRKSt9nothrow_t",
    "_ZnwmSt11align_val_tRKSt9nothrow_t12__hot_cold_t",
    "__size_returning_new", "__size_returning_new_hot_cold",
    "__size_returning_new_aligned", "__size_returning_new_aligned_hot_cold"
  };
  static const std::unordered_set<std::string_view> set(std::begin(names),
                                                        std::end(names));
  bool found = set.count(name) != 0;

  if (is_new != nullptr)
    *is_new = found && name.substr(0, 3) == "_Zn";
  return found;
}  /* is_replaceable_operator_name */

void a_emitter::apply_nobuiltin()
/*
The replaceable global allocation and deallocation functions are `nobuiltin`
(declarations and definitions alike, by final symbol).  LLVM recognizes them
by name (TargetLibraryInfo.td): without the attribute a call to the
translation unit's own operator new is a removable allocation that the
optimizer folds, pairs with a delete, or sizes by a cookie of its own, and
a program that replaces the functions miscompiles at -O1 and above.  This is
the half of clang's rule that holds for every call; the other half is the
`builtin` call-site attribute of the calls of a new- or delete-expression
(call_record, delete_record), which allows allocation elision.  A direct call
takes the attribute of its callee in LLVM (CallBase::isNoBuiltin looks at the
called function), so the declaration is enough.  As CIRGen (and clang) the
allocation functions also carry allocsize(0) and their results noalias (and
nonnull when they cannot return null), see apply_signature_attributes.
*/
{
  mlir::StringAttr name = mlir::StringAttr::get(
      ctx_, cir::CIRDialect::getNoBuiltinAttrName());

  for (a_entity *entity : entity_order_) {
    bool is_new = false;

    if (entity->is_function && entity->op != nullptr &&
        is_replaceable_operator_name(
            std::string_view(
                mlir::cast<mlir::SymbolOpInterface>(entity->op).getName()),
            &is_new)) {
      entity->op->setAttr(name, unit_attr_);
      if (is_new)
        entity->op->setAttr(allocsize_name_,
                            builder_.getDenseI32ArrayAttr({0}));
    }
  }
}  /* a_emitter::apply_nobuiltin */

bool a_emitter::is_discardable(const a_entity &e) const
/*
Whether every translation unit that needs the entity defines it itself:
internal linkage, vague (linkonce_odr) linkage, or a C99 inline-only
function.  The writer re-issues the declaration of a dropped inline function
with external linkage, so for an entity that carries UNSUPPORTED the linkage
it once had counts too (one that later got a supported definition is judged
by its current linkage).
*/
{
  bool dropped = e.is_function ? (e.flags & ECIL_DECL_FUNC_UNSUPPORTED) != 0
                               : (e.flags & ECIL_DECL_VAR_UNSUPPORTED) != 0;

  return e.linkage == elk_internal || e.linkage == elk_private ||
         e.linkage == elk_linkonce_odr || (e.had_linkonce && dropped) ||
         (e.is_function && (e.flags & ECIL_DECL_FUNC_INLINE_ONLY) != 0);
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
    std::unordered_map<const a_entity *, std::vector<a_entity *>> &uses,
    const std::unordered_map<const a_entity *, std::vector<a_entity *>>
        *extra,
    const std::vector<a_entity *> *extra_roots)
/*
Which entities can be needed.  The roots are the definitions that cannot be
discarded (external, weak, ... linkage), definitions with dynamic
initialization, `used` definitions, and uses from ops that belong to no
entity; a use is any symbol reference (a call, an address, a global_view in an
initializer, a register_exit_dtor, a delete function) or an alias.  A
STRUCTOR pair lives or dies together: a use of C1 is a use of C2.  extra
and extra_roots add the edges and roots that the module does not show (the
bodies lazy emission has not emitted yet).  Runs at finish, on the worker.
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
  for (mlir::Operation &top : *module_.getBody()) {
    auto from = by_op.find(&top);

    for_each_symbol_use(&top, [&](mlir::Operation *,
                                  mlir::StringAttr symbol) {
      auto target = by_name.find(std::string_view(symbol.getValue()));

      if (target == by_name.end())
        return;
      if (from == by_op.end())
        mark(target->second);
      else
        uses[from->second].push_back(target->second);
    });
  }
  for (const a_structor_pair &pair : structors_) {
    uses[pair.complete].push_back(pair.base);
    uses[pair.base].push_back(pair.complete);
  }
  if (extra != nullptr) {
    for (const auto &item : *extra) {
      for (a_entity *target : item.second)
        uses[item.first].push_back(target);
    }
  }
  if (extra_roots != nullptr) {
    for (a_entity *entity : *extra_roots)
      mark(entity);
  }
  for (a_entity *entity : entity_order_) {
    a_entity &e = *entity;
    bool discardable = is_discardable(e);
    bool defined = e.is_function
        ? e.has_body || e.is_alias
        : e.has_init || (e.flags & ECIL_DECL_VAR_DEFINITION) != 0;

    if (e.op != nullptr && defined && (!discardable || e.dyn_init || e.used))
      mark(&e);
  }
  while (!work.empty()) {
    a_entity *e = work.back();
    work.pop_back();
    for (a_entity *target : uses[e])
      mark(target);
  }
}  /* a_emitter::entity_liveness */

void a_emitter::emit_llvm_used()
/*
The definitions marked `used` (GNU used, supp key 11) go into
llvm.compiler.used (llvm.used where the object format is not ELF), as CIRGen
does: an appending array of their addresses in the section llvm.metadata,
which keeps them against GlobalDCE and the linker's garbage collection.
Declarations are not listed.  Run at the end of finish_entities.
*/
{
  llvm::SmallVector<mlir::Attribute> elements;
  mlir::Type void_pointer = compat::pointer_to(compat::void_type(ctx_));

  for (a_entity *entity : entity_order_) {
    a_entity &e = *entity;
    bool defined = e.is_function
        ? e.has_body && !e.is_alias
        : e.has_init || (e.flags & ECIL_DECL_VAR_DEFINITION) != 0;

    if (e.used && e.op != nullptr && defined)
      elements.push_back(compat::global_view(
          void_pointer,
          mlir::cast<mlir::SymbolOpInterface>(e.op).getNameAttr(),
          mlir::ArrayAttr(), false));
  }
  if (!elements.empty()) {
    mlir::Type array_type = compat::array_of(void_pointer, elements.size());
    cir::GlobalOp list = compat::create_global(
        builder_, module_, loc(),
        session_.module->triple.isOSBinFormatELF() ? "llvm.compiler.used"
                                                   : "llvm.used",
        array_type, false, cir::GlobalLinkageKind::AppendingLinkage);

    list.setInitialValueAttr(cir::ConstArrayAttr::get(
        mlir::cast<cir::ArrayType>(array_type),
        builder_.getArrayAttr(elements)));
    list.setSectionAttr(builder_.getStringAttr("llvm.metadata"));
  }
}  /* a_emitter::emit_llvm_used */

bool a_emitter::finish()
/*
Runs on the worker thread: the class facts' products (vtables, typeinfo,
thunks, D0), then the policy of the entities.
*/
{
  /* The deferred bodies the program reaches are replayed first (lazy
     emission): the class facts below read what is defined. */
  bool ok = deferred_.empty() || lazy_finish();

  /* The class facts: the vtables, typeinfo, thunks and D0 they produce are
     entities like any other and take part in the naming that follows. */
  if (ok)
    ok = finish_classes();

  if (ok)
    ok = finish_entities();
  return ok;
}  /* a_emitter::finish */

bool a_emitter::finish_entities()
/*
Late names, record layout metadata, inline-only bodies and the unsupported
entity policy (spec 8.11).
*/
{
  bool ok = true;
  bool keep_going = (session_.flags & EDG_CIR_FLAG_KEEP_GOING) != 0;
  std::vector<std::string> errors;
  std::vector<mlir::NamedAttribute> layouts;
  bool lazy_failed = false;

  /* Final names, and their one-pass substitution. */
  assign_final_names();
  apply_structors();
  apply_substitution();
  finish_structors();
  apply_dynamic_init();
  apply_nobuiltin();
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
    } else if (!(e.flags & ECIL_DECL_VAR_DEFINITION)) {
      /* An external declaration of an incomplete type whose address is used
         (conv20: `extern B<int> b; f(b);` with B<int> never instantiated): as
         clang, a declaration of the incomplete record with alignment 1 (it
         lowers to an empty struct). */
      g.setAlignmentAttr(builder_.getI64IntegerAttr(1));
    } else {
      errors.push_back("the variable '" + std::string(e.symbol) +
                       "' has a record type without a body and is used");
    }
  }
  /* C99 inline-only functions (their definition lives in another
     translation unit): with inlining on (-O1 and up) the body stays as an
     available_externally definition, as clang makes it, so that the
     inliner can use it; at -O0 they keep no body and are external
     declarations.  So do a `noinline` function and a trivially recursive
     one at any level (clang's isTriviallyRecursive, PR9614): glibc's
     inline-only memchr, strchr, ... call __builtin_memchr, which is their
     own symbol, and an available_externally body that calls itself becomes
     a loop (TailCallElim) that is undefined behavior once the function is
     pure and willreturn. */
  for (a_entity *entity : entity_order_) {
    a_entity &e = *entity;
    bool keep_body = e.is_function && e.has_body && e.op != nullptr &&
        (e.flags & ECIL_DECL_FUNC_INLINE_ONLY) && session_.opt_level >= 1 &&
        !e.no_inline && !calls_own_symbol(mlir::cast<cir::FuncOp>(e.op));

    if (keep_body) {
      cir::FuncOp fn = mlir::cast<cir::FuncOp>(e.op);

      fn.setLinkageAttr(
          linkage_attr(cir::GlobalLinkageKind::AvailableExternallyLinkage));
      mlir::SymbolTable::setSymbolVisibility(
          fn, mlir::SymbolTable::Visibility::Public);
      /* The definition is in another translation unit (a shared object
         under PIC): not local. */
      compat::set_dso_local(fn.getOperation(), false);
    } else if (e.is_function && e.has_body && e.op != nullptr &&
        (e.flags & ECIL_DECL_FUNC_INLINE_ONLY)) {
      cir::FuncOp fn = mlir::cast<cir::FuncOp>(e.op);
      /* Uses may outlive their definitions across blocks: drop them first. */
      fn.getBody().dropAllReferences();
      fn.getBody().getBlocks().clear();
      fn.setLinkageAttr(
          linkage_attr(cir::GlobalLinkageKind::ExternalLinkage));
      mlir::SymbolTable::setSymbolVisibility(
          fn, mlir::SymbolTable::Visibility::Private);
      /* A declaration of another translation unit's function: not local. */
      compat::set_dso_local(fn.getOperation(), false);
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
        /* An inline-only function loses nothing (its definition lives in
           another translation unit), so no warning: glibc's fortify headers
           would warn at every <stdio.h>. */
        if (!e.is_function || (e.flags & ECIL_DECL_FUNC_INLINE_ONLY) == 0)
          dropped_list.push_back(&e);
      } else if (e.is_function &&
                 (e.flags & ECIL_DECL_FUNC_INLINE_ONLY) != 0) {
        /* A C99 or gnu_inline inline-only function whose body was dropped:
           its definition lives in another translation unit, so a reference
           is to a plain declaration (no error, no warning). */
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
  if (!lazy_erase_dropped())
    lazy_failed = true;
  apply_signature_attributes();
  apply_comdats();
  emit_llvm_used();
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
  ok = errors.empty() && !lazy_failed;
  if (!errors.empty()) {
    /* fail() reports the first message and sets the status. */
    session_.fail(EDG_CIR_ERROR_UNSUPPORTED, edc_unsupported,
                  errors.front());
    for (size_t k = 1; k < errors.size(); ++k)
      session_.report(EDG_CIR_SEVERITY_ERROR, edc_unsupported, errors[k]);
  }
  return ok;
}  /* a_emitter::finish_entities */

}  /* namespace edg_cir */
