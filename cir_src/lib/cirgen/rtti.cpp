/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
rtti.cpp -- RTTI (ECIL 1.2, M4 slice S3): the typeinfo objects of types that
are not classes (TYPEINFO_DEF: the std::type_info classes of the C++ runtime,
Itanium C++ ABI 2.9.5), typeid (static, and through the vtable with the null
check) and dynamic_cast (cir.dyn_cast with clang's offset hint).

Derived in part from llvm-project clang/lib/CIR/CodeGen (CIRGenCXXABI.cpp,
CIRGenItaniumCXXABI.cpp, CIRGenExprCXX.cpp), Apache-2.0 WITH LLVM-exception;
see CIRCompat.h for the upstream API surface used.
*/

#include "abi_rtti.h"
#include "emitter.h"

namespace edg_cir {

namespace {

const char *runtime_vtable_symbol(unsigned kind)
/*
The vtable of the abi:: class that describes a type that is not a class: its
address point (+16 bytes) is the first word of the typeinfo object.
*/
{
  const char *result = "_ZTVN10__cxxabiv117__array_type_infoE";

  switch (kind) {
  case etik_pointer:
    result = "_ZTVN10__cxxabiv119__pointer_type_infoE";
    break;
  case etik_member_pointer:
    result = "_ZTVN10__cxxabiv129__pointer_to_member_type_infoE";
    break;
  case etik_function:
    result = "_ZTVN10__cxxabiv120__function_type_infoE";
    break;
  case etik_enum:
    result = "_ZTVN10__cxxabiv116__enum_type_infoE";
    break;
  default:
    break;
  }
  return result;
}  /* runtime_vtable_symbol */

}  /* anonymous namespace */

/* Typeinfo of types that are not classes. */

bool a_emitter::typeinfo_def_record(const ecil::a_record &r)
/*
TYPEINFO_DEF(g_ti, kind, flags, g_a|nil, g_b|nil): the content of a typeinfo
that DECL_ABI_DATA declared; the object is built at finish.
*/
{
  bool ok = true;
  uint64_t g = 0;
  uint64_t kind = 0;
  uint64_t flags = 0;
  uint64_t a = 0;
  uint64_t b = 0;
  ecil::a_gid_entry *entry;

  uint_operand(r.ops[0], g);
  uint_operand(r.ops[1], kind);
  uint_operand(r.ops[2], flags);
  entry = ids_.find_gid(g);
  if (entry == nullptr || entry->payload == nullptr) {
    ok = internal("TYPEINFO_DEF of an unknown typeinfo");
  } else {
    a_abi_entity &abi = *static_cast<a_abi_entity *>(entry->payload);

    if (r.ops[3].kind != ecil::an_operand_kind::nil)
      uint_operand(r.ops[3], a);
    if (r.ops[4].kind != ecil::an_operand_kind::nil)
      uint_operand(r.ops[4], b);
    abi.has_def = true;
    abi.def_kind = static_cast<unsigned>(kind);
    abi.def_flags = static_cast<uint32_t>(flags);
    abi.def_a = a;
    abi.def_b = b;
  }
  return ok;
}  /* a_emitter::typeinfo_def_record */

a_entity *a_emitter::typeinfo_name_global(a_entity &typeinfo,
                                          const std::string &symbol,
                                          const std::string &text)
/*
The _ZTS global of a typeinfo: the characters of the type's name, with the
typeinfo's linkage (and comdat when that linkage is vague).
*/
{
  bool vague = typeinfo.linkage == elk_linkonce_odr ||
               typeinfo.linkage == elk_weak_odr;
  mlir::Type char_type = compat::int_type(ctx_, 8, true);
  mlir::Type array_type = compat::array_of(char_type, text.size() + 1);
  bool known = true;
  cir::GlobalLinkageKind kind = compat::linkage_kind(typeinfo.linkage, known);
  cir::GlobalOp global;

  entity_store_.emplace_back();
  a_entity &name_entity = entity_store_.back();
  name_entity.linkage = typeinfo.linkage;
  if (typeinfo.linkage == elk_linkonce_odr)
    name_entity.had_linkonce = true;
  name_entity.flags = ECIL_DECL_VAR_CONST | ECIL_DECL_VAR_DEFINITION |
                      (vague ? ECIL_DECL_VAR_COMDAT : 0u);
  name_entity.align = 1;
  name_entity.named = true;
  name_entity.want = keep_string(symbol);
  name_entity.symbol = claim_symbol(name_entity.want, 0);
  global = compat::create_global(builder_, module_, loc(),
                                 name_entity.symbol, array_type, true, kind);
  name_entity.op = global.getOperation();
  global.setAlignmentAttr(alignment_attr(1));
  global.setInitialValueAttr(string_attribute(
      reinterpret_cast<const uint8_t *>(text.data()),
      static_cast<uint32_t>(text.size() + 1), array_type));
  compat::set_dso_local(global.getOperation(), true);
  mlir::SymbolTable::setSymbolVisibility(
      global, compat::symbol_visibility(kind, false));
  name_entity.has_init = true;
  entity_order_.push_back(&name_entity);
  return &name_entity;
}  /* a_emitter::typeinfo_name_global */

bool a_emitter::finish_plain_typeinfo(a_abi_entity &abi,
                                      edg_abi::a_class_db &db)
/*
The typeinfo object of a type that is not a class: __fundamental_type_info
(an external declaration), __pointer_type_info and
__pointer_to_member_type_info with the pbase masks, __function_type_info,
__array_type_info or __enum_type_info; the name string (_ZTS, with the '*' of
a TU-local type).
*/
{
  bool ok = true;
  a_entity &e = *abi.entity;
  cir::GlobalOp global = mlir::cast<cir::GlobalOp>(e.op);
  bool defined = (abi.flags & ECIL_ABI_DATA_DEFINED_HERE) != 0;
  mlir::Type byte_pointer = compat::pointer_to(compat::int_type(ctx_, 8,
                                                                false));

  (void)db;
  e.want = keep_string(abi.symbol);
  e.named = true;
  if (!defined || !abi.has_def || abi.def_kind == etik_fundamental) {
    retype_variable(e, byte_pointer);
    compat::clear_initial_value(global);
  } else if (abi.symbol.compare(0, 4, "_ZTI") != 0) {
    ok = format_error("the typeinfo '" + abi.symbol + "' is not named "
                      "_ZTI<type>");
  } else {
    std::string text = abi.symbol.substr(4);
    bool tu_local = (abi.flags & ECIL_ABI_DATA_TU_LOCAL) != 0;
    a_entity *name_entity = typeinfo_name_global(
        e, "_ZTS" + text, (tu_local ? "*" : "") + text);
    a_entity *runtime = library_entity(runtime_vtable_symbol(abi.def_kind),
                                       false,
                                       compat::pointer_to(byte_pointer),
                                       false);
    std::vector<mlir::Attribute> fields;

    abi.name_entity = name_entity;
    if (runtime == nullptr) {
      ok = false;
    } else {
      cir::GlobalOp vtable = mlir::cast<cir::GlobalOp>(runtime->op);
      /* The stream's own vtable entity of the abi:: class (it ends up a
         struct of one array) is indexed through its struct; a library
         declaration is opaque bytes. */
      llvm::SmallVector<mlir::Attribute, 2> path;

      runtime->referenced = true;
      if (runtime->gid != 0)
        path.push_back(builder_.getI32IntegerAttr(0));
      path.push_back(builder_.getI32IntegerAttr(2));
      fields.push_back(compat::global_view(
          byte_pointer, vtable.getSymNameAttr(),
          builder_.getArrayAttr(path), false));
      fields.push_back(symbol_pointer(*name_entity));
      if (abi.def_kind == etik_pointer ||
          abi.def_kind == etik_member_pointer) {
        const uint64_t refs[2] = {abi.def_a, abi.def_b};

        fields.push_back(cir::IntAttr::get(
            compat::int_type(ctx_, 32, false),
            llvm::APInt(32, abi.def_flags)));
        for (unsigned k = 0; ok && k < (abi.def_kind == etik_pointer ? 1u
                                                                     : 2u);
             ++k) {
          ecil::a_gid_entry *target = ids_.find_gid(refs[k]);

          if (target == nullptr || target->payload == nullptr) {
            ok = internal("a typeinfo refers to a typeinfo that has no "
                          "object");
          } else {
            fields.push_back(symbol_pointer(
                *static_cast<a_abi_entity *>(target->payload)->entity));
          }
        }
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
}  /* a_emitter::finish_plain_typeinfo */

/* typeid and dynamic_cast. */

bool a_emitter::typeid_record(const ecil::a_record &r)
/*
TYPEID(g_ti|nil, t_result, flags): with a typeinfo, the object it describes
(nothing is popped); without, a glvalue of a polymorphic class: its vptr is
loaded and the typeinfo read from the vtable (cir.vtable.get_type_info).
NULL_CHECK is typeid(*p): a null object is __cxa_bad_typeid.
*/
{
  bool ok = true;
  uint64_t g = 0;
  uint64_t tresult = 0;
  uint64_t flags = 0;
  a_type *result = nullptr;

  uint_operand(r.ops[1], tresult);
  uint_operand(r.ops[2], flags);
  result = type_at(tresult);
  if (!throws_.empty())
    ok = materialize_throws();   /* May throw (bad_typeid). */
  if (result == nullptr) {
    ok = false;
  } else if (r.ops[0].kind != ecil::an_operand_kind::nil) {
    ecil::a_gid_entry *entry;

    uint_operand(r.ops[0], g);
    entry = ids_.find_gid(g);
    if (entry == nullptr || entry->payload == nullptr) {
      ok = internal("TYPEID of an unknown typeinfo");
    } else {
      a_abi_entity &abi = *static_cast<a_abi_entity *>(entry->payload);
      mlir::Value address = variable_address(*abi.entity);

      push_glvalue(builder_.createBitcast(loc(), address,
                                          compat::pointer_to(result->cir)),
                   8);
    }
  } else {
    a_value object;

    if (!pop_glvalue(object) || object.kind != a_value_kind::glvalue) {
      ok = false;
    } else {
      mlir::Value pointer = object.value;
      mlir::Type info_pointer = compat::pointer_to(result->cir);

      if (flags & ECIL_TYPEID_NULL_CHECK) {
        /* if (p == null) { __cxa_bad_typeid(); unreachable } */
        mlir::Value is_null = builder_.createPtrIsNull(pointer);
        cir::IfOp guard = compat::open_if(builder_, loc(), is_null);
        a_entity *bad = library_entity(
            "__cxa_bad_typeid", true,
            compat::function_type({}, compat::void_type(ctx_), false),
            false);

        if (bad == nullptr) {
          ok = false;
        } else {
          mlir::OpBuilder::InsertionGuard saved(builder_);

          bad->referenced = true;
          mlir::cast<cir::FuncOp>(bad->op)->setAttr(noreturn_name_,
                                                    unit_attr_);
          builder_.setInsertionPointToStart(&guard.getThenRegion().front());
          builder_.createCallOp(loc(), mlir::cast<cir::FuncOp>(bad->op),
                                llvm::ArrayRef<mlir::Value>());
          cir::UnreachableOp::create(builder_, loc());
        }
      }
      if (ok) {
        mlir::Value vptr_address = compat::vtable_get_vptr(builder_, loc(),
                                                           pointer);
        mlir::Value vptr = builder_.createLoad(loc(), vptr_address, false, 8);
        mlir::Value slot = compat::vtable_get_type_info(
            builder_, loc(), compat::pointer_to(info_pointer), vptr);

        push_glvalue(builder_.createLoad(loc(), slot, false, 8), 8);
      }
    }
  }
  return ok;
}  /* a_emitter::typeid_record */

bool a_emitter::dynamic_cast_record(const ecil::a_record &r)
/*
DYNAMIC_CAST(g_src_ti, g_dst_ti|nil, t_result, flags): cir.dyn_cast on the
pointer: to a class (with the typeinfos, __dynamic_cast, __cxa_bad_cast and
clang's offset hint, which finish computes from the class facts) or, with no
destination, to the most derived object (void *).  REF throws bad_cast.
*/
{
  bool ok = true;
  uint64_t gsrc = 0;
  uint64_t gdst = 0;
  uint64_t tresult = 0;
  uint64_t flags = 0;
  a_type *result = nullptr;
  mlir::Value pointer;

  uint_operand(r.ops[0], gsrc);
  uint_operand(r.ops[2], tresult);
  uint_operand(r.ops[3], flags);
  result = type_at(tresult);
  if (!throws_.empty())
    ok = materialize_throws();   /* May throw (bad_cast). */
  if (result == nullptr || !pop_prvalue(pointer)) {
    ok = false;
  } else if (!mlir::isa<cir::PointerType>(pointer.getType()) ||
             !mlir::isa<cir::PointerType>(result->cir)) {
    ok = format_error("DYNAMIC_CAST needs a pointer operand and a pointer "
                      "result type");
  } else {
    ecil::a_gid_entry *src_entry = ids_.find_gid(gsrc);
    a_abi_entity *src = src_entry != nullptr && src_entry->payload != nullptr
        ? static_cast<a_abi_entity *>(src_entry->payload) : nullptr;
    a_type *src_class = src != nullptr ? type_at(src->type) : nullptr;

    if (src_class == nullptr) {
      ok = internal("DYNAMIC_CAST of an unknown source typeinfo");
    } else {
      mlir::Type source = compat::pointer_to(src_class->cir);

      if (pointer.getType() != source)
        pointer = builder_.createBitcast(loc(), pointer, source);
      if (r.ops[1].kind == ecil::an_operand_kind::nil) {
        push_prvalue(compat::dynamic_cast_op(builder_, loc(), pointer,
                                             result->cir, false,
                                             mlir::Attribute()));
      } else {
        ecil::a_gid_entry *dst_entry;
        a_abi_entity *dst;
        a_entity *runtime;
        a_entity *bad;
        mlir::Type byte = compat::int_type(ctx_, 8, false);
        mlir::Type void_pointer = compat::pointer_to(
            compat::void_type(ctx_));

        uint_operand(r.ops[1], gdst);
        dst_entry = ids_.find_gid(gdst);
        dst = dst_entry != nullptr && dst_entry->payload != nullptr
            ? static_cast<a_abi_entity *>(dst_entry->payload) : nullptr;
        runtime = library_entity(
            "__dynamic_cast", true,
            compat::function_type({void_pointer, compat::pointer_to(byte),
                                   compat::pointer_to(byte),
                                   compat::int_type(ctx_, 64, true)},
                                  void_pointer, false), false);
        bad = library_entity(
            "__cxa_bad_cast", true,
            compat::function_type({}, compat::void_type(ctx_), false),
            false);
        if (dst == nullptr || runtime == nullptr || bad == nullptr) {
          ok = dst == nullptr ? internal("DYNAMIC_CAST of an unknown "
                                         "destination typeinfo") : false;
        } else {
          mlir::Attribute info;
          mlir::Value cast;

          runtime->referenced = true;
          bad->referenced = true;
          mlir::cast<cir::FuncOp>(runtime->op)->setAttr(
              nothrow_name_, unit_attr_);
          mlir::cast<cir::FuncOp>(bad->op)->setAttr(noreturn_name_,
                                                    unit_attr_);
          info = compat::dyn_cast_info(
              symbol_pointer(*src->entity), symbol_pointer(*dst->entity),
              mlir::cast<cir::FuncOp>(runtime->op).getSymNameAttr(),
              mlir::cast<cir::FuncOp>(bad->op).getSymNameAttr(),
              compat::int_type(ctx_, 64, true), 0);
          cast = compat::dynamic_cast_op(
              builder_, loc(), pointer, result->cir,
              (flags & ECIL_DYNAMIC_CAST_REF) != 0, info);
          late_uses_.push_back({a_late_kind::dyn_cast_hint,
                                cast.getDefiningOp(), src->type, dst->type,
                                0, false});
          push_prvalue(cast);
        }
      }
    }
  }
  return ok;
}  /* a_emitter::dynamic_cast_record */

}  /* namespace edg_cir */
