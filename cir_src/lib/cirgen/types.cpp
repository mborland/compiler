/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
types.cpp -- TY_* records to CIR types, and record layout lowering.  The
record lowering is a port of clang/lib/CIR/CodeGen/CIRGenRecordLayoutBuilder
(CIRRecordLowering) that works from the layout facts of the ECIL stream (size,
data size, alignment, field byte/bit offsets and widths) instead of an
ASTRecordLayout: explicit padding members, bit-field access units, packed
detection and union storage.
*/

#include <algorithm>
#include <cstring>

#include "emitter.h"

namespace edg_cir {

namespace {

uint64_t align_to(uint64_t value, uint64_t align)
/*
Round value up to a multiple of align (align >= 1).
*/
{
  return align == 0 ? value : (value + align - 1) / align * align;
}  /* align_to */

}  /* anonymous namespace */

/* Diagnostics and operand helpers. */

bool a_emitter::fail(an_edg_cir_status status, uint32_t code,
                     const std::string &text)
/*
Report an error to the session and return false.
*/
{
  session_.fail(status, code, text);
  return false;
}  /* a_emitter::fail */

bool a_emitter::format_error(const std::string &text)
/*
A malformed stream (semantic error the format layer cannot see).
*/
{
  return fail(EDG_CIR_ERROR_FORMAT, edc_format, text);
}  /* a_emitter::format_error */

bool a_emitter::v1_flags(const ecil::an_operand &op, uint32_t mask,
                         const char *what)
/*
Reject C++ flag bits the library does not implement yet.
*/
{
  uint64_t flags = 0;
  bool ok = true;

  if (uint_operand(op, flags) && (flags & mask) != 0)
    ok = unsupported(std::string(what) + ": C++ constructs (ECIL v1 flag "
                     "bits) are not supported yet");
  return ok;
}  /* a_emitter::v1_flags */

bool a_emitter::internal(const std::string &text)
/*
A failed internal check: something the validator guarantees did not hold.
*/
{
  return fail(EDG_CIR_ERROR_INTERNAL, edc_internal,
              "internal error in the emitter: " + text);
}  /* a_emitter::internal */

bool a_emitter::unsupported(const std::string &text)
/*
A construct the CIR back end cannot express.
*/
{
  return fail(EDG_CIR_ERROR_UNSUPPORTED, edc_unsupported, text);
}  /* a_emitter::unsupported */

bool a_emitter::uint_operand(const ecil::an_operand &op, uint64_t &out)
/*
Fetch an unsigned operand; false for nil or negative.
*/
{
  return ecil::operand_u64(op, out);
}  /* a_emitter::uint_operand */

bool a_emitter::int_operand_apint(const ecil::an_operand &op, unsigned bits,
                                  llvm::APInt &out)
/*
Convert an integer operand (any tag, up to 128 bits) to an APInt of the given
width, sign-extending signed forms and truncating wider ones.
*/
{
  bool result = true;

  switch (op.kind) {
  case ecil::an_operand_kind::uint:
    out = llvm::APInt(bits, op.u, /*isSigned=*/false);
    break;
  case ecil::an_operand_kind::sint:
    out = llvm::APInt(bits, static_cast<uint64_t>(op.i), /*isSigned=*/true);
    break;
  case ecil::an_operand_kind::u128:
  case ecil::an_operand_kind::i128: {
    uint64_t words[2] = {0, 0};
    std::memcpy(words, op.data, 16);
    llvm::APInt wide(128, llvm::ArrayRef<uint64_t>(words, 2));
    out = bits >= 128 ? (op.kind == ecil::an_operand_kind::i128
                           ? wide.sext(bits) : wide.zext(bits))
                      : wide.trunc(bits);
    break;
  }
  default:
    result = false;
    break;
  }
  return result;
}  /* a_emitter::int_operand_apint */

std::string_view a_emitter::string_for(const ecil::an_operand &op, bool &ok)
/*
Resolve a sid operand to its string (a view into the reader's arena); nil
gives "" (ok stays true).  The validator checked that every sid was
introduced, so an unknown one is an internal error.
*/
{
  std::string_view result;
  uint64_t sid = 0;

  ok = true;
  if (op.kind != ecil::an_operand_kind::nil) {
    if (uint_operand(op, sid) && ids_.has_string(sid)) {
      result = ids_.string(sid);
    } else {
      ok = false;
      internal("string id used before its STR record");
    }
  }
  return result;
}  /* a_emitter::string_for */

/* The emitter's setup. */

a_emitter::a_emitter(an_edg_cir_session &session)
/*
Create the emitter for the session's module (already holding its mandatory
attributes).
*/
  : session_(session), cm_(*session.module), ctx_(&session.module->context),
    module_(session.module->module.get()), builder_(*ctx_),
    layout_(module_), loc_(mlir::UnknownLoc::get(ctx_)), reg_bits_(64),
    cheap_unaligned_(false), c_language_(true), ids_(session.reader->ids())
{
  reg_bits_ = session.header.reg_bits != 0 ? session.header.reg_bits : 64;
  cheap_unaligned_ = (session.header.target_flags &
                      ECIL_TARGET_CHEAP_UNALIGNED_BF) != 0;
  c_language_ = session.header.language == elang_c;
  unit_attr_ = builder_.getUnitAttr();
  nothrow_name_ = mlir::StringAttr::get(ctx_,
                                        cir::CIRDialect::getNoThrowAttrName());
  noreturn_name_ = mlir::StringAttr::get(
                      ctx_, cir::CIRDialect::getNoReturnAttrName());
  builder_.setInsertionPointToEnd(module_.getBody());
}  /* a_emitter::a_emitter */

/* Types. */

a_type *a_emitter::type_at(uint64_t gid)
/*
The type object of a type G-id.  The validator guarantees that every type
operand names an introduced type, so a miss is an internal error.
*/
{
  a_type *result = nullptr;
  const ecil::a_gid_entry *entry = ids_.find_gid(gid);

  if (entry == nullptr || entry->payload == nullptr ||
      entry->kind == ecil::gk_func || entry->kind == ecil::gk_var)
    internal("type G-id " + std::to_string(gid) + " has no type object");
  else
    result = static_cast<a_type *>(entry->payload);
  return result;
}  /* a_emitter::type_at */

a_type *a_emitter::new_type(uint64_t gid)
/*
Create the type object of a newly introduced type and hang it on the G-id's
payload slot (the validator already introduced the G-id once).
*/
{
  a_type *result = nullptr;
  ecil::a_gid_entry *entry = ids_.find_gid(gid);

  if (entry == nullptr || entry->payload != nullptr) {
    internal("type G-id " + std::to_string(gid) +
             " was not introduced or has an object already");
  } else {
    type_store_.emplace_back();
    result = &type_store_.back();
    entry->payload = result;
  }
  return result;
}  /* a_emitter::new_type */

a_local *a_emitter::local_at(uint64_t l)
/*
The local object of a value L-id of the current function (validated: LREF
names a live value).
*/
{
  a_local *result = nullptr;
  ecil::a_lid_entry *entry = ids_.find_lid(l);

  if (entry == nullptr || entry->payload == nullptr ||
      entry->state != ecil::ls_value)
    internal("L-id " + std::to_string(l) + " has no local object");
  else
    result = static_cast<a_local *>(entry->payload);
  return result;
}  /* a_emitter::local_at */

a_local *a_emitter::new_local(uint64_t l)
/*
Create the local object of a value L-id the validator just introduced.
*/
{
  a_local *result = nullptr;
  ecil::a_lid_entry *entry = ids_.find_lid(l);

  if (entry == nullptr || entry->state != ecil::ls_value ||
      entry->payload != nullptr) {
    internal("L-id " + std::to_string(l) + " was not introduced as a value");
  } else {
    local_store_.emplace_back();
    result = &local_store_.back();
    entry->payload = result;
  }
  return result;
}  /* a_emitter::new_local */

uint64_t a_emitter::type_size(mlir::Type t)
/*
Return the size in bytes of a complete CIR type.
*/
{
  return layout_.getTypeSize(t).getFixedValue();
}  /* a_emitter::type_size */

uint64_t a_emitter::type_align(mlir::Type t, bool natural)
/*
Return the ABI alignment in bytes used for objects of type t.  A record uses
the alignment the front end gave it, unless natural is set: the lowering of a
record must use the DataLayout alignment of its members, as CIRGen does, so
that the offsets CIR computes agree with the front end's.
*/
{
  uint64_t result = 1;

  if (auto bf = mlir::dyn_cast<cir::BitFieldType>(t)) {
    result = bf.getStorageType() ? type_align(bf.getStorageType(), natural)
                                 : 1;
  } else if (auto array = mlir::dyn_cast<cir::ArrayType>(t)) {
    result = type_align(array.getElementType(), natural);
  } else if (auto record = mlir::dyn_cast<cir::RecordType>(t)) {
    /* The front end's alignment is authoritative (a packed record is less
       aligned than its DataLayout alignment, an aligned(N) one more). */
    auto found = record_of_type_.find(record.getAsOpaquePointer());

    if (!natural && found != record_of_type_.end() &&
        found->second->complete)
      result = found->second->align;
    else
      result = layout_.getTypeABIAlignment(t);
  } else {
    result = layout_.getTypeABIAlignment(t);
  }
  return result;
}  /* a_emitter::type_align */

bool a_emitter::type_is_complete(mlir::Type t) const
/*
True unless t is a record without a body or an array of one: computing the
size, alignment or storage of such a type is a front-end contract violation.
Arrays are peeled with a loop.
*/
{
  while (auto array = mlir::dyn_cast<cir::ArrayType>(t))
    t = array.getElementType();
  auto record = mlir::dyn_cast<cir::RecordType>(t);
  return record == nullptr || record.isComplete();
}  /* a_emitter::type_is_complete */

uint32_t a_emitter::derived_align(uint32_t base, uint64_t offset) const
/*
Alignment of an address at base alignment plus offset bytes: the largest
power of two dividing both.
*/
{
  uint32_t result = base;

  if (offset != 0) {
    uint64_t low = offset & (~offset + 1);
    if (low < result)
      result = static_cast<uint32_t>(low);
  }
  return result == 0 ? 1 : result;
}  /* a_emitter::derived_align */

std::string a_emitter::unique_record_name(const std::string &hint)
/*
A unique record name: the hint, then hint.1, hint.2, ... (as CIRGen).
*/
{
  std::string base = hint.empty() ? "anon" : hint;
  unsigned &count = record_names_[base];
  std::string result = count == 0 ? base : base + "." + std::to_string(count);

  ++count;
  return result;
}  /* a_emitter::unique_record_name */

bool a_emitter::type_record(const ecil::a_record &r)
/*
Handle one type introduction record (TY_*, except the body records).
*/
{
  bool ok = true;
  uint64_t gid = 0;
  uint64_t a = 0;
  uint64_t b = 0;
  uint64_t c = 0;
  a_type *t;

  if (!uint_operand(r.ops[0], gid)) {
    ok = internal("type id must be an unsigned integer");
  } else {
    t = new_type(gid);
    ok = t != nullptr;
  }
  if (ok) switch (r.opcode) {
  case eop_ty_void:
    t->kind = a_type_kind::void_type;
    t->cir = compat::void_type(ctx_);
    break;
  case eop_ty_bool:
    uint_operand(r.ops[1], a);
    uint_operand(r.ops[2], b);
    t->kind = a_type_kind::bool_type;
    t->cir = compat::bool_type(ctx_);
    t->size = a;
    t->align = b;
    break;
  case eop_ty_int: {
    uint64_t bits = 0;
    uint_operand(r.ops[1], bits);
    uint_operand(r.ops[3], a);
    uint_operand(r.ops[4], b);
    t->is_signed = r.ops[2].kind == ecil::an_operand_kind::boolean
                     ? r.ops[2].u != 0 : (uint_operand(r.ops[2], c) && c != 0);
    /* The widths of C and C++ integer types: 8 to 64 and __int128. */
    if (bits == 0 || bits > 128 ||
        (bits != 128 && !cir::isValidFundamentalIntWidth(
                            static_cast<unsigned>(bits)))) {
      ok = unsupported("integer type of " + std::to_string(bits) +
                       " bits is not supported");
    } else {
      t->kind = a_type_kind::int_type;
      t->bits = static_cast<unsigned>(bits);
      t->cir = compat::int_type(ctx_, t->bits, t->is_signed);
      t->size = a;
      t->align = b;
    }
    break;
  }
  case eop_ty_float: {
    bool known = false;
    uint_operand(r.ops[1], a);
    uint_operand(r.ops[2], b);
    uint_operand(r.ops[3], c);
    t->cir = compat::float_type(ctx_, static_cast<unsigned>(a), known);
    if (!known) {
      ok = unsupported("floating-point format " + std::to_string(a) +
                       " is not supported");
    } else {
      t->kind = a_type_kind::float_type;
      t->float_format = static_cast<unsigned>(a);
      t->size = b;
      t->align = c;
    }
    break;
  }
  case eop_ty_ptr: {
    a_type *pointee;
    uint_operand(r.ops[1], a);
    pointee = type_at(a);
    if (pointee == nullptr) {
      ok = false;
    } else {
      t->kind = a_type_kind::pointer_type;
      t->target = a;
      t->cir = compat::pointer_to(pointee->cir);
      t->size = 8;
      t->align = 8;
    }
    break;
  }
  case eop_ty_array: {
    a_type *element;
    uint64_t flags = 0;
    uint_operand(r.ops[1], a);
    element = type_at(a);
    uint_operand(r.ops[3], flags);
    if (element == nullptr) {
      ok = false;
    } else {
      uint64_t count = 0;
      if (r.ops[2].kind != ecil::an_operand_kind::nil)
        uint_operand(r.ops[2], count);
      if (flags & (ECIL_ARRAY_INCOMPLETE | ECIL_ARRAY_ZERO_LENGTH))
        count = 0;
      t->kind = a_type_kind::array_type;
      t->target = a;
      t->count = count;
      t->flags = static_cast<uint32_t>(flags);
      t->cir = compat::array_of(element->cir, count);
    }
    break;
  }
  case eop_ty_func: {
    a_type *ret;
    uint64_t flags = 0;
    std::vector<mlir::Type> inputs;
    ecil::an_array_reader reader = ecil::array_elements(r.ops[3]);
    ecil::an_operand element;

    uint_operand(r.ops[1], a);
    uint_operand(r.ops[2], flags);
    ret = type_at(a);
    ok = ret != nullptr;
    while (ok && ecil::next_array_element(reader, element)) {
      uint64_t p = 0;
      a_type *param;
      uint_operand(element, p);
      param = type_at(p);
      if (param == nullptr) {
        ok = false;
      } else {
        t->params.push_back(p);
        inputs.push_back(param->cir);
      }
    }
    if (ok) {
      t->kind = a_type_kind::function_type;
      t->target = a;
      t->variadic = (flags & ECIL_FUNC_TYPE_VARIADIC) != 0;
      t->no_proto = (flags & ECIL_FUNC_TYPE_NO_PROTO) != 0;
      t->cir = compat::function_type(inputs, ret->cir, t->variadic);
    }
    break;
  }
  case eop_ty_record_decl: {
    std::string name;
    bool good;
    uint_operand(r.ops[1], a);
    name = std::string(string_for(r.ops[2], good));
    if (!good) {
      ok = false;
    } else {
      records_.emplace_back();
      a_record_info &info = records_.back();
      info.is_union = a == erk_union;
      info.is_class = a == erk_class;
      info.nontrivial_abi = false;
      info.nontrivial_dtor = false;
      info.complete = false;
      info.packed = false;
      info.size = info.data_size = 0;
      info.align = 1;
      info.pending_fields = 0;
      info.name = unique_record_name(name);
      t->kind = a_type_kind::record_type;
      t->record = &info;
      t->cir = info.is_union ? compat::incomplete_union(ctx_, info.name)
                             : compat::incomplete_struct(ctx_, info.name,
                                                         info.is_class);
      record_of_type_[t->cir.getAsOpaquePointer()] = &info;
    }
    break;
  }
  case eop_ty_opaque: {
    uint_operand(r.ops[1], a);
    uint_operand(r.ops[2], b);
    t->kind = a_type_kind::opaque_type;
    t->size = a;
    t->align = b;
    t->cir = a == 0 ? static_cast<mlir::Type>(compat::array_of(
                          compat::int_type(ctx_, 8, false), 0))
                    : static_cast<mlir::Type>(compat::array_of(
                          compat::int_type(ctx_, 8, false), a));
    break;
  }
  default:
    ok = format_error("unexpected type record");
    break;
  }
  return ok;
}  /* a_emitter::type_record */

bool a_emitter::record_body(const ecil::a_record &r)
/*
TY_RECORD_BODY: remember the layout facts and await the REC_FIELD records.
*/
{
  bool ok = true;
  uint64_t gid = 0;
  uint64_t size = 0;
  uint64_t data_size = 0;
  uint64_t align = 1;
  uint64_t flags = 0;
  uint64_t n = 0;
  a_type *t;

  uint_operand(r.ops[0], gid);
  t = type_at(gid);
  if (t == nullptr)
    ok = false;
  else if (pending_body_gid_ != 0 || t->kind != a_type_kind::record_type ||
           t->record->complete)
    ok = internal("TY_RECORD_BODY out of step with the validator");
  if (ok) {
    uint_operand(r.ops[1], size);
    uint_operand(r.ops[2], data_size);
    uint_operand(r.ops[3], align);
    uint_operand(r.ops[4], flags);
    uint_operand(r.ops[5], n);
    if (data_size > size)
      ok = format_error("TY_RECORD_BODY: data_size " +
                        std::to_string(data_size) + " exceeds size " +
                        std::to_string(size));
  }
  if (ok) {
    a_record_info &info = *t->record;
    info.size = size;
    info.data_size = data_size;
    info.align = align == 0 ? 1 : align;
    info.nontrivial_abi = (flags & ECIL_RECORD_NONTRIVIAL_ABI) != 0;
    info.nontrivial_dtor = (flags & ECIL_RECORD_NONTRIVIAL_DTOR) != 0;
    info.fields.clear();
    info.pending_fields = static_cast<uint32_t>(n);
    t->size = size;
    t->align = info.align;
    pending_body_gid_ = gid;
    if (n == 0)
      ok = complete_record_layout(gid, *t);
  }
  return ok;
}  /* a_emitter::record_body */

bool a_emitter::record_field(const ecil::a_record &r)
/*
REC_FIELD: append to the record being defined; complete it with the last one.
*/
{
  bool ok = true;
  a_type *t = pending_body_gid_ != 0 ? type_at(pending_body_gid_) : nullptr;
  uint64_t v = 0;
  uint64_t flags = 0;
  a_field f;
  bool good = true;

  if (t == nullptr || t->record->pending_fields == 0) {
    ok = internal("REC_FIELD outside a TY_RECORD_BODY");
  } else {
    f.type = 0;
    uint_operand(r.ops[0], f.type);
    uint_operand(r.ops[1], f.byte_offset);
    f.bit_offset = 0;
    f.bit_width = 0;
    f.is_bitfield = r.ops[3].kind != ecil::an_operand_kind::nil;
    if (r.ops[2].kind != ecil::an_operand_kind::nil &&
        uint_operand(r.ops[2], v))
      f.bit_offset = static_cast<uint32_t>(v);
    if (f.is_bitfield && uint_operand(r.ops[3], v))
      f.bit_width = static_cast<uint32_t>(v);
    f.name = std::string(string_for(r.ops[4], good));
    uint_operand(r.ops[5], flags);
    f.is_unnamed = (flags & ECIL_FIELD_UNNAMED) != 0 ||
                   r.ops[4].kind == ecil::an_operand_kind::nil;
    f.empty_for_abi = (flags & ECIL_FIELD_EMPTY_FOR_ABI) != 0;
    f.bitfield_signed = (flags & ECIL_FIELD_BITFIELD_SIGNED) != 0;
    f.flexible_array = (flags & ECIL_FIELD_FLEXIBLE_ARRAY) != 0;
    f.is_base = (flags & ECIL_FIELD_BASE) != 0;
    f.overlapping = (flags & (ECIL_FIELD_BASE |
                              ECIL_FIELD_POTENTIALLY_OVERLAPPING)) != 0;
    f.omitted = f.overlapping && f.empty_for_abi;
    if (!good || type_at(f.type) == nullptr)
      ok = false;
    else if (f.is_bitfield &&
             !mlir::isa<cir::IntType, cir::BoolType>(type_at(f.type)->cir))
      ok = internal("bit-field member of a non-integer type");
    else if (f.overlapping &&
             (f.is_bitfield || type_at(f.type)->kind !=
                                   a_type_kind::record_type ||
              !type_at(f.type)->record->complete))
      ok = format_error("REC_FIELD: a BASE or POTENTIALLY_OVERLAPPING "
                        "member must be a complete record");
    else if (f.is_base && t->record->is_union)
      ok = format_error("REC_FIELD: a union has no base class");
    else if (f.overlapping && type_at(f.type)->record->is_union &&
             type_at(f.type)->record->data_size <
                 type_at(f.type)->record->size)
      ok = unsupported("a union with reusable tail padding as a base "
                       "subobject or [[no_unique_address]] member");
    else {
      t->record->fields.push_back(f);
      if (--t->record->pending_fields == 0)
        ok = complete_record_layout(pending_body_gid_, *t);
    }
  }
  return ok;
}  /* a_emitter::record_field */

/* Record layout lowering (CIRRecordLowering). */

namespace {

struct a_member {
  uint64_t                 offset;
  mlir::Type               data;      /* null: a bit-field placeholder. */
  cir::RecordMemberKind    kind;
  int                      field;     /* -1 for padding and units. */
};  /* a_member */

}  /* anonymous namespace */

uint64_t a_emitter::consistent_data_size(const a_record_info &rec)
/*
The record's data size, raised to the end of its last non-overlapping member
or base when the front end reported less.  The Itanium data size of a class
ends after every data member (whole, tail padding included) and every base
(its data size only); a smaller value cannot be right and would, among other
things, make the record look packed.  (The front end's own data size of a class
whose last member is itself a class with tail padding, "struct D2 : B { B x;
};", falls 3 bytes short of the layout it uses for derived classes.)
*/
{
  uint64_t end = rec.data_size;

  for (const a_field &f : rec.fields) {
    const a_type *ft = type_at(f.type);
    uint64_t f_end;

    if (f.omitted || (f.empty_for_abi && f.overlapping))
      continue;
    if (f.is_bitfield)
      f_end = (field_bit_offset(f) + f.bit_width + 7) / 8;
    else if (f.overlapping && ft->record != nullptr)
      f_end = f.byte_offset + ft->record->data_size;
    else
      f_end = f.byte_offset + ft->size;
    end = std::max(end, f_end);
  }
  return std::min(end, rec.size);
}  /* a_emitter::consistent_data_size */

bool a_emitter::complete_record_layout(uint64_t gid, a_type &t)
/*
All fields have arrived: lower the record and complete its CIR type.
*/
{
  a_record_info &rec = *t.record;
  bool ok = true;

  (void)gid;
  if (!rec.is_union && !c_language_)
    rec.data_size = consistent_data_size(rec);
  rec.member_index.assign(rec.fields.size(), no_member);
  rec.bitfields.assign(rec.fields.size(), a_bitfield_info());
  rec.members.clear();
  rec.kinds.clear();
  rec.union_padding = mlir::Type();
  if (rec.is_union)
    lower_union(t, rec);
  else
    lower_struct(t, rec, false, false);
  compat::complete_record(mlir::cast<cir::RecordType>(t.cir), rec.members,
                          rec.packed, rec.union_padding, rec.kinds);
  rec.complete = true;
  pending_body_gid_ = 0;
  return ok;
}  /* a_emitter::complete_record_layout */

const a_record_info *a_emitter::record_info_of(mlir::Type t) const
/*
The record info of a complete CIR record type (null for other types and for
base-subobject types).
*/
{
  const a_record_info *result = nullptr;

  if (auto record = mlir::dyn_cast<cir::RecordType>(t)) {
    auto found = record_of_type_.find(record.getAsOpaquePointer());
    if (found != record_of_type_.end())
      result = found->second;
  }
  return result;
}  /* a_emitter::record_info_of */

bool a_emitter::is_indirect_class(mlir::Type t) const
/*
True for a record that is passed and returned indirectly (NONTRIVIAL_ABI).
*/
{
  const a_record_info *info = record_info_of(t);

  return info != nullptr && info->nontrivial_abi;
}  /* a_emitter::is_indirect_class */

mlir::Type a_emitter::base_subobject_type(a_type &t)
/*
The CIR type of a base subobject or potentially-overlapping member of record
type t (CIRGenRecordLayout::getBaseSubobjectCIRType).  A record whose data
size is smaller than its size has a distinct "<name>.base" record of that
size, lowered from the same fields; it is created the first time something
needs it.  Otherwise the complete type serves.
*/
{
  a_record_info &rec = *t.record;
  mlir::Type result = t.cir;

  if (rec.data_size < rec.size) {
    if (!rec.base_type) {
      a_record_info scratch = rec;
      std::string name = unique_record_name(rec.name + ".base");
      /* CIRGen's base-subobject record is a plain struct, also for a
         class. */
      cir::RecordType base = compat::incomplete_struct(ctx_, name, false);

      scratch.size = rec.data_size;
      scratch.name = name;
      scratch.member_index.assign(scratch.fields.size(), no_member);
      scratch.bitfields.assign(scratch.fields.size(), a_bitfield_info());
      scratch.members.clear();
      scratch.kinds.clear();
      scratch.union_padding = mlir::Type();
      lower_struct(t, scratch, true, rec.packed);
      compat::complete_record(base, scratch.members, scratch.packed,
                              scratch.union_padding, scratch.kinds);
      rec.base_type = base;
    }
    result = rec.base_type;
  }
  return result;
}  /* a_emitter::base_subobject_type */

void a_emitter::lower_union(a_type &t, a_record_info &rec)
/*
Every non-zero-width field is a variant member at offset 0; bit-field
variants are access units of their own.  Storage is the most aligned variant,
the rest of the size is the union's tail-padding type.
*/
{
  mlir::Type byte = compat::int_type(ctx_, 8, false);
  uint64_t layout_size = rec.size;

  (void)t;
  for (size_t i = 0; i < rec.fields.size(); ++i) {
    const a_field &f = rec.fields[i];
    mlir::Type type;
    cir::RecordMemberKind kind;

    if (f.is_bitfield) {
      if (f.bit_width == 0)
        continue;
      unsigned bits = static_cast<unsigned>(align_to(f.bit_width, 8));
      mlir::Type storage = cir::isValidFundamentalIntWidth(bits)
          ? compat::int_type(ctx_, bits, false)
          : static_cast<mlir::Type>(compat::array_of(byte, bits / 8));
      mlir::Type declared = type_at(f.type)->cir;
      cir::BitFieldDeclAttr decl = compat::bitfield_decl(declared, f.bit_width,
                                                         f.is_unnamed);
      type = compat::bitfield_unit(ctx_, storage, {decl});
      kind = f.is_unnamed ? cir::RecordMemberKind::Empty
                          : cir::RecordMemberKind::BitField;
      a_bitfield_info &info = rec.bitfields[i];
      info.name = f.name;
      info.storage = storage;
      info.declared = declared;
      info.unit_index = static_cast<uint32_t>(rec.members.size());
      info.unit_offset = 0;
      info.offset = f.bit_offset;
      info.size = std::min<unsigned>(f.bit_width, bits);
      info.is_signed = f.bitfield_signed;
    } else {
      type = type_at(f.type)->cir;
      kind = f.empty_for_abi ? cir::RecordMemberKind::Empty
                             : cir::RecordMemberKind::Data;
    }
    rec.member_index[i] = static_cast<uint32_t>(rec.members.size());
    rec.members.push_back(type);
    rec.kinds.push_back(kind);
  }
  rec.packed = false;
  if (rec.members.empty()) {
    if (layout_size != 0)
      rec.union_padding = layout_size == 1
          ? byte : static_cast<mlir::Type>(compat::array_of(byte,
                                                            layout_size));
  } else {
    mlir::Type storage = compat::union_storage_type(layout_, rec.members);
    uint64_t storage_size = storage ? type_size(storage) : 0;
    if (layout_size < storage_size) {
      storage = layout_size == 1
          ? byte : static_cast<mlir::Type>(compat::array_of(byte,
                                                            layout_size));
      storage_size = layout_size;
    }
    if (layout_size > storage_size) {
      uint64_t pad = layout_size - storage_size;
      rec.union_padding = pad == 1
          ? byte : static_cast<mlir::Type>(compat::array_of(byte, pad));
    }
    rec.packed = layout_size % type_align(storage, true) != 0;
  }
}  /* a_emitter::lower_union */

void a_emitter::lower_struct(a_type &t, a_record_info &rec, bool as_base,
                             bool force_packed)
/*
Port of CIRRecordLowering::lower for structs and classes: accumulate fields
(bases after them, as CIRGen does) and bit-field access units, sort by
offset, determine packedness, insert padding, and number the members.  With
as_base the record is laid out as its base subobject (rec.size is its data
size, and force_packed carries the packedness of the complete type).
*/
{
  std::vector<a_member> members;
  std::vector<a_member> base_members;
  mlir::Type byte = compat::int_type(ctx_, 8, false);
  const size_t count = rec.fields.size();
  const uint64_t reg_size = reg_bits_ / 8;
  const uint64_t record_align = rec.align;
  bool packed = force_packed;

  (void)t;

  auto uint_type = [&](uint64_t bits) -> mlir::Type {
    uint64_t aligned = 8;
    while (aligned < bits)
      aligned *= 2;
    return compat::int_type(ctx_, static_cast<unsigned>(aligned), false);
  };
  auto byte_array = [&](uint64_t n) -> mlir::Type {
    return n == 1 ? byte
                  : static_cast<mlir::Type>(compat::array_of(byte, n));
  };
  auto member_align = [&](mlir::Type type) -> uint64_t {
    return type_align(type, true);
  };
  auto owns_bytes = [&](const a_member &m) {
    return m.data && compat::member_owns_bytes(m.data);
  };
  auto decl_of = [&](const a_field &f) {
    return compat::bitfield_decl(type_at(f.type)->cir, f.bit_width,
                                 f.is_unnamed);
  };
  auto unit_member = [&](uint64_t offset, mlir::Type storage,
                         llvm::ArrayRef<cir::BitFieldDeclAttr> decls) {
    mlir::Type unit = compat::bitfield_unit(ctx_, storage, decls);
    bool named = mlir::cast<cir::BitFieldType>(unit).holdsNamedField();
    a_member m = {offset, unit, named ? cir::RecordMemberKind::BitField
                                       : cir::RecordMemberKind::Empty, -1};
    return m;
  };
  auto is_empty_for_layout = [&](const a_field &f) {
    bool result;
    mlir::Type ft = type_at(f.type)->cir;
    if (f.is_bitfield) {
      result = f.bit_width == 0;
    } else if (f.omitted) {
      result = true;
    } else {
      /* A record with no data (an empty class) counts as empty. */
      const a_record_info *info = record_info_of(ft);
      bool no_data = false;
      if (info != nullptr && !info->is_union) {
        no_data = true;
        for (const a_field &member : info->fields)
          no_data = no_data && member.omitted;
      }
      result = mlir::isa<cir::RecordType>(ft) &&
               (type_size(ft) == 0 || no_data);
    }
    return result;
  };

  /* accumulateFields */
  for (size_t i = 0; i < count;) {
    const a_field &f = rec.fields[i];
    if (!f.is_bitfield) {
      if (!f.omitted) {
        mlir::Type data = f.overlapping ? base_subobject_type(*type_at(f.type))
                                        : type_at(f.type)->cir;
        a_member m = {f.byte_offset, data,
                      f.empty_for_abi ? cir::RecordMemberKind::Empty
                                      : cir::RecordMemberKind::Data,
                      static_cast<int>(i)};
        (f.is_base ? base_members : members).push_back(m);
      }
      ++i;
      continue;
    }
    /* accumulateBitFields (non-discrete algorithm). */
    size_t field = i;
    size_t begin = count;      /* count means "no current span". */
    uint64_t begin_offset = 0;
    uint64_t bit_size_since_begin = 0;
    size_t best_end = begin;
    uint64_t best_end_offset = 0;
    bool best_clipped = true;

    for (;;) {
      bool at_boundary = false;
      bool barrier = false;
      bool install = false;

      if (field < count && rec.fields[field].is_bitfield) {
        uint64_t bit_offset = field_bit_offset(rec.fields[field]);
        if (begin == count) {
          begin = field;
          best_end = begin;
          begin_offset = bit_offset / 8;
          bit_size_since_begin = 0;
        } else if (bit_offset % 8 != 0) {
          /* Same character as the previous bit-field: same span. */
        } else {
          if (rec.fields[field].bit_width == 0)
            barrier = true;
          at_boundary = true;
        }
      } else {
        if (begin == count)
          break;
        barrier = true;
        at_boundary = true;
      }
      if (at_boundary) {
        uint64_t access_size = (bit_size_since_begin + 7) / 8;
        if (best_end == begin) {
          best_end = field;
          best_end_offset = begin_offset + access_size;
          best_clipped = true;
          if (bit_size_since_begin == 0)
            install = true;
        } else if (access_size > reg_size) {
          install = true;
        }
        if (!install) {
          mlir::Type type = uint_type(access_size * 8);
          if (!cheap_unaligned_) {
            uint64_t align = member_align(type);
            if (align > record_align)
              install = true;
            else if (begin_offset % align != 0)
              install = true;
            if (install && best_end == field && type_size(type) == access_size)
              best_clipped = false;
          }
          if (!install) {
            uint64_t limit = rec.data_size;
            for (size_t probe = field; probe < count; ++probe) {
              if (!is_empty_for_layout(rec.fields[probe])) {
                limit = field_bit_offset(rec.fields[probe]) / 8;
                break;
              }
            }
            uint64_t type_bytes = type_size(type);
            if (begin_offset + type_bytes <= limit) {
              best_end_offset = begin_offset + type_bytes;
              best_end = field;
              best_clipped = false;
            }
            if (barrier)
              install = true;
            else
              bit_size_since_begin = (limit - begin_offset) * 8;
          }
        }
      }
      if (install) {
        uint64_t access_size = best_end_offset - begin_offset;
        if (access_size != 0) {
          mlir::Type type = best_clipped ? byte_array(access_size)
                                         : uint_type(access_size * 8);
          std::vector<cir::BitFieldDeclAttr> decls;
          for (size_t o = begin; o != best_end; ++o) {
            if (rec.fields[o].bit_width != 0)
              decls.push_back(decl_of(rec.fields[o]));
          }
          members.push_back(unit_member(begin_offset, type, decls));
          for (size_t o = begin; o != best_end; ++o) {
            if (rec.fields[o].bit_width != 0) {
              a_member m = {begin_offset, mlir::Type(),
                            cir::RecordMemberKind::Data,
                            static_cast<int>(o)};
              members.push_back(m);
            }
          }
        }
        field = best_end;
        begin = count;
      } else {
        if (rec.fields[field].bit_width == 0) {
          /* A zero-width bit-field gets a member of its own, owns no bytes. */
          std::vector<cir::BitFieldDeclAttr> decls = {
              decl_of(rec.fields[field])};
          members.push_back(unit_member(
              field_bit_offset(rec.fields[field]) / 8, mlir::Type(), decls));
        }
        bit_size_since_begin += rec.fields[field].bit_width;
        ++field;
      }
    }
    i = field;
  }

  members.insert(members.end(), base_members.begin(), base_members.end());
  std::stable_sort(members.begin(), members.end(),
                   [](const a_member &x, const a_member &y) {
                     return x.offset < y.offset;
                   });
  /* The sentinel marks the end of the record. */
  members.push_back({rec.size, uint_type(8), cir::RecordMemberKind::Data, -1});

  /* determinePacked */
  if (!packed) {
    uint64_t alignment = 1;
    uint64_t nv_alignment = 1;
    /* The non-virtual size of a C++ class is its data size. */
    uint64_t nv_size = !as_base && !c_language_ ? rec.data_size : 0;
    for (const a_member &m : members) {
      if (!owns_bytes(m))
        continue;
      uint64_t ma = member_align(m.data);
      if (m.offset % ma != 0)
        packed = true;
      if (m.offset < nv_size)
        nv_alignment = std::max(nv_alignment, ma);
      alignment = std::max(alignment, ma);
    }
    if (members.back().offset % alignment != 0)
      packed = true;
    if (nv_size % nv_alignment != 0)
      packed = true;
    if (!packed)
      members.back().data = uint_type(alignment * 8);
  }
  rec.packed = packed;

  /* insertPadding */
  {
    std::vector<std::pair<uint64_t, uint64_t>> padding;
    uint64_t size = 0;
    for (const a_member &m : members) {
      if (!m.data)
        continue;
      if (!owns_bytes(m)) {
        if (m.offset > size) {
          padding.push_back({size, m.offset - size});
          size = m.offset;
        }
        continue;
      }
      uint64_t offset = m.offset;
      uint64_t natural = packed ? size : align_to(size, member_align(m.data));
      if (offset != natural)
        padding.push_back({size, offset - size});
      size = offset + type_size(m.data);
    }
    for (const auto &p : padding)
      members.push_back({p.first, byte_array(p.second),
                         cir::RecordMemberKind::Pad, -1});
    if (!padding.empty())
      std::stable_sort(members.begin(), members.end(),
                       [](const a_member &x, const a_member &y) {
                         return x.offset < y.offset;
                       });
  }
  members.pop_back();  /* The sentinel. */

  /* fillOutputFields */
  for (const a_member &m : members) {
    if (m.data) {
      rec.members.push_back(m.data);
      rec.kinds.push_back(m.kind);
    }
    if (m.field >= 0) {
      const a_field &f = rec.fields[static_cast<size_t>(m.field)];
      size_t index = static_cast<size_t>(m.field);
      if (m.data) {
        rec.member_index[index] =
            static_cast<uint32_t>(rec.members.size() - 1);
      } else {
        /* A bit-field: numbered as the access unit just before it. */
        mlir::Type unit = rec.members.back();
        mlir::Type storage = compat::member_storage_type(unit);
        a_bitfield_info &info = rec.bitfields[index];
        uint64_t storage_bits = type_size(storage) * 8;
        info.name = f.name;
        info.storage = storage;
        info.declared = type_at(f.type)->cir;
        info.unit_index = static_cast<uint32_t>(rec.members.size() - 1);
        info.unit_offset = m.offset;
        info.offset = static_cast<unsigned>(field_bit_offset(f) -
                                            m.offset * 8);
        info.size = static_cast<unsigned>(
                          std::min<uint64_t>(f.bit_width, storage_bits));
        info.is_signed = f.bitfield_signed;
        rec.member_index[index] = info.unit_index;
      }
    }
  }
}  /* a_emitter::lower_struct */

}  /* namespace edg_cir */
