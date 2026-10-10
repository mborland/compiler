/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
expr.cpp -- expression records: the value stack (glvalue / prvalue / bit-field
glvalue), loads and stores, member access, operators, casts, calls, varargs,
conditional expressions through OperationState-built cir.ternary, and object
initialization.  Shapes follow clang/lib/CIR/CodeGen (CIRGenExpr*.cpp,
CIRGenCall.cpp) as described in research/cirgen_map.md.
*/

#include <algorithm>
#include <cstring>

#include "emitter.h"

namespace edg_cir {

namespace {

bool is_aggregate_type(mlir::Type t)
/*
Records and arrays are aggregates (loaded and stored as whole values).
*/
{
  return mlir::isa<cir::RecordType>(t) || mlir::isa<cir::ArrayType>(t);
}  /* is_aggregate_type */

mlir::NamedAttribute nothrow_attribute(mlir::Builder &b)
/*
The call-site attribute meaning "cannot unwind".
*/
{
  return mlir::NamedAttribute(
      mlir::StringAttr::get(b.getContext(),
                            cir::CIRDialect::getNoThrowAttrName()),
      b.getUnitAttr());
}  /* nothrow_attribute */

}  /* anonymous namespace */

/* The value stack. */

bool a_emitter::pop_value(a_value &out)
/*
Pop the top entry.
*/
{
  bool ok = !values_.empty();

  if (!ok) {
    format_error("the value stack is empty");
  } else {
    out = values_.back();
    values_.pop_back();
  }
  return ok;
}  /* a_emitter::pop_value */

bool a_emitter::pop_prvalue(mlir::Value &out)
/*
Pop a prvalue.  A function designator (a glvalue of function type) is its own
function pointer.
*/
{
  a_value v;
  bool ok = pop_value(v);

  if (ok) {
    if (v.kind == a_value_kind::prvalue) {
      out = v.value;
    } else if (v.kind == a_value_kind::glvalue &&
               mlir::isa<cir::FuncType>(
                   mlir::cast<cir::PointerType>(v.value.getType())
                       .getPointee())) {
      out = v.value;
    } else {
      ok = format_error("a glvalue was used where a prvalue is required");
    }
  }
  return ok;
}  /* a_emitter::pop_prvalue */

bool a_emitter::pop_glvalue(a_value &out)
/*
Pop a glvalue (an address or a bit-field).
*/
{
  bool ok = pop_value(out);

  if (ok && out.kind == a_value_kind::prvalue)
    ok = format_error("a prvalue was used where a glvalue is required");
  return ok;
}  /* a_emitter::pop_glvalue */

bool a_emitter::push_prvalue(mlir::Value v)
/*
Push a prvalue.
*/
{
  a_value value;

  value.kind = a_value_kind::prvalue;
  value.value = v;
  values_.push_back(value);
  return true;
}  /* a_emitter::push_prvalue */

void a_emitter::push_glvalue(mlir::Value address, uint32_t align)
/*
Push a glvalue.
*/
{
  a_value value;

  value.kind = a_value_kind::glvalue;
  value.value = address;
  value.align = align == 0 ? 1 : align;
  values_.push_back(value);
}  /* a_emitter::push_glvalue */

/* Memory access. */

mlir::Value a_emitter::load_value(const a_value &v, bool is_volatile)
/*
Load through a glvalue (cir.load) or a bit-field (cir.get_bitfield).
*/
{
  mlir::Value result;

  if (v.kind == a_value_kind::bitfield) {
    const a_bitfield_info &info = *v.bitfield;
    result = compat::get_bitfield(builder_, loc(), info.declared, v.value,
                                  info.storage, info.name, info.size,
                                  info.offset, info.is_signed, is_volatile,
                                  v.align);
  } else {
    result = builder_.createLoad(loc(), v.value, is_volatile, v.align);
  }
  return result;
}  /* a_emitter::load_value */

bool a_emitter::store_value(const a_value &target, mlir::Value value,
                            bool is_volatile, mlir::Value *result)
/*
Store through a glvalue or a bit-field.  A whole-aggregate store whose value
was just loaded becomes cir.copy.  *result (if given) receives the value the
object now holds.
*/
{
  bool ok = true;

  if (target.kind == a_value_kind::bitfield) {
    const a_bitfield_info &info = *target.bitfield;
    if (value.getType() != info.declared) {
      mlir::Value converted;
      ok = cast_value(mlir::isa<cir::BoolType>(info.declared) ? eck_int_to_bool
                                                              : eck_integral,
                      value, info.declared, converted);
      value = converted;
    }
    if (ok) {
      mlir::Value stored = compat::set_bitfield(
          builder_, loc(), info.declared, target.value, info.storage, value,
          info.name, info.size, info.offset, info.is_signed, is_volatile,
          target.align);
      if (result != nullptr)
        *result = stored;
    }
  } else {
    mlir::Type pointee =
        mlir::cast<cir::PointerType>(target.value.getType()).getPointee();
    cir::LoadOp load = value.getDefiningOp<cir::LoadOp>();

    if (is_aggregate_type(value.getType()) && load && value.use_empty() &&
        result == nullptr &&
        pointee == value.getType() && !load.getIsVolatile()) {
      cir::CopyOp copy = builder_.createCopy(target.value, load.getAddr(),
                                             is_volatile);
      copy.setDstAlignment(target.align);
      copy.setSrcAlignment(
          static_cast<uint64_t>(load.getAlignment().value_or(1)));
      load->erase();
    } else if (is_aggregate_type(value.getType()) &&
               value.getDefiningOp<cir::CallOp>() &&
               pointee == value.getType()) {
      /* An aggregate call result may be returned through a hidden sret
         pointer that CallConvLowering aims at the store's destination.  The
         destination may alias something the callee reads (a1 = f() where f
         reads a1), so the result lands in a fresh temporary first, as CodeGen
         does for aliased destinations. */
      uint32_t align = static_cast<uint32_t>(type_align(value.getType()));
      mlir::Value temp = alloca_here(value.getType(), "agg.tmp", align);
      builder_.createStore(loc(), value, temp, false, false,
                           builder_.getAlignmentAttr(align));
      cir::CopyOp copy = builder_.createCopy(target.value, temp, is_volatile);
      copy.setDstAlignment(target.align);
      copy.setSrcAlignment(align);
    } else {
      builder_.createStore(loc(), value, target.value, is_volatile, false,
                           builder_.getAlignmentAttr(target.align));
    }
    if (result != nullptr)
      *result = value;
  }
  return ok;
}  /* a_emitter::store_value */

bool a_emitter::field_address(const a_value &base, uint64_t rec_gid,
                              uint64_t index, a_value &out)
/*
Address of field index of the record glvalue base (a bit-field gives a
bit-field glvalue addressing its access unit).
*/
{
  bool ok = true;
  a_type *t = type_at(rec_gid);
  mlir::Value address = base.value;

  if (base.kind != a_value_kind::glvalue)
    ok = format_error("FIELD of something that is not a glvalue");
  else if (t == nullptr || t->kind != a_type_kind::record_type ||
           !t->record->complete)
    ok = format_error("FIELD names a type that is not a complete record");
  else if (index >= t->record->fields.size())
    ok = format_error("FIELD index past the record's fields");
  if (ok) {
    a_record_info &rec = *t->record;
    if (mlir::cast<cir::PointerType>(address.getType()).getPointee() !=
        t->cir)
      address = builder_.createPtrBitcast(address, t->cir);
    const a_field &f = rec.fields[static_cast<size_t>(index)];
    uint32_t member = rec.member_index[static_cast<size_t>(index)];

    if (f.is_bitfield) {
      const a_bitfield_info &info =
          rec.bitfields[static_cast<size_t>(index)];
      mlir::Value unit = builder_.createGetMember(
          loc(), cir::PointerType::get(info.storage), address, f.name,
          info.unit_index);
      out.kind = a_value_kind::bitfield;
      out.value = unit;
      out.align = derived_align(base.align, info.unit_offset);
      out.bitfield = &info;
    } else {
      mlir::Value member_address = builder_.createGetMember(
          loc(), cir::PointerType::get(rec.members[member]), address, f.name,
          member);
      out.kind = a_value_kind::glvalue;
      out.value = member_address;
      out.align = derived_align(base.align,
                                rec.is_union ? 0 : f.byte_offset);
      out.bitfield = nullptr;
    }
  }
  return ok;
}  /* a_emitter::field_address */

bool a_emitter::element_address(const a_value &base, uint64_t array_gid,
                                uint64_t index, a_value &out)
/*
Address of element index of the array glvalue base.
*/
{
  bool ok = true;
  a_type *t = type_at(array_gid);
  a_type *element;
  mlir::Value decayed;
  mlir::Value stride;

  if (t == nullptr || t->kind != a_type_kind::array_type) {
    ok = format_error("AGG_SEL on a type that is neither record nor array");
  } else {
    element = type_at(t->target);
    decayed = builder_.createCast(loc(), cir::CastKind::array_to_ptrdecay,
                                  base.value,
                                  cir::PointerType::get(element->cir));
    stride = builder_.getConstantInt(loc(), builder_.getSIntNTy(64),
                                     static_cast<int64_t>(index));
    out.kind = a_value_kind::glvalue;
    out.value = builder_.createPtrStride(loc(), decayed, stride);
    out.align = derived_align(base.align, index * type_size(element->cir));
    out.bitfield = nullptr;
  }
  return ok;
}  /* a_emitter::element_address */

/* Operators. */

mlir::Value a_emitter::to_bool(mlir::Value v)
/*
Convert a scalar to !cir.bool (no-op on a bool).
*/
{
  mlir::Value result = v;
  mlir::Type t = v.getType();

  if (!mlir::isa<cir::BoolType>(t)) {
    cir::CastKind kind =
        mlir::isa<cir::IntType>(t) ? cir::CastKind::int_to_bool
        : mlir::isa<cir::PointerType>(t) ? cir::CastKind::ptr_to_bool
                                         : cir::CastKind::float_to_bool;
    result = builder_.createCast(loc(), kind, v, builder_.getBoolTy());
  }
  return result;
}  /* a_emitter::to_bool */

mlir::Value a_emitter::arithmetic(uint8_t op, uint32_t flags, mlir::Value lhs,
                                  mlir::Value rhs)
/*
Binary arithmetic and bitwise operators; the integer/float selection comes
from the operand type.  Returns a null value for an invalid combination.
*/
{
  mlir::Value result;
  mlir::Type t = lhs.getType();
  cir::OverflowBehavior ob = cir::OverflowBehavior::None;

  if (flags & ECIL_ARITH_NSW)
    ob |= cir::OverflowBehavior::NoSignedWrap;
  if (flags & ECIL_ARITH_NUW)
    ob |= cir::OverflowBehavior::NoUnsignedWrap;
  if (mlir::isa<cir::IntType>(t) || mlir::isa<cir::BoolType>(t)) {
    switch (op) {
    case eop_add: result = builder_.createAdd(loc(), lhs, rhs, ob); break;
    case eop_sub: result = builder_.createSub(loc(), lhs, rhs, ob); break;
    case eop_mul: result = builder_.createMul(loc(), lhs, rhs, ob); break;
    case eop_div: result = builder_.createDiv(loc(), lhs, rhs); break;
    case eop_rem: result = builder_.createRem(loc(), lhs, rhs); break;
    case eop_shl:
    case eop_shr:
      if (rhs.getType() != t)
        rhs = builder_.createCast(loc(), cir::CastKind::integral, rhs, t);
      result = builder_.createShift(loc(), lhs, rhs, op == eop_shl);
      break;
    case eop_bit_and: result = builder_.createAnd(loc(), lhs, rhs); break;
    case eop_bit_or: result = builder_.createOr(loc(), lhs, rhs); break;
    case eop_bit_xor: result = builder_.createXor(loc(), lhs, rhs); break;
    default: break;
    }
  } else if (cir::isAnyFloatingPointType(t)) {
    switch (op) {
    case eop_add: result = builder_.createFAdd(loc(), lhs, rhs); break;
    case eop_sub: result = builder_.createFSub(loc(), lhs, rhs); break;
    case eop_mul: result = builder_.createFMul(loc(), lhs, rhs); break;
    case eop_div: result = builder_.createFDiv(loc(), lhs, rhs); break;
    case eop_rem: result = builder_.createFRem(loc(), lhs, rhs); break;
    default: break;
    }
  }
  return result;
}  /* a_emitter::arithmetic */

bool a_emitter::cast_value(uint32_t kind, mlir::Value v, mlir::Type to,
                           mlir::Value &out)
/*
Apply an ECIL cast kind.
*/
{
  bool ok = true;
  cir::CastKind ck = cir::CastKind::bitcast;

  switch (kind) {
  case eck_integral: ck = cir::CastKind::integral; break;
  case eck_int_to_bool: ck = cir::CastKind::int_to_bool; break;
  case eck_float_to_bool: ck = cir::CastKind::float_to_bool; break;
  case eck_ptr_to_bool: ck = cir::CastKind::ptr_to_bool; break;
  case eck_bool_to_int: ck = cir::CastKind::bool_to_int; break;
  case eck_bool_to_float: ck = cir::CastKind::bool_to_float; break;
  case eck_int_to_float: ck = cir::CastKind::int_to_float; break;
  case eck_float_to_int: ck = cir::CastKind::float_to_int; break;
  case eck_floating: ck = cir::CastKind::floating; break;
  case eck_int_to_ptr: ck = cir::CastKind::int_to_ptr; break;
  case eck_ptr_to_int: ck = cir::CastKind::ptr_to_int; break;
  case eck_bitcast:
    /* Strict: pointer/integer conversions have their own cast kinds. */
    if (mlir::isa<cir::PointerType>(v.getType()) !=
        mlir::isa<cir::PointerType>(to))
      ok = format_error("CAST bitcast between a pointer and a non-pointer "
                        "type (use ptr_to_int or int_to_ptr)");
    break;
  case eck_array_to_ptrdecay: ck = cir::CastKind::array_to_ptrdecay; break;
  default:
    ok = format_error("unknown cast kind");
    break;
  }
  if (ok)
    out = builder_.createCast(loc(), ck, v, to);
  return ok;
}  /* a_emitter::cast_value */

/* Constants in expressions and initialization. */

mlir::Value a_emitter::get_constant_value(mlir::Attribute attr)
/*
A cir.const of the (typed) attribute.
*/
{
  mlir::Value result;
  auto pointer = mlir::dyn_cast<cir::ConstPtrAttr>(attr);

  if (pointer && !pointer.isNullValue()) {
    /* The LLVM lowering has no non-null pointer-valued cir.const. */
    mlir::Value integer = builder_.getConstantInt(
        loc(), compat::int_type(ctx_, 64, false),
        static_cast<int64_t>(pointer.getValue().getInt()));
    result = builder_.createCast(loc(), cir::CastKind::int_to_ptr, integer,
                                 pointer.getType());
  } else {
    result = builder_.getConstant(loc(), mlir::cast<mlir::TypedAttr>(attr));
  }
  return result;
}  /* a_emitter::get_constant_value */

void a_emitter::zero_fill(mlir::Value address, mlir::Type type, uint32_t align)
/*
Zero an object: a store of the zero constant for small ones, cir.libc.memset
for the rest.
*/
{
  uint64_t size = type_size(type);

  if (size <= 16) {
    builder_.createStore(loc(), builder_.getNullValue(type, loc()), address,
                         false, false, builder_.getAlignmentAttr(align));
  } else {
    compat::memset_zero(builder_, loc(), address, builder_.getUIntNTy(64),
                        size, align);
  }
}  /* a_emitter::zero_fill */

bool a_emitter::materialize_constant_into(const a_value &target,
                                          const a_const &c, mlir::Type type)
/*
Store a constant into an object (INIT_CONST).  Big aggregates are copied
from a private constant global instead of being built in registers.
*/
{
  bool ok = true;

  if (target.kind == a_value_kind::bitfield) {
    mlir::Value v = get_constant_value(c.attr);
    ok = store_value(target, v, false, nullptr);
  } else if (c.is_zero && is_aggregate_type(type)) {
    zero_fill(target.value, type, target.align);
  } else if (is_aggregate_type(type) && type_size(type) > 256) {
    std::string name = unique_symbol(
        "__const." + (fn_entity_ ? fn_entity_->name : std::string("fn")) +
        "." + std::to_string(temp_counter_++));
    cir::GlobalOp g = compat::create_global(
        builder_, module_, loc(), name, type, true,
        cir::GlobalLinkageKind::PrivateLinkage);
    g.setInitialValueAttr(c.attr);
    g.setAlignmentAttr(builder_.getI64IntegerAttr(
        static_cast<int64_t>(type_align(type))));
    mlir::SymbolTable::setSymbolVisibility(
        g, mlir::SymbolTable::Visibility::Private);
    compat::set_dso_local(g.getOperation(), true);
    mlir::Value source = builder_.createGetGlobal(loc(), g);
    cir::CopyOp copy = builder_.createCopy(target.value, source, false);
    copy.setDstAlignment(target.align);
    copy.setSrcAlignment(type_align(type));
  } else {
    mlir::Value v = get_constant_value(c.attr);
    ok = store_value(target, v, false, nullptr);
  }
  return ok;
}  /* a_emitter::materialize_constant_into */

bool a_emitter::constant_expression(const ecil::a_record &r)
/*
CONST_INT / CONST_FLOAT / CONST_NULL / CONST_PTR.
*/
{
  bool ok = true;
  uint64_t tgid = 0;
  a_type *t;
  mlir::Attribute attr;

  uint_operand(r.ops[0], tgid);
  t = type_at(tgid);
  if (t == nullptr) {
    ok = format_error("constant names an unknown type");
  } else if (r.opcode == eop_const_ptr) {
    /* As CIRGen: an integer constant converted by int_to_ptr (the LLVM
       lowering has no pointer-valued cir.const). */
    mlir::Type word = compat::int_type(ctx_, 64, false);
    mlir::Value integer;
    mlir::Value pointer;

    attr = scalar_int_attribute(word, r.ops[1], ok);
    if (!ok) {
      ok = format_error("CONST_PTR does not fit a pointer");
    } else {
      integer = get_constant_value(attr);
      ok = cast_value(eck_int_to_ptr, integer, t->cir, pointer);
      if (ok)
        push_prvalue(pointer);
    }
  } else {
    switch (r.opcode) {
    case eop_const_int:
      attr = scalar_int_attribute(t->cir, r.ops[1], ok);
      if (!ok)
        ok = format_error(std::string(r.info->name) +
                          " does not fit its type");
      break;
    case eop_const_float:
      attr = float_attribute(t->cir, r.ops[1], ok);
      if (!ok)
        ok = format_error("CONST_FLOAT does not match its type");
      break;
    default:
      attr = zero_attribute(t->cir);
      break;
    }
    if (ok)
      push_prvalue(get_constant_value(attr));
  }
  return ok;
}  /* a_emitter::constant_expression */

/* Calls. */

bool a_emitter::call_record(const ecil::a_record &r, bool indirect)
/*
CALL / CALL_INDIRECT with the call-site type rule of spec 8.8: a direct
cir.call when the call type is prototyped and equals the callee's type,
otherwise a bitcast to a zero-fixed-parameter variadic function pointer type
and an indirect call with all arguments as variadic extras.
*/
{
  bool ok = true;
  uint64_t gid = 0;
  uint64_t t_call = 0;
  uint64_t nargs = 0;
  uint64_t flags = 0;
  a_entity *e = nullptr;
  a_type *call_type;
  llvm::SmallVector<mlir::Value> args;
  mlir::Value callee;
  cir::CallOp op;
  bool noreturn;
  mlir::NamedAttribute nothrow = nothrow_attribute(builder_);
  bool use_nothrow = c_language_ &&
                     !(session_.header.lang_flags & ECIL_LANG_EXCEPTIONS);
  llvm::SmallVector<mlir::NamedAttribute> attrs;

  if (indirect) {
    uint_operand(r.ops[0], t_call);
    uint_operand(r.ops[1], nargs);
    uint_operand(r.ops[2], flags);
  } else {
    uint_operand(r.ops[0], gid);
    uint_operand(r.ops[1], t_call);
    uint_operand(r.ops[2], nargs);
    uint_operand(r.ops[3], flags);
  }
  noreturn = (flags & ECIL_CALL_NORETURN) != 0;
  call_type = type_at(t_call);
  if (call_type == nullptr || call_type->kind != a_type_kind::function_type)
    ok = format_error("CALL names a type that is not a function type");
  if (ok) {
    /* Result and parameters of a call must be complete. */
    for (uint64_t p : call_type->params)
      ok = ok && type_at(p) != nullptr && type_is_complete(type_at(p)->cir);
    ok = ok && type_at(call_type->target) != nullptr &&
         type_is_complete(type_at(call_type->target)->cir);
    if (!ok)
      ok = format_error("CALL with an incomplete record type as argument "
                        "or result");
  }
  if (ok) {
    args.resize(static_cast<size_t>(nargs));
    for (size_t k = static_cast<size_t>(nargs); ok && k > 0; --k)
      ok = pop_prvalue(args[k - 1]);
  }
  if (ok) {
    if (use_nothrow)
      attrs.push_back(nothrow);
    cir::FuncType wanted = mlir::cast<cir::FuncType>(call_type->cir);
    bool prototyped = !call_type->no_proto;
    mlir::Type result_type = wanted.hasVoidReturn()
        ? static_cast<mlir::Type>(compat::void_type(ctx_))
        : wanted.getReturnType();

    auto args_match = [&](cir::FuncType fn_type) {
      bool good = wanted == fn_type && prototyped &&
                  (fn_type.isVarArg()
                       ? args.size() >= fn_type.getNumInputs()
                       : args.size() == fn_type.getNumInputs());
      for (unsigned k = 0; good && k < fn_type.getNumInputs(); ++k) {
        if (args[k].getType() != fn_type.getInput(k)) {
          mlir::Type want = fn_type.getInput(k);
          mlir::Type have = args[k].getType();
          if (mlir::isa<cir::PointerType>(want) &&
              mlir::isa<cir::PointerType>(have))
            args[k] = builder_.createBitcast(loc(), args[k], want);
          else
            good = false;
        }
      }
      return good;
    };

    if (!indirect) {
      e = entity_for(gid, true);
      if (e == nullptr) {
        ok = format_error("CALL of an entity that is not a function");
      } else {
        cir::FuncOp fn = mlir::cast<cir::FuncOp>(e->op);
        e->referenced = true;
        if (args_match(fn.getFunctionType())) {
          op = builder_.createCallOp(loc(), fn, args, attrs);
          e->calls.push_back(op);
        } else {
          cir::FuncType variadic = mlir::cast<cir::FuncType>(
              compat::function_type({}, result_type, true));
          mlir::Value address = function_address(*e);
          callee = builder_.createBitcast(loc(), address,
                                          compat::pointer_to(variadic));
          op = builder_.createIndirectCallOp(loc(), callee, variadic, args,
                                             attrs);
        }
      }
    } else {
      mlir::Value pointer;
      ok = pop_prvalue(pointer);
      auto ptr_type = ok ? mlir::dyn_cast<cir::PointerType>(pointer.getType())
                         : cir::PointerType();
      if (ok && !ptr_type) {
        ok = format_error("CALL_INDIRECT callee is not a pointer");
      } else if (ok) {
        auto pointee = mlir::dyn_cast<cir::FuncType>(ptr_type.getPointee());
        if (pointee && args_match(pointee)) {
          op = builder_.createIndirectCallOp(loc(), pointer, pointee, args,
                                             attrs);
        } else {
          cir::FuncType variadic = mlir::cast<cir::FuncType>(
              compat::function_type({}, result_type, true));
          callee = builder_.createBitcast(loc(), pointer,
                                          compat::pointer_to(variadic));
          op = builder_.createIndirectCallOp(loc(), callee, variadic, args,
                                             attrs);
        }
      }
    }
  }
  if (ok) {
    if (op.getNumResults() == 1)
      push_prvalue(op.getResult());
    if (noreturn) {
      cir::UnreachableOp::create(builder_, loc());
      start_new_block();
    }
  }
  return ok;
}  /* a_emitter::call_record */

/* ++ and --. */

bool a_emitter::inc_dec(const ecil::a_record &r)
/*
INC_DEC: load, step by one (element for pointers), store, push the new
(pre) or old (post) value.
*/
{
  bool ok = true;
  uint64_t kind = 0;
  uint64_t flags = 0;
  a_value target;
  mlir::Value old_value;
  mlir::Value new_value;
  mlir::Value stored;
  bool is_inc;
  bool is_post;
  bool nsw;
  bool is_volatile;
  mlir::Type t;

  uint_operand(r.ops[0], kind);
  uint_operand(r.ops[1], flags);
  ok = pop_glvalue(target);
  if (ok) {
    is_inc = kind == eidk_pre_inc || kind == eidk_post_inc;
    is_post = kind == eidk_post_inc || kind == eidk_post_dec;
    nsw = (flags & ECIL_INC_DEC_NSW) != 0;
    is_volatile = (flags & ECIL_INC_DEC_VOLATILE) != 0;
    old_value = load_value(target, is_volatile);
    t = old_value.getType();
    if (mlir::isa<cir::IntType>(t)) {
      new_value = is_inc ? builder_.createInc(loc(), old_value, nsw)
                         : builder_.createDec(loc(), old_value, nsw);
    } else if (mlir::isa<cir::PointerType>(t)) {
      mlir::Value one = builder_.getConstantInt(
          loc(), builder_.getSIntNTy(64), is_inc ? 1 : -1);
      new_value = builder_.createPtrStride(loc(), old_value, one);
    } else if (cir::isAnyFloatingPointType(t)) {
      mlir::Value one = get_constant_value(cir::FPAttr::get(
          t, llvm::APFloat(mlir::cast<cir::FPTypeInterface>(t)
                               .getFloatSemantics(), 1)));
      new_value = is_inc ? builder_.createFAdd(loc(), old_value, one)
                         : builder_.createFSub(loc(), old_value, one);
    } else {
      ok = unsupported("++/-- of a _Bool or an unsupported type");
    }
  }
  if (ok)
    ok = store_value(target, new_value, is_volatile, &stored);
  if (ok)
    push_prvalue(is_post ? old_value : stored);
  return ok;
}  /* a_emitter::inc_dec */

/* Conditional expressions. */

bool a_emitter::tern_record(const ecil::a_record &r)
/*
TERN_*, LAND_* and LOR_*: all are cir.ternary built with OperationState.
*/
{
  bool ok = true;

  switch (r.opcode) {
  case eop_tern_begin: {
    mlir::Value cond = pop_bool_condition();
    mlir::Type result_type;
    uint64_t tgid = 0;
    if (!cond) {
      ok = false;
      break;
    }
    if (r.ops[0].kind != ecil::an_operand_kind::nil) {
      uint_operand(r.ops[0], tgid);
      a_type *t = type_at(tgid);
      if (t == nullptr) {
        ok = format_error("TERN_BEGIN names an unknown type");
        break;
      }
      result_type = t->cir;
    }
    mlir::Operation *op = compat::open_ternary(builder_, loc(), cond,
                                               result_type);
    mlir::Block *block = builder_.createBlock(&op->getRegion(0));
    open_frame(a_frame_kind::ternary, op, block);
    frames_.back().result_type = result_type;
    break;
  }
  case eop_tern_else: {
    a_frame *f = top_frame(a_frame_kind::ternary, "TERN_ELSE");
    if (f == nullptr) {
      ok = false;
      break;
    }
    if (f->result_type) {
      mlir::Value v;
      if (!pop_prvalue(v)) {
        ok = false;
        break;
      }
      builder_.createYield(loc(), mlir::ValueRange{v});
    } else {
      builder_.createYield(loc());
    }
    if (values_.size() != f->value_base) {
      ok = format_error("values left on the stack in a ?: arm");
      break;
    }
    f->alloca_block = builder_.createBlock(&f->op->getRegion(1));
    f->phase = 1;
    break;
  }
  case eop_tern_end: {
    a_frame *f = top_frame(a_frame_kind::ternary, "TERN_END");
    if (f == nullptr) {
      ok = false;
      break;
    }
    mlir::Operation *op = f->op;
    bool has_result = static_cast<bool>(f->result_type);
    if (has_result) {
      mlir::Value v;
      if (!pop_prvalue(v)) {
        ok = false;
        break;
      }
      builder_.createYield(loc(), mlir::ValueRange{v});
    } else {
      builder_.createYield(loc());
    }
    builder_.setInsertionPointAfter(op);
    if (!pop_frame()) {
      ok = false;
      break;
    }
    if (has_result)
      push_prvalue(op->getResult(0));
    break;
  }
  case eop_land_begin:
  case eop_lor_begin: {
    mlir::Value lhs = pop_bool_condition();
    if (!lhs) {
      ok = false;
      break;
    }
    mlir::Operation *op = compat::open_ternary(builder_, loc(), lhs,
                                               builder_.getBoolTy());
    bool is_and = r.opcode == eop_land_begin;
    mlir::Block *first = builder_.createBlock(&op->getRegion(0));
    open_frame(is_and ? a_frame_kind::land : a_frame_kind::lor, op, first);
    if (!is_and) {
      /* LOR: the true arm is the constant true; the rhs goes in the false
         arm. */
      builder_.createYield(loc(), mlir::ValueRange{builder_.getTrue(loc())});
      frames_.back().alloca_block = builder_.createBlock(&op->getRegion(1));
      frames_.back().phase = 1;
    }
    break;
  }
  case eop_land_end:
  case eop_lor_end: {
    bool is_and = r.opcode == eop_land_end;
    a_frame *f = top_frame(is_and ? a_frame_kind::land : a_frame_kind::lor,
                           is_and ? "LAND_END" : "LOR_END");
    if (f == nullptr) {
      ok = false;
      break;
    }
    mlir::Operation *op = f->op;
    mlir::Value rhs = pop_bool_condition();
    if (!rhs) {
      ok = false;
      break;
    }
    builder_.createYield(loc(), mlir::ValueRange{rhs});
    if (is_and) {
      builder_.createBlock(&op->getRegion(1));
      builder_.createYield(loc(), mlir::ValueRange{builder_.getFalse(loc())});
    }
    builder_.setInsertionPointAfter(op);
    if (!pop_frame()) {
      ok = false;
      break;
    }
    push_prvalue(op->getResult(0));
    break;
  }
  default:
    ok = format_error("unexpected conditional record");
    break;
  }
  return ok;
}  /* a_emitter::tern_record */

/* Object initialization. */

bool a_emitter::agg_record(const ecil::a_record &r)
/*
AGG_INIT_BEGIN / AGG_SEL / AGG_INIT_END / INIT_CONST / INIT_ZERO.
*/
{
  bool ok = true;

  switch (r.opcode) {
  case eop_agg_init_begin: {
    uint64_t tgid = 0;
    uint64_t flags = 0;
    a_value address;
    a_type *t;
    uint_operand(r.ops[0], tgid);
    uint_operand(r.ops[1], flags);
    t = type_at(tgid);
    if (t == nullptr) {
      ok = format_error("AGG_INIT_BEGIN names an unknown type");
      break;
    }
    if (!pop_glvalue(address) || address.kind != a_value_kind::glvalue) {
      ok = format_error("AGG_INIT_BEGIN needs an object address");
      break;
    }
    if (flags & ECIL_AGG_INIT_ZERO_FILL)
      zero_fill(address.value, t->cir, address.align);
    open_frame(a_frame_kind::agg_init, nullptr, current_alloca_block());
    a_init_target target = {address, tgid};
    targets_.push_back(target);
    break;
  }
  case eop_agg_sel: {
    uint64_t index = 0;
    a_value out;
    uint_operand(r.ops[0], index);
    if (targets_.empty()) {
      ok = format_error("AGG_SEL outside AGG_INIT_BEGIN");
      break;
    }
    const a_init_target &target = targets_.back();
    a_type *t = type_at(target.type);
    if (t->kind == a_type_kind::record_type) {
      if (!field_address(target.address, target.type, index, out)) {
        ok = false;
        break;
      }
    } else if (!element_address(target.address, target.type, index, out)) {
      ok = false;
      break;
    }
    values_.push_back(out);
    break;
  }
  case eop_agg_init_end: {
    a_frame *f = top_frame(a_frame_kind::agg_init, "AGG_INIT_END");
    if (f == nullptr) {
      ok = false;
      break;
    }
    targets_.pop_back();
    ok = pop_frame();
    break;
  }
  case eop_init_const: {
    a_value target;
    a_const c;
    if (consts_.empty() || !const_aggs_.empty()) {
      ok = format_error("INIT_CONST without a constant");
      break;
    }
    c = consts_.back();
    consts_.pop_back();
    if (!pop_glvalue(target)) {
      ok = false;
      break;
    }
    mlir::Type type = target.kind == a_value_kind::bitfield
        ? target.bitfield->declared
        : mlir::cast<cir::PointerType>(target.value.getType()).getPointee();
    ok = materialize_constant_into(target, c, type);
    break;
  }
  case eop_init_zero: {
    a_value target;
    if (!pop_glvalue(target)) {
      ok = false;
      break;
    }
    if (target.kind == a_value_kind::bitfield) {
      mlir::Value zero = builder_.getNullValue(target.bitfield->declared,
                                               loc());
      ok = store_value(target, zero, false, nullptr);
    } else {
      zero_fill(target.value,
                mlir::cast<cir::PointerType>(target.value.getType())
                    .getPointee(), target.align);
    }
    break;
  }
  default:
    ok = format_error("unexpected initialization record");
    break;
  }
  return ok;
}  /* a_emitter::agg_record */

/* The expression dispatcher. */

bool a_emitter::builtin_operation(uint64_t id,
                                  llvm::SmallVectorImpl<mlir::Value> &args,
                                  a_type *result_type)
/*
The minor-5 builtins: alloca, byte swap, bit counting and the overflow-
checked arithmetic.  The CIR operations want unsigned (or, for ffs and clrsb,
signed) operands of fixed widths, so operands are converted around them as
CIRGen does.
*/
{
  bool ok = true;
  auto int_of = [&](mlir::Value v) {
    return mlir::dyn_cast<cir::IntType>(v.getType());
  };
  auto with_sign = [&](mlir::Value v, bool is_signed) {
    cir::IntType t = int_of(v);
    mlir::Value out = v;
    if (t && t.isSigned() != is_signed)
      out = builder_.createCast(loc(), cir::CastKind::integral, v,
                                compat::int_type(ctx_, t.getWidth(),
                                                 is_signed));
    return out;
  };
  auto to_result = [&](mlir::Value v) {
    mlir::Value out = v;
    if (result_type != nullptr && v.getType() != result_type->cir)
      out = builder_.createCast(loc(), cir::CastKind::integral, v,
                                result_type->cir);
    return out;
  };

  if (id == ebi_alloca) {
    if (args.size() != 1 || !int_of(args[0]) || result_type == nullptr) {
      ok = format_error("__builtin_alloca needs one integer argument");
    } else {
      mlir::Type byte = compat::int_type(ctx_, 8, false);
      mlir::Value size = args[0];
      mlir::Value memory;
      if (int_of(size).getWidth() != 64 || int_of(size).isSigned())
        size = builder_.createCast(loc(), cir::CastKind::integral, size,
                                   compat::int_type(ctx_, 64, false));
      memory = builder_.createAlloca(loc(), cir::PointerType::get(byte),
                                     "bi_alloca",
                                     builder_.getAlignmentAttr(16), size);
      push_prvalue(builder_.createBitcast(loc(), memory, result_type->cir));
    }
  } else if (id == ebi_is_fp_class || id == ebi_signbit) {
    if (args.empty() || !cir::isAnyFloatingPointType(args[0].getType())) {
      ok = format_error("floating-point class builtins need a floating "
                        "operand");
    } else if (id == ebi_signbit) {
      push_prvalue(cir::SignBitOp::create(builder_, loc(),
                                          builder_.getBoolTy(), args[0]));
    } else {
      auto constant = args.size() == 2
                          ? args[1].getDefiningOp<cir::ConstantOp>()
                          : cir::ConstantOp();
      auto mask = constant ? mlir::dyn_cast<cir::IntAttr>(constant.getValue())
                           : cir::IntAttr();
      if (!mask) {
        ok = format_error("is_fp_class needs a constant mask");
      } else {
        push_prvalue(cir::IsFPClassOp::create(
            builder_, loc(), builder_.getBoolTy(), args[0],
            static_cast<cir::FPClassTest>(mask.getUInt())));
      }
    }
  } else if (id >= ebi_add_overflow && id <= ebi_mul_overflow) {
    cir::IntType types[3];
    mlir::Value pointer;
    cir::IntType wide;
    unsigned width = 0;
    bool is_signed = false;

    if (args.size() != 3 || !int_of(args[0]) || !int_of(args[1]) ||
        !mlir::isa<cir::PointerType>(args[2].getType()) ||
        !mlir::isa<cir::IntType>(mlir::cast<cir::PointerType>(
                                     args[2].getType()).getPointee())) {
      ok = format_error("overflow builtins need two integers and a result "
                        "pointer");
    } else {
      pointer = args[2];
      types[0] = int_of(args[0]);
      types[1] = int_of(args[1]);
      types[2] = mlir::cast<cir::IntType>(
          mlir::cast<cir::PointerType>(pointer.getType()).getPointee());
      for (cir::IntType t : types)
        is_signed = is_signed || t.isSigned();
      for (cir::IntType t : types)
        width = std::max(width, t.getWidth() + (is_signed && !t.isSigned()));
      wide = mlir::cast<cir::IntType>(compat::int_type(ctx_, width,
                                                       is_signed));
      mlir::Value x = args[0];
      mlir::Value y = args[1];
      if (x.getType() != wide)
        x = builder_.createCast(loc(), cir::CastKind::integral, x, wide);
      if (y.getType() != wide)
        y = builder_.createCast(loc(), cir::CastKind::integral, y, wide);
      mlir::Value value;
      mlir::Value overflow;
      if (id == ebi_add_overflow) {
        auto op = cir::AddOverflowOp::create(builder_, loc(), types[2], x, y);
        value = op.getResult();
        overflow = op.getOverflow();
      } else if (id == ebi_sub_overflow) {
        auto op = cir::SubOverflowOp::create(builder_, loc(), types[2], x, y);
        value = op.getResult();
        overflow = op.getOverflow();
      } else {
        auto op = cir::MulOverflowOp::create(builder_, loc(), types[2], x, y);
        value = op.getResult();
        overflow = op.getOverflow();
      }
      builder_.createStore(
          loc(), value, pointer, false, false,
          builder_.getAlignmentAttr(
              static_cast<uint32_t>(type_align(types[2]))));
      push_prvalue(overflow);
    }
  } else if (args.size() != 1 || !int_of(args[0])) {
    ok = format_error("bit builtin needs one integer argument");
  } else {
    mlir::Value x = args[0];
    mlir::Value out;
    switch (id) {
    case ebi_bswap:
      out = cir::ByteSwapOp::create(builder_, loc(), with_sign(x, false));
      out = with_sign(out, int_of(x).isSigned());
      break;
    case ebi_clz:
      out = cir::BitClzOp::create(builder_, loc(), with_sign(x, false), true)
                .getResult();
      break;
    case ebi_ctz:
      out = cir::BitCtzOp::create(builder_, loc(), with_sign(x, false), true)
                .getResult();
      break;
    case ebi_popcount:
      out = cir::BitPopcountOp::create(builder_, loc(), with_sign(x, false))
                .getResult();
      break;
    case ebi_parity:
      out = cir::BitParityOp::create(builder_, loc(), with_sign(x, false))
                .getResult();
      break;
    case ebi_ffs:
      out = cir::BitFfsOp::create(builder_, loc(), with_sign(x, true))
                .getResult();
      break;
    default:
      out = cir::BitClrsbOp::create(builder_, loc(), with_sign(x, true))
                .getResult();
      break;
    }
    push_prvalue(id == ebi_bswap ? out : to_result(out));
  }
  return ok;
}  /* a_emitter::builtin_operation */

bool a_emitter::expression(const ecil::a_record &r)
/*
Handle one expression record.
*/
{
  bool ok = true;

  switch (r.opcode) {
  case eop_init_const:
  case eop_init_zero:
  case eop_agg_init_begin:
  case eop_agg_sel:
  case eop_agg_init_end:
    ok = agg_record(r);
    break;
  case eop_tern_begin:
  case eop_tern_else:
  case eop_tern_end:
  case eop_land_begin:
  case eop_land_end:
  case eop_lor_begin:
  case eop_lor_end:
    ok = tern_record(r);
    break;
  case eop_const_int:
  case eop_const_float:
  case eop_const_null:
  case eop_const_ptr:
    ok = constant_expression(r);
    break;
  case eop_call:
    ok = call_record(r, false);
    break;
  case eop_call_indirect:
    ok = call_record(r, true);
    break;
  case eop_inc_dec:
    ok = inc_dec(r);
    break;
  case eop_lref: {
    uint64_t l = 0;
    uint_operand(r.ops[0], l);
    auto found = locals_.find(l);
    if (found == locals_.end()) {
      ok = format_error("LREF of an undeclared local " + std::to_string(l));
      break;
    }
    push_glvalue(found->second.address, found->second.align);
    break;
  }
  case eop_gref: {
    uint64_t g = 0;
    uint_operand(r.ops[0], g);
    auto found = entities_.find(g);
    if (found == entities_.end()) {
      ok = format_error("GREF of an unknown entity");
      break;
    }
    a_entity &e = found->second;
    if (e.is_function) {
      push_glvalue(function_address(e), 1);
    } else {
      uint32_t align = e.align != 0
          ? e.align
          : static_cast<uint32_t>(type_align(type_at(e.type)->cir));
      push_glvalue(variable_address(e), align);
    }
    break;
  }
  case eop_load: {
    a_value v;
    if (!pop_glvalue(v)) {
      ok = false;
      break;
    }
    if (v.kind == a_value_kind::glvalue &&
        mlir::isa<cir::FuncType>(
            mlir::cast<cir::PointerType>(v.value.getType()).getPointee())) {
      ok = format_error("LOAD of a function designator");
      break;
    }
    if (v.kind == a_value_kind::glvalue &&
        !type_is_complete(mlir::cast<cir::PointerType>(v.value.getType())
                              .getPointee())) {
      ok = format_error("LOAD of an incomplete record type");
      break;
    }
    push_prvalue(load_value(v, (r.ops[0].u & ECIL_ACCESS_VOLATILE) != 0));
    break;
  }
  case eop_store: {
    mlir::Value value;
    a_value target;
    mlir::Value stored;
    uint64_t result_kind = 0;
    uint_operand(r.ops[1], result_kind);
    if (!pop_prvalue(value) || !pop_glvalue(target)) {
      ok = false;
      break;
    }
    if (!type_is_complete(value.getType())) {
      ok = format_error("STORE of an incomplete record type");
      break;
    }
    if (!store_value(target, value, (r.ops[0].u & ECIL_ACCESS_VOLATILE) != 0,
                     result_kind == esr_none ? nullptr : &stored)) {
      ok = false;
      break;
    }
    if (result_kind == esr_glv)
      values_.push_back(target);
    else if (result_kind == esr_prv)
      push_prvalue(stored);
    break;
  }
  case eop_field: {
    uint64_t rec = 0;
    uint64_t index = 0;
    a_value base;
    a_value out;
    uint_operand(r.ops[0], rec);
    uint_operand(r.ops[1], index);
    if (!pop_glvalue(base) || !field_address(base, rec, index, out)) {
      ok = false;
      break;
    }
    values_.push_back(out);
    break;
  }
  case eop_extract: {
    uint64_t rec = 0;
    uint64_t index = 0;
    mlir::Value record_value;
    a_value base;
    a_value out;
    uint_operand(r.ops[0], rec);
    uint_operand(r.ops[1], index);
    if (!pop_prvalue(record_value)) {
      ok = false;
      break;
    }
    a_type *t = type_at(rec);
    if (t == nullptr || t->cir != record_value.getType()) {
      ok = format_error("EXTRACT of a value that is not that record");
      break;
    }
    base.kind = a_value_kind::glvalue;
    base.align = static_cast<uint32_t>(type_align(t->cir));
    base.value = alloca_here(t->cir, "extract.tmp", base.align);
    builder_.createStore(loc(), record_value, base.value, false, false,
                         builder_.getAlignmentAttr(base.align));
    if (!field_address(base, rec, index, out)) {
      ok = false;
      break;
    }
    push_prvalue(load_value(out, false));
    break;
  }
  case eop_deref: {
    mlir::Value pointer;
    if (!pop_prvalue(pointer)) {
      ok = false;
      break;
    }
    auto type = mlir::dyn_cast<cir::PointerType>(pointer.getType());
    if (!type) {
      ok = format_error("DEREF of a non-pointer");
      break;
    }
    push_glvalue(pointer, static_cast<uint32_t>(
                              mlir::isa<cir::VoidType>(type.getPointee())
                                  ? 1 : type_align(type.getPointee())));
    break;
  }
  case eop_addr: {
    a_value v;
    if (!pop_glvalue(v)) {
      ok = false;
      break;
    }
    if (v.kind != a_value_kind::glvalue) {
      ok = format_error("address of a bit-field");
      break;
    }
    push_prvalue(v.value);
    break;
  }
  case eop_addr_offset: {
    uint64_t tgid = 0;
    int64_t bytes = 0;
    mlir::Value pointer;
    a_type *t;
    uint_operand(r.ops[0], tgid);
    ecil::operand_i64(r.ops[1], bytes);
    t = type_at(tgid);
    if (t == nullptr || !pop_prvalue(pointer)) {
      ok = format_error("ADDR_OFFSET names an unknown type");
      break;
    }
    mlir::Value result = pointer;
    if (bytes != 0) {
      mlir::Type byte = compat::int_type(ctx_, 8, true);
      mlir::Value base = builder_.createBitcast(loc(), pointer,
                                                compat::pointer_to(byte));
      mlir::Value offset = builder_.getConstantInt(
          loc(), builder_.getSIntNTy(64), bytes);
      result = builder_.createPtrStride(loc(), base, offset);
    }
    if (!mlir::isa<cir::PointerType>(t->cir))
      ok = format_error("ADDR_OFFSET result type is not a pointer type");
    else
      push_prvalue(builder_.createBitcast(loc(), result, t->cir));
    break;
  }
  case eop_dup:
    if (values_.empty()) {
      ok = format_error("DUP on an empty stack");
      break;
    }
    values_.push_back(values_.back());
    break;
  case eop_drop:
    if (values_.empty()) {
      ok = format_error("DROP on an empty stack");
      break;
    }
    values_.pop_back();
    break;
  case eop_materialize_temp: {
    uint64_t l = 0;
    uint64_t tgid = 0;
    mlir::Value v;
    a_type *t;
    a_local local;
    uint_operand(r.ops[0], l);
    uint_operand(r.ops[1], tgid);
    t = type_at(tgid);
    if (t == nullptr || !pop_prvalue(v)) {
      ok = format_error("MATERIALIZE_TEMP names an unknown type");
      break;
    }
    if (!type_is_complete(t->cir)) {
      ok = format_error("MATERIALIZE_TEMP of an incomplete record type");
      break;
    }
    local.align = static_cast<uint32_t>(type_align(t->cir));
    local.type = tgid;
    local.address = alloca_here(t->cir, "ref.tmp" + std::to_string(l),
                                local.align);
    a_value target;
    target.kind = a_value_kind::glvalue;
    target.value = local.address;
    target.align = local.align;
    if (!store_value(target, v, false, nullptr)) {
      ok = false;
      break;
    }
    locals_[l] = local;
    push_glvalue(local.address, local.align);
    break;
  }
  case eop_add: case eop_sub: case eop_mul: case eop_div: case eop_rem:
  case eop_shl: case eop_shr: case eop_bit_and: case eop_bit_or:
  case eop_bit_xor: {
    mlir::Value lhs;
    mlir::Value rhs;
    mlir::Value result;
    if (!pop_prvalue(rhs) || !pop_prvalue(lhs)) {
      ok = false;
      break;
    }
    result = arithmetic(r.opcode, static_cast<uint32_t>(r.ops[0].u), lhs, rhs);
    if (!result) {
      std::string lhs_text;
      std::string rhs_text;
      llvm::raw_string_ostream lhs_stream(lhs_text);
      llvm::raw_string_ostream rhs_stream(rhs_text);
      lhs.getType().print(lhs_stream);
      rhs.getType().print(rhs_stream);
      ok = format_error(std::string(r.info->name) +
                        ": invalid operand types (" + lhs_stream.str() +
                        ", " + rhs_stream.str() + ")");
      break;
    }
    push_prvalue(result);
    break;
  }
  case eop_cmp: {
    mlir::Value lhs;
    mlir::Value rhs;
    if (!pop_prvalue(rhs) || !pop_prvalue(lhs)) {
      ok = false;
      break;
    }
    if (lhs.getType() != rhs.getType() &&
        mlir::isa<cir::PointerType>(lhs.getType()) &&
        mlir::isa<cir::PointerType>(rhs.getType()))
      rhs = builder_.createBitcast(loc(), rhs, lhs.getType());
    static const cir::CmpOpKind kinds[] = {
        cir::CmpOpKind::lt, cir::CmpOpKind::le, cir::CmpOpKind::gt,
        cir::CmpOpKind::ge, cir::CmpOpKind::eq, cir::CmpOpKind::ne,
        cir::CmpOpKind::one, cir::CmpOpKind::uno};
    push_prvalue(builder_.createCompare(loc(), kinds[r.ops[0].u], lhs, rhs));
    break;
  }
  case eop_neg: {
    mlir::Value v;
    if (!pop_prvalue(v)) {
      ok = false;
      break;
    }
    if (cir::isAnyFloatingPointType(v.getType()))
      push_prvalue(builder_.createFNeg(loc(), v));
    else
      push_prvalue(builder_.createMinus(loc(), v,
                                        (r.ops[0].u & ECIL_ARITH_NSW) != 0));
    break;
  }
  case eop_bit_not:
  case eop_log_not: {
    mlir::Value v;
    if (!pop_prvalue(v)) {
      ok = false;
      break;
    }
    push_prvalue(builder_.createNot(loc(), v));
    break;
  }
  case eop_to_bool: {
    mlir::Value v;
    if (!pop_prvalue(v)) {
      ok = false;
      break;
    }
    push_prvalue(to_bool(v));
    break;
  }
  case eop_ptr_stride: {
    mlir::Value index;
    mlir::Value pointer;
    uint64_t flags = r.ops[0].u;
    if (!pop_prvalue(index) || !pop_prvalue(pointer)) {
      ok = false;
      break;
    }
    if (!mlir::isa<cir::PointerType>(pointer.getType()) ||
        !mlir::isa<cir::IntType>(index.getType())) {
      ok = format_error("PTR_STRIDE needs a pointer and an integer");
      break;
    }
    auto it = mlir::cast<cir::IntType>(index.getType());
    if ((flags & ECIL_PTR_STRIDE_INDEX_UNSIGNED) && it.isSigned())
      index = builder_.createCast(loc(), cir::CastKind::integral, index,
                                  builder_.getUIntNTy(it.getWidth()));
    if (flags & ECIL_PTR_STRIDE_NEGATE) {
      /* Negate in a signed type wide enough for the index. */
      mlir::Type wide = builder_.getSIntNTy(
          std::max<unsigned>(64, mlir::cast<cir::IntType>(index.getType())
                                     .getWidth()));
      index = builder_.createCast(loc(), cir::CastKind::integral, index,
                                  wide);
      index = builder_.createMinus(loc(), index, false);
    } else if (mlir::cast<cir::IntType>(index.getType()).getWidth() < 64 ||
               !mlir::cast<cir::IntType>(index.getType()).isSigned()) {
      index = builder_.createCast(loc(), cir::CastKind::integral, index,
                                  builder_.getSIntNTy(64));
    }
    push_prvalue(builder_.createPtrStride(loc(), pointer, index));
    break;
  }
  case eop_ptr_diff: {
    uint64_t tgid = 0;
    mlir::Value lhs;
    mlir::Value rhs;
    a_type *t;
    uint_operand(r.ops[0], tgid);
    t = type_at(tgid);
    if (t == nullptr || !pop_prvalue(rhs) || !pop_prvalue(lhs)) {
      ok = format_error("PTR_DIFF names an unknown type");
      break;
    }
    if (lhs.getType() != rhs.getType())
      rhs = builder_.createBitcast(loc(), rhs, lhs.getType());
    push_prvalue(cir::PtrDiffOp::create(builder_, loc(), t->cir, lhs, rhs));
    break;
  }
  case eop_cast: {
    uint64_t tgid = 0;
    mlir::Value v;
    mlir::Value out;
    a_type *t;
    uint_operand(r.ops[1], tgid);
    t = type_at(tgid);
    if (t == nullptr || !pop_prvalue(v)) {
      ok = format_error("CAST names an unknown type");
      break;
    }
    if (!cast_value(static_cast<uint32_t>(r.ops[0].u), v, t->cir, out)) {
      ok = false;
      break;
    }
    push_prvalue(out);
    break;
  }
  case eop_va_start:
  case eop_va_end: {
    mlir::Value ap;
    if (!pop_prvalue(ap)) {
      ok = false;
      break;
    }
    if (r.opcode == eop_va_start)
      cir::VAStartOp::create(builder_, loc(), ap);
    else
      cir::VAEndOp::create(builder_, loc(), ap);
    break;
  }
  case eop_va_arg: {
    uint64_t tgid = 0;
    mlir::Value ap;
    a_type *t;
    uint_operand(r.ops[0], tgid);
    t = type_at(tgid);
    if (t == nullptr || !pop_prvalue(ap)) {
      ok = format_error("VA_ARG names an unknown type");
      break;
    }
    push_prvalue(cir::VAArgOp::create(builder_, loc(), t->cir, ap));
    break;
  }
  case eop_va_copy: {
    mlir::Value src;
    mlir::Value dst;
    if (!pop_prvalue(src) || !pop_prvalue(dst)) {
      ok = false;
      break;
    }
    cir::VACopyOp::create(builder_, loc(), dst, src);
    break;
  }
  case eop_builtin: {
    uint64_t id = 0;
    uint64_t nargs = 0;
    llvm::SmallVector<mlir::Value> args;
    uint_operand(r.ops[0], id);
    uint_operand(r.ops[1], nargs);
    args.resize(static_cast<size_t>(nargs));
    for (size_t k = static_cast<size_t>(nargs); ok && k > 0; --k)
      ok = pop_prvalue(args[k - 1]);
    if (!ok) {
      /* The failure is already recorded. */
    } else if (id == ebi_expect) {
      if (args.size() != 2)
        ok = format_error("__builtin_expect needs two arguments");
      else
        push_prvalue(cir::ExpectOp::create(builder_, loc(),
                                           args[0].getType(), args[0],
                                           args[1], mlir::FloatAttr()));
    } else if (id == ebi_unreachable) {
      cir::UnreachableOp::create(builder_, loc());
      start_new_block();
    } else if (id == ebi_trap) {
      cir::TrapOp::create(builder_, loc());
      start_new_block();
    } else if (id >= ebi_alloca && id <= ebi_signbit) {
      a_type *result_type = nullptr;
      uint64_t result_gid = 0;
      if (r.ops[2].kind != ecil::an_operand_kind::nil &&
          uint_operand(r.ops[2], result_gid))
        result_type = type_at(result_gid);
      ok = builtin_operation(id, args, result_type);
    } else {
      ok = unsupported("builtin " + std::to_string(id));
    }
    break;
  }
  default:
    ok = format_error(std::string(r.info->name) +
                      " is not an expression record");
    break;
  }
  return ok;
}  /* a_emitter::expression */

}  /* namespace edg_cir */
