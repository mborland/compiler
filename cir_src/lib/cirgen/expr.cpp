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

}  /* anonymous namespace */

/* The value stack. */

size_t a_emitter::values_in_frame() const
/*
The number of values on the stack that the innermost construct pushed: the
height above its value base.  A pop, DUP or DROP may take only these; a
stray one would otherwise eat a value of an enclosing construct and show up
only at the end of a frame, far from its cause.
*/
{
  size_t base = frames_.empty() ? 0 : frames_.back().value_base;

  return values_.size() > base ? values_.size() - base : 0;
}  /* a_emitter::values_in_frame */

bool a_emitter::pop_value(a_value &out)
/*
Pop the top entry, which must belong to the innermost construct (above its
value base).
*/
{
  bool ok = values_in_frame() != 0;

  if (!ok) {
    format_error(values_.empty()
                     ? "the value stack is empty"
                     : "a pop below the value base of the innermost "
                       "construct (the value stack holds only values of "
                       "an enclosing one)");
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
                            bool is_volatile, mlir::Value *result,
                            uint32_t store_flags)
/*
Store through a glvalue or a bit-field.  A whole-aggregate store whose value
was just loaded becomes cir.copy (cir.libc.memcpy of the data size with
STORE(DSIZE)).  STORE(INIT) of a call result is a direct store: the callee
constructs the object in the destination (ECIL spec 11.6).  *result (if
given) receives the value the object now holds.
*/
{
  bool ok = true;

  if (target.omitted) {
    /* An omitted empty field has no storage of its own and its address may
       be that of another member: there is nothing to write. */
    if (result != nullptr)
      *result = value;
  } else if (target.kind == a_value_kind::bitfield) {
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
    const a_record_info *record = record_info_of(value.getType());

    if ((store_flags & ECIL_STORE_DSIZE) != 0) {
      if (!load || !value.use_empty() || result != nullptr ||
          record == nullptr || pointee != value.getType()) {
        ok = format_error("STORE(DSIZE) needs the value just loaded from an "
                          "object of the same record type, and no result");
      } else {
        compat::memcpy_bytes(
            builder_, loc(), target.value, load.getAddr(), record->data_size,
            target.align,
            static_cast<unsigned>(load.getAlignment().value_or(1)));
        load->erase();
      }
    } else if (is_aggregate_type(value.getType()) && load &&
        value.use_empty() && result == nullptr &&
        pointee == value.getType() && !load.getIsVolatile()) {
      cir::CopyOp copy = builder_.createCopy(target.value, load.getAddr(),
                                             is_volatile);
      copy.setDstAlignment(target.align);
      copy.setSrcAlignment(
          static_cast<uint64_t>(load.getAlignment().value_or(1)));
      load->erase();
    } else if (is_aggregate_type(value.getType()) &&
               value.getDefiningOp<cir::CallOp>() && record != nullptr &&
               record->data_size < record->size &&
               pointee == value.getType() &&
               target.value.getDefiningOp<cir::BaseClassAddrOp>() != nullptr) {
      /* A call result stored into a base subobject: the callee may write
         the whole object, tail padding included, where a virtual base or
         another member can live (tail-padding1).  As CodeGen does for a
         potentially overlapping destination, the result lands in a temporary
         and only the data size is copied. */
      uint32_t align = static_cast<uint32_t>(type_align(value.getType()));
      mlir::Value temp = alloca_here(value.getType(), "agg.tmp.ensured",
                                     align);

      builder_.createStore(loc(), value, temp, false, false,
                           alignment_attr(align));
      compat::memcpy_bytes(builder_, loc(), target.value, temp,
                           record->data_size, target.align, align);
    } else if (is_aggregate_type(value.getType()) &&
               value.getDefiningOp<cir::CallOp>() &&
               pointee == value.getType() &&
               (store_flags & ECIL_STORE_INIT) == 0) {
      if (is_indirect_class(value.getType())) {
        ok = format_error("a NONTRIVIAL_ABI call result can only be stored "
                          "with STORE(INIT) (or materialized)");
      } else {
        /* An aggregate call result may be returned through a hidden sret
           pointer that CallConvLowering aims at the store's destination.  The
           destination may alias something the callee reads (a1 = f() where f
           reads a1), so the result lands in a fresh temporary first, as
           CodeGen does for aliased destinations. */
        uint32_t align = static_cast<uint32_t>(type_align(value.getType()));
        mlir::Value temp = alloca_here(value.getType(), "agg.tmp", align);
        builder_.createStore(loc(), value, temp, false, false,
                             alignment_attr(align));
        cir::CopyOp copy = builder_.createCopy(target.value, temp,
                                               is_volatile);
        copy.setDstAlignment(target.align);
        copy.setSrcAlignment(align);
      }
    } else {
      builder_.createStore(loc(), value, target.value, is_volatile, false,
                           alignment_attr(target.align));
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
    ok = internal("FIELD names a type that is not a complete record");
  else if (index >= t->record->fields.size())
    ok = internal("FIELD index past the record's fields");
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
      mlir::Type field_type = type_at(f.type)->cir;
      mlir::Value member_address;

      if (member == no_member) {
        /* An empty base or member has no storage: it sits at its offset
           (zero bytes of it are ever accessed). */
        member_address = address;
        if (f.byte_offset != 0) {
          mlir::Type byte = compat::int_type(ctx_, 8, true);
          mlir::Value bytes = builder_.createBitcast(
              loc(), address, compat::pointer_to(byte));
          mlir::Value stride = builder_.getConstantInt(
              loc(), builder_.getSIntNTy(64),
              static_cast<int64_t>(f.byte_offset));
          member_address = builder_.createPtrStride(loc(), bytes, stride);
        }
      } else {
        member_address = builder_.createGetMember(
            loc(), cir::PointerType::get(rec.members[member]), address,
            f.name, member);
      }
      /* A base or overlapping member is stored as its ".base" record; the
         value of the field is the complete type. */
      if (mlir::cast<cir::PointerType>(member_address.getType())
              .getPointee() != field_type)
        member_address = builder_.createBitcast(
            loc(), member_address, compat::pointer_to(field_type));
      out.kind = a_value_kind::glvalue;
      out.value = member_address;
      out.align = derived_align(base.align,
                                rec.is_union ? 0 : f.byte_offset);
      out.bitfield = nullptr;
      out.omitted = member == no_member;
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
    ok = internal("AGG_SEL on a type that is neither record nor array");
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
        : mlir::isa<cir::DataMemberType, cir::MethodType>(t)
              ? cir::CastKind::member_ptr_to_bool
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
for the rest.  An object with a pointer to a data member (12.7) gets its null
constant instead: stored, or copied from a private constant when big.
*/
{
  uint64_t size = type_size(type);

  if (!zero_initializable(type)) {
    a_const null_constant = {null_attribute(type), false};
    a_value target;

    target.kind = a_value_kind::glvalue;
    target.value = address;
    target.align = align;
    materialize_constant_into(target, null_constant, type);
  } else if (size <= 16) {
    builder_.createStore(loc(), builder_.getNullValue(type, loc()), address,
                         false, false, alignment_attr(align));
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

  if (target.omitted) {
    /* Nothing to write (see store_value). */
  } else if (target.kind == a_value_kind::bitfield) {
    mlir::Value v = get_constant_value(c.attr);
    ok = store_value(target, v, false, nullptr);
  } else if (c.is_zero && is_aggregate_type(type)) {
    zero_fill(target.value, type, target.align);
  } else if (is_aggregate_type(type) && type_size(type) > 256) {
    /* The name derives from the function's final name, so it is chosen at
       finish (a_generated_symbol); a unique placeholder serves until then. */
    unsigned index = temp_counter_++;
    std::string_view name = claim_symbol(
        keep_string("__ecil.const." + std::to_string(generated_.size())), 0);
    cir::GlobalOp g = compat::create_global(
        builder_, module_, loc(), name, type, true,
        cir::GlobalLinkageKind::PrivateLinkage);
    generated_.push_back({g, fn_entity_ != nullptr ? fn_entity_->gid : 0,
                          index});
    g.setInitialValueAttr(c.attr);
    g.setAlignmentAttr(alignment_attr(type_align(type)));
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
    ok = false;
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

bool a_emitter::check_class_arguments(
                              const llvm::SmallVectorImpl<mlir::Value> &args)
/*
A by-value argument of a NONTRIVIAL_ABI record is the load of an alloca (a
local, a parameter or a temporary): CallConvLowering passes that storage
itself, which is what keeps the copy elided.  Any other argument value
(a call result, a member, an object reached through a pointer) would be
rejected by it or, worse, copied silently (ECIL spec 11.6).
*/
{
  bool ok = true;

  for (mlir::Value arg : args) {
    if (is_indirect_class(arg.getType())) {
      cir::LoadOp load = arg.getDefiningOp<cir::LoadOp>();
      if (!load || !load.getAddr().getDefiningOp<cir::AllocaOp>()) {
        ok = format_error("a NONTRIVIAL_ABI record argument must be the "
                          "LOAD of a local, parameter or temporary object "
                          "(copy it into a temporary first)");
        break;
      }
    }
  }
  return ok;
}  /* a_emitter::check_class_arguments */

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
  mlir::Value vtt;
  mlir::Value callee;
  cir::CallOp op;
  bool noreturn;
  bool with_vtt;
  mlir::NamedAttribute nothrow = nothrow_attr();
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
  with_vtt = !indirect && (flags & ECIL_CALL_VTT) != 0;
  call_type = type_at(t_call);
  if (call_type == nullptr || call_type->kind != a_type_kind::function_type)
    ok = format_error("CALL names a type that is not a function type");
  if (ok) {
    /* Result and parameters of a call must be complete. */
    for (uint64_t p : call_type->params)
      ok = ok && type_is_complete(type_at(p)->cir);
    ok = ok && type_is_complete(type_at(call_type->target)->cir);
    if (!ok)
      ok = format_error("CALL with an incomplete record type as argument "
                        "or result");
  }
  if (ok && nargs + (indirect ? 1u : 0u) + (with_vtt ? 1u : 0u) >
                values_in_frame())
    /* Bound the allocation by what the value stack can supply. */
    ok = format_error("CALL needs " + std::to_string(nargs) +
                      " argument values but the value stack holds " +
                      std::to_string(values_in_frame()));
  if (ok && with_vtt && nargs == 0)
    ok = format_error("CALL VTT without an object argument");
  if (ok) {
    /* The VTT_ADDR operand sits between argument 0 and argument 1. */
    args.resize(static_cast<size_t>(nargs) + (with_vtt ? 1u : 0u));
    for (size_t k = args.size(); ok && k > 0; --k)
      ok = pop_prvalue(args[k - 1]);
    if (ok && with_vtt) {
      vtt = args[1];
      args.erase(args.begin() + 1);
    }
  }
  if (ok)
    ok = check_class_arguments(args);
  if (ok && with_vtt) {
    if (vtt.getType() != vtt_parameter_type())
      ok = format_error("CALL VTT: the operand is not a VTT address");
    else
      args.insert(args.begin() + 1, vtt);
  }
  if (ok && !throws_.empty()) {
    /* A call that can throw while a thrown object is being initialized: the
       cleanup that frees the object starts here (materialize_throws). */
    bool may_throw = !is_nothrow_call(flags, nullptr);

    if (may_throw && !indirect) {
      const a_entity *callee_entity = entity_for(gid, true);

      if (callee_entity != nullptr && is_nothrow_call(0, callee_entity))
        may_throw = false;
    }
    if (may_throw)
      ok = materialize_throws();
  }
  if (ok) {
    if (is_nothrow_call(flags, nullptr))
      attrs.push_back(nothrow);
    cir::FuncType wanted = mlir::cast<cir::FuncType>(call_type->cir);
    if (with_vtt)
      wanted = with_vtt_parameter(wanted);
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

    if (!indirect && (flags & ECIL_CALL_VIRTUAL)) {
      uint32_t slot = 0;

      e = entity_for(gid, true);
      if (e != nullptr && attrs.empty() && is_nothrow_call(0, e))
        attrs.push_back(nothrow);
      if (e == nullptr) {
        ok = false;
      } else if (!ids_.vslot_of(static_cast<uint32_t>(gid), slot)) {
        ok = internal("CALL VIRTUAL of a function without a slot");
      } else if (!args_match(mlir::cast<cir::FuncOp>(e->op)
                                 .getFunctionType())) {
        ok = format_error("CALL VIRTUAL: the call type differs from the "
                          "function's");
      } else {
        ok = virtual_call(*e, args, slot, attrs, op);
      }
    } else if (!indirect) {
      e = entity_for(gid, true);
      if (e == nullptr) {
        ok = false;  /* entity_for reported the internal error. */
      } else if (with_vtt != e->has_vtt) {
        ok = format_error("CALL " + std::string(with_vtt ? "with" : "without")
                          + " VTT of a function that " +
                          (e->has_vtt ? "takes" : "does not take") +
                          " a VTT parameter");
      } else {
        cir::FuncOp fn = mlir::cast<cir::FuncOp>(e->op);
        e->referenced = true;
        if (attrs.empty() && is_nothrow_call(0, e))
          attrs.push_back(nothrow);
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
    /* After a noreturn call control does not continue: cir.unreachable.
       Inside a cleanup scope body that would leave the body with no exit,
       which FlattenCFG does not accept (an assertion): there the call is
       marked noreturn, as CIRGen does, and the code goes on in the same
       block (the body ends in its yield). */
    if (noreturn) {
      bool single_block = false;

      /* A STATIC_INIT region (ctor region of a global or local_init) is a
         single block: no cir.unreachable and no new block there either. */
      for (const a_frame &f : frames_) {
        if (f.kind == a_frame_kind::static_init)
          single_block = true;
      }
      if (cleanups_.empty() && !single_block) {
        cir::UnreachableOp::create(builder_, loc());
        start_new_block();
      } else {
        op->setAttr(noreturn_name_, unit_attr_);
      }
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

bool a_emitter::arm_terminated(size_t value_base)
/*
True when the arm of a conditional expression that is closing ended in
THROW_END, RETHROW or UNREACHABLE with the value stack as at its start: the
current block is the empty one the terminator left, nothing branches to it.
It is erased (the arm ends in the cir.unreachable before it).
*/
{
  bool result = false;
  mlir::Block *block = builder_.getBlock();

  if (values_.size() == value_base && block != &block->getParent()->front() &&
      block->hasNoPredecessors()) {
    result = true;
    if (block->empty()) {
      builder_.clearInsertionPoint();
      block->erase();
    } else {
      builder_.setInsertionPointToEnd(block);
      cir::UnreachableOp::create(builder_, loc());
    }
  }
  return result;
}  /* a_emitter::arm_terminated */

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
        ok = false;
        break;
      }
      if (is_indirect_class(t->cir)) {
        ok = format_error("a ?: result of a NONTRIVIAL_ABI record type: "
                          "use a void TERN whose arms STORE(INIT)");
        break;
      }
      result_type = t->cir;
    }
    /* An aggregate result does not leave the arms as a cir.yield operand:
       the CIR-to-CIR call conversion lowering (the pin after 036efe53)
       accepts a record value only in a store, a call, a return or a member
       read.  Both arms store into a temporary and the value is loaded after
       the ternary, as CIRGen's aggregate conditional writes into its
       destination slot. */
    mlir::Value slot;

    if (result_type && is_aggregate_type(result_type))
      slot = alloca_here(result_type, "tern.tmp",
                         static_cast<uint32_t>(type_align(result_type)));
    mlir::Operation *op = compat::open_ternary(
        builder_, loc(), cond, slot ? mlir::Type() : result_type);
    builder_.createBlock(&op->getRegion(0));
    open_frame(a_frame_kind::ternary, op);
    frames_.back().result_type = result_type;
    frames_.back().tern_slot = slot;
    break;
  }
  case eop_tern_else: {
    a_frame *f = top_frame(a_frame_kind::ternary, "TERN_ELSE");
    if (f == nullptr) {
      ok = false;
      break;
    }
    if (arm_terminated(f->value_base)) {
      /* A throwing arm contributes no value. */
    } else if (f->result_type) {
      mlir::Value v;
      if (!pop_prvalue(v)) {
        ok = false;
        break;
      }
      if (f->tern_slot) {
        builder_.createStore(loc(), v, f->tern_slot, false, false,
                             alignment_attr(type_align(v.getType())));
        builder_.createYield(loc());
      } else {
        builder_.createYield(loc(), mlir::ValueRange{v});
      }
    } else {
      builder_.createYield(loc());
    }
    if (values_.size() != f->value_base) {
      ok = format_error("values left on the stack in a ?: arm");
      break;
    }
    builder_.createBlock(&f->op->getRegion(1));
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
    mlir::Value slot = f->tern_slot;
    bool terminated = arm_terminated(f->value_base);
    if (terminated) {
      /* A throwing arm contributes no value. */
    } else if (has_result) {
      mlir::Value v;
      if (!pop_prvalue(v)) {
        ok = false;
        break;
      }
      if (slot) {
        builder_.createStore(loc(), v, slot, false, false,
                             alignment_attr(type_align(v.getType())));
        builder_.createYield(loc());
      } else {
        builder_.createYield(loc(), mlir::ValueRange{v});
      }
    } else {
      builder_.createYield(loc());
    }
    builder_.setInsertionPointAfter(op);
    if (!pop_frame()) {
      ok = false;
      break;
    }
    if (slot) {
      mlir::Type type = mlir::cast<cir::PointerType>(slot.getType())
                            .getPointee();

      push_prvalue(builder_.createLoad(
          loc(), slot, false, static_cast<uint32_t>(type_align(type))));
    } else if (has_result) {
      push_prvalue(op->getResult(0));
    }
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
    builder_.createBlock(&op->getRegion(0));
    open_frame(is_and ? a_frame_kind::land : a_frame_kind::lor, op);
    if (!is_and) {
      /* LOR: the true arm is the constant true; the rhs goes in the false
         arm. */
      builder_.createYield(loc(), mlir::ValueRange{builder_.getTrue(loc())});
      builder_.createBlock(&op->getRegion(1));
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
    mlir::Value rhs;
    if (arm_terminated(f->value_base)) {
      /* A throwing operand contributes no value. */
    } else {
      rhs = pop_bool_condition();
      if (!rhs) {
        ok = false;
        break;
      }
      builder_.createYield(loc(), mlir::ValueRange{rhs});
    }
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
      ok = false;
      break;
    }
    if (!pop_glvalue(address) || address.kind != a_value_kind::glvalue) {
      ok = format_error("AGG_INIT_BEGIN needs an object address");
      break;
    }
    if ((flags & ECIL_AGG_INIT_ZERO_FILL) != 0 && !address.omitted)
      zero_fill(address.value, t->cir, address.align);
    open_frame(a_frame_kind::agg_init, nullptr);
    a_init_target target = {address, tgid};
    targets_.push_back(target);
    break;
  }
  case eop_agg_sel: {
    uint64_t index = 0;
    a_value out;
    uint_operand(r.ops[0], index);
    if (targets_.empty()) {
      ok = internal("AGG_SEL outside AGG_INIT_BEGIN");
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
    resolve_vptr_markers(c, type);
    ok = materialize_constant_into(target, c, type);
    break;
  }
  case eop_init_zero: {
    a_value target;
    if (!pop_glvalue(target)) {
      ok = false;
      break;
    }
    if (target.omitted) {
      /* Nothing to write (see store_value). */
    } else if (target.kind == a_value_kind::bitfield) {
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

bool a_emitter::class_cast(const ecil::a_record &r)
/*
BASE_CAST / DERIVED_CAST: a pointer to a derived class becomes a pointer to
its direct base at a byte offset, or back (cir.base_class_addr and
cir.derived_class_addr).  Without the nonnull flag, null stays null.
*/
{
  bool ok = true;
  bool to_derived = r.opcode == eop_derived_cast;
  uint64_t t_der = 0;
  uint64_t t_base = 0;
  int64_t offset = 0;
  a_type *from;
  a_type *to;
  mlir::Value pointer;

  uint_operand(r.ops[to_derived ? 1 : 0], t_der);
  uint_operand(r.ops[to_derived ? 0 : 1], t_base);
  ecil::operand_i64(r.ops[2], offset);
  from = type_at(to_derived ? t_base : t_der);
  to = type_at(to_derived ? t_der : t_base);
  if (from == nullptr || to == nullptr || !pop_prvalue(pointer)) {
    ok = false;
  } else if (!mlir::isa<cir::PointerType>(pointer.getType())) {
    ok = format_error(std::string(r.info->name) + " of a non-pointer");
  } else if (offset < 0) {
    ok = format_error(std::string(r.info->name) + " with a negative "
                      "offset");
  } else {
    mlir::Type source = compat::pointer_to(from->cir);
    if (pointer.getType() != source)
      pointer = builder_.createBitcast(loc(), pointer, source);
    push_prvalue(compat::class_address(
        builder_, loc(), to_derived, pointer, compat::pointer_to(to->cir),
        static_cast<uint64_t>(offset),
        (r.ops[3].u & ECIL_CAST_NONNULL) != 0));
  }
  return ok;
}  /* a_emitter::class_cast */

bool a_emitter::zero_bytes(const ecil::a_record &r)
/*
ZERO_BYTES: zero the first bytes bytes of the object a glvalue designates
(cir.libc.memset), as for the data size of an overlapping subobject.
*/
{
  bool ok = true;
  uint64_t bytes = 0;
  a_value target;

  uint_operand(r.ops[0], bytes);
  if (!pop_glvalue(target)) {
    ok = false;
  } else if (target.kind != a_value_kind::glvalue) {
    ok = format_error("ZERO_BYTES of a bit-field");
  } else if (!zero_initializable(
                 mlir::cast<cir::PointerType>(target.value.getType())
                     .getPointee())) {
    ok = format_error("ZERO_BYTES of an object with a pointer to a data "
                      "member (null is -1: use INIT_ZERO or "
                      "INIT_ZERO_BASE)");
  } else if (target.omitted) {
    /* An empty class has at most one byte, which the object shares with
       another member: nothing is written for 0 or 1; more is an error. */
    if (bytes > 1)
      ok = format_error("ZERO_BYTES of more than one byte through an "
                        "omitted empty field");
  } else if (bytes != 0) {
    uint64_t size = type_size(mlir::cast<cir::PointerType>(
                                  target.value.getType()).getPointee());

    /* The range lies inside the glvalue's object.  A glvalue made by DEREF
       may designate an array element (`new T[n]()` zeroes n elements from
       the first one, through a pointer that may have been reloaded from a
       local or carried through casts), so it may be zeroed beyond itself by
       a whole number of elements; a glvalue rooted in a named object is
       bounded by its type. */
    if (size != 0 && bytes > size && !target.from_deref)
      ok = format_error("ZERO_BYTES of " + std::to_string(bytes) +
                        " bytes through a glvalue of a " +
                        std::to_string(size) + "-byte type: more than the "
                        "object it designates (only a glvalue made by "
                        "DEREF, such as the first element of a new[] "
                        "allocation, may be zeroed beyond itself)");
    else if (size != 0 && bytes > size && bytes % size != 0)
      ok = format_error("ZERO_BYTES of " + std::to_string(bytes) +
                        " bytes through a DEREF glvalue of a " +
                        std::to_string(size) + "-byte type: not a whole "
                        "number of elements");
    else
      compat::memset_zero(builder_, loc(), target.value,
                          builder_.getUIntNTy(64), bytes, target.align);
  }
  return ok;
}  /* a_emitter::zero_bytes */

bool a_emitter::cmp3way(const ecil::a_record &r)
/*
CMP3WAY: the built-in <=> of two arithmetic or pointer prvalues of one type
(cir.cmp3way).  The result is the integer of type t_result holding lt, eq or
gt, or unordered for a floating operand pair that has a NaN.
*/
{
  bool ok = true;
  uint64_t tgid = 0;
  int64_t values[4] = {0, 0, 0, 0};
  bool partial = r.ops[4].kind != ecil::an_operand_kind::nil;
  mlir::Value lhs;
  mlir::Value rhs;
  a_type *t;

  uint_operand(r.ops[0], tgid);
  t = type_at(tgid);
  for (int k = 0; k < 4; ++k) {
    if (r.ops[k + 1].kind != ecil::an_operand_kind::nil)
      ecil::operand_i64(r.ops[k + 1], values[k]);
  }
  if (!pop_prvalue(rhs) || !pop_prvalue(lhs)) {
    ok = false;
  } else if (t == nullptr || !mlir::isa<cir::IntType>(t->cir)) {
    ok = internal("CMP3WAY result type is not an integer");
  } else {
    mlir::Type operand_type = lhs.getType();
    bool is_floating = cir::isAnyFloatingPointType(operand_type);

    if (lhs.getType() != rhs.getType() &&
        mlir::isa<cir::PointerType>(lhs.getType()) &&
        mlir::isa<cir::PointerType>(rhs.getType()))
      rhs = builder_.createBitcast(loc(), rhs, lhs.getType());
    if (lhs.getType() != rhs.getType()) {
      ok = format_error("CMP3WAY operands have different types");
    } else if (!mlir::isa<cir::IntType, cir::BoolType, cir::PointerType>(
                   operand_type) && !is_floating) {
      ok = format_error("CMP3WAY operands are not arithmetic or pointer "
                        "values");
    } else if (partial != is_floating) {
      ok = format_error(is_floating ? "CMP3WAY of floating operands needs "
                                      "an unordered value"
                                    : "CMP3WAY unordered is only for "
                                      "floating operands");
    } else {
      cir::IntType result_type = mlir::cast<cir::IntType>(t->cir);
      unsigned bits = result_type.getWidth();

      /* The op's constants are signed: an unsigned category type is given
         as its bit pattern. */
      if (bits < 64) {
        for (int64_t &value : values)
          value = static_cast<int64_t>(static_cast<uint64_t>(value) <<
                                       (64 - bits)) >> (64 - bits);
      }
      mlir::Value result = compat::cmp3way(
          builder_, loc(), bits, lhs, rhs, partial,
          values[0], values[1], values[2], values[3]);

      /* The op yields a signed integer; an unsigned category type takes
         the same bits. */
      if (result.getType() != t->cir)
        result = builder_.createCast(loc(), cir::CastKind::integral, result,
                                     t->cir);
      push_prvalue(result);
    }
  }
  return ok;
}  /* a_emitter::cmp3way */

bool a_emitter::new_array_record(const ecil::a_record &r)
/*
NEW_ARRAY_SIZE: the byte size of new T[n], elements and cookie, with the
overflow checks of CIRGen (cir.mul.overflow, cir.add.overflow, or, select): an
overflow or a negative count gives SIZE_MAX, so operator new[] fails.
NEW_ARRAY_COOKIE: (void *p, count) -> T *: the count is stored at
p + cookie - sizeof(size_t), the elements start at p + cookie (Itanium).
*/
{
  bool ok = true;
  uint64_t tgid = 0;
  uint64_t cookie = 0;
  a_type *element;
  mlir::Value count;
  mlir::Type word = compat::int_type(ctx_, 64, false);

  uint_operand(r.ops[0], tgid);
  uint_operand(r.ops[1], cookie);
  element = type_at(tgid);
  if (r.opcode == eop_new_array_size_throw && !throws_.empty())
    ok = materialize_throws();
  if (!ok) {
    /* materialize_throws reported it. */
  } else if (element == nullptr || !pop_prvalue(count)) {
    ok = false;
  } else if (!mlir::isa<cir::IntType>(count.getType())) {
    ok = format_error(std::string(r.info->name) + " needs an integer count");
  } else if (!type_is_complete(element->cir)) {
    ok = internal("array new of an incomplete record type");
  } else if (r.opcode == eop_new_array_size ||
             r.opcode == eop_new_array_size_throw) {
    auto count_type = mlir::cast<cir::IntType>(count.getType());
    mlir::Value wide = count.getType() == word ? count
        : builder_.createCast(loc(), cir::CastKind::integral, count, word);
    mlir::Value bytes = builder_.getConstantInt(
        loc(), word, static_cast<int64_t>(type_size(element->cir)));
    mlir::Value total;
    mlir::Value overflow;
    mlir::Value invalid;   /* A negative count. (Counts wider than 64 bits
                              cannot occur: such integer types are not
                              supported.) */

    if (count_type.isSigned()) {
      invalid = builder_.createCompare(
          loc(), cir::CmpOpKind::lt, count,
          builder_.getConstantInt(loc(), count.getType(), 0));
    }
    compat::overflow_op(builder_, loc(), compat::a_overflow_kind::mul, word,
                        wide, bytes, total, overflow);
    if (cookie != 0) {
      mlir::Value extra = builder_.getConstantInt(
          loc(), word, static_cast<int64_t>(cookie));
      mlir::Value sum;
      mlir::Value second;
      compat::overflow_op(builder_, loc(), compat::a_overflow_kind::add, word,
                          total, extra, sum, second);
      total = sum;
      overflow = builder_.createOr(loc(), overflow, second);
    }
    if (invalid)
      overflow = builder_.createOr(loc(), overflow, invalid);
    if (r.opcode == eop_new_array_size_throw) {
      /* A count that does not fit throws std::bad_array_new_length. */
      a_entity *bad = library_entity(
          "__cxa_throw_bad_array_new_length", true,
          compat::function_type({}, compat::void_type(ctx_), false), false);

      if (bad == nullptr) {
        ok = false;
      } else {
        cir::IfOp guard = compat::open_if(builder_, loc(), overflow);
        mlir::OpBuilder::InsertionGuard saved(builder_);

        bad->referenced = true;
        mlir::cast<cir::FuncOp>(bad->op)->setAttr(noreturn_name_,
                                                  unit_attr_);
        builder_.setInsertionPointToStart(&guard.getThenRegion().front());
        builder_.createCallOp(loc(), mlir::cast<cir::FuncOp>(bad->op),
                              llvm::ArrayRef<mlir::Value>());
        cir::UnreachableOp::create(builder_, loc());
      }
      if (ok)
        push_prvalue(total);
    } else {
      mlir::Value limit = builder_.getConstantInt(loc(), word, -1);
      push_prvalue(builder_.createSelect(loc(), overflow, limit, total));
    }
  } else {
    mlir::Value pointer;
    mlir::Type pointer_type = compat::pointer_to(element->cir);

    ok = pop_prvalue(pointer);
    /* Operands: the pointer first, then the count (just popped). */
    if (ok && !mlir::isa<cir::PointerType>(pointer.getType())) {
      ok = format_error("NEW_ARRAY_COOKIE needs the allocation pointer "
                        "below the count");
    } else if (ok && cookie != 0 && cookie < 8) {
      ok = format_error("NEW_ARRAY_COOKIE: a cookie is at least "
                        "sizeof(size_t) bytes");
    } else if (ok && cookie == 0) {
      push_prvalue(builder_.createBitcast(loc(), pointer, pointer_type));
    } else if (ok) {
      mlir::Type byte = compat::int_type(ctx_, 8, false);
      mlir::Value bytes = builder_.createBitcast(loc(), pointer,
                                                 compat::pointer_to(byte));
      mlir::Value wide = count.getType() == word ? count
          : builder_.createCast(loc(), cir::CastKind::integral, count, word);
      mlir::Value slot = builder_.createPtrStride(
          loc(), bytes, builder_.getConstantInt(
              loc(), builder_.getSIntNTy(64),
              static_cast<int64_t>(cookie - 8)));
      mlir::Value first = builder_.createPtrStride(
          loc(), bytes, builder_.getConstantInt(
              loc(), builder_.getSIntNTy(64), static_cast<int64_t>(cookie)));

      slot = builder_.createBitcast(loc(), slot, compat::pointer_to(word));
      builder_.createStore(loc(), wide, slot, false, false,
                           alignment_attr(8));
      push_prvalue(builder_.createBitcast(loc(), first, pointer_type));
    }
  }
  return ok;
}  /* a_emitter::new_array_record */

bool a_emitter::delete_record(const ecil::a_record &r)
/*
DELETE(g_delete_fn, g_dtor|nil, t_obj, flags): a null-checked delete.  An
array delete is cir.delete_array (the pin lowers it: it reads the cookie and
runs the element destructors); a single object is the destructor call
followed by the operator delete call, which takes the size and the alignment
when the flags say so.
*/
{
  bool ok = true;
  uint64_t g_fn = 0;
  uint64_t g_dtor = 0;
  uint64_t tgid = 0;
  uint64_t flags = 0;
  bool has_dtor = r.ops[1].kind != ecil::an_operand_kind::nil;
  a_entity *fn;
  a_entity *dtor = nullptr;
  a_type *object;
  mlir::Value pointer;

  /* A delete runs a destructor and an operator that can throw. */
  if (!throws_.empty())
    ok = materialize_throws();

  uint_operand(r.ops[0], g_fn);
  if (has_dtor)
    uint_operand(r.ops[1], g_dtor);
  uint_operand(r.ops[2], tgid);
  uint_operand(r.ops[3], flags);
  fn = entity_for(g_fn, true);
  if (has_dtor)
    dtor = entity_for(g_dtor, true);
  object = type_at(tgid);
  if (fn == nullptr || (has_dtor && dtor == nullptr) || object == nullptr ||
      !pop_prvalue(pointer)) {
    ok = false;
  } else if (!mlir::isa<cir::PointerType>(pointer.getType())) {
    ok = format_error("DELETE of a value that is not a pointer");
  } else if (!deletable_type(*object) &&
             (has_dtor || (flags & (ECIL_DELETE_SIZED | ECIL_DELETE_ALIGNED |
                                    ECIL_DELETE_VIRTUAL)) != 0)) {
    /* A pointer to an incomplete class (C++23 allows the delete) or to void
       (undefined, but compiled) has no destructor to call and no size or
       alignment: only the plain operator delete or delete[] is possible. */
    ok = format_error("DELETE of an incomplete class or void takes no "
                      "destructor and none of the sized, aligned and "
                      "virtual flags");
  } else {
    bool array = (flags & ECIL_DELETE_ARRAY) != 0;
    bool destroying = (flags & ECIL_DELETE_DESTROYING) != 0;
    bool sized = (flags & ECIL_DELETE_SIZED) != 0;
    bool aligned = (flags & ECIL_DELETE_ALIGNED) != 0;
    cir::FuncOp delete_fn = mlir::cast<cir::FuncOp>(fn->op);
    bool plain = !deletable_type(*object);

    if (destroying && (has_dtor || array || plain ||
                       (flags & (ECIL_DELETE_VIRTUAL | ECIL_DELETE_GLOBAL)))) {
      ok = format_error("DELETE DESTROYING takes a complete class and no "
                        "destructor, and none of ARRAY, VIRTUAL and GLOBAL");
    }
    uint64_t align = plain ? 1 : type_align(object->cir);
    mlir::Value not_null = builder_.createPtrIsNotNull(pointer);
    cir::IfOp guard = compat::open_if(builder_, loc(), not_null);
    mlir::Value typed = builder_.createBitcast(
        loc(), pointer, compat::pointer_to(object->cir));

    fn->referenced = true;
    builder_.setInsertionPointToStart(&guard.getThenRegion().front());
    if (array && !plain) {
      compat::delete_array(builder_, loc(), typed, delete_fn, sized,
                           aligned ? align : 0,
                           has_dtor ? mlir::cast<cir::FuncOp>(dtor->op)
                                    : cir::FuncOp(),
                           align,
                           exceptions_ && has_dtor &&
                               !is_nothrow_call(0, dtor));
      if (has_dtor)
        dtor->referenced = true;
    } else if ((flags & ECIL_DELETE_VIRTUAL) &&
               !(flags & ECIL_DELETE_GLOBAL)) {
      if (!has_dtor)
        ok = format_error("DELETE VIRTUAL needs the virtual destructor");
      else
        ok = virtual_delete(g_dtor, pointer, object);
    } else {
      mlir::NamedAttribute nothrow = nothrow_attr();
      llvm::SmallVector<mlir::NamedAttribute> del_attrs;
      llvm::SmallVector<mlir::NamedAttribute> dtor_attrs;
      cir::FuncType type = delete_fn.getFunctionType();
      /* The empty std::destroying_delete_t tag counts one. */
      unsigned want = 1 + (sized ? 1 : 0) + (aligned ? 1 : 0) +
                      (destroying ? 1 : 0);
      /* A destructor that can throw: the operator delete is its cleanup
         (clang's CallObjectDelete). */
      bool wrap = exceptions_ && has_dtor && !is_nothrow_call(0, dtor);
      cir::CleanupScopeOp scope;
      /* ::delete of a polymorphic object (clang's emitVirtualObjectDelete):
         the complete object's address is computed first (offset-to-top), D1
         is called through the vtable and the global operator delete frees
         the complete object, also when D1 throws. */
      bool global_virtual = (flags & ECIL_DELETE_VIRTUAL) != 0;
      mlir::Value delete_pointer = pointer;
      uint32_t d1_slot = 0;

      if (global_virtual) {
        if (!has_dtor || !ids_.vslot_of(static_cast<uint32_t>(g_dtor),
                                        d1_slot)) {
          ok = format_error("DELETE GLOBAL needs the virtual destructor");
        } else {
          mlir::Type word = compat::int_type(ctx_, 64, true);
          mlir::Type byte_pointer = compat::pointer_to(
              compat::int_type(ctx_, 8, false));
          mlir::Value object_pointer = builder_.createBitcast(
              loc(), pointer, compat::pointer_to(object->cir));
          mlir::Value vptr_address = compat::vtable_get_vptr(
              builder_, loc(), object_pointer);
          mlir::Value vptr = builder_.createLoad(loc(), vptr_address, false,
                                                 8);
          mlir::Value vtable = builder_.createBitcast(loc(), vptr,
                                                      byte_pointer);
          mlir::Value slot_address = builder_.createPtrStride(
              loc(), vtable, builder_.getConstantInt(loc(), word, -16));
          mlir::Value offset = builder_.createLoad(
              loc(), builder_.createBitcast(loc(), slot_address,
                                            compat::pointer_to(word)),
              false, 8);
          mlir::Value bytes = builder_.createBitcast(loc(), pointer,
                                                     byte_pointer);

          delete_pointer = builder_.createPtrStride(loc(), bytes, offset);
        }
      }
      if (is_nothrow_call(0, fn))
        del_attrs.push_back(nothrow);
      if (has_dtor && is_nothrow_call(0, dtor))
        dtor_attrs.push_back(nothrow);
      if (type.getNumInputs() != want) {
        ok = format_error("DELETE: the operator delete takes " +
                          std::to_string(type.getNumInputs()) +
                          " arguments but the flags give " +
                          std::to_string(want));
      }
      if (ok && wrap) {
        scope = compat::open_cleanup_scope(builder_, loc(),
                                           cir::CleanupKind::All);
        builder_.setInsertionPointToStart(&scope.getBodyRegion().front());
      }
      if (ok && has_dtor) {
        cir::FuncOp dtor_fn = mlir::cast<cir::FuncOp>(dtor->op);
        mlir::Value self = pointer;
        if (dtor_fn.getFunctionType().getNumInputs() != 1)
          ok = format_error("DELETE: the destructor takes one argument");
        else if (self.getType() != dtor_fn.getFunctionType().getInput(0))
          self = builder_.createBitcast(
              loc(), pointer, dtor_fn.getFunctionType().getInput(0));
        if (ok && global_virtual) {
          cir::FuncType dtor_type = dtor_fn.getFunctionType();
          mlir::Value object_pointer = builder_.createBitcast(
              loc(), pointer, compat::pointer_to(object->cir));
          mlir::Value vptr = builder_.createLoad(
              loc(), compat::vtable_get_vptr(builder_, loc(), object_pointer),
              false, 8);
          mlir::Value function = builder_.createLoad(
              loc(), compat::vtable_function_slot(
                  builder_, loc(),
                  compat::pointer_to(compat::pointer_to(dtor_type)), vptr,
                  d1_slot), false, 8);

          builder_.createIndirectCallOp(loc(), function, dtor_type,
                                        llvm::ArrayRef<mlir::Value>{self},
                                        dtor_attrs);
          dtor->referenced = true;
        } else if (ok) {
          dtor->calls.push_back(builder_.createCallOp(loc(), dtor_fn, self,
                                                      dtor_attrs));
          dtor->referenced = true;
        }
      }
      if (ok && wrap) {
        builder_.createYield(loc());
        builder_.setInsertionPointToStart(&scope.getCleanupRegion().front());
      }
      if (ok) {
        llvm::SmallVector<mlir::Value> args;
        unsigned k = 0;
        mlir::Value first = delete_pointer;
        if (first.getType() != type.getInput(0))
          first = builder_.createBitcast(loc(), delete_pointer,
                                         type.getInput(0));
        args.push_back(first);
        if (destroying && type.getNumInputs() > 1) {
          /* The tag is an empty class passed by value: a temporary, as
             clang's emitDeleteCall makes it. */
          mlir::Type tag = type.getInput(1);
          uint32_t tag_align = static_cast<uint32_t>(type_align(tag));
          mlir::Value tag_slot = alloca_here(tag, "destroying.delete.tag",
                                             tag_align);

          args.push_back(builder_.createLoad(loc(), tag_slot, false,
                                             tag_align));
        }
        for (uint64_t value : {sized ? type_size(object->cir) : 0,
                               aligned ? align : 0}) {
          bool present = k == 0 ? sized : aligned;
          ++k;
          if (!present)
            continue;
          mlir::Type input = type.getInput(args.size());
          if (!mlir::isa<cir::IntType>(input)) {
            ok = format_error("DELETE: a size or alignment parameter is not "
                              "an integer");
            break;
          }
          args.push_back(builder_.getConstantInt(
              loc(), input, static_cast<int64_t>(value)));
        }
        if (ok)
          fn->calls.push_back(builder_.createCallOp(loc(), delete_fn, args,
                                                    del_attrs));
      }
      if (wrap) {
        builder_.createYield(loc());
        builder_.setInsertionPointAfter(scope.getOperation());
      }
    }
    terminate_region(guard.getThenRegion());
    builder_.setInsertionPointAfter(guard.getOperation());
  }
  return ok;
}  /* a_emitter::delete_record */

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
                                     alignment_attr(16), size);
      push_prvalue(builder_.createBitcast(loc(), memory, result_type->cir));
    }
  } else if (id == ebi_is_fp_class || id == ebi_signbit) {
    if (args.empty() || !cir::isAnyFloatingPointType(args[0].getType())) {
      ok = format_error("floating-point class builtins need a floating "
                        "operand");
    } else if (id == ebi_signbit) {
      push_prvalue(compat::sign_bit(builder_, loc(), args[0]));
    } else {
      auto constant = args.size() == 2
                          ? args[1].getDefiningOp<cir::ConstantOp>()
                          : cir::ConstantOp();
      auto mask = constant ? mlir::dyn_cast<cir::IntAttr>(constant.getValue())
                           : cir::IntAttr();
      if (!mask) {
        ok = format_error("is_fp_class needs a constant mask");
      } else {
        push_prvalue(compat::is_fp_class(builder_, loc(), args[0],
                                         mask.getUInt()));
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
      compat::overflow_op(builder_, loc(),
                          id == ebi_add_overflow ? compat::a_overflow_kind::add
                          : id == ebi_sub_overflow
                              ? compat::a_overflow_kind::sub
                              : compat::a_overflow_kind::mul,
                          types[2], x, y, value, overflow);
      builder_.createStore(
          loc(), value, pointer, false, false,
          alignment_attr(
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
      out = compat::bit_op(builder_, loc(), compat::a_bit_op::byte_swap,
                           with_sign(x, false));
      out = with_sign(out, int_of(x).isSigned());
      break;
    case ebi_clz:
      out = compat::bit_op(builder_, loc(),
                           compat::a_bit_op::count_leading_zeros,
                           with_sign(x, false));
      break;
    case ebi_ctz:
      out = compat::bit_op(builder_, loc(),
                           compat::a_bit_op::count_trailing_zeros,
                           with_sign(x, false));
      break;
    case ebi_popcount:
      out = compat::bit_op(builder_, loc(),
                           compat::a_bit_op::population_count,
                           with_sign(x, false));
      break;
    case ebi_parity:
      out = compat::bit_op(builder_, loc(), compat::a_bit_op::parity,
                           with_sign(x, false));
      break;
    case ebi_ffs:
      out = compat::bit_op(builder_, loc(), compat::a_bit_op::find_first_set,
                           with_sign(x, true));
      break;
    default:
      out = compat::bit_op(builder_, loc(),
                           compat::a_bit_op::redundant_sign_bits,
                           with_sign(x, true));
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
    a_local *local;
    uint_operand(r.ops[0], l);
    local = local_at(l);
    if (local == nullptr) {
      ok = false;  /* local_at reported the internal error. */
      break;
    }
    /* The first thing a normal cleanup does to the NRVO variable (the
       destruction the writer sends for it): skip it once it is returned. */
    if (local->nrvo && !frames_.empty() &&
        frames_.back().kind == a_frame_kind::cleanup &&
        frames_.back().cleanup_mode == 0 &&
        frames_.back().nrvo_guard == nullptr &&
        builder_.getBlock()->empty()) {
      mlir::Value returned = builder_.createLoad(loc(), nrvo_flag(), false, 1);
      cir::IfOp guard = compat::open_if(
          builder_, loc(), builder_.createNot(returned));

      frames_.back().nrvo_guard = guard.getOperation();
      builder_.setInsertionPointToStart(&guard.getThenRegion().front());
    }
    push_glvalue(local->address, local->align);
    break;
  }
  case eop_gref: {
    uint64_t g = 0;
    a_entity *entity;
    uint_operand(r.ops[0], g);
    entity = entity_at(g);
    if (entity == nullptr) {
      ok = false;
      break;
    }
    a_entity &e = *entity;
    if (e.is_function) {
      push_glvalue(function_address(e), 1);
    } else {
      /* A variable of a record type without a body has no alignment: the
         use is in a body the writer is about to drop. */
      uint32_t align = e.align != 0
          ? e.align
          : !type_is_complete(type_at(e.type)->cir)
              ? 1
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
    uint64_t store_flags = 0;
    uint_operand(r.ops[0], store_flags);
    uint_operand(r.ops[1], result_kind);
    if (!pop_prvalue(value) || !pop_glvalue(target)) {
      ok = false;
      break;
    }
    if (!type_is_complete(value.getType())) {
      ok = format_error("STORE of an incomplete record type");
      break;
    }
    if (!store_value(target, value, (store_flags & ECIL_ACCESS_VOLATILE) != 0,
                     result_kind == esr_none ? nullptr : &stored,
                     static_cast<uint32_t>(store_flags))) {
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
    if (is_indirect_class(t->cir)) {
      ok = format_error("EXTRACT from a NONTRIVIAL_ABI record prvalue "
                        "(materialize it first)");
      break;
    }
    base.kind = a_value_kind::glvalue;
    base.align = static_cast<uint32_t>(type_align(t->cir));
    base.value = alloca_here(t->cir, "extract.tmp", base.align);
    builder_.createStore(loc(), record_value, base.value, false, false,
                         alignment_attr(base.align));
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
    /* A void, function or incomplete-record lvalue has no data layout:
       alignment 1 (an incomplete one cannot be used further). */
    push_glvalue(pointer, static_cast<uint32_t>(
                              mlir::isa<cir::VoidType, cir::FuncType>(
                                  type.getPointee()) ||
                                      !type_is_complete(type.getPointee())
                                  ? 1 : type_align(type.getPointee())));
    values_.back().from_deref = true;
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
      ok = false;
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
      ok = internal("ADDR_OFFSET result type is not a pointer type");
    else
      push_prvalue(builder_.createBitcast(loc(), result, t->cir));
    break;
  }
  case eop_dup:
    if (values_in_frame() == 0) {
      ok = format_error(values_.empty()
                            ? "DUP on an empty stack"
                            : "DUP below the value base of the innermost "
                              "construct");
      break;
    }
    if (values_.back().kind == a_value_kind::prvalue &&
        is_indirect_class(values_.back().value.getType())) {
      ok = format_error("DUP of a NONTRIVIAL_ABI record prvalue");
      break;
    }
    values_.push_back(values_.back());
    break;
  case eop_drop:
    if (values_in_frame() == 0) {
      ok = format_error(values_.empty()
                            ? "DROP on an empty stack"
                            : "DROP below the value base of the innermost "
                              "construct");
      break;
    }
    if (values_.back().kind == a_value_kind::prvalue) {
      const a_record_info *info =
          record_info_of(values_.back().value.getType());
      if (info != nullptr && info->nontrivial_abi && info->nontrivial_dtor) {
        ok = format_error("DROP of a NONTRIVIAL_ABI record prvalue that "
                          "has a destructor (materialize it and destroy it)");
        break;
      }
    }
    values_.pop_back();
    break;
  case eop_materialize_temp: {
    uint64_t l = 0;
    uint64_t tgid = 0;
    mlir::Value v;
    a_type *t;
    a_local *local;
    uint_operand(r.ops[0], l);
    uint_operand(r.ops[1], tgid);
    t = type_at(tgid);
    if (t == nullptr || (local = new_local(l)) == nullptr || !pop_prvalue(v)) {
      ok = false;
      break;
    }
    if (!type_is_complete(t->cir)) {
      ok = internal("MATERIALIZE_TEMP of an incomplete record type");
      break;
    }
    local->align = static_cast<uint32_t>(type_align(t->cir));
    local->type = tgid;
    local->address = alloca_here(t->cir, "ref.tmp" + std::to_string(l),
                                 local->align);
    a_value target;
    target.kind = a_value_kind::glvalue;
    target.value = local->address;
    target.align = local->align;
    if (!store_value(target, v, false, nullptr, ECIL_STORE_INIT)) {
      ok = false;
      break;
    }
    push_glvalue(local->address, local->align);
    break;
  }
  case eop_return_slot:
    push_glvalue(return_slot(), static_cast<uint32_t>(
                     type_align(type_at(fn_return_type_)->cir)));
    break;
  case eop_base_cast:
  case eop_derived_cast:
    ok = class_cast(r);
    break;
  case eop_zero_bytes:
    ok = zero_bytes(r);
    break;
  case eop_cmp3way:
    ok = cmp3way(r);
    break;
  case eop_new_array_size:
  case eop_new_array_size_throw:
  case eop_new_array_cookie:
    ok = new_array_record(r);
    break;
  case eop_delete:
    ok = delete_record(r);
    break;
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
    /* A bool index (p + (a < b)) is promoted to int. */
    if (mlir::isa<cir::BoolType>(index.getType()))
      index = builder_.createCast(loc(), cir::CastKind::bool_to_int, index,
                                  builder_.getSIntNTy(32));
    if (!mlir::isa<cir::PointerType>(pointer.getType()) ||
        !mlir::isa<cir::IntType>(index.getType())) {
      std::string pt, it;
      llvm::raw_string_ostream ps(pt), is(it);
      ps << pointer.getType();
      is << index.getType();
      ok = format_error("PTR_STRIDE needs a pointer and an integer (got " +
                        ps.str() + " and " + is.str() + ")");
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
    }
    /* The stride operand is a 64-bit integer (a 128-bit index is cut). */
    if (mlir::cast<cir::IntType>(index.getType()).getWidth() != 64 ||
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
      ok = false;
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
      ok = false;
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
      ok = false;
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
    if (nargs > values_in_frame())
      /* Bound the allocation by what the value stack can supply. */
      ok = format_error("BUILTIN needs " + std::to_string(nargs) +
                        " argument values but the value stack holds " +
                        std::to_string(values_in_frame()));
    if (ok)
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
