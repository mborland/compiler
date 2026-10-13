/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
classes.cpp -- the C++ class machinery of ECIL 1.2 (M4): the vtable pointer
of an object, virtual calls and virtual delete, and, at finish, everything the
class facts produce: the types and initializers of the vtables and class
typeinfo objects (DECL_ABI_DATA), the _ZTS names, the thunks, the deleting
destructors, __cxa_pure_virtual and __cxa_deleted_virtual.  The layouts come
from lib/abi (a port of clang's Itanium vtable builder), fed by the decoder of
lib/format.

Derived in part from llvm-project clang/lib/CIR/CodeGen (CIRGenVTables.cpp,
CIRGenItaniumCXXABI.cpp, CIRGenClass.cpp) and clang/lib/CodeGen
(ItaniumCXXABI.cpp), Apache-2.0 WITH LLVM-exception; see CIRCompat.h for the
upstream API surface used.
*/

#include <algorithm>
#include <map>
#include <set>

#include "abi_names.h"
#include "abi_rtti.h"
#include "ecil_abi_decode.h"
#include "emitter.h"

#include "mlir/IR/AttrTypeSubElements.h"

namespace edg_cir {

/* Records. */

bool a_emitter::structor_deleting_record(const ecil::a_record &r)
/*
STRUCTOR_DELETING(g_d0, g_d1, g_delete, flags): the deleting destructor D0 of
a virtual destructor.  It is synthesized at finish (build_deleting_dtor): the
complete destructor under a cleanup that calls operator delete.
*/
{
  bool ok = true;
  uint64_t g_d0 = 0;
  uint64_t g_d1 = 0;
  uint64_t g_delete = 0;
  uint64_t flags = 0;
  a_entity *d0;
  a_entity *d1;
  a_entity *del;

  uint_operand(r.ops[0], g_d0);
  uint_operand(r.ops[1], g_d1);
  uint_operand(r.ops[2], g_delete);
  uint_operand(r.ops[3], flags);
  d0 = entity_for(g_d0, true);
  d1 = entity_for(g_d1, true);
  del = entity_for(g_delete, true);
  if (d0 == nullptr || d1 == nullptr || del == nullptr) {
    ok = false;
  } else {
    a_type *ft = type_at(d1->type);
    a_type *pointer = ft != nullptr && !ft->params.empty()
                          ? type_at(ft->params[0]) : nullptr;
    a_deleting_dtor entry = {d0, d1, del, static_cast<uint32_t>(flags),
                             pointer != nullptr ? pointer->target : 0};

    if (entry.class_gid == 0)
      ok = internal("STRUCTOR_DELETING of a destructor without this");
    else
      deleting_dtors_.push_back(entry);
  }
  return ok;
}  /* a_emitter::structor_deleting_record */

bool a_emitter::abi_data_record(const ecil::a_record &r)
/*
DECL_ABI_DATA(g, kind, t, linkage, flags, sid|nil): a vtable, a VTT or the
class typeinfo.  A global is declared (the symbol is chosen at finish from the
class's mangling); its type and initializer are built at finish.  A re-issue
updates the linkage and flags, the latest wins.  The typeinfo of a type that
is not a class is named by its sid and described by a TYPEINFO_DEF.
*/
{
  bool ok = true;
  uint64_t g = 0;
  uint64_t kind = 0;
  uint64_t t = 0;
  uint64_t linkage = 0;
  uint64_t flags = 0;
  ecil::a_gid_entry *entry;
  const ecil::a_gid_entry *te;

  uint_operand(r.ops[0], g);
  uint_operand(r.ops[1], kind);
  uint_operand(r.ops[2], t);
  uint_operand(r.ops[3], linkage);
  uint_operand(r.ops[4], flags);
  entry = ids_.find_gid(g);
  te = ids_.find_gid(t);
  if (entry == nullptr || te == nullptr) {
    ok = internal("DECL_ABI_DATA out of step with the validator");
  } else if (entry->payload == nullptr) {
    bool known = true;
    cir::GlobalLinkageKind lk = compat::linkage_kind(
                                    static_cast<unsigned>(linkage), known);
    bool defined = (flags & ECIL_ABI_DATA_DEFINED_HERE) != 0;
    cir::GlobalOp global;

    abi_store_.emplace_back();
    a_abi_entity &abi = abi_store_.back();
    entity_store_.emplace_back();
    a_entity &e = entity_store_.back();
    abi.gid = g;
    abi.kind = static_cast<unsigned>(kind);
    abi.type = t;
    abi.flags = static_cast<uint32_t>(flags);
    abi.entity = &e;
    if (te->kind != ecil::gk_record) {
      /* The typeinfo of a type that is not a class is named by its sid. */
      bool good = true;

      abi.symbol = std::string(string_for(r.ops[5], good));
      e.want = keep_string(abi.symbol);
      e.named = true;
    }
    e.gid = g;
    e.linkage = static_cast<unsigned>(linkage);
    if (linkage == elk_linkonce_odr)
      e.had_linkonce = true;
    e.flags = ECIL_DECL_VAR_CONST |
              (defined ? ECIL_DECL_VAR_DEFINITION : 0u) |
              ((flags & ECIL_ABI_DATA_COMDAT) ? ECIL_DECL_VAR_COMDAT : 0u);
    e.align = 8;
    e.symbol = claim_symbol(keep_string("__ecil.abi." + std::to_string(g)), g);
    /* The type is a placeholder: finish gives the vtable or typeinfo its
       record type. */
    global = compat::create_global(builder_, module_, loc(), e.symbol,
                                   compat::array_of(
                                       compat::int_type(ctx_, 8, false), 0),
                                   true, lk);
    e.op = global.getOperation();
    global.setAlignmentAttr(alignment_attr(8));
    if (defined) {
      global.setInitialValueAttr(zero_attribute(global.getSymType()));
      compat::set_dso_local(global.getOperation(), true);
    }
    mlir::SymbolTable::setSymbolVisibility(
        global, compat::symbol_visibility(lk, !defined));
    entity_order_.push_back(&e);
    abi_order_.push_back(&abi);
    entry->payload = &abi;
  } else {
    a_abi_entity &abi = *static_cast<a_abi_entity *>(entry->payload);
    a_entity &e = *abi.entity;
    cir::GlobalOp global = mlir::cast<cir::GlobalOp>(e.op);
    bool known = true;
    cir::GlobalLinkageKind lk = compat::linkage_kind(
                                    static_cast<unsigned>(linkage), known);
    bool defined = (flags & ECIL_ABI_DATA_DEFINED_HERE) != 0;

    abi.flags = static_cast<uint32_t>(flags);
    e.linkage = static_cast<unsigned>(linkage);
    if (linkage == elk_linkonce_odr)
      e.had_linkonce = true;
    e.flags = ECIL_DECL_VAR_CONST |
              (defined ? ECIL_DECL_VAR_DEFINITION : 0u) |
              ((flags & ECIL_ABI_DATA_COMDAT) ? ECIL_DECL_VAR_COMDAT : 0u);
    global.setLinkageAttr(linkage_attr(lk));
    if (defined && !global.getInitialValue().has_value()) {
      global.setInitialValueAttr(zero_attribute(global.getSymType()));
      compat::set_dso_local(global.getOperation(), true);
    } else if (!defined && global.getInitialValue().has_value()) {
      compat::clear_initial_value(global);
    }
    mlir::SymbolTable::setSymbolVisibility(
        global, compat::symbol_visibility(lk, !defined));
  }
  return ok;
}  /* a_emitter::abi_data_record */

/* Bodies. */

bool a_emitter::vptr_init_record()
/*
VPTR_INIT: store the address point of the class's vtable group into the
vptr of every base subobject that has its own, as clang's
InitializeVTablePointers does for a class without virtual bases: the subobject
at offset 0 first, then the non-virtual bases in declaration order; a
non-virtual primary base shares the vptr of its derived class and is skipped.
The vtable's symbol and the address points are chosen at finish (the vtable is
declared at the end of the translation unit): the ops are made with a
placeholder.
*/
{
  bool ok = true;

  const ecil::a_class_facts *self_facts =
      this_class_ != 0 ? ids_.class_facts(static_cast<uint32_t>(this_class_))
                       : nullptr;

  vptr_init_seen_ = true;
  if (this_local_ == nullptr || this_class_ == 0) {
    ok = internal("VPTR_INIT outside a function with this");
  } else if (self_facts != nullptr && !self_facts->vbases.empty()) {
    ok = vptr_init_with_vbases();
  } else {
    struct a_item {
      uint32_t  cls;
      uint64_t  offset;
      bool      primary;
    };
    std::vector<a_item> stack;
    std::vector<std::pair<uint32_t, uint64_t>> stores;

    stack.push_back({static_cast<uint32_t>(this_class_), 0, false});
    while (!stack.empty()) {
      a_item item = stack.back();
      const ecil::a_class_facts *f = ids_.class_facts(item.cls);

      stack.pop_back();
      if (f == nullptr || !f->dynamic)
        continue;
      if (!item.primary)
        stores.push_back({item.cls, item.offset});
      /* The bases in declaration order: pushed in reverse. */
      for (size_t k = f->fields.size(); k > 0; --k) {
        const ecil::a_class_field &field = f->fields[k - 1];
        const ecil::a_class_facts *bf = ids_.class_facts(field.type);

        if (!(field.flags & ECIL_FIELD_BASE) || bf == nullptr || !bf->dynamic)
          continue;
        stack.push_back({field.type, item.offset + field.offset,
                         f->primary_idx == static_cast<int64_t>(k - 1)});
      }
    }
    for (const auto &store : stores) {
      mlir::Value self = builder_.createLoad(loc(), this_local_->address,
                                             false, this_local_->align);
      mlir::Value address = self;
      mlir::Operation *point;
      mlir::Value slot;

      if (store.second != 0) {
        a_type *base = type_at(store.first);

        address = compat::class_address(builder_, loc(), false, self,
                                        compat::pointer_to(base->cir),
                                        store.second, true);
      }
      slot = compat::vtable_get_vptr(builder_, loc(), address);
      point = compat::vtable_address_point(builder_, loc(),
                                           mlir::StringAttr::get(
                                               ctx_, "__ecil.vtable.pending"),
                                           0, 0);
      builder_.createStore(loc(), point->getResult(0), slot, false, false,
                           alignment_attr(8));
      vptr_uses_.push_back({point, this_class_, store.first,
                            static_cast<int64_t>(store.second)});
    }
  }
  return ok;
}  /* a_emitter::vptr_init_record */

bool a_emitter::init_zero_base_record()
/*
INIT_ZERO_BASE: zero the first nvsize bytes of a base subobject (a class's
nvsize, its data size until its CLASS_INFO), so that a virtual base or a field
in its tail padding is not overwritten.  Nothing for an empty base.
*/
{
  bool ok = true;
  a_value target;

  if (!pop_glvalue(target)) {
    ok = false;
  } else if (target.kind != a_value_kind::glvalue) {
    ok = format_error("INIT_ZERO_BASE of a bit-field");
  } else if (!target.omitted) {
    mlir::Type pointee =
        mlir::cast<cir::PointerType>(target.value.getType()).getPointee();
    const a_record_info *rec = record_info_of(pointee);

    if (rec == nullptr)
      ok = format_error("INIT_ZERO_BASE of something that is not a class");
    else if (rec->nvsize != 0 && !zero_initializable(pointee)) {
      /* A pointer to a data member (null is -1) inside: the null constant of
         the base subobject, its nvsize bytes (12.7). */
      mlir::Type base_type = base_subobject_type(*type_at(rec->gid));
      a_value base_target = target;
      a_const null_constant = {null_attribute(base_type), false};

      if (base_type != pointee)
        base_target.value = builder_.createBitcast(
            loc(), target.value, compat::pointer_to(base_type));
      materialize_constant_into(base_target, null_constant, base_type);
    } else if (rec->nvsize != 0) {
      compat::memset_zero(builder_, loc(), target.value,
                          builder_.getUIntNTy(64), rec->nvsize, target.align);
    }
  }
  return ok;
}  /* a_emitter::init_zero_base_record */

bool a_emitter::virtual_call(a_entity &callee,
                             llvm::SmallVectorImpl<mlir::Value> &args,
                             uint64_t slot,
                             llvm::ArrayRef<mlir::NamedAttribute> attrs,
                             cir::CallOp &out)
/*
CALL VIRTUAL: load the vptr of the object (argument 0, already converted to
the class of the function), take the function pointer of the slot and call it
with the arguments (cir.vtable.get_vptr, cir.vtable.get_virtual_fn_addr).
*/
{
  bool ok = true;
  cir::FuncOp fn = mlir::cast<cir::FuncOp>(callee.op);
  cir::FuncType type = fn.getFunctionType();

  if (args.empty() || !mlir::isa<cir::PointerType>(args[0].getType())) {
    ok = format_error("CALL VIRTUAL without an object pointer");
  } else if (type.isVarArg() ? args.size() < type.getNumInputs()
                             : args.size() != type.getNumInputs()) {
    ok = format_error("CALL VIRTUAL: the arguments do not fit the function");
  } else {
    mlir::Value slot_address;
    mlir::Value function;
    mlir::Value vptr_address;
    mlir::Value vptr;

    for (unsigned k = 0; k < type.getNumInputs(); ++k) {
      if (args[k].getType() != type.getInput(k) &&
          mlir::isa<cir::PointerType>(args[k].getType()) &&
          mlir::isa<cir::PointerType>(type.getInput(k)))
        args[k] = builder_.createBitcast(loc(), args[k], type.getInput(k));
    }
    vptr_address = compat::vtable_get_vptr(builder_, loc(), args[0]);
    vptr = builder_.createLoad(loc(), vptr_address, false, 8);
    slot_address = compat::vtable_function_slot(
        builder_, loc(),
        compat::pointer_to(compat::pointer_to(type)), vptr, slot);
    function = builder_.createLoad(loc(), slot_address, false, 8);
    out = builder_.createIndirectCallOp(loc(), function, type, args, attrs);
    callee.referenced = true;
  }
  return ok;
}  /* a_emitter::virtual_call */

bool a_emitter::virtual_delete(uint64_t g_dtor, mlir::Value pointer,
                               a_type *object_type)
/*
DELETE VIRTUAL after the null check: D0 of the object's class through the
vtable, the slot after the one of D1 (D1 is the destructor the stream names).
D0 runs the complete destructor and the operator delete.
*/
{
  bool ok = true;
  uint32_t d1_slot = 0;
  uint32_t d0 = 0;
  uint32_t del = 0;
  a_entity *d0_entity;

  if (!ids_.vslot_of(static_cast<uint32_t>(g_dtor), d1_slot) ||
      !ids_.deleting_of(static_cast<uint32_t>(g_dtor), d0, del)) {
    ok = format_error("DELETE VIRTUAL needs the virtual destructor of a "
                      "STRUCTOR_DELETING");
  } else {
    d0_entity = entity_for(d0, true);
    if (d0_entity == nullptr) {
      ok = false;
    } else {
      cir::FuncType type =
          mlir::cast<cir::FuncOp>(d0_entity->op).getFunctionType();
      mlir::Value object = builder_.createBitcast(
          loc(), pointer, compat::pointer_to(object_type->cir));
      mlir::Value vptr_address = compat::vtable_get_vptr(builder_, loc(),
                                                         object);
      mlir::Value vptr = builder_.createLoad(loc(), vptr_address, false, 8);
      mlir::Value slot_address = compat::vtable_function_slot(
          builder_, loc(), compat::pointer_to(compat::pointer_to(type)), vptr,
          static_cast<uint64_t>(d1_slot) + 1);
      mlir::Value function = builder_.createLoad(loc(), slot_address, false,
                                                 8);
      llvm::SmallVector<mlir::NamedAttribute> attrs;
      mlir::Value self = object;

      if (is_nothrow_call(0, d0_entity))
        attrs.push_back(nothrow_attr());
      if (self.getType() != type.getInput(0))
        self = builder_.createBitcast(loc(), self, type.getInput(0));
      builder_.createIndirectCallOp(loc(), function, type,
                                    llvm::ArrayRef<mlir::Value>{self}, attrs);
      d0_entity->referenced = true;
    }
  }
  return ok;
}  /* a_emitter::virtual_delete */

/* Finish. */

a_abi_entity *a_emitter::abi_entity_of(unsigned kind, uint64_t class_gid)
/*
The DECL_ABI_DATA of the class for a kind (null when the stream has none).
*/
{
  uint32_t g = ids_.abi_data_of((static_cast<uint64_t>(kind) << 32) |
                                class_gid);
  ecil::a_gid_entry *entry = g != 0 ? ids_.find_gid(g) : nullptr;

  return entry != nullptr ? static_cast<a_abi_entity *>(entry->payload)
                          : nullptr;
}  /* a_emitter::abi_entity_of */

a_entity *a_emitter::function_of_key(uint64_t g)
/*
The function entity of the stream id of a virtual function (null when the id
is not a declared function).
*/
{
  const ecil::a_gid_entry *entry = ids_.find_gid(g);

  return entry != nullptr && entry->kind == ecil::gk_func &&
         entry->payload != nullptr
             ? static_cast<a_entity *>(entry->payload) : nullptr;
}  /* a_emitter::function_of_key */

a_entity *a_emitter::library_entity(std::string_view name, bool is_function,
                                    mlir::Type type, bool constant)
/*
The entity of a name the library owns (12.9): the stream's entity of that
name when it declares one, else a new external declaration of the given type
(a variable is a non-constant unless constant says so).
*/
{
  a_entity *result = nullptr;
  auto found = library_names_.end();

  if (!library_names_seeded_) {
    /* The stream's named external entities (a name is one entity: one G-id
       per external symbol). */
    for (a_entity *e : entity_order_) {
      if (e->op != nullptr && e->named && e->linkage != elk_internal &&
          e->linkage != elk_private)
        stream_names_[e->want] = e;
    }
    library_names_seeded_ = true;
  }
  found = library_names_.find(name);
  if (found != library_names_.end()) {
    result = found->second;
  } else {
    auto stream = stream_names_.find(name);

    if (stream != stream_names_.end() &&
        stream->second->is_function == is_function) {
      result = stream->second;
    } else {
      /* An entity named after the first seeding (a vtable or typeinfo that
         finish named): a name is one entity. */
      for (a_entity *e : entity_order_) {
        if (e->op != nullptr && e->named && e->want == name &&
            e->is_function == is_function && e->linkage != elk_internal &&
            e->linkage != elk_private) {
          result = e;
          stream_names_[e->want] = e;
          break;
        }
      }
    }
  }
  if (result != nullptr && found == library_names_.end() &&
      (result->is_function ? result->has_body
                           : (result->flags & ECIL_DECL_VAR_DEFINITION) != 0 ||
                                 result->has_init)) {
    /* 12.9: a stream entity of the name is reused, unless it defines it. */
    format_error("the stream defines '" + std::string(name) + "', a name "
                 "the library owns");
    result = nullptr;
  } else if (result == nullptr) {
    std::string_view want = keep_string(name);

    entity_store_.emplace_back();
    result = &entity_store_.back();
    result->is_function = is_function;
    result->linkage = elk_external;
    result->named = true;
    result->want = want;
    result->symbol = claim_symbol(want, 0);
    if (is_function) {
      cir::FuncOp fn = compat::create_function(builder_, module_, loc(),
                                               result->symbol, type);

      result->op = fn.getOperation();
      fn.setLinkageAttr(linkage_attr(cir::GlobalLinkageKind::ExternalLinkage));
      mlir::SymbolTable::setSymbolVisibility(
          fn, mlir::SymbolTable::Visibility::Private);
    } else {
      cir::GlobalOp g = compat::create_global(
          builder_, module_, loc(), result->symbol, type, constant,
          cir::GlobalLinkageKind::ExternalLinkage);

      result->op = g.getOperation();
      g.setAlignmentAttr(alignment_attr(8));
      mlir::SymbolTable::setSymbolVisibility(
          g, mlir::SymbolTable::Visibility::Private);
    }
    entity_order_.push_back(result);
  }
  if (result != nullptr)
    library_names_[result->want] = result;
  return result;
}  /* a_emitter::library_entity */

mlir::Attribute a_emitter::symbol_pointer(a_entity &e)
/*
#cir.global_view of an entity as a !cir.ptr<!u8i> (a vtable component, a
typeinfo field).
*/
{
  mlir::StringAttr name = e.is_function
      ? mlir::cast<cir::FuncOp>(e.op).getSymNameAttr()
      : mlir::cast<cir::GlobalOp>(e.op).getSymNameAttr();

  e.referenced = true;
  return compat::global_view(compat::pointer_to(compat::int_type(ctx_, 8,
                                                                 false)),
                             name, mlir::ArrayAttr(), false);
}  /* a_emitter::symbol_pointer */

mlir::Attribute a_emitter::pointer_constant(int64_t value)
/*
A !cir.ptr<!u8i> constant (null for 0): the offset components of a vtable.
*/
{
  return builder_.getConstPtrAttr(
      compat::pointer_to(compat::int_type(ctx_, 8, false)), value);
}  /* a_emitter::pointer_constant */



bool a_emitter::defined_here(const a_entity &e) const
/*
Whether the translation unit defines a function: it has a body, or it is the
complete variant of a structor pair whose base variant has one (C1/D1 is
then an alias of, or redirected to, the base variant).
*/
{
  return e.has_body || complete_defined_.count(&e) != 0;
}  /* a_emitter::defined_here */

/* Thunks. */

namespace {

mlir::Value adjust_pointer(compat::a_builder &builder, mlir::Location loc,
                           mlir::Value pointer, int64_t non_virtual,
                           int64_t vcall_offset_offset, bool is_return)
/*
CIRGenItaniumCXXABI's performTypeAdjustment: the pointer moved by a constant
(a this adjustment first, a result adjustment last) and by the offset the
vtable holds at the given offset from the address point (the virtual part,
from the vptr of the object the pointer designates).
*/
{
  mlir::MLIRContext *ctx = builder.getContext();
  mlir::Type byte_pointer = cir::PointerType::get(cir::IntType::get(ctx, 8,
                                                                    false));
  mlir::Type word = cir::IntType::get(ctx, 64, true);
  mlir::Value v = pointer;
  mlir::Value result;

  if (non_virtual == 0 && vcall_offset_offset == 0) {
    result = pointer;
  } else {
    v = builder.createBitcast(loc, pointer, byte_pointer);
    if (non_virtual != 0 && !is_return)
      v = builder.createPtrStride(loc, v,
                                  builder.getConstantInt(loc, word,
                                                         non_virtual));
    result = v;
    if (vcall_offset_offset != 0) {
      mlir::Value vptr_address = compat::vtable_get_vptr(builder, loc, v);
      mlir::Value vptr = builder.createLoad(loc, vptr_address, false, 8);
      mlir::Value vtable = builder.createBitcast(loc, vptr, byte_pointer);
      mlir::Value offset_address = builder.createPtrStride(
          loc, vtable, builder.getConstantInt(loc, word, vcall_offset_offset));
      mlir::Value offset_word = builder.createBitcast(
          loc, offset_address, cir::PointerType::get(word));
      mlir::Value offset = builder.createLoad(loc, offset_word, false, 8);

      result = builder.createPtrStride(loc, v, offset);
    }
    if (non_virtual != 0 && is_return)
      result = builder.createPtrStride(loc, result,
                                       builder.getConstantInt(loc, word,
                                                              non_virtual));
    result = builder.createBitcast(loc, result, pointer.getType());
  }
  return result;
}  /* adjust_pointer */

}  /* anonymous namespace */

void a_emitter::build_thunk_body(a_entity &thunk, a_entity &target,
                                 const edg_abi::a_thunk_info &info)
/*
CIRGenFunction::generateThunk: adjust `this`, call the target with the
adjusted pointer and the other arguments, adjust the result.  A variadic
function forwards its arguments with a musttail call (a return adjustment
cannot be combined with it: that is diagnosed where the thunk is requested).
*/
{
  cir::FuncOp fn = mlir::cast<cir::FuncOp>(thunk.op);
  cir::FuncOp callee = mlir::cast<cir::FuncOp>(target.op);
  cir::FuncType type = fn.getFunctionType();
  mlir::Block *entry = fn.addEntryBlock();
  mlir::OpBuilder::InsertionGuard guard(builder_);
  llvm::SmallVector<mlir::Value> arguments(entry->getArguments().begin(),
                                           entry->getArguments().end());
  llvm::SmallVector<mlir::NamedAttribute> attrs;
  mlir::Value adjusted;
  cir::CallOp call;

  builder_.setInsertionPointToStart(entry);
  if (is_nothrow_call(0, &target))
    attrs.push_back(nothrow_attr());
  if (type.isVarArg()) {
    /* The arguments are forwarded as they are, `this` adjusted. */
    adjusted = adjust_pointer(builder_, loc(), arguments[0],
                              info.this_adj.non_virtual,
                              info.this_adj.offset_offset, false);
    arguments[0] = adjusted;
    call = builder_.createCallOp(loc(), callee, arguments, attrs);
    compat::set_must_tail(call);
    if (call.getNumResults() == 0)
      cir::ReturnOp::create(builder_, loc());
    else
      cir::ReturnOp::create(builder_, loc(), call.getResult());
  } else {
    llvm::SmallVector<mlir::Value> slots;
    llvm::SmallVector<mlir::Value> call_arguments;
    mlir::Value this_value;

    /* Every parameter goes through an alloca, as CIRGen's does: a
       NONTRIVIAL_ABI record is passed on as the load of its object. */
    for (unsigned k = 0; k < arguments.size(); ++k) {
      mlir::Type param = arguments[k].getType();
      uint32_t align = static_cast<uint32_t>(type_align(param));
      std::string slot_name = k == 0 ? "this" : "arg" + std::to_string(k);
      mlir::Value slot = builder_.createAlloca(
          loc(), cir::PointerType::get(param), slot_name,
          alignment_attr(align));

      builder_.createStore(loc(), arguments[k], slot, false, false,
                           alignment_attr(align));
      slots.push_back(slot);
    }
    mlir::Type result_type = type.hasVoidReturn()
        ? static_cast<mlir::Type>(compat::void_type(ctx_))
        : type.getReturnType();
    mlir::Value retval;

    if (!type.hasVoidReturn())
      retval = builder_.createAlloca(
          loc(), cir::PointerType::get(result_type), "__retval",
          alignment_attr(type_align(result_type)));
    this_value = builder_.createLoad(
        loc(), slots[0], false,
        static_cast<uint32_t>(type_align(arguments[0].getType())));
    adjusted = adjust_pointer(builder_, loc(), this_value,
                              info.this_adj.non_virtual,
                              info.this_adj.offset_offset, false);
    call_arguments.push_back(adjusted);
    for (unsigned k = 1; k < arguments.size(); ++k)
      call_arguments.push_back(builder_.createLoad(
          loc(), slots[k], false,
          static_cast<uint32_t>(type_align(arguments[k].getType()))));
    call = builder_.createCallOp(loc(), callee, call_arguments, attrs);
    if (type.hasVoidReturn()) {
      cir::ReturnOp::create(builder_, loc());
    } else {
      mlir::Value result = call.getResult();

      if (!info.return_adj.is_empty() && info.ref_result) {
        /* A reference result is adjusted even when it is a (bogus) null
           reference. */
        result = adjust_pointer(builder_, loc(), result,
                                info.return_adj.non_virtual,
                                info.return_adj.offset_offset, true);
      } else if (!info.return_adj.is_empty()) {
        mlir::Value not_null = builder_.createPtrIsNotNull(result);
        mlir::Type pointer_type = result.getType();
        mlir::Value pick = cir::TernaryOp::create(
            builder_, loc(), not_null,
            [&](mlir::OpBuilder &, mlir::Location) {
              mlir::Value moved = adjust_pointer(
                  builder_, loc(), result, info.return_adj.non_virtual,
                  info.return_adj.offset_offset, true);

              builder_.createYield(loc(), moved);
            },
            [&](mlir::OpBuilder &, mlir::Location) {
              builder_.createYield(
                  loc(), builder_.getNullPtr(pointer_type, loc())
                             .getResult());
            }).getResult();

        result = pick;
      }
      builder_.createStore(loc(), result, retval, false, false,
                           alignment_attr(type_align(result_type)));
      mlir::Value loaded = builder_.createLoad(
          loc(), retval, false,
          static_cast<uint32_t>(type_align(result_type)));

      cir::ReturnOp::create(builder_, loc(), mlir::ValueRange{loaded});
    }
  }
  target.calls.push_back(call);
}  /* a_emitter::build_thunk_body */

a_entity *a_emitter::define_library_function(std::string_view want,
                                             mlir::Type type, unsigned linkage,
                                             bool comdat)
/*
A function the library defines (a thunk, D0): a new entity with the given
linkage and, for a vague one, its own comdat.  The body is the caller's.
*/
{
  bool known = true;
  cir::GlobalLinkageKind kind = compat::linkage_kind(linkage, known);
  cir::FuncOp fn;

  entity_store_.emplace_back();
  a_entity &e = entity_store_.back();
  e.is_function = true;
  e.linkage = linkage;
  if (linkage == elk_linkonce_odr)
    e.had_linkonce = true;
  e.flags = comdat ? ECIL_DECL_FUNC_COMDAT : 0u;
  e.named = true;
  e.want = keep_string(want);
  e.symbol = claim_symbol(e.want, 0);
  e.has_body = true;
  fn = compat::create_function(builder_, module_, loc(), e.symbol, type);
  e.op = fn.getOperation();
  fn.setLinkageAttr(linkage_attr(kind));
  mlir::SymbolTable::setSymbolVisibility(fn,
                                         compat::symbol_visibility(kind,
                                                                   false));
  compat::set_dso_local(fn.getOperation(), true);
  compat::set_unwind_tables(fn);
  if (!(session_.header.lang_flags & ECIL_LANG_EXCEPTIONS))
    fn->setAttr(nothrow_name_, unit_attr_);
  entity_order_.push_back(&e);
  return &e;
}  /* a_emitter::define_library_function */

a_entity *a_emitter::thunk_entity(const edg_abi::a_thunk_info &info,
                                  a_entity &target, bool &ok)
/*
The function a vtable entry points at when it needs a this or result
adjustment: _ZTh/_ZTv/_ZTc<offsets><the target's symbol>, made once per
distinct thunk and typed like its target.  When the target is defined in this
translation unit the thunk is defined too, with the target's linkage (and its
own comdat when that linkage is vague), as clang emits the thunks of a method
along with its definition; otherwise it is an external declaration that the
translation unit defining the target provides.
*/
{
  a_thunk_key key = {&target, info.this_adj.non_virtual,
                     info.this_adj.offset_offset,
                     info.return_adj.non_virtual,
                     info.return_adj.offset_offset};
  auto found = thunks_.find(key);
  a_entity *result = nullptr;

  ok = true;
  if (found != thunks_.end()) {
    result = found->second;
  } else if (!target.named || target.want.compare(0, 2, "_Z") != 0) {
    ok = format_error("a thunk of the function '" + std::string(target.want) +
                      "' needs its mangled name");
  } else {
    std::string name = edg_abi::thunk_symbol(info.this_adj, info.return_adj,
                                             std::string(target.want));
    mlir::Type type = mlir::cast<cir::FuncOp>(target.op).getFunctionType();
    bool defined = defined_here(target);

    if (name.empty()) {
      ok = format_error("cannot name a thunk of '" +
                        std::string(target.want) + "'");
    } else if (defined &&
               mlir::cast<cir::FuncType>(type).isVarArg() &&
               !info.return_adj.is_empty()) {
      ok = unsupported("a return-adjusting thunk of a variadic function");
    } else if (defined) {
      bool vague = target.linkage == elk_linkonce_odr ||
                   target.linkage == elk_weak_odr;

      result = define_library_function(name, type, target.linkage, vague);
      thunks_[key] = result;
      build_thunk_body(*result, target, info);
    } else {
      result = library_entity(name, true, type, false);
      ok = result != nullptr;
      if (ok)
        thunks_[key] = result;
    }
  }
  return result;
}  /* a_emitter::thunk_entity */

/* Deleting destructors. */

void a_emitter::build_deleting_dtor(a_deleting_dtor &d)
/*
D0 of a virtual destructor, when the complete destructor is defined here:
`cleanup.scope all { D1(this) } cleanup { operator delete(this[, size]
[, align]) }`, CIRGenFunction::enterDtorCleanups' CallDtorDelete after
emitDestructorBody.  The size is the class's size, the alignment its
alignment, as the flags of STRUCTOR_DELETING say.
*/
{
  a_type *object = type_at(d.class_gid);

  if (!defined_here(*d.d1)) {
    /* No D1 here, so no D0. */
  } else if (object == nullptr || object->kind != a_type_kind::record_type ||
             !object->record->complete || d.d0->has_body) {
    internal("STRUCTOR_DELETING out of step with the class");
  } else {
    cir::FuncOp fn = mlir::cast<cir::FuncOp>(d.d0->op);
    cir::FuncOp complete = mlir::cast<cir::FuncOp>(d.d1->op);
    cir::FuncOp del = mlir::cast<cir::FuncOp>(d.del->op);
    cir::FuncType del_type = del.getFunctionType();
    mlir::Block *entry = fn.addEntryBlock();
    mlir::OpBuilder::InsertionGuard guard(builder_);
    llvm::SmallVector<mlir::NamedAttribute> attrs;
    mlir::Type param = entry->getArgument(0).getType();
    uint32_t align = static_cast<uint32_t>(type_align(param));
    mlir::Value slot;
    mlir::Value self;
    cir::CleanupScopeOp scope;
    llvm::SmallVector<mlir::Value> args;
    unsigned k = 1;
    bool destroying = (d.flags & ECIL_STRUCTOR_DELETING_DESTROYING) != 0;

    d.d0->has_body = true;
    d.d0->referenced = true;
    apply_function_attributes(*d.d0, fn);
    compat::set_dso_local(fn.getOperation(), true);
    compat::set_unwind_tables(fn);
    if (is_nothrow_call(0, d.d1))
      fn->setAttr(nothrow_name_, unit_attr_);
    builder_.setInsertionPointToStart(entry);
    slot = builder_.createAlloca(loc(), cir::PointerType::get(param), "this",
                                 alignment_attr(align));
    builder_.createStore(loc(), entry->getArgument(0), slot, false, false,
                         alignment_attr(align));
    self = builder_.createLoad(loc(), slot, false, align);
    if (destroying) {
      /* A destroying operator delete: D0 only calls it. */
    } else {
      scope = compat::open_cleanup_scope(
          builder_, loc(),
          exceptions_ ? cir::CleanupKind::All : cir::CleanupKind::Normal);
      builder_.setInsertionPointToStart(&scope.getBodyRegion().front());
      if (is_nothrow_call(0, d.d1))
        attrs.push_back(nothrow_attr());
      d.d1->calls.push_back(builder_.createCallOp(loc(), complete, self,
                                                  attrs));
      attrs.clear();
      builder_.createYield(loc());
      builder_.setInsertionPointToStart(&scope.getCleanupRegion().front());
    }
    args.push_back(builder_.createBitcast(loc(), self, del_type.getInput(0)));
    if (destroying) {
      /* The empty std::destroying_delete_t tag: a temporary, passed by
         value (clang's emitDeleteCall). */
      if (k < del_type.getNumInputs()) {
        mlir::Type tag = del_type.getInput(k);
        uint32_t tag_align = static_cast<uint32_t>(type_align(tag));
        mlir::Value tag_slot;

        {
          mlir::OpBuilder::InsertionGuard alloca_guard(builder_);

          builder_.setInsertionPointAfter(slot.getDefiningOp());
          tag_slot = builder_.createAlloca(
              loc(), cir::PointerType::get(tag), "destroying.delete.tag",
              alignment_attr(tag_align));
        }
        args.push_back(builder_.createLoad(loc(), tag_slot, false,
                                           tag_align));
      }
      ++k;
    }
    if (d.flags & ECIL_STRUCTOR_DELETING_SIZED) {
      if (k < del_type.getNumInputs() &&
          mlir::isa<cir::IntType>(del_type.getInput(k)))
        args.push_back(builder_.getConstantInt(
            loc(), del_type.getInput(k),
            static_cast<int64_t>(object->record->size)));
      ++k;
    }
    if (d.flags & ECIL_STRUCTOR_DELETING_ALIGNED) {
      if (k < del_type.getNumInputs() &&
          mlir::isa<cir::IntType>(del_type.getInput(k)))
        args.push_back(builder_.getConstantInt(
            loc(), del_type.getInput(k),
            static_cast<int64_t>(object->record->align)));
      ++k;
    }
    if (args.size() != del_type.getNumInputs()) {
      format_error("STRUCTOR_DELETING flags do not fit the operator delete "
                   "of the class");
    } else {
      if (is_nothrow_call(0, d.del))
        attrs.push_back(nothrow_attr());
      d.del->calls.push_back(builder_.createCallOp(loc(), del, args, attrs));
      d.del->referenced = true;
    }
    if (destroying) {
      cir::ReturnOp::create(builder_, loc());
    } else {
      builder_.createYield(loc());
      builder_.setInsertionPointAfter(scope.getOperation());
      cir::ReturnOp::create(builder_, loc());
    }
  }
}  /* a_emitter::build_deleting_dtor */

/* The ABI data. */

mlir::Attribute a_emitter::rtti_pointer(edg_abi::a_class_db &db,
                                        const edg_abi::a_component &component,
                                        bool &ok)
/*
The RTTI slot of a vtable: the typeinfo of the class the component names (null
without rtti: 12.6).
*/
{
  mlir::Attribute result;

  ok = true;
  if (!(session_.header.lang_flags & ECIL_LANG_RTTI)) {
    result = pointer_constant(0);
  } else {
    a_abi_entity *ti = abi_entity_of(eadk_typeinfo,
                                     db.cls(component.id).key);

    if (ti == nullptr) {
      ok = format_error("a vtable refers to the typeinfo of class '" +
                        db.cls(component.id).name + "', which the stream "
                        "does not declare");
    } else {
      result = symbol_pointer(*ti->entity);
    }
  }
  return result;
}  /* a_emitter::rtti_pointer */

mlir::Attribute a_emitter::vtable_component(
    edg_abi::a_class_db &db, const edg_abi::a_vtable_layout &layout,
    size_t index,
    const std::map<size_t, const edg_abi::a_thunk_info *> &thunks, bool &ok)
/*
CIRGenVTables::getVTableComponent: the constant of one vtable entry.
*/
{
  const edg_abi::a_component &comp = layout.components[index];
  mlir::Attribute result;
  a_entity *target = nullptr;

  ok = true;
  switch (comp.kind) {
  case edg_abi::ck_unused_function:
    result = pointer_constant(0);
    break;
  case edg_abi::ck_vcall_offset:
  case edg_abi::ck_vbase_offset:
  case edg_abi::ck_offset_to_top:
    result = pointer_constant(comp.offset);
    break;
  case edg_abi::ck_rtti:
    result = rtti_pointer(db, comp, ok);
    break;
  default: {
    const edg_abi::a_fn_fact &fn = db.fn(comp.id);
    auto thunk = thunks.find(index);

    if (fn.is_pure || fn.is_deleted) {
      target = library_entity(fn.is_pure ? "__cxa_pure_virtual"
                                         : "__cxa_deleted_virtual",
                              true,
                              compat::function_type({}, compat::void_type(
                                                            ctx_), false),
                              false);
      ok = target != nullptr;
    } else {
      uint32_t d0 = 0;
      uint32_t del = 0;

      if (comp.kind == edg_abi::ck_deleting_dtor) {
        if (!ids_.deleting_of(static_cast<uint32_t>(fn.key), d0, del))
          ok = format_error("the vtable has the deleting destructor of '" +
                            fn.name + "', which has no STRUCTOR_DELETING");
        else
          target = function_of_key(d0);
      } else {
        target = function_of_key(fn.key);
      }
      if (ok && target == nullptr)
        ok = format_error("a vtable refers to the function '" + fn.name +
                          "', which the stream does not declare");
      if (ok && thunk != thunks.end())
        target = thunk_entity(*thunk->second, *target, ok);
    }
    if (ok && target != nullptr)
      result = symbol_pointer(*target);
    break;
  }
  }
  return result;
}  /* a_emitter::vtable_component */

void a_emitter::name_class_abi_entities()
/*
Name the vtable and the typeinfo of every class before any of them is
finished: a library name (the vtable of an abi:: class that a non-class
typeinfo points at, for one) must find the stream's entity of that name.
*/
{
  for (a_abi_entity *abi : abi_order_) {
    const ecil::a_gid_entry *type_entry = ids_.find_gid(abi->type);
    const ecil::a_class_facts *facts =
        ids_.class_facts(static_cast<uint32_t>(abi->type));

    if (abi->kind == eadk_vtable && facts != nullptr &&
        facts->mangle_sid != 0) {
      abi->entity->want = keep_string(edg_abi::vtable_symbol(
          std::string(ids_.string(facts->mangle_sid))));
      abi->entity->named = true;
    } else if (abi->kind == eadk_typeinfo && type_entry != nullptr &&
               type_entry->kind == ecil::gk_record && facts != nullptr &&
               facts->mangle_sid != 0) {
      abi->entity->want = keep_string(edg_abi::typeinfo_symbol(
          std::string(ids_.string(facts->mangle_sid))));
      abi->entity->named = true;
    }
  }
}  /* a_emitter::name_class_abi_entities */

bool a_emitter::finish_abi_entity(a_abi_entity &abi,
                                  edg_abi::a_vtable_context &ctx,
                                  edg_abi::a_class_db &db,
                                  const std::map<uint64_t, edg_abi::a_class_id>
                                      &by_key)
/*
The type and, when the entity is defined here, the initializer of a vtable or
a class typeinfo, and its name (derived from the class's mangling).  A typeinfo
also gets its _ZTS name string.
*/
{
  bool ok = true;
  a_entity &e = *abi.entity;
  cir::GlobalOp global = mlir::cast<cir::GlobalOp>(e.op);
  bool defined = (abi.flags & ECIL_ABI_DATA_DEFINED_HERE) != 0;
  const ecil::a_class_facts *facts =
      ids_.class_facts(static_cast<uint32_t>(abi.type));
  auto found = by_key.find(abi.type);
  std::string mangled = facts != nullptr
      ? std::string(ids_.string(facts->mangle_sid)) : std::string();
  mlir::Type byte_pointer = compat::pointer_to(compat::int_type(ctx_, 8,
                                                                false));
  std::string error;
  const ecil::a_gid_entry *type_entry = ids_.find_gid(abi.type);

  if (abi.kind == eadk_typeinfo && type_entry != nullptr &&
      type_entry->kind != ecil::gk_record) {
    ok = finish_plain_typeinfo(abi, db);
  } else if (facts == nullptr || mangled.empty()) {
    ok = format_error("the ABI data of a class without its CLASS_MANGLE");
  } else if (abi.kind == eadk_vtable) {
    e.want = keep_string(edg_abi::vtable_symbol(mangled));
    e.named = true;
    if (found == by_key.end()) {
      ok = format_error("the vtable of class '" + mangled + "' needs its "
                        "CLASS_VFUNCS and CLASS_OVERRIDES");
    } else {
      const edg_abi::a_vtable_info &info = ctx.vtable_info(found->second,
                                                           error);

      if (!error.empty()) {
        ok = format_error("vtable of '" + mangled + "': " + error);
      } else {
        cir::RecordType record;
        mlir::Attribute value;

        ok = vtable_value(db, info.layout, record, value);
        if (ok) {
          global.setSymTypeAttr(mlir::TypeAttr::get(record));
          if (defined) {
            global.setInitialValueAttr(value);
            e.has_init = true;
          }
        }
      }
    }
  } else if (abi.kind == eadk_vtt) {
    ok = finish_vtt(abi, ctx, db, by_key, mangled);
  } else {
    /* The class typeinfo. */
    e.want = keep_string(edg_abi::typeinfo_symbol(mangled));
    e.named = true;
    retype_variable(e, byte_pointer);
    if (defined) {
      ok = finish_class_typeinfo(abi, ctx, db, by_key, mangled);
    } else {
      compat::clear_initial_value(global);
    }
  }
  return ok;
}  /* a_emitter::finish_abi_entity */

bool a_emitter::finish_class_typeinfo(
    a_abi_entity &abi, edg_abi::a_vtable_context &ctx,
    edg_abi::a_class_db &db,
    const std::map<uint64_t, edg_abi::a_class_id> &by_key,
    const std::string &mangled)
/*
The typeinfo object of a class defined here: abi::__class_type_info,
__si_class_type_info or __vmi_class_type_info (the layout of lib/abi) and the
_ZTS string global it points to (with the g++ '*' for a TU-local type).
*/
{
  bool ok = true;
  a_entity &e = *abi.entity;
  cir::GlobalOp global = mlir::cast<cir::GlobalOp>(e.op);
  auto found = by_key.find(abi.type);
  mlir::Type byte_pointer = compat::pointer_to(compat::int_type(ctx_, 8,
                                                                false));
  edg_abi::a_rtti_layout layout;
  std::string error;
  bool tu_local = (abi.flags & ECIL_ABI_DATA_TU_LOCAL) != 0;
  bool vague = e.linkage == elk_linkonce_odr || e.linkage == elk_weak_odr;

  const ecil::a_class_facts *class_facts =
      ids_.class_facts(static_cast<uint32_t>(abi.type));
  /* The typeinfo of an incomplete class (typeid(I *)) has no facts: a plain
     abi::__class_type_info with no bases (12.3). */
  bool incomplete = found == by_key.end() &&
                    (class_facts == nullptr || !class_facts->complete);

  if (incomplete) {
    layout.kind = edg_abi::rtti_class;
    layout.vtable_symbol = "_ZTVN10__cxxabiv117__class_type_infoE";
    layout.fields.push_back({edg_abi::rf_vtable_ptr, edg_abi::no_id, 2});
    layout.fields.push_back({edg_abi::rf_name_ptr, edg_abi::no_id, 0});
  }
  if (found == by_key.end() && !incomplete) {
    ok = format_error("the typeinfo of class '" + mangled + "' needs the "
                      "facts of its class");
  } else if (!incomplete &&
             !edg_abi::rtti_layout(ctx, found->second, layout, error)) {
    ok = format_error("typeinfo of '" + mangled + "': " + error);
  } else {
    /* _ZTS<type>: the mangled type name, same linkage as the typeinfo. */
    std::string name_symbol = edg_abi::typeinfo_name_symbol(mangled);
    std::string text = edg_abi::typeinfo_name_string(mangled, tu_local);
    mlir::Type char_type = compat::int_type(ctx_, 8, true);
    mlir::Type array_type = compat::array_of(char_type, text.size() + 1);
    bool known = true;
    cir::GlobalLinkageKind kind = compat::linkage_kind(e.linkage, known);
    cir::GlobalOp name_global;
    std::vector<mlir::Attribute> fields;

    entity_store_.emplace_back();
    a_entity &name_entity = entity_store_.back();
    name_entity.linkage = e.linkage;
    if (e.linkage == elk_linkonce_odr)
      name_entity.had_linkonce = true;
    name_entity.flags = ECIL_DECL_VAR_CONST | ECIL_DECL_VAR_DEFINITION |
                        (vague ? ECIL_DECL_VAR_COMDAT : 0u);
    name_entity.align = 1;
    name_entity.named = true;
    name_entity.want = keep_string(name_symbol);
    name_entity.symbol = claim_symbol(name_entity.want, 0);
    name_global = compat::create_global(builder_, module_, loc(),
                                        name_entity.symbol, array_type, true,
                                        kind);
    name_entity.op = name_global.getOperation();
    name_global.setAlignmentAttr(alignment_attr(1));
    name_global.setInitialValueAttr(string_attribute(
        reinterpret_cast<const uint8_t *>(text.data()),
        static_cast<uint32_t>(text.size() + 1), array_type));
    compat::set_dso_local(name_global.getOperation(), true);
    mlir::SymbolTable::setSymbolVisibility(
        name_global, compat::symbol_visibility(kind, false));
    name_entity.has_init = true;
    entity_order_.push_back(&name_entity);
    abi.name_entity = &name_entity;
    for (const edg_abi::a_rtti_field &field : layout.fields) {
      switch (field.kind) {
      case edg_abi::rf_vtable_ptr: {
        a_entity *abi_vtable = library_entity(
            layout.vtable_symbol, false,
            compat::pointer_to(byte_pointer), false);
        llvm::SmallVector<mlir::Attribute, 2> path;

        if (abi_vtable == nullptr) {
          ok = false;
          break;
        }
        /* The stream's own vtable entity of the abi:: class ends up a struct
           of one array: indexed through its struct. */
        if (abi_vtable->gid != 0)
          path.push_back(builder_.getI32IntegerAttr(0));
        path.push_back(
            builder_.getI32IntegerAttr(static_cast<int32_t>(field.value)));
        mlir::ArrayAttr indices = builder_.getArrayAttr(path);

        abi_vtable->referenced = true;
        fields.push_back(compat::global_view(
            byte_pointer, mlir::cast<cir::GlobalOp>(abi_vtable->op)
                              .getSymNameAttr(), indices, false));
        break;
      }
      case edg_abi::rf_name_ptr:
        fields.push_back(symbol_pointer(name_entity));
        break;
      case edg_abi::rf_base_typeinfo: {
        a_abi_entity *base = abi_entity_of(eadk_typeinfo,
                                           db.cls(field.cls).key);

        if (base == nullptr)
          ok = format_error("the typeinfo of '" + mangled + "' needs the "
                            "typeinfo of its base '" + db.cls(field.cls).name +
                            "'");
        else
          fields.push_back(symbol_pointer(*base->entity));
        break;
      }
      case edg_abi::rf_u32:
        fields.push_back(cir::IntAttr::get(
            compat::int_type(ctx_, 32, false),
            llvm::APInt(32, static_cast<uint64_t>(field.value))));
        break;
      default:
        fields.push_back(cir::IntAttr::get(
            compat::int_type(ctx_, 64, true),
            llvm::APInt(64, static_cast<uint64_t>(field.value), true)));
        break;
      }
    }
    if (ok) {
      std::vector<mlir::Type> members;
      cir::RecordType record;

      for (mlir::Attribute attr : fields)
        members.push_back(mlir::cast<mlir::TypedAttr>(attr).getType());
      record = compat::anonymous_record(ctx_, members);
      retype_variable(e, record);
      global.setInitialValueAttr(compat::typeinfo_attr(
          record, builder_.getArrayAttr(fields)));
      e.has_init = true;
    }
  }
  return ok;
}  /* a_emitter::finish_class_typeinfo */

bool a_emitter::patch_vptr_constants(
    edg_abi::a_vtable_context &ctx, edg_abi::a_class_db &db,
    const std::map<uint64_t, edg_abi::a_class_id> &by_key)
/*
The vptrs inside constants (12.7): each placeholder #cir.global_view becomes
the address point of the base subobject in the vtable group of the complete
object's class, `#cir.global_view<@_ZTV.., [index, offset], address_point>`.
One replacement pass over the module.
*/
{
  bool ok = true;
  std::vector<mlir::Attribute> replacement(vptr_markers_.size());
  mlir::MLIRContext *mctx = ctx_;
  mlir::AttrTypeReplacer replacer;

  for (size_t i = 0; ok && i < vptr_markers_.size(); ++i) {
    const a_vptr_marker &marker = vptr_markers_[i];
    a_abi_entity *vtable;
    auto root = by_key.find(marker.vtable_class);
    auto base = by_key.find(marker.class_gid);
    std::string error;

    if (marker.vtable_class == 0)
      continue;   /* A constant that was never used. */
    vtable = abi_entity_of(eadk_vtable, marker.vtable_class);
    if (vtable == nullptr || root == by_key.end() || base == by_key.end()) {
      ok = format_error("a constant of a dynamic class needs the vtable "
                        "(DECL_ABI_DATA) of the class and its vtable facts");
    } else {
      const edg_abi::a_vtable_info &info = ctx.vtable_info(root->second,
                                                           error);
      auto point = info.layout.address_points.find({base->second,
                                                    marker.offset});

      if (!error.empty() || point == info.layout.address_points.end()) {
        ok = format_error("no address point for the vptr of a constant: " +
                          error);
      } else {
        replacement[i] = compat::global_view(
            cir::VPtrType::get(mctx),
            mlir::cast<cir::GlobalOp>(vtable->entity->op).getSymNameAttr(),
            builder_.getArrayAttr(
                {builder_.getI32IntegerAttr(static_cast<int32_t>(
                     point->second.vtable_index)),
                 builder_.getI32IntegerAttr(static_cast<int32_t>(
                     point->second.address_point_index))}),
            true);
        vtable->entity->referenced = true;
      }
    }
  }
  if (ok) {
    replacer.addReplacement(
        [&](cir::GlobalViewAttr attr)
            -> std::pair<mlir::Attribute, mlir::WalkResult> {
          llvm::StringRef name = attr.getSymbol().getValue();
          mlir::Attribute result = attr;

          if (name.starts_with("__ecil.vptr.")) {
            unsigned long long index = 0;

            if (!name.drop_front(12).getAsInteger(10, index) &&
                index < replacement.size() && replacement[index])
              result = replacement[index];
          }
          return {result, mlir::WalkResult::skip()};
        });
    replacer.recursivelyReplaceElementsIn(module_.getOperation(),
                                          /*replaceAttrs=*/true,
                                          /*replaceLocs=*/false,
                                          /*replaceTypes=*/false);
  }
  return ok;
}  /* a_emitter::patch_vptr_constants */

/* Virtual bases and the VTT (S2). */

mlir::Type a_emitter::vtt_parameter_type()
/*
The type of the hidden VTT parameter and of a VTT address: void**.
*/
{
  return compat::pointer_to(compat::pointer_to(compat::void_type(ctx_)));
}  /* a_emitter::vtt_parameter_type */

cir::FuncType a_emitter::with_vtt_parameter(cir::FuncType type)
/*
The type of a base-variant structor of a class with virtual bases: the VTT
parameter follows `this` (12.3).
*/
{
  llvm::SmallVector<mlir::Type> inputs(type.getInputs().begin(),
                                       type.getInputs().end());
  mlir::Type result = type.hasVoidReturn()
      ? static_cast<mlir::Type>(compat::void_type(ctx_))
      : type.getReturnType();

  inputs.insert(inputs.begin() + (inputs.empty() ? 0 : 1),
                vtt_parameter_type());
  return mlir::cast<cir::FuncType>(
      compat::function_type(inputs, result, type.isVarArg()));
}  /* a_emitter::with_vtt_parameter */

mlir::Value a_emitter::virtual_base_offset(mlir::Value object, uint64_t der,
                                           uint64_t vbase)
/*
CIRGenItaniumCXXABI::getVirtualBaseClassOffset: the offset of virtual base
`vbase` in the complete object that `object` (a subobject of class `der`)
belongs to, read from the vtable of the object.  Where it is stored is a fact
of the vtable layout: the offset of the slot is a constant that finish fills
in.
*/
{
  mlir::Type word = compat::int_type(ctx_, 64, true);
  mlir::Type byte_pointer = compat::pointer_to(compat::int_type(ctx_, 8,
                                                                false));
  mlir::Value vptr_address = compat::vtable_get_vptr(builder_, loc(), object);
  mlir::Value vptr = builder_.createLoad(loc(), vptr_address, false, 8);
  mlir::Value vtable = builder_.createBitcast(loc(), vptr, byte_pointer);
  mlir::Value slot_offset = builder_.getConstantInt(loc(), word, 0);
  mlir::Value slot_address;
  mlir::Value word_address;

  late_uses_.push_back({a_late_kind::vbase_offset_offset,
                        slot_offset.getDefiningOp(), der, vbase, 0, false});
  slot_address = builder_.createPtrStride(loc(), vtable, slot_offset);
  word_address = builder_.createBitcast(loc(), slot_address,
                                        compat::pointer_to(word));
  return builder_.createLoad(loc(), word_address, false, 8);
}  /* a_emitter::virtual_base_offset */

mlir::Value a_emitter::virtual_base_address(mlir::Value object, uint64_t der,
                                            uint64_t vbase,
                                            mlir::Type result_type,
                                            bool null_check)
/*
The address of virtual base `vbase` of the object `object` points to (a class
`der`), through the vtable: the object moved by the vbase offset.  Null stays
null when null_check is set (CIRGenFunction::getAddressOfBaseClass).
*/
{
  mlir::Type byte_pointer = compat::pointer_to(compat::int_type(ctx_, 8,
                                                                false));
  auto adjust = [&](mlir::Value pointer) {
    mlir::Value offset = virtual_base_offset(pointer, der, vbase);
    mlir::Value bytes = builder_.createBitcast(loc(), pointer, byte_pointer);
    mlir::Value moved = builder_.createPtrStride(loc(), bytes, offset);

    return builder_.createBitcast(loc(), moved, result_type);
  };
  mlir::Value result;

  if (!null_check) {
    result = adjust(object);
  } else {
    mlir::Value not_null = builder_.createPtrIsNotNull(object);

    result = cir::TernaryOp::create(
        builder_, loc(), not_null,
        [&](mlir::OpBuilder &, mlir::Location) {
          builder_.createYield(loc(), adjust(object));
        },
        [&](mlir::OpBuilder &, mlir::Location) {
          builder_.createYield(
              loc(), builder_.getNullPtr(result_type, loc()).getResult());
        }).getResult();
  }
  return result;
}  /* a_emitter::virtual_base_address */

bool a_emitter::early_vptr_store()
/*
The vtable pointers of the object, stored at the start of the function (after
its parameters) for the code that reads the vbase offset through them before
the VPTR_INIT of the body (clang stores VTT[0] at entry of a C2 whose base
initializers need it; VPTR_INIT stores again at its place).
*/
{
  mlir::OpBuilder::InsertionGuard guard(builder_);
  bool ok;

  vptr_set_early_ = true;
  builder_.setInsertionPointAfter(entry_mark_);
  ok = vptr_init_with_vbases();
  return ok;
}  /* a_emitter::early_vptr_store */

bool a_emitter::vbase_cast_record(const ecil::a_record &r)
/*
VBASE_CAST(t_der, t_vbase, flags): a pointer to a class becomes a pointer to
one of its virtual bases.  COMPLETE: the object is a complete object of the
class, the offset is the field's.  Otherwise the offset comes from the vtable
of the object, with a null check unless NONNULL.
*/
{
  bool ok = true;
  uint64_t der = 0;
  uint64_t vbase = 0;
  uint64_t flags = 0;
  a_type *from;
  a_type *to;
  mlir::Value pointer;

  uint_operand(r.ops[0], der);
  uint_operand(r.ops[1], vbase);
  uint_operand(r.ops[2], flags);
  from = type_at(der);
  to = type_at(vbase);
  if (from == nullptr || to == nullptr || !pop_prvalue(pointer)) {
    ok = false;
  } else if (!mlir::isa<cir::PointerType>(pointer.getType())) {
    ok = format_error("VBASE_CAST of a non-pointer");
  } else {
    bool nonnull = (flags & ECIL_CAST_NONNULL) != 0;
    mlir::Type source = compat::pointer_to(from->cir);
    mlir::Type result_type = compat::pointer_to(to->cir);

    if (pointer.getType() != source)
      pointer = builder_.createBitcast(loc(), pointer, source);
    if (flags & ECIL_VBASE_CAST_COMPLETE) {
      const ecil::a_class_facts *facts =
          ids_.class_facts(static_cast<uint32_t>(der));
      bool found = false;
      uint64_t offset = 0;

      for (size_t k = 0; facts != nullptr && k < facts->fields.size(); ++k) {
        const ecil::a_class_field &field = facts->fields[k];

        if ((field.flags & ECIL_FIELD_VIRTUAL_BASE) && field.type == vbase) {
          found = true;
          offset = field.offset;
        }
      }
      if (!found)
        ok = internal("VBASE_CAST COMPLETE: no VIRTUAL_BASE field");
      else
        push_prvalue(compat::class_address(builder_, loc(), false, pointer,
                                           result_type, offset, nonnull));
    } else {
      /* The offset is read from the vtable of the object: in a constructor
         whose base initializers convert `this` to a virtual base, the vptr
         is stored at entry too (PR 9629). */
      if (vtt_arg_ && !vptr_init_seen_ && !vptr_set_early_ &&
          entry_mark_ != nullptr && this_local_ != nullptr)
        ok = early_vptr_store();
      if (ok)
        push_prvalue(virtual_base_address(pointer, der, vbase, result_type,
                                          !nonnull));
    }
  }
  return ok;
}  /* a_emitter::vbase_cast_record */

bool a_emitter::vtt_addr_record(const ecil::a_record &r)
/*
VTT_ADDR(kind, idx|nil): the VTT operand of a call of a base-variant
structor: the address, inside the VTT of the function's class, of the
sub-VTT of itself, one of its bases or one of its virtual bases.  A function
that has the VTT as a parameter (C2, D2) indexes into it; the others (C1,
D1) name the class's _ZTT.  The index is a fact of the VTT layout, chosen at
finish.
*/
{
  bool ok = true;
  uint64_t kind = 0;
  uint64_t idx = 0;
  const ecil::a_class_facts *facts = nullptr;
  uint64_t sub = 0;
  int64_t offset = 0;

  uint_operand(r.ops[0], kind);
  if (this_class_ != 0)
    facts = ids_.class_facts(static_cast<uint32_t>(this_class_));
  if (facts == nullptr) {
    ok = internal("VTT_ADDR outside a function with this");
  } else if (kind != evak_self &&
             (!uint_operand(r.ops[1], idx) || idx >= facts->fields.size())) {
    ok = internal("VTT_ADDR out of step with the validator");
  } else if (kind == evak_self && vtt_arg_) {
    push_prvalue(vtt_arg_);
  } else {
    mlir::Operation *op;

    if (kind != evak_self) {
      sub = facts->fields[idx].type;
      offset = static_cast<int64_t>(facts->fields[idx].offset);
    }
    op = compat::vtt_address_point(
        builder_, loc(), vtt_parameter_type(),
        vtt_arg_ ? mlir::StringAttr()
                 : mlir::StringAttr::get(ctx_, "__ecil.vtt.pending"),
        vtt_arg_, 0);
    late_uses_.push_back({a_late_kind::vtt_sub, op, this_class_, sub, offset,
                          !vtt_arg_});
    push_prvalue(op->getResult(0));
  }
  return ok;
}  /* a_emitter::vtt_addr_record */

bool a_emitter::vptr_init_with_vbases()
/*
VPTR_INIT in a class with virtual bases: clang's InitializeVTablePointers.
Every subobject with a vptr of its own (getVTablePointers: the bases in
declaration order, a primary base skipped, each virtual base once, at its
offset in the complete object) gets its address point.  A structor that has
the VTT as a parameter (C2, D2) loads the address point from it for a
subobject that has virtual bases or lives in a virtual base, and stores it
through the offset of the virtual base read from the vtable (the virtual base
may sit elsewhere in the most derived object); the others use the vtable of
the class and static offsets.  The vtable and VTT indices are chosen at
finish.
*/
{
  bool ok = true;
  struct a_item {
    uint32_t  cls;
    int64_t   offset;
    uint32_t  nearest;          /* The nearest virtual base: 0 for none. */
    int64_t   from_nearest;
    bool      primary;          /* A non-virtual primary base. */
    bool      is_virtual;
  };
  struct a_vptr {
    uint32_t  cls;
    int64_t   offset;
    uint32_t  nearest;
    int64_t   from_nearest;
  };
  std::vector<a_item> stack;
  std::vector<a_vptr> vptrs;
  std::set<uint32_t> visited;
  const ecil::a_class_facts *root =
      ids_.class_facts(static_cast<uint32_t>(this_class_));
  mlir::Type byte_pointer = compat::pointer_to(compat::int_type(ctx_, 8,
                                                                false));
  mlir::Type word = compat::int_type(ctx_, 64, true);

  stack.push_back({static_cast<uint32_t>(this_class_), 0, 0, 0, false,
                   false});
  while (!stack.empty()) {
    a_item item = stack.back();
    const ecil::a_class_facts *f = ids_.class_facts(item.cls);

    stack.pop_back();
    if (f == nullptr || !f->dynamic)
      continue;
    if (item.is_virtual && !visited.insert(item.cls).second)
      continue;
    if (!item.primary)
      vptrs.push_back({item.cls, item.offset, item.nearest,
                       item.from_nearest});
    for (size_t k = f->base_entries.size(); k > 0; --k) {
      const ecil::a_class_base_entry &entry = f->base_entries[k - 1];
      const ecil::a_class_field &field = f->fields[entry.field];
      const ecil::a_class_facts *bf = ids_.class_facts(field.type);

      if (bf == nullptr || !bf->dynamic)
        continue;
      if (field.flags & ECIL_FIELD_VIRTUAL_BASE) {
        int64_t offset = 0;

        for (const ecil::a_class_field &candidate : root->fields) {
          if ((candidate.flags & ECIL_FIELD_VIRTUAL_BASE) &&
              candidate.type == field.type)
            offset = static_cast<int64_t>(candidate.offset);
        }
        stack.push_back({field.type, offset, field.type, 0, false, true});
      } else {
        int64_t at = static_cast<int64_t>(field.offset);

        stack.push_back({field.type, item.offset + at, item.nearest,
                         item.from_nearest + at,
                         f->primary_idx ==
                             static_cast<int64_t>(entry.field), false});
      }
    }
  }
  for (const a_vptr &vptr : vptrs) {
    const ecil::a_class_facts *bf = ids_.class_facts(vptr.cls);
    mlir::Value self = builder_.createLoad(loc(), this_local_->address, false,
                                           this_local_->align);
    mlir::Value address_point;
    mlir::Value address;
    mlir::Value slot;
    a_type *base = type_at(vptr.cls);

    if (vtt_arg_ && (!bf->vbases.empty() || vptr.nearest != 0)) {
      mlir::Operation *point = compat::vtt_address_point(
          builder_, loc(), vtt_parameter_type(), mlir::StringAttr(), vtt_arg_,
          0);
      mlir::Value element = builder_.createBitcast(
          loc(), point->getResult(0),
          compat::pointer_to(cir::VPtrType::get(ctx_)));

      late_uses_.push_back({a_late_kind::vtt_secondary, point, this_class_,
                            vptr.cls, vptr.offset, false});
      address_point = builder_.createLoad(loc(), element, false, 8);
    } else {
      mlir::Operation *point = compat::vtable_address_point(
          builder_, loc(), mlir::StringAttr::get(ctx_,
                                                 "__ecil.vtable.pending"),
          0, 0);

      vptr_uses_.push_back({point, this_class_, vptr.cls, vptr.offset});
      address_point = point->getResult(0);
    }
    if (vptr.nearest != 0 && vtt_arg_) {
      /* The virtual base may be elsewhere in the most derived object. */
      mlir::Value offset = virtual_base_offset(self, this_class_,
                                               vptr.nearest);
      mlir::Value bytes = builder_.createBitcast(loc(), self, byte_pointer);

      if (vptr.from_nearest != 0)
        offset = builder_.createAdd(
            loc(), offset,
            builder_.getConstantInt(loc(), word, vptr.from_nearest));
      address = builder_.createBitcast(
          loc(), builder_.createPtrStride(loc(), bytes, offset),
          compat::pointer_to(base->cir));
    } else if (vptr.offset != 0) {
      mlir::Value bytes = builder_.createBitcast(loc(), self, byte_pointer);

      address = builder_.createBitcast(
          loc(),
          builder_.createPtrStride(
              loc(), bytes,
              builder_.getConstantInt(loc(), word, vptr.offset)),
          compat::pointer_to(base->cir));
    } else {
      address = self;
    }
    slot = compat::vtable_get_vptr(builder_, loc(), address);
    builder_.createStore(loc(), address_point, slot, false, false,
                         alignment_attr(8));
  }
  return ok;
}  /* a_emitter::vptr_init_with_vbases */

bool a_emitter::patch_late_uses(
    edg_abi::a_vtable_context &ctx,
    const std::map<uint64_t, edg_abi::a_class_id> &by_key)
/*
Fill in what the layouts decide: the index of a sub-VTT or of a secondary
virtual pointer in the VTT, the VTT's own symbol, and the place of a virtual
base offset in a vtable.
*/
{
  bool ok = true;
  std::map<uint64_t, std::unique_ptr<edg_abi::a_vtt_layout>> vtt_layouts;

  for (const a_late_use &use : late_uses_) {
    auto owner = by_key.find(use.owner);
    std::string error;

    if (owner == by_key.end()) {
      ok = format_error("virtual-base code in a class whose facts were not "
                        "sent");
      break;
    }
    if (use.kind == a_late_kind::dyn_cast_hint) {
      auto dest = by_key.find(use.sub);

      if (dest == by_key.end()) {
        ok = format_error("DYNAMIC_CAST to a class whose facts were not "
                          "sent");
        break;
      }
      compat::set_dyn_cast_hint(use.op, edg_abi::dynamic_cast_offset_hint(
                                    ctx.db(), owner->second, dest->second));
    } else if (use.kind == a_late_kind::vbase_offset_offset) {
      auto vbase = by_key.find(use.sub);
      int64_t where = 0;

      if (vbase == by_key.end()) {
        ok = format_error("VBASE_CAST of a virtual base whose facts were "
                          "not sent");
        break;
      }
      where = ctx.virtual_base_offset_offset(owner->second, vbase->second,
                                             error);
      if (!error.empty()) {
        ok = format_error("virtual base offset: " + error);
        break;
      }
      compat::set_signed_constant(use.op, where);
    } else {
      auto layout = vtt_layouts.find(use.owner);
      uint64_t index = 0;
      mlir::StringAttr symbol;

      if (layout == vtt_layouts.end()) {
        std::unique_ptr<edg_abi::a_vtt_layout> made =
            ctx.vtt_layout(owner->second, false, error);

        if (!made) {
          ok = format_error("VTT of class '" + ctx.db().cls(owner->second)
                                .name + "': " + error);
          break;
        }
        layout = vtt_layouts.emplace(use.owner, std::move(made)).first;
      }
      if (use.sub != 0) {
        auto sub = by_key.find(use.sub);
        const auto &indices = use.kind == a_late_kind::vtt_sub
            ? layout->second->sub_vtt_indices
            : layout->second->secondary_virtual_pointer_indices;
        edg_abi::a_subobject key = {0, use.offset};
        auto found = indices.end();

        if (sub != by_key.end()) {
          key.cls = sub->second;
          found = indices.find(key);
        }
        if (found == indices.end()) {
          ok = format_error("the VTT of class '" + ctx.db().cls(owner->second)
                                .name + "' has no entry for the subobject "
                            "at offset " + std::to_string(use.offset));
          break;
        }
        index = found->second;
      }
      if (use.names_vtt) {
        a_abi_entity *vtt = abi_entity_of(eadk_vtt, use.owner);

        if (vtt == nullptr) {
          ok = format_error("a structor of class '" +
                            ctx.db().cls(owner->second).name +
                            "' uses its VTT, which the stream does not "
                            "declare (DECL_ABI_DATA vtt)");
          break;
        }
        symbol = mlir::cast<cir::GlobalOp>(vtt->entity->op).getSymNameAttr();
        vtt->entity->referenced = true;
      }
      compat::set_vtt_address_point(use.op, symbol,
                                    static_cast<uint32_t>(index));
    }
  }
  return ok;
}  /* a_emitter::patch_late_uses */

bool a_emitter::vtable_value(edg_abi::a_class_db &db,
                             const edg_abi::a_vtable_layout &layout,
                             cir::RecordType &record, mlir::Attribute &value)
/*
The type and the initializer of a vtable group: one array of entries per
vtable (CIRGenVTables::createVTableInitializer).
*/
{
  bool ok = true;
  mlir::Type byte_pointer = compat::pointer_to(compat::int_type(ctx_, 8,
                                                                false));
  std::map<size_t, const edg_abi::a_thunk_info *> thunks;
  std::vector<mlir::Type> members;
  std::vector<mlir::Attribute> arrays;

  for (const auto &t : layout.thunks)
    thunks[t.first] = &t.second;
  for (size_t i = 0; ok && i < layout.num_vtables(); ++i) {
    size_t first = layout.vtable_indices[i];
    size_t size = layout.vtable_size(i);
    mlir::Type array_type = compat::array_of(byte_pointer, size);
    std::vector<mlir::Attribute> elements;

    for (size_t k = first; ok && k < first + size; ++k)
      elements.push_back(vtable_component(db, layout, k, thunks, ok));
    if (ok) {
      arrays.push_back(cir::ConstArrayAttr::get(
          mlir::cast<cir::ArrayType>(array_type),
          builder_.getArrayAttr(elements)));
      members.push_back(array_type);
    }
  }
  if (ok) {
    record = compat::anonymous_record(ctx_, members);
    value = compat::vtable_attr(record, builder_.getArrayAttr(arrays));
  }
  return ok;
}  /* a_emitter::vtable_value */

a_entity *a_emitter::construction_vtable(
    edg_abi::a_vtable_context &ctx, edg_abi::a_class_db &db,
    edg_abi::a_class_id complete, const edg_abi::a_vtt_vtable &which,
    const a_entity &vtt, std::unique_ptr<edg_abi::a_vtable_layout> &layout,
    bool &ok)
/*
CIRGenVTables::generateConstructionVTable: the vtable group of a base
subobject as it is while the complete object is being constructed, _ZTC<the
complete class><offset>_<the base class>, with the VTT's linkage.
*/
{
  a_entity *result = nullptr;
  std::string error;

  ok = true;
  layout = ctx.construction_vtable_layout(which.base.cls, which.base.offset,
                                          which.base_is_virtual, complete,
                                          error);
  if (!layout) {
    ok = format_error("construction vtable of '" +
                      db.cls(which.base.cls).name + "' in '" +
                      db.cls(complete).name + "': " + error);
  } else {
    const ecil::a_class_facts *cf =
        ids_.class_facts(static_cast<uint32_t>(db.cls(complete).key));
    const ecil::a_class_facts *bf =
        ids_.class_facts(static_cast<uint32_t>(db.cls(which.base.cls).key));
    bool exact = true;
    std::string name = edg_abi::ctor_vtable_symbol(
        std::string(ids_.string(cf->mangle_sid)), which.base.offset,
        std::string(ids_.string(bf->mangle_sid)), exact);
    cir::RecordType record;
    mlir::Attribute value;

    ok = vtable_value(db, *layout, record, value);
    if (ok) {
      bool vague = vtt.linkage == elk_linkonce_odr ||
                   vtt.linkage == elk_weak_odr;
      bool known = true;
      cir::GlobalLinkageKind kind = compat::linkage_kind(vtt.linkage, known);
      cir::GlobalOp global;

      entity_store_.emplace_back();
      a_entity &e = entity_store_.back();
      e.linkage = vtt.linkage;
      if (vtt.linkage == elk_linkonce_odr)
        e.had_linkonce = true;
      e.flags = ECIL_DECL_VAR_CONST | ECIL_DECL_VAR_DEFINITION |
                (vague ? ECIL_DECL_VAR_COMDAT : 0u);
      e.align = 8;
      e.named = true;
      e.want = keep_string(name);
      e.symbol = claim_symbol(e.want, 0);
      global = compat::create_global(builder_, module_, loc(), e.symbol,
                                     record, true, kind);
      e.op = global.getOperation();
      global.setAlignmentAttr(alignment_attr(8));
      global.setInitialValueAttr(value);
      compat::set_dso_local(global.getOperation(), true);
      mlir::SymbolTable::setSymbolVisibility(
          global, compat::symbol_visibility(kind, false));
      e.has_init = true;
      entity_order_.push_back(&e);
      result = &e;
    }
  }
  return result;
}  /* a_emitter::construction_vtable */

bool a_emitter::finish_vtt(
    a_abi_entity &abi, edg_abi::a_vtable_context &ctx,
    edg_abi::a_class_db &db,
    const std::map<uint64_t, edg_abi::a_class_id> &by_key,
    const std::string &mangled)
/*
The VTT of a class with virtual bases (CIRGenVTables::emitVTTDefinition): an
array of the address points of its vtable and of the construction vtables of
its bases, in the order of the VTT layout.  Its type is known whether or not
it is defined here.
*/
{
  bool ok = true;
  a_entity &e = *abi.entity;
  cir::GlobalOp global = mlir::cast<cir::GlobalOp>(e.op);
  bool defined = (abi.flags & ECIL_ABI_DATA_DEFINED_HERE) != 0;
  auto found = by_key.find(abi.type);
  mlir::Type byte_pointer = compat::pointer_to(compat::int_type(ctx_, 8,
                                                                false));
  std::string error;

  e.want = keep_string(edg_abi::vtt_symbol(mangled));
  e.named = true;
  if (found == by_key.end()) {
    ok = format_error("the VTT of class '" + mangled + "' needs the facts of "
                      "its class");
  } else {
    std::unique_ptr<edg_abi::a_vtt_layout> layout =
        ctx.vtt_layout(found->second, defined, error);

    if (!layout) {
      ok = format_error("VTT of '" + mangled + "': " + error);
    } else {
      mlir::Type array_type = compat::array_of(byte_pointer,
                                               layout->components.size());

      global.setSymTypeAttr(mlir::TypeAttr::get(array_type));
      if (!defined) {
        compat::clear_initial_value(global);
      } else {
        const edg_abi::a_vtable_info &own = ctx.vtable_info(found->second,
                                                            error);
        std::vector<a_entity *> vtables;
        std::vector<std::unique_ptr<edg_abi::a_vtable_layout>> constructions;
        std::vector<const std::map<edg_abi::a_subobject,
                                   edg_abi::a_address_point> *> points;
        std::vector<mlir::Attribute> elements;

        if (!error.empty())
          ok = format_error("vtable of '" + mangled + "': " + error);
        for (size_t i = 0; ok && i < layout->vtables.size(); ++i) {
          const edg_abi::a_vtt_vtable &which = layout->vtables[i];

          constructions.emplace_back();
          if (which.base.cls == found->second && which.base.offset == 0) {
            a_abi_entity *vtable = abi_entity_of(eadk_vtable, abi.type);

            if (vtable == nullptr) {
              ok = format_error("the VTT of class '" + mangled + "' needs "
                                "the vtable of the class (DECL_ABI_DATA "
                                "vtable)");
              break;
            }
            vtables.push_back(vtable->entity);
            points.push_back(&own.layout.address_points);
          } else {
            a_entity *construction = construction_vtable(
                ctx, db, found->second, which, e, constructions.back(), ok);

            if (!ok)
              break;
            vtables.push_back(construction);
            points.push_back(&constructions.back()->address_points);
          }
        }
        for (size_t k = 0; ok && k < layout->components.size(); ++k) {
          const edg_abi::a_vtt_component &component = layout->components[k];
          auto point = points[component.vtable_index]->find(
              component.vtable_base);

          if (point == points[component.vtable_index]->end()) {
            ok = format_error("the VTT of class '" + mangled + "' has no "
                              "address point for its component " +
                              std::to_string(k));
            break;
          }
          a_entity *vtable = vtables[component.vtable_index];
          mlir::ArrayAttr indices = builder_.getArrayAttr(
              {builder_.getI32IntegerAttr(static_cast<int32_t>(
                   point->second.vtable_index)),
               builder_.getI32IntegerAttr(static_cast<int32_t>(
                   point->second.address_point_index))});

          vtable->referenced = true;
          elements.push_back(compat::global_view(
              byte_pointer, mlir::cast<cir::GlobalOp>(vtable->op)
                                .getSymNameAttr(), indices, true));
        }
        if (ok) {
          global.setInitialValueAttr(cir::ConstArrayAttr::get(
              mlir::cast<cir::ArrayType>(array_type),
              builder_.getArrayAttr(elements)));
          e.has_init = true;
        }
      }
    }
  }
  return ok;
}  /* a_emitter::finish_vtt */

bool a_emitter::finish_classes()
/*
Everything the class facts produce, before the final names are chosen: the
facts of the stream are decoded and checked (a failure is a fatal
diagnostic: the front end's slots against the builder's layout, the offsets,
the overriders), D0 bodies are made, the address points of the VPTR_INITs are
chosen, the vtables and typeinfo objects are typed and initialized, and the
thunks of the methods defined here are defined.
*/
{
  bool ok = true;

  if (!abi_order_.empty() || !vptr_uses_.empty() || !deleting_dtors_.empty() ||
      !vptr_markers_.empty() || !late_uses_.empty() || have_vtable_facts_) {
    edg_abi::a_class_db db;
    std::string problem;

    for (const a_structor_pair &pair : structors_) {
      if (pair.base->has_body)
        complete_defined_.insert(pair.complete);
    }
    if (!ecil::build_abi_facts(ids_, db, problem)) {
      ok = format_error("class facts: " + problem);
    } else {
      edg_abi::a_vtable_context ctx(db);
      std::map<uint64_t, edg_abi::a_class_id> by_key;

      for (edg_abi::a_class_id c = 0; c < db.classes.size(); ++c)
        by_key[db.classes[c].key] = c;
      for (a_deleting_dtor &d : deleting_dtors_)
        build_deleting_dtor(d);
      for (const a_vptr_use &use : vptr_uses_) {
        a_abi_entity *vtable = abi_entity_of(eadk_vtable,
                                             use.vtable_class);
        auto root = by_key.find(use.vtable_class);
        auto base = by_key.find(use.base_class);
        std::string error;

        if (vtable == nullptr || root == by_key.end() ||
            base == by_key.end()) {
          ok = format_error("a VPTR_INIT in a class whose vtable is not "
                            "declared (DECL_ABI_DATA) or whose vtable facts "
                            "were not sent");
          break;
        }
        const edg_abi::a_vtable_info &info = ctx.vtable_info(root->second,
                                                             error);
        auto point = info.layout.address_points.find(
            {base->second, use.base_offset});

        if (!error.empty() || point == info.layout.address_points.end()) {
          ok = format_error("no address point for the base subobject at "
                            "offset " + std::to_string(use.base_offset) +
                            " in the vtable of the class: " + error);
          break;
        }
        compat::set_vtable_address_point(
            use.op, mlir::cast<cir::GlobalOp>(vtable->entity->op)
                        .getSymNameAttr(),
            point->second.vtable_index, point->second.address_point_index);
        vtable->entity->referenced = true;
      }
      if (ok)
        ok = patch_late_uses(ctx, by_key);
      name_class_abi_entities();
      for (size_t k = 0; ok && k < abi_order_.size(); ++k)
        ok = finish_abi_entity(*abi_order_[k], ctx, db, by_key);
      if (ok)
        ok = patch_vptr_constants(ctx, db, by_key);
      /* The thunks of the virtual functions defined here (clang emits them
         along with the definition: CodeGenVTables::EmitThunks), for every
         vtable group whose layout the translation unit computed. */
      for (edg_abi::a_class_id c = 0; ok && c < db.classes.size(); ++c) {
        const edg_abi::a_class_fact &cf = db.classes[c];
        std::string error;

        if (!cf.is_dynamic || cf.layout_only)
          continue;
        const edg_abi::a_vtable_info &info = ctx.vtable_info(c, error);

        if (!error.empty()) {
          ok = format_error("vtable of '" + cf.name + "': " + error);
          break;
        }
        for (const auto &entry : info.thunks) {
          const edg_abi::a_fn_fact &fn = db.fn(entry.first);

          for (const edg_abi::a_thunk_info &thunk : entry.second) {
            llvm::SmallVector<a_entity *, 2> targets;

            if (fn.special == edg_abi::fs_destructor) {
              uint32_t d0 = 0;
              uint32_t del = 0;

              targets.push_back(function_of_key(fn.key));
              if (ids_.deleting_of(static_cast<uint32_t>(fn.key), d0, del))
                targets.push_back(function_of_key(d0));
            } else {
              targets.push_back(function_of_key(fn.key));
            }
            for (a_entity *target : targets) {
              if (target != nullptr && defined_here(*target))
                thunk_entity(thunk, *target, ok);
            }
          }
        }
      }
    }
  }
  return ok;
}  /* a_emitter::finish_classes */

}  /* namespace edg_cir */
