/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
members.cpp -- pointers to members (ECIL 1.2, M4 slice S3): the constants
(C_DATA_MEMBER, C_METHOD, CONST_DATA_MEMBER, CONST_METHOD), PM_FIELD,
PM_CALL and PM_CAST, and the null constants of types that are not
zero-initializable (12.7: a null pointer to a data member is -1).

Derived in part from llvm-project clang/lib/CIR/CodeGen (CIRGenExprCXX.cpp,
CIRGenExprScalar.cpp, CIRGenExprConstant.cpp), Apache-2.0 WITH LLVM-exception;
see CIRCompat.h for the upstream API surface used.
*/

#include <algorithm>

#include "emitter.h"

namespace edg_cir {

/* Zero-initializable types (12.7). */

bool a_emitter::zero_initializable(mlir::Type type)
/*
True unless a pointer to a data member (null is -1) is anywhere in the type:
a field, a base, an array element.  A walk with an explicit stack; the answer
is cached per type.
*/
{
  bool result = true;
  const void *key = type.getAsOpaquePointer();
  auto cached = zero_init_cache_.find(key);

  if (cached != zero_init_cache_.end()) {
    result = cached->second;
  } else {
    std::vector<mlir::Type> stack;
    std::unordered_set<const void *> seen;

    stack.push_back(type);
    while (result && !stack.empty()) {
      mlir::Type t = stack.back();

      stack.pop_back();
      if (!seen.insert(t.getAsOpaquePointer()).second)
        continue;
      if (mlir::isa<cir::DataMemberType>(t)) {
        result = false;
      } else if (auto array = mlir::dyn_cast<cir::ArrayType>(t)) {
        stack.push_back(array.getElementType());
      } else if (auto record = mlir::dyn_cast<cir::RecordType>(t)) {
        if (record.isComplete()) {
          for (mlir::Type member : record.getMembers())
            stack.push_back(member);
        }
      }
    }
    zero_init_cache_[key] = result;
  }
  return result;
}  /* a_emitter::zero_initializable */

mlir::Attribute a_emitter::null_attribute(mlir::Type type)
/*
The null constant of a type (CIRGen's EmitNullConstant): the zero constant,
except that a pointer to a data member is #cir.data_member<null>, in a
record, a base or an array element too.  Post-order over the types with an
explicit stack; the attribute of each type is cached.
*/
{
  std::vector<std::pair<mlir::Type, bool>> stack;

  stack.push_back({type, false});
  while (!stack.empty()) {
    mlir::Type t = stack.back().first;
    bool expanded = stack.back().second;
    const void *key = t.getAsOpaquePointer();
    llvm::SmallVector<mlir::Type> children;

    stack.pop_back();
    if (null_cache_.count(key) != 0)
      continue;
    if (zero_initializable(t)) {
      null_cache_[key] = builder_.getZeroInitAttr(t);
      continue;
    }
    if (mlir::isa<cir::DataMemberType>(t)) {
      null_cache_[key] = compat::data_member_attr(t, {}, true);
      continue;
    }
    if (auto array = mlir::dyn_cast<cir::ArrayType>(t)) {
      children.push_back(array.getElementType());
    } else if (auto record = mlir::dyn_cast<cir::RecordType>(t)) {
      for (mlir::Type member : record.getMembers()) {
        if (compat::member_owns_bytes(member))
          children.push_back(compat::member_storage_type(member));
      }
    }
    if (!expanded) {
      stack.push_back({t, true});
      for (mlir::Type child : children) {
        if (null_cache_.count(child.getAsOpaquePointer()) == 0)
          stack.push_back({child, false});
      }
      continue;
    }
    if (auto array = mlir::dyn_cast<cir::ArrayType>(t)) {
      mlir::Attribute element = null_cache_[
          array.getElementType().getAsOpaquePointer()];
      std::vector<mlir::Attribute> elements(array.getSize(), element);

      null_cache_[key] = cir::ConstArrayAttr::get(
          array, builder_.getArrayAttr(elements));
    } else if (auto record = mlir::cast<cir::RecordType>(t);
               record.isUnion()) {
      /* CodeGenModule::EmitNullConstant: only the first member of a union is
         filled in (its null constant); the rest is zero. */
      mlir::Attribute first;

      for (mlir::Type member : record.getMembers()) {
        if (compat::member_owns_bytes(member)) {
          mlir::Type storage = compat::member_storage_type(member);

          first = null_cache_[storage.getAsOpaquePointer()];
          break;
        }
      }
      if (first)
        null_cache_[key] = cir::ConstRecordAttr::get(
            record, builder_.getArrayAttr({first}));
      else
        null_cache_[key] = cir::ZeroAttr::get(t);
    } else {
      std::vector<mlir::Attribute> members;

      for (mlir::Type child : children)
        members.push_back(null_cache_[child.getAsOpaquePointer()]);
      null_cache_[key] = cir::ConstRecordAttr::get(
          record, builder_.getArrayAttr(members));
    }
  }
  return null_cache_[type.getAsOpaquePointer()];
}  /* a_emitter::null_attribute */

/* Data members. */

bool a_emitter::data_member_path(uint64_t class_gid, uint64_t offset,
                                 mlir::Type member_type,
                                 std::vector<int32_t> &path,
                                 bool *empty_member)
/*
The CIR member indices that lead from the class down to the data member of
the given type at the given byte offset of the complete class (the member's
own offset plus that of the non-virtual base subobject that holds it): the
BASE fields and the unnamed struct and union members are descended (empty
bases have no CIR member and are skipped), and the member type selects among
members at one offset (the variants of a union).  An empty member that has no
CIR field ([[no_unique_address]]) is found too: *empty_member is set and the
path is empty (the constant is then a byte offset).  False when there is no
such member.
*/
{
  bool ok = true;
  bool found = false;
  a_type *class_type = type_at(class_gid);
  const a_record_info *rec = class_type != nullptr ? class_type->record
                                                   : nullptr;
  bool as_base = false;
  uint64_t rel = offset;

  path.clear();
  while (ok && !found && rec != nullptr) {
    const std::vector<uint32_t> &index = as_base ? rec->base_member_index
                                                 : rec->member_index;
    size_t leaf = rec->fields.size();
    size_t base = rec->fields.size();
    size_t anonymous = rec->fields.size();

    for (size_t i = 0; i < rec->fields.size(); ++i) {
      const a_field &f = rec->fields[i];
      a_type *ft = type_at(f.type);

      if (f.is_bitfield || f.is_vbase || ft == nullptr)
        continue;
      if (f.omitted && (f.is_base || leaf != rec->fields.size()))
        continue;
      if (f.is_base) {
        const a_record_info *child = ft->record;

        if (base == rec->fields.size() && rel >= f.byte_offset &&
            rel < f.byte_offset + std::max<uint64_t>(child->nvsize, 1))
          base = i;
      } else if (leaf == rec->fields.size() && f.byte_offset == rel &&
                 ft->cir == member_type) {
        leaf = i;
      } else if (f.is_unnamed && ft->record != nullptr &&
                 anonymous == rec->fields.size() && rel >= f.byte_offset &&
                 rel < f.byte_offset + std::max<uint64_t>(ft->record->size,
                                                          1)) {
        anonymous = i;   /* An anonymous struct or union member. */
      }
    }
    if (leaf != rec->fields.size() && index[leaf] != no_member) {
      path.push_back(static_cast<int32_t>(index[leaf]));
      found = true;
    } else if (leaf != rec->fields.size()) {
      /* A member that has no CIR field (an empty [[no_unique_address]] one):
         designated by its byte offset. */
      if (empty_member != nullptr)
        *empty_member = true;
      found = true;
    } else if (base != rec->fields.size() && index[base] != no_member) {
      a_type *bt = type_at(rec->fields[base].type);

      base_subobject_type(*bt);   /* Its member indices, if it has a .base. */
      path.push_back(static_cast<int32_t>(index[base]));
      rel -= rec->fields[base].byte_offset;
      as_base = bt->record->nvsize < bt->record->size;
      rec = bt->record;
    } else if (anonymous != rec->fields.size() &&
               index[anonymous] != no_member) {
      a_type *at = type_at(rec->fields[anonymous].type);

      path.push_back(static_cast<int32_t>(index[anonymous]));
      rel -= rec->fields[anonymous].byte_offset;
      as_base = false;
      rec = at->record;
    } else {
      ok = false;
    }
  }
  return ok && found;
}  /* a_emitter::data_member_path */

mlir::Attribute a_emitter::data_member_constant(a_type &t,
                                                const ecil::an_operand &offset,
                                                bool &ok)
/*
The constant of a pointer to a data member: null (offset nil), or the path to
the member at the byte offset.
*/
{
  mlir::Attribute result;
  uint64_t value = 0;

  ok = true;
  if (offset.kind == ecil::an_operand_kind::nil) {
    result = compat::data_member_attr(t.cir, {}, true);
  } else if (!uint_operand(offset, value)) {
    ok = internal("data member offset out of step with the validator");
  } else {
    std::vector<int32_t> path;
    a_type *member = type_at(t.target);
    bool empty_member = false;

    if (member == nullptr || !data_member_path(t.count, value, member->cir,
                                                path, &empty_member)) {
      ok = format_error("no data member of that type at offset " +
                        std::to_string(value) + " of the class (G-id " +
                        std::to_string(t.count) + ") of a pointer to data "
                        "member constant");
    } else if (empty_member) {
      result = compat::data_member_offset_attr(t.cir, value);
    } else {
      result = compat::data_member_attr(t.cir, path, false);
    }
  }
  return result;
}  /* a_emitter::data_member_constant */

/* Member functions. */

mlir::Type a_emitter::method_type_in_class_of(a_entity &fn)
/*
The type of a pointer to member function in the class `this` of the function
points to.
*/
{
  mlir::Type result;
  a_type *ft = type_at(fn.type);

  if (ft != nullptr && ft->kind == a_type_kind::function_type &&
      !ft->params.empty()) {
    a_type *self = type_at(ft->params[0]);

    if (self != nullptr && self->kind == a_type_kind::pointer_type) {
      a_type *cls = type_at(self->target);

      if (cls != nullptr && cls->kind == a_type_kind::record_type)
        result = compat::method_type(mlir::cast<cir::FuncType>(ft->cir),
                                     cls->cir);
    }
  }
  return result;
}  /* a_emitter::method_type_in_class_of */

mlir::Attribute a_emitter::method_constant(a_type &t, const ecil::a_record &r,
                                           bool in_function, bool &ok,
                                           bool &lowered)
/*
The constant of C_METHOD(t, g|nil, adj, flags) or CONST_METHOD: #cir.method
of the function's symbol, of its vtable offset (VIRTUAL: slot * 8) or null.
A #cir.method cannot hold the this-adjustment (G2): in a function the
constant is made in the function's own class and adjusted at run time
(derived_method); in a variable initializer it is the lowered {i64, i64}
(lowered is set).
*/
{
  mlir::Attribute result;
  uint64_t adj = 0;
  uint64_t flags = 0;
  uint64_t g = 0;
  a_entity *fn = nullptr;
  uint32_t slot = 0;
  bool is_virtual;

  ok = true;
  lowered = false;
  uint_operand(r.ops[2], adj);
  uint_operand(r.ops[3], flags);
  is_virtual = (flags & ECIL_METHOD_VIRTUAL) != 0;
  if (r.ops[1].kind != ecil::an_operand_kind::nil) {
    uint_operand(r.ops[1], g);
    fn = entity_for(g, true);
    if (fn == nullptr)
      ok = false;
    else if (is_virtual && !ids_.vslot_of(static_cast<uint32_t>(g), slot))
      ok = internal("a virtual method constant of a function without a "
                    "slot");
  }
  if (ok) {
    mlir::Type own_type = t.cir;

    if (fn != nullptr && adj != 0 && in_function) {
      own_type = method_type_in_class_of(*fn);
      if (!own_type)
        ok = format_error("a method constant whose function has no class");
    }
    if (!ok) {
      /* Reported. */
    } else if (fn == nullptr) {
      result = compat::method_attr_null(t.cir);
    } else if (adj != 0 && !in_function) {
      /* {symbol or vtable offset + 1, adjustment} of the lowered form. */
      mlir::Type word = compat::int_type(ctx_, 64, true);
      mlir::Attribute first;

      if (is_virtual) {
        first = cir::IntAttr::get(word, static_cast<int64_t>(slot) * 8 + 1);
      } else {
        fn->referenced = true;
        address_uses_.push_back({fn_entity_, fn});
        first = cir::GlobalViewAttr::get(
            word, mlir::FlatSymbolRefAttr::get(
                      mlir::cast<cir::FuncOp>(fn->op).getSymNameAttr()));
      }
      result = cir::ConstRecordAttr::get(
          mlir::cast<cir::RecordType>(lowered_method_type()),
          builder_.getArrayAttr(
              {first, cir::IntAttr::get(word, static_cast<int64_t>(adj))}));
      lowered = true;
    } else if (is_virtual) {
      result = compat::method_attr_virtual(own_type, slot * 8);
    } else {
      fn->referenced = true;
      address_uses_.push_back({fn_entity_, fn});
      result = compat::method_attr_symbol(
          own_type, mlir::cast<cir::FuncOp>(fn->op).getSymNameAttr());
    }
  }
  return result;
}  /* a_emitter::method_constant */

mlir::Type a_emitter::lowered_method_type()
/*
The lowered form of a pointer to member function: { i64, i64 } (CXXABI
lowering's record).
*/
{
  mlir::Type word = compat::int_type(ctx_, 64, true);

  return compat::anonymous_record(ctx_, {word, word});
}  /* a_emitter::lowered_method_type */

mlir::Attribute a_emitter::lowered_method_attr(mlir::Attribute attr)
/*
The lowered {i64, i64} constant of a #cir.method (adjustment 0): the symbol or
vtable offset + 1, and 0.
*/
{
  mlir::Type word = compat::int_type(ctx_, 64, true);
  auto method = mlir::cast<cir::MethodAttr>(attr);
  mlir::Attribute first = cir::IntAttr::get(word, 0);

  if (method.getSymbol())
    first = cir::GlobalViewAttr::get(word, *method.getSymbol());
  else if (method.getVtableOffset())
    first = cir::IntAttr::get(word,
                              static_cast<int64_t>(*method.getVtableOffset()) +
                                  1);
  return cir::ConstRecordAttr::get(
      mlir::cast<cir::RecordType>(lowered_method_type()),
      builder_.getArrayAttr({first, cir::IntAttr::get(word, 0)}));
}  /* a_emitter::lowered_method_attr */

mlir::Type a_emitter::lowered_type_of(mlir::Type type)
/*
The type of a constant in the lowered form (G2): every pointer to member
function becomes { i64, i64 }; arrays and records that hold one are rebuilt
(a record as the identified "<name>.lowered", with the same layout).  A type
without pointers to member functions is its own lowered type.  Post-order
over the type with an explicit stack; the answer for each type is cached.
*/
{
  std::vector<std::pair<mlir::Type, bool>> stack;

  stack.push_back({type, false});
  while (!stack.empty()) {
    mlir::Type t = stack.back().first;
    bool expanded = stack.back().second;
    const void *key = t.getAsOpaquePointer();
    llvm::SmallVector<mlir::Type> children;

    stack.pop_back();
    if (lowered_cache_.count(key) != 0)
      continue;
    if (mlir::isa<cir::MethodType>(t)) {
      lowered_cache_[key] = lowered_method_type();
      continue;
    }
    if (auto array = mlir::dyn_cast<cir::ArrayType>(t)) {
      children.push_back(array.getElementType());
    } else if (auto record = mlir::dyn_cast<cir::RecordType>(t);
               record && record.isComplete()) {
      for (mlir::Type member : record.getMembers())
        children.push_back(member);
    } else {
      lowered_cache_[key] = t;
      continue;
    }
    if (!expanded) {
      stack.push_back({t, true});
      for (mlir::Type child : children) {
        if (lowered_cache_.count(child.getAsOpaquePointer()) == 0)
          stack.push_back({child, false});
      }
      continue;
    }
    mlir::Type result = t;

    if (auto array = mlir::dyn_cast<cir::ArrayType>(t)) {
      mlir::Type element = lowered_cache_[
          array.getElementType().getAsOpaquePointer()];

      if (element != array.getElementType())
        result = compat::array_of(element, array.getSize());
    } else {
      auto record = mlir::cast<cir::RecordType>(t);
      llvm::SmallVector<mlir::Type> members;
      bool changed = false;

      for (mlir::Type member : record.getMembers()) {
        mlir::Type lowered = lowered_cache_[member.getAsOpaquePointer()];

        changed = changed || lowered != member;
        members.push_back(lowered);
      }
      if (changed)
        result = compat::lowered_record(ctx_, record, members);
    }
    lowered_cache_[key] = result;
  }
  return lowered_cache_[type.getAsOpaquePointer()];
}  /* a_emitter::lowered_type_of */

namespace {

struct a_lower_frame {
  mlir::Attribute  attr;
  mlir::Type       type;      /* The original type of attr. */
  bool             is_array;
  std::vector<std::pair<mlir::Attribute, mlir::Type>>
                   kids;
  size_t           next = 0;
  std::vector<mlir::Attribute>
                   out;
};  /* a_lower_frame */

}  /* anonymous namespace */

mlir::Attribute a_emitter::lower_constant(mlir::Attribute attr,
                                          mlir::Type type)
/*
The constant attr of the given type in the lowered form of lowered_type_of
(a constant that is already lowered is not given here).  The members of an
array or record constant are lowered one by one with an explicit stack of
frames.
*/
{
  std::vector<a_lower_frame> stack;
  mlir::Attribute result;

  /* make: the lowered attribute of a leaf, or null for a container (then
     the frame is made).  A constant of a type that needs no lowering is
     itself. */
  auto leaf = [&](mlir::Attribute a, mlir::Type t, bool &done)
      -> mlir::Attribute {
    mlir::Attribute r = a;

    done = true;
    if (lowered_type_of(t) == t) {
      /* Nothing to lower. */
    } else if (mlir::isa<cir::MethodAttr>(a)) {
      r = lowered_method_attr(a);
    } else if (mlir::isa<cir::MethodType>(t)) {
      /* The null pointer to member function, not given as a #cir.method. */
      r = lowered_method_attr(compat::method_attr_null(t));
    } else if (mlir::isa<cir::ZeroAttr>(a)) {
      r = cir::ZeroAttr::get(lowered_type_of(t));
    } else if (mlir::isa<cir::ConstArrayAttr, cir::ConstRecordAttr>(a)) {
      done = false;
    }
    return r;
  };
  auto make_frame = [&](mlir::Attribute a, mlir::Type t) {
    a_lower_frame f;

    f.attr = a;
    f.type = t;
    f.is_array = mlir::isa<cir::ConstArrayAttr>(a);
    if (auto array = mlir::dyn_cast<cir::ConstArrayAttr>(a)) {
      auto array_type = mlir::cast<cir::ArrayType>(t);

      for (mlir::Attribute element :
           mlir::cast<mlir::ArrayAttr>(array.getElts()))
        f.kids.push_back({element, array_type.getElementType()});
    } else {
      auto agg = mlir::cast<cir::ConstRecordAttr>(a);
      auto record = mlir::cast<cir::RecordType>(t);
      size_t next_member = 0;

      for (mlir::Attribute element : agg.getMembers()) {
        mlir::Type member_type;

        if (record.isUnion()) {
          /* One member: the one whose type the constant has. */
          auto typed = mlir::cast<mlir::TypedAttr>(element);

          for (mlir::Type m : record.getMembers()) {
            if (!member_type && compat::member_owns_bytes(m) &&
                compat::member_storage_type(m) == typed.getType())
              member_type = typed.getType();
          }
        } else {
          while (next_member < record.getMembers().size() &&
                 !compat::member_owns_bytes(record.getMembers()[next_member]))
            ++next_member;
          if (next_member < record.getMembers().size())
            member_type = compat::member_storage_type(
                record.getMembers()[next_member]);
          ++next_member;
        }
        f.kids.push_back({element, member_type});
      }
    }
    stack.push_back(std::move(f));
  };
  bool done = false;

  result = leaf(attr, type, done);
  if (!done)
    make_frame(attr, type);
  while (!stack.empty()) {
    a_lower_frame &top = stack.back();

    if (top.next < top.kids.size()) {
      auto kid = top.kids[top.next++];
      bool kid_done = false;
      mlir::Attribute r = kid.second ? leaf(kid.first, kid.second, kid_done)
                                     : kid.first;

      if (!kid.second || kid_done)
        top.out.push_back(r);
      else
        make_frame(kid.first, kid.second);   /* top may move: not used */
    } else {
      mlir::Type lowered = lowered_type_of(top.type);
      mlir::Attribute built;

      if (top.is_array)
        built = cir::ConstArrayAttr::get(mlir::cast<cir::ArrayType>(lowered),
                                         builder_.getArrayAttr(top.out));
      else
        built = cir::ConstRecordAttr::get(mlir::cast<cir::RecordType>(lowered),
                                          builder_.getArrayAttr(top.out));
      stack.pop_back();
      if (stack.empty())
        result = built;
      else
        stack.back().out.push_back(built);
    }
  }
  return result;
}  /* a_emitter::lower_constant */

/* The records. */

bool a_emitter::member_pointer_record(const ecil::a_record &r)
/*
CONST_DATA_MEMBER, CONST_METHOD, PM_FIELD, PM_CALL and PM_CAST.
*/
{
  bool ok = true;
  uint64_t tgid = 0;
  a_type *t = nullptr;

  switch (r.opcode) {
  case eop_const_data_member: {
    mlir::Attribute attr;

    uint_operand(r.ops[0], tgid);
    t = type_at(tgid);
    if (t == nullptr) {
      ok = false;
    } else {
      attr = data_member_constant(*t, r.ops[1], ok);
      if (ok)
        push_prvalue(get_constant_value(attr));
    }
    break;
  }
  case eop_const_method: {
    mlir::Attribute attr;
    bool lowered = false;
    uint64_t adj = 0;

    uint_operand(r.ops[0], tgid);
    t = type_at(tgid);
    if (t == nullptr) {
      ok = false;
    } else {
      attr = method_constant(*t, r, true, ok, lowered);
      if (ok) {
        mlir::Value value = get_constant_value(attr);

        uint_operand(r.ops[2], adj);
        if (adj != 0 && value.getType() != t->cir)
          value = compat::convert_member_pointer(builder_, loc(), true, true,
                                                 t->cir, value, adj);
        push_prvalue(value);
      }
    }
    break;
  }
  case eop_pm_field: {
    mlir::Value member;
    mlir::Value object;

    uint_operand(r.ops[0], tgid);
    t = type_at(tgid);
    if (t == nullptr || !pop_prvalue(member) || !pop_prvalue(object)) {
      ok = false;
    } else if (!mlir::isa<cir::PointerType>(object.getType()) ||
               member.getType() != t->cir) {
      ok = format_error("PM_FIELD needs an object pointer and a pointer to "
                        "data member of its type");
    } else {
      mlir::Type wanted = compat::pointer_to(type_at(t->count)->cir);
      a_type *member_type = type_at(t->target);

      if (object.getType() != wanted)
        object = builder_.createBitcast(loc(), object, wanted);
      push_glvalue(compat::get_runtime_member(builder_, loc(), object,
                                              member),
                   static_cast<uint32_t>(type_align(member_type->cir)));
    }
    break;
  }
  case eop_pm_cast: {
    uint64_t kind = 0;
    uint64_t from = 0;
    uint64_t to = 0;
    uint64_t offset = 0;
    mlir::Value value;
    a_type *ft;

    uint_operand(r.ops[0], kind);
    uint_operand(r.ops[1], from);
    uint_operand(r.ops[2], to);
    uint_operand(r.ops[3], offset);
    ft = type_at(from);
    t = type_at(to);
    if (ft == nullptr || t == nullptr || !pop_prvalue(value)) {
      ok = false;
    } else if (value.getType() != ft->cir) {
      ok = format_error("PM_CAST: the operand does not have the source "
                        "member pointer type");
    } else {
      push_prvalue(compat::convert_member_pointer(
          builder_, loc(), kind == epck_to_derived,
          ft->kind == a_type_kind::method_type, t->cir, value, offset));
    }
    break;
  }
  case eop_pm_call:
    ok = pm_call_record(r);
    break;
  default:
    ok = format_error("unexpected member pointer record");
    break;
  }
  return ok;
}  /* a_emitter::member_pointer_record */

bool a_emitter::pm_call_record(const ecil::a_record &r)
/*
PM_CALL(t_call, nargs, flags): the object pointer, the method pointer, then
the other arguments.  cir.get_method resolves the pointer to the function
and the adjusted `this`, and the call is indirect.
*/
{
  bool ok = true;
  uint64_t tcall = 0;
  uint64_t nargs = 0;
  uint64_t flags = 0;
  a_type *call_type;
  llvm::SmallVector<mlir::Value> popped;
  llvm::SmallVector<mlir::NamedAttribute> attrs;

  uint_operand(r.ops[0], tcall);
  uint_operand(r.ops[1], nargs);
  uint_operand(r.ops[2], flags);
  call_type = type_at(tcall);
  if (call_type == nullptr) {
    ok = false;
  } else if (nargs + 1 > values_in_frame()) {
    ok = format_error("PM_CALL needs more values than the value stack "
                      "holds");
  } else {
    popped.resize(static_cast<size_t>(nargs) + 1);
    for (size_t k = popped.size(); ok && k > 0; --k)
      ok = pop_prvalue(popped[k - 1]);
  }
  if (ok) {
    mlir::Value object = popped[0];
    mlir::Value method = popped[1];
    auto method_type = mlir::dyn_cast<cir::MethodType>(method.getType());

    if (!method_type || !mlir::isa<cir::PointerType>(object.getType())) {
      ok = format_error("PM_CALL needs an object pointer and a method "
                        "pointer");
    } else {
      mlir::Type wanted = compat::pointer_to(method_type.getClassTy());
      llvm::SmallVector<mlir::Value> args;
      llvm::SmallVector<mlir::Value> rest(popped.begin() + 2, popped.end());
      cir::FuncType callee_type;

      if (object.getType() != wanted)
        object = builder_.createBitcast(loc(), object, wanted);
      auto resolved = builder_.createGetMethod(loc(), method, object);

      callee_type = mlir::cast<cir::FuncType>(
          mlir::cast<cir::PointerType>(resolved.callee.getType())
              .getPointee());
      args.push_back(resolved.adjustedThis);
      for (mlir::Value v : rest)
        args.push_back(v);
      if (args.size() != callee_type.getNumInputs()) {
        ok = format_error("PM_CALL: the arguments do not fit the method "
                          "type");
      } else {
        for (unsigned k = 1; k < args.size(); ++k) {
          mlir::Type want = callee_type.getInput(k);

          if (args[k].getType() != want &&
              mlir::isa<cir::PointerType>(args[k].getType()) &&
              mlir::isa<cir::PointerType>(want))
            args[k] = builder_.createBitcast(loc(), args[k], want);
        }
        ok = check_class_arguments(args);
      }
      if (ok) {
        cir::CallOp op;

        if (is_nothrow_call(flags, nullptr))
          attrs.push_back(nothrow_attr());
        else if (!throws_.empty())
          ok = materialize_throws();
        op = builder_.createIndirectCallOp(loc(), resolved.callee,
                                           callee_type, args, attrs);
        if (op.getNumResults() == 1)
          push_prvalue(op.getResult());
      }
    }
  }
  return ok;
}  /* a_emitter::pm_call_record */

}  /* namespace edg_cir */
