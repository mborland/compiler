/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
ecil_refs.cpp -- reference checking of the ECIL stream validator: every
string id, G-id and L-id a record uses must have been introduced earlier,
selectors must name complete records with an index in range, declarations
follow the kind, effective-name and one-body rules, one external symbol name
is owned by one G-id, and a local is not used after its scope region closed.
All facts live in the shared id tables (ecil_ids.h).
*/

#include <algorithm>
#include <cstdio>

#include "ecil_reader.h"

namespace ecil {

namespace {

std::string number(uint64_t v)
/*
Decimal text of v.
*/
{
  return std::to_string(static_cast<unsigned long long>(v));
}  /* number */

bool operand_id(const an_operand &op, uint64_t &value)
/*
True with the id if op is a present (non-nil, non-zero) id.
*/
{
  return operand_u64(op, value) && value != 0;
}  /* operand_id */

bool is_external_linkage(uint64_t linkage)
/*
True if a symbol with this linkage is visible to other translation units.
*/
{
  return linkage != elk_internal && linkage != elk_private;
}  /* is_external_linkage */

bool is_declaration(const a_gid_entry *e)
/*
True for a function or variable.
*/
{
  return e != nullptr && (e->kind == gk_func || e->kind == gk_var);
}  /* is_declaration */

uint64_t saturating_add(uint64_t a, uint64_t b)
/*
a + b, or the maximum value on overflow.
*/
{
  return a > UINT64_MAX - b ? UINT64_MAX : a + b;
}  /* saturating_add */

uint64_t saturating_mul(uint64_t a, uint64_t b)
/*
a * b, or the maximum value on overflow.
*/
{
  return a != 0 && b > UINT64_MAX / a ? UINT64_MAX : a * b;
}  /* saturating_mul */

bool is_valid_utf8(const uint8_t *data, size_t length)
/*
True if the bytes are well-formed UTF-8: no overlong forms, surrogates or
code points above U+10FFFF.
*/
{
  bool ok = true;
  size_t k = 0;

  while (ok && k < length) {
    uint8_t lead = data[k];
    size_t extra = 0;
    uint32_t code = lead;

    if (lead >= 0xC2 && lead <= 0xDF)
      extra = 1;
    else if (lead >= 0xE0 && lead <= 0xEF)
      extra = 2;
    else if (lead >= 0xF0 && lead <= 0xF4)
      extra = 3;
    else if (lead >= 0x80)
      ok = false;
    if (extra != 0)
      code = lead & (0x3Fu >> extra);
    for (size_t j = 1; ok && j <= extra; ++j) {
      if (k + j >= length || (data[k + j] & 0xC0) != 0x80)
        ok = false;
      else
        code = (code << 6) | (data[k + j] & 0x3Fu);
    }
    if (ok && ((extra == 2 && code < 0x800) ||
               (extra == 3 && code < 0x10000) ||
               (code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF))
      ok = false;
    k += extra + 1;
  }
  return ok;
}  /* is_valid_utf8 */

}  /* anonymous namespace */

uint64_t a_ref_checker::type_size(const a_gid_entry *e) const
/*
The size in bytes of a field type: the size a TY_BOOL/INT/FLOAT/OPAQUE or
TY_RECORD_BODY gave, the pointer size of HDR_TARGET, or an array's element
size times its count (a flexible array has no count and no size).  Arrays of
arrays are walked in a loop.
*/
{
  uint64_t count = 1;
  uint64_t size = 0;

  while (e != nullptr && e->kind == gk_array) {
    count = (e->flags & gf_has_count) ? saturating_mul(count, e->extent) : 0;
    e = ids_.find_gid(e->element);
  }
  if (e != nullptr) {
    if (e->kind == gk_ptr || e->kind == gk_vptr || e->kind == gk_data_member)
      size = ptr_bytes_;
    else if (e->kind == gk_method)
      size = 2 * static_cast<uint64_t>(ptr_bytes_);
    else if (e->kind == gk_bool || e->kind == gk_int || e->kind == gk_float ||
             e->kind == gk_opaque || e->kind == gk_record)
      size = e->size;
  }
  return saturating_mul(size, count);
}  /* a_ref_checker::type_size */

uint64_t a_ref_checker::field_end(const a_record &r, const a_gid_entry *e,
                                  uint64_t type_g) const
/*
The end offset in bytes of a REC_FIELD: a bit-field ends at its last bit, a
base subobject or potentially overlapping member at its data size, anything
else at its size.  An EMPTY_FOR_ABI field takes no space (0).
*/
{
  uint64_t offset = 0;
  uint64_t bit_offset = 0;
  uint64_t width = 0;
  uint64_t flags = 0;
  uint64_t end = 0;

  operand_u64(r.ops[1], offset);
  operand_u64(r.ops[5], flags);
  if (!(flags & ECIL_FIELD_EMPTY_FOR_ABI)) {
    if (r.ops[3].kind != an_operand_kind::nil &&
        operand_u64(r.ops[3], width)) {
      operand_u64(r.ops[2], bit_offset);
      /* byte_off + ceil((bit_off + width) / 8); a zero-width bit-field ends
         at its byte offset. */
      end = width == 0 ? offset : saturating_add(offset, saturating_add(
          saturating_add(bit_offset, width), 7) / 8);
    } else if ((flags & (ECIL_FIELD_BASE | ECIL_FIELD_VIRTUAL_BASE)) &&
               e->kind == gk_record) {
      /* A base ends at its offset plus the non-virtual size of its class
         (12.2: nvsize from CLASS_INFO, else its data size). */
      const a_class_facts *f = ids_.class_facts(static_cast<uint32_t>(type_g));

      end = saturating_add(offset, f != nullptr ? nvsize_estimate(*f)
          : ids_.record_data_size(static_cast<uint32_t>(type_g), e->size));
    } else if ((flags & ECIL_FIELD_POTENTIALLY_OVERLAPPING)
               && e->kind == gk_record) {
      end = saturating_add(offset, ids_.record_data_size(
          static_cast<uint32_t>(type_g), e->size));
    } else {
      end = saturating_add(offset, type_size(e));
    }
  }
  return end;
}  /* a_ref_checker::field_end */

bool a_ref_checker::not_a_type(const a_gid_entry *e) const
/*
True for a G-id that is a declaration (function, variable, ABI data), not a
type.
*/
{
  return e != nullptr && (e->kind == gk_func || e->kind == gk_var ||
                          e->kind == gk_abi_data);
}  /* a_ref_checker::not_a_type */

a_ref_checker::a_ref_checker(a_id_tables &ids, a_bracket_tracker &tracker)
  : ids_(ids), tracker_(tracker), fields_pending_(0), cur_record_(0),
    members_seen_(false), cur_size_(0), cur_data_size_(0), cur_end_(0),
    cur_union_(false), ptr_bytes_(0), fn_class_(0), allow_vptr_(false),
    vbase_seen_(false), vptr_fields_(0), layout_class_(0), layout_expect_(0)
{
}  /* a_ref_checker::a_ref_checker */

void a_ref_checker::begin_frame()
/*
Per-frame state: the initializer target stacks.
*/
{
  agg_targets_.clear();
  const_targets_.clear();
}  /* a_ref_checker::begin_frame */

bool a_ref_checker::static_init_may_repeat(uint32_t var) const
/*
The second body of a structor pair with virtual bases (C1 and C2, D1 and D2)
repeats the STATIC_INIT of a static local of the function (K5a, 12.3): the
same variable, the other variant of the same STRUCTOR pair, at most two.
*/
{
  auto found = static_inits_.find(var);

  auto partner = structor_partner_.find(fn_gid_);

  return tracker_.function_depth() != 0 && fn_info_.has_vbases &&
         found != static_inits_.end() && found->second.second < 2 &&
         partner != structor_partner_.end() &&
         partner->second == found->second.first;
}  /* a_ref_checker::static_init_may_repeat */

bool a_ref_checker::need_sid(const an_operand &op, bool optional,
                             const char *what, std::string &problem)
/*
A string id operand: it must have been introduced by STR.
*/
{
  uint64_t v = 0;
  bool ok = true;

  if (operand_id(op, v)) {
    if (!ids_.has_string(v)) {
      problem = std::string(what) + " names string id " + number(v) +
                " that was not introduced by an earlier STR";
      ok = false;
    }
  } else if (!optional) {
    problem = std::string(what) + " has no string id";
    ok = false;
  }
  return ok;
}  /* a_ref_checker::need_sid */

bool a_ref_checker::check_int_value(const an_operand &type,
                                    const an_operand &value, const char *what,
                                    std::string &problem)
/*
CONST_INT / C_INT of an integer type: the value is within the signed or the
unsigned range of the type's width.
*/
{
  uint64_t t = 0;
  const a_gid_entry *e = operand_id(type, t) ? ids_.find_gid(t) : nullptr;
  bool ok = true;

  if (e != nullptr && e->kind == gk_int &&
      !operand_fits_int_width(value, e->extent)) {
    problem = std::string(what) + " value does not fit the " +
              std::to_string(e->extent) + "-bit integer type G-id " +
              std::to_string(t);
    ok = false;
  }
  return ok;
}  /* a_ref_checker::check_int_value */

bool a_ref_checker::need_type(const an_operand &op, bool optional,
                              std::string &problem)
/*
A type operand: an introduced type G-id (not a function or variable).
*/
{
  uint64_t v = 0;
  bool ok = true;

  if (operand_id(op, v)) {
    const a_gid_entry *e = ids_.find_gid(v);
    if (e == nullptr) {
      problem = "unknown type: G-id " + number(v) +
                " was not introduced earlier in the stream";
      ok = false;
    } else if (not_a_type(e)) {
      problem = "G-id " + number(v) + " is a declaration, not a type";
      ok = false;
    } else if (e->kind == gk_vptr && !allow_vptr_) {
      problem = "G-id " + number(v) + " (TY_VPTR) is only the type of the "
                "vptr field of a record";
      ok = false;
    }
  } else if (!optional) {
    problem = "missing type G-id";
    ok = false;
  }
  return ok;
}  /* a_ref_checker::need_type */

bool a_ref_checker::need_complete(const an_operand &op, std::string &problem,
                                  const a_gid_entry *&out)
/*
A type operand that must be a complete record, or an array (for AGG_INIT and
C_AGG targets).
*/
{
  uint64_t v = 0;
  bool ok = need_type(op, false, problem);

  out = nullptr;
  if (ok && operand_id(op, v)) {
    out = ids_.find_gid(v);
    if (out->kind == gk_record && !(out->flags & gf_complete)) {
      problem = "incomplete record: G-id " + number(v) +
                " has no TY_RECORD_BODY yet";
      ok = false;
    }
  }
  return ok;
}  /* a_ref_checker::need_complete */

bool a_ref_checker::need_object(const an_operand &op, const char *what,
                                std::string &problem)
/*
A type operand naming an object that is allocated or defined: a record (or
an array of records) must have its body by then.
*/
{
  bool ok = need_type(op, false, problem);
  uint64_t v = 0;

  if (ok && operand_id(op, v)) {
    const a_gid_entry *e = ids_.find_gid(v);
    while (e != nullptr && e->kind == gk_array)
      e = ids_.find_gid(e->element);
    if (e != nullptr && e->kind == gk_record &&
        !(e->flags & gf_complete)) {
      problem = std::string("incomplete record: ") + what + " uses G-id " +
                number(v) + " before its TY_RECORD_BODY";
      ok = false;
    }
  }
  return ok;
}  /* a_ref_checker::need_object */

a_gid_entry *a_ref_checker::introduce(const an_operand &op, a_gid_kind kind,
                                      std::string &problem)
/*
A G-id introduced as a type: it must be new.
*/
{
  uint64_t v = 0;
  a_gid_entry *result = nullptr;

  if (!operand_id(op, v))
    problem = "G-id 0 cannot be introduced";
  else
    result = ids_.add_gid(v, kind, problem);
  return result;
}  /* a_ref_checker::introduce */

bool a_ref_checker::apply_name(uint64_t g, a_gid_entry &entry, uint64_t sid,
                               uint64_t linkage, std::string &problem)
/*
Apply a DECL_* or NAME_BIND to the declaration's effective name and linkage
(spec section 7).  A nil name (sid 0) keeps the effective name, another name
renames the entity.  The external symbol name is owned by one G-id: the old
name is released when the entity is renamed or becomes internal, and the new
one must be free.
*/
{
  bool ok = !(sid != 0 && ids_.string(sid).empty());
  bool owned = (entry.flags & gf_owns_name) != 0;
  std::string_view old_text = (entry.flags & gf_named)
                                  ? ids_.string(entry.name_sid)
                                  : std::string_view();
  uint32_t new_sid = sid != 0 ? static_cast<uint32_t>(sid) : entry.name_sid;
  bool named = sid != 0 || (entry.flags & gf_named) != 0;
  std::string_view new_text = named ? ids_.string(new_sid)
                                    : std::string_view();
  bool wants = is_external_linkage(linkage) && named;

  if (!ok) {
    problem = "empty symbol name: the name sid of G-id " + number(g) +
              " is the empty string (use nil for no name)";
  } else {
    if (owned && !(wants && new_text == old_text)) {
      ids_.release_symbol(old_text, static_cast<uint32_t>(g));
      entry.flags &= static_cast<uint8_t>(~gf_owns_name);
      owned = false;
    }
    entry.linkage = static_cast<uint8_t>(linkage);
    entry.name_sid = named ? new_sid : 0;
    if (named)
      entry.flags |= gf_named;
    if (wants && !owned) {
      uint32_t other = 0;
      if (ids_.bind_symbol(new_text, static_cast<uint32_t>(g), other)) {
        entry.flags |= gf_owns_name;
      } else {
        problem = "one G-id per symbol: '" + std::string(new_text) +
                  "' is bound by G-ids " + number(other) + " and " +
                  number(g);
        ok = false;
      }
    }
  }
  return ok;
}  /* a_ref_checker::apply_name */

bool a_ref_checker::check_comdat(uint64_t decl_flags, uint64_t linkage,
                                 a_gid_kind kind, std::string &problem) const
/*
COMDAT, linkonce_odr and weak_odr describe definitions: a declaration that is
not defined here is external without COMDAT, and COMDAT is set if and only if
a definition has linkonce_odr or weak_odr linkage (spec 11.2).
*/
{
  bool ok = true;
  uint64_t comdat = kind == gk_func ? ECIL_DECL_FUNC_COMDAT
                                    : ECIL_DECL_VAR_COMDAT;
  bool defined = (decl_flags & 1) != 0;  /* DEFINED_HERE / DEFINITION. */
  bool vague = linkage == elk_linkonce_odr || linkage == elk_weak_odr;

  if (vague && !defined) {
    problem = "linkonce_odr/weak_odr is only for definitions (the "
              "declaration is not defined here)";
    ok = false;
  } else if ((decl_flags & comdat) != 0 && !(defined && vague)) {
    problem = "COMDAT is only for linkonce_odr/weak_odr definitions";
    ok = false;
  } else if (defined && vague && !(decl_flags & comdat)) {
    problem = "a linkonce_odr/weak_odr definition needs COMDAT";
    ok = false;
  }
  return ok;
}  /* a_ref_checker::check_comdat */

bool a_ref_checker::declare(const a_record &r, a_gid_kind kind,
                            std::string &problem)
/*
DECL_FUNC / DECL_VAR (g, t, linkage, flags, [align,] sid): introduce the
entity or re-issue it (the same kind only).
*/
{
  uint64_t g = 0;
  uint64_t t = 0;
  uint64_t linkage = 0;
  uint64_t name = 0;
  bool ok = operand_id(r.ops[0], g);
  const a_gid_entry *te;
  a_gid_entry *e;
  const an_operand &name_op = r.ops[r.count - 1];

  if (!ok) {
    problem = "G-id 0 cannot be declared";
  } else {
    ok = need_type(r.ops[1], false, problem) &&
         need_sid(name_op, true, "the name", problem);
  }
  if (ok && kind == gk_var) {
    uint64_t flags = 0;
    operand_u64(r.ops[3], flags);
    if (flags & ECIL_DECL_VAR_DEFINITION)
      ok = need_object(r.ops[1], "a DECL_VAR definition", problem);
  }
  if (ok) {
    operand_id(r.ops[1], t);
    te = ids_.find_gid(t);
    if (kind == gk_func && te->kind != gk_func_type) {
      problem = "DECL_FUNC type G-id " + number(t) +
                " is not a function type";
      ok = false;
    } else if (kind == gk_var &&
               (te->kind == gk_func_type || te->kind == gk_void)) {
      problem = "DECL_VAR type G-id " + number(t) +
                " is not an object type (function or void)";
      ok = false;
    }
  }
  if (ok) {
    e = ids_.find_gid(g);
    if (e != nullptr && e->kind != kind) {
      problem = "one G-id per symbol: G-id " + number(g) +
                " was introduced as something else and cannot be re-declared "
                "as a " + (kind == gk_func ? "function" : "variable");
      ok = false;
    } else if (e == nullptr) {
      e = ids_.add_gid(g, kind, problem);
      ok = e != nullptr;
    }
  }
  if (ok) {
    uint64_t dflags = 0;
    bool fresh = e->kind == kind && e->decl_flags == 0 && e->name_sid == 0 &&
                 !(e->flags & gf_named);
    operand_u64(r.ops[2], linkage);
    operand_u64(r.ops[3], dflags);
    operand_id(name_op, name);
    e->element = static_cast<uint32_t>(t);
    ok = check_comdat(dflags, linkage, kind, problem);
    if (ok && kind == gk_func && (e->decl_flags & ECIL_DECL_FUNC_NOTHROW) &&
        !(dflags & ECIL_DECL_FUNC_NOTHROW)) {
      problem = "a re-issue of DECL_FUNC G-id " + number(g) + " clears "
                "NOTHROW (a wrong NOTHROW silently skips cleanups, so it is "
                "never cleared)";
      ok = false;
    }
    if (fresh && name == 0)
      e->decl_flags |= decl_flag_nil_introduced;
    e->decl_flags = static_cast<uint32_t>(dflags) |
                    (e->decl_flags & decl_flag_nil_introduced);
    if (ok)
      ok = apply_name(g, *e, name, linkage, problem);
  }
  return ok;
}  /* a_ref_checker::declare */

bool a_ref_checker::introduce_local(const an_operand &op, a_lid_state state,
                                    std::string &problem, uint32_t type)
/*
PARAM / LOCAL / MATERIALIZE_TEMP / LABEL: a new L-id of the current function.
A value belongs to the innermost open scope region.
*/
{
  uint64_t v = 0;
  bool ok = true;

  if (!ids_.in_function()) {
    problem = "local id introduced outside a function";
    ok = false;
  } else if (!operand_id(op, v)) {
    problem = "L-id 0 cannot be introduced";
    ok = false;
  } else {
    a_lid_entry *entry = ids_.touch_lid(v, problem);
    if (entry == nullptr) {
      ok = false;
    } else if (entry->state != ls_unseen) {
      problem = "L-id " + number(v) + " is introduced twice in the function";
      ok = false;
    } else {
      entry->state = state;
      entry->type = type;
      if (state == ls_value)
        tracker_.note_local(ids_.lid_index(entry));
    }
  }
  return ok;
}  /* a_ref_checker::introduce_local */

bool a_ref_checker::check_selector(uint64_t index, uint64_t repeat,
                                   bool is_const, std::string &problem)
/*
AGG_SEL / C_SEL / C_REPEAT: the index (and run) is within the current target.
*/
{
  std::vector<uint32_t> &stack = is_const ? const_targets_ : agg_targets_;
  bool ok = !stack.empty();

  if (!ok) {
    problem = std::string("selector outside ") +
              (is_const ? "C_AGG_BEGIN" : "AGG_INIT_BEGIN");
  } else {
    const a_gid_entry *t = ids_.find_gid(stack.back());
    if (t->kind == gk_record) {
      if (index >= t->extent || repeat != 1) {
        problem = "field index " + number(index) + " is out of range for "
                  "record G-id " + number(stack.back()) + " with " +
                  number(t->extent) + " fields";
        ok = false;
      } else {
        const a_class_facts *cf = ids_.class_facts(stack.back());

        if (cf != nullptr && index < cf->fields.size()) {
          const a_gid_entry *ft = ids_.find_gid(cf->fields[index].type);

          if (cf->fields[index].flags & ECIL_FIELD_VIRTUAL_BASE) {
            problem = "selector of a VIRTUAL_BASE field (a virtual base is "
                      "not part of a constant or an initializer)";
            ok = false;
          } else if (is_const && ft != nullptr && ft->kind == gk_vptr) {
            problem = "C_SEL of the vptr field (the library stores the "
                      "vptrs of a constant)";
            ok = false;
          }
        }
      }
    } else if (t->kind == gk_array) {
      /* index + repeat > count, without the overflow. */
      if ((t->flags & gf_has_count) &&
          (index > t->extent || repeat > t->extent - index)) {
        problem = "element index " + number(index) + " (+" + number(repeat) +
                  ") is out of range for array G-id " +
                  number(stack.back()) + " of " + number(t->extent);
        ok = false;
      }
    } else {
      problem = "selector on a target that is neither record nor array";
      ok = false;
    }
  }
  return ok;
}  /* a_ref_checker::check_selector */

bool a_ref_checker::returns_aggregate() const
/*
True inside a function that returns a record or an array.
*/
{
  bool result = false;

  if (!fns_.empty()) {
    const a_gid_entry *t = ids_.find_gid(fns_.back().ret_type);
    result = t != nullptr && (t->kind == gk_record || t->kind == gk_array);
  }
  return result;
}  /* a_ref_checker::returns_aggregate */

bool a_ref_checker::check_structor(const a_record &r, std::string &problem)
/*
STRUCTOR(g_complete, g_base, flags): two declared functions of one type and
one linkage, not in a group yet, whose names differ in exactly one character
(1 and 2).
*/
{
  bool ok = true;
  uint64_t gc = 0;
  uint64_t gb = 0;
  uint64_t sflags = 0;
  a_gid_entry *c = nullptr;
  a_gid_entry *b = nullptr;

  operand_u64(r.ops[2], sflags);
  if (!operand_id(r.ops[0], gc) || !operand_id(r.ops[1], gb) ||
      (c = ids_.find_gid(gc)) == nullptr || c->kind != gk_func ||
      (b = ids_.find_gid(gb)) == nullptr || b->kind != gk_func || gc == gb) {
    problem = "STRUCTOR names two distinct declared functions";
    ok = false;
  } else if (c->element != b->element || c->linkage != b->linkage) {
    problem = "STRUCTOR functions must have the same type and linkage";
    ok = false;
  } else if ((c->flags | b->flags) & gf_in_group) {
    problem = "STRUCTOR function is already in a group";
    ok = false;
  } else if ((c->flags & gf_named) && (b->flags & gf_named)) {
    std::string_view nc = ids_.string(c->name_sid);
    std::string_view nb = ids_.string(b->name_sid);
    size_t diff = 0;
    bool digits = true;
    if (nc.size() != nb.size()) {
      diff = 2;
    } else {
      for (size_t k = 0; k < nc.size(); ++k) {
        if (nc[k] != nb[k]) {
          ++diff;
          digits = digits && nc[k] == '1' && nb[k] == '2';
        }
      }
    }
    if (diff != 1 || !digits) {
      problem = "STRUCTOR names must differ in exactly one character, 1 "
                "(complete) and 2 (base)";
      ok = false;
    }
  }
  if (ok) {
    /* The class: HAS_VBASES must equal "the class has VIRTUAL_BASE fields"
       (12.3), and a class that has them is complete by now. */
    uint32_t cls = 0;
    const a_class_facts *f = nullptr;
    bool has_vbases = (sflags & ECIL_STRUCTOR_HAS_VBASES) != 0;

    if (class_of_function(c, cls))
      f = ids_.class_facts(cls);
    if (has_vbases && (f == nullptr || !f->complete)) {
      problem = "STRUCTOR has_vbases: the class is not complete yet (send "
                "its TY_RECORD_BODY first)";
      ok = false;
    } else if (f != nullptr && f->complete &&
               f->vbases.empty() == has_vbases) {
      problem = "STRUCTOR has_vbases does not match the VIRTUAL_BASE fields "
                "of the class";
      ok = false;
    }
  }
  if (ok) {
    uint8_t common = static_cast<uint8_t>(
        ((sflags & ECIL_STRUCTOR_HAS_VBASES) ? sr_vbases : 0) |
        ((sflags & ECIL_STRUCTOR_DTOR) ? sr_dtor : 0));

    structor_partner_[static_cast<uint32_t>(gc)] = static_cast<uint32_t>(gb);
    structor_partner_[static_cast<uint32_t>(gb)] = static_cast<uint32_t>(gc);
    c->flags |= gf_in_group;
    b->flags |= gf_in_group;
    c->role = static_cast<uint8_t>(sr_complete | common);
    b->role = static_cast<uint8_t>(sr_base | common);
  }
  return ok;
}  /* a_ref_checker::check_structor */

bool a_ref_checker::check_v1(const a_record &r, std::string &problem)
/*
Operand rules of the C++ records that need the id tables (the structural
ones are the bracket tracker's).
*/
{
  bool ok = true;
  uint64_t v = 0;
  uint64_t w = 0;
  uint64_t x = 0;
  a_gid_entry *ne = nullptr;

  switch (r.opcode) {
  case eop_structor:
    ok = check_structor(r, problem);
    break;
  case eop_return_from_slot:
  case eop_return_slot:
    if (!returns_aggregate()) {
      problem = std::string(r.info->name) + " in a function that does not "
                "return a record or array";
      ok = false;
    }
    break;
  case eop_cleanup_begin: {
    bool is_exit = operand_u64(r.ops[0], v) && v == ecl_exit;
    bool has_g = operand_id(r.ops[1], w);
    if (is_exit != has_g) {
      problem = "CLEANUP_BEGIN has a G-id if and only if its kind is exit";
      ok = false;
    } else if (has_g) {
      const a_gid_entry *g = ids_.find_gid(w);
      if (g == nullptr || g->kind != gk_var) {
        problem = "CLEANUP_BEGIN(exit) names G-id " + number(w) +
                  ", which is not a declared variable";
        ok = false;
      }
    }
    break;
  }
  case eop_static_init_begin: {
    bool vague = false;
    ok = operand_id(r.ops[0], v) && (ne = ids_.find_gid(v)) != nullptr &&
         ne->kind == gk_var;
    if (ok)
      vague = ne->linkage == elk_linkonce_odr || ne->linkage == elk_weak_odr;
    if (!ok) {
      problem = "STATIC_INIT_BEGIN names a G-id that is not a declared "
                "variable";
    } else if (!(ne->decl_flags & ECIL_DECL_VAR_DEFINITION)) {
      problem = "STATIC_INIT_BEGIN of G-id " + number(v) +
                ", which is not a definition";
      ok = false;
    } else if ((ne->role & vr_static_init) && !replaying &&
               !static_init_may_repeat(static_cast<uint32_t>(v))) {
      problem = "G-id " + number(v) + " is initialized twice "
                "(STATIC_INIT_BEGIN already seen)";
      ok = false;
    } else if (tracker_.function_depth() != 0 &&
               (ne->decl_flags & (ECIL_DECL_VAR_STATIC_LOCAL |
                                  ECIL_DECL_VAR_GUARDED)) !=
               (ECIL_DECL_VAR_STATIC_LOCAL | ECIL_DECL_VAR_GUARDED)) {
      problem = "STATIC_INIT_BEGIN in a function needs a static_local, "
                "guarded variable (G-id " + number(v) + ")";
      ok = false;
    } else if (vague && !(ne->decl_flags & ECIL_DECL_VAR_GUARDED)) {
      problem = "STATIC_INIT_BEGIN of a vague-linkage variable needs GUARDED "
                "(G-id " + number(v) + ")";
      ok = false;
    } else if ((r.ops[1].u & ECIL_STATIC_INIT_PARTIAL) &&
               !(ne->role & vr_var_init)) {
      problem = "STATIC_INIT_BEGIN PARTIAL of G-id " + number(v) +
                ", which has no VAR_INIT";
      ok = false;
    } else {
      bool own = tracker_.static_init_is_global();
      /* With a VAR_INIT the group may hold only the exit cleanup, unless it
         is PARTIAL: then it stores the dynamic elements. */
      if (r.ops[1].u & ECIL_STATIC_INIT_PARTIAL)
        tracker_.mark_static_init_partial();
      else if (ne->role & vr_var_init)
        tracker_.mark_static_init_exit_only();
      ne->role |= vr_static_init;
      if (tracker_.function_depth() != 0 && fn_info_.has_vbases) {
        auto &seen = static_inits_[static_cast<uint32_t>(v)];

        if (seen.second == 0)
          seen.first = fn_gid_;
        ++seen.second;
      }
      static_own_ids_.push_back(own);
      if (own) {
        /* Each initializer in F_GLOBALS has its own L-id space. */
        a_fn_state state = {0, 0};
        ids_.begin_function();
        fns_.push_back(state);
      }
    }
    break;
  }
  case eop_static_init_end:
    if (!static_own_ids_.empty()) {
      if (static_own_ids_.back() && !fns_.empty())
        fns_.pop_back();
      static_own_ids_.pop_back();
    }
    break;
  case eop_base_cast:
  case eop_derived_cast: {
    bool derived = r.opcode == eop_derived_cast;
    uint64_t t_der = 0;
    uint64_t t_base = 0;
    ok = need_type(r.ops[0], false, problem) &&
         need_type(r.ops[1], false, problem);
    if (ok) {
      operand_u64(r.ops[derived ? 1 : 0], t_der);
      operand_u64(r.ops[derived ? 0 : 1], t_base);
      if (!operand_u64(r.ops[2], x) ||
          !ids_.has_base(static_cast<uint32_t>(t_der),
                         static_cast<uint32_t>(t_base), x)) {
        problem = std::string(r.info->name) + ": G-id " + number(t_der) +
                  " has no BASE field of G-id " + number(t_base) +
                  " at that offset";
        ok = false;
      }
    }
    break;
  }
  case eop_array_ctor_begin:
  case eop_array_dtor_begin:
  case eop_new_array_size:
  case eop_new_array_size_throw:
  case eop_new_array_cookie:
    ok = need_type(r.ops[0], false, problem);
    break;
  case eop_cmp3way: {
    const a_gid_entry *rt = nullptr;
    ok = need_type(r.ops[0], false, problem);
    if (ok && operand_id(r.ops[0], v))
      rt = ids_.find_gid(v);
    if (ok && (rt == nullptr || rt->kind != gk_int || rt->extent > 64)) {
      problem = "CMP3WAY result type G-id " + number(v) +
                " is not an integer type of at most 64 bits";
      ok = false;
    }
    for (int k = 1; ok && k <= 4; ++k) {
      int64_t value = 0;

      if (r.ops[k].kind == an_operand_kind::nil)
        continue;
      if (!operand_i64(r.ops[k], value)) {
        problem = "CMP3WAY result value " + number(k) + " does not fit 64 "
                  "bits";
        ok = false;
      } else {
        ok = check_int_value(r.ops[0], r.ops[k], "CMP3WAY result", problem);
      }
    }
    break;
  }
  case eop_delete: {
    const a_gid_entry *fn = nullptr;
    ok = operand_id(r.ops[0], v) && (fn = ids_.find_gid(v)) != nullptr &&
         fn->kind == gk_func;
    if (!ok) {
      problem = "DELETE names a G-id that is not a declared function";
    } else if (operand_id(r.ops[1], w)) {
      const a_gid_entry *d = ids_.find_gid(w);
      if (d == nullptr || d->kind != gk_func) {
        problem = "DELETE destructor G-id " + number(w) +
                  " is not a declared function";
        ok = false;
      }
    }
    if (ok)
      ok = need_type(r.ops[2], false, problem);
    if (ok && (r.ops[3].u & ECIL_DELETE_DESTROYING) &&
        ((r.ops[3].u & (ECIL_DELETE_VIRTUAL | ECIL_DELETE_GLOBAL |
                        ECIL_DELETE_ARRAY)) != 0 ||
         r.ops[1].kind != an_operand_kind::nil)) {
      /* A destroying operator delete destroys the object itself: no
         destructor, and not with VIRTUAL (D0 calls it), GLOBAL or ARRAY. */
      problem = "DELETE DESTROYING takes no destructor and none of "
                "VIRTUAL, GLOBAL and ARRAY";
      ok = false;
    } else if (ok && (r.ops[3].u & ECIL_DELETE_GLOBAL) &&
        !(r.ops[3].u & ECIL_DELETE_VIRTUAL)) {
      problem = "DELETE GLOBAL needs VIRTUAL";
      ok = false;
    } else if (ok && (r.ops[3].u & ECIL_DELETE_GLOBAL)) {
      uint32_t slot = 0;

      /* ::delete of a polymorphic object: D1 is called through the vtable
         and g_delete_fn is the global operator delete. */
      if (!operand_id(r.ops[1], w) ||
          !ids_.vslot_of(static_cast<uint32_t>(w), slot)) {
        problem = "DELETE GLOBAL needs the virtual destructor (D1 of a "
                  "CLASS_VFUNCS entry)";
        ok = false;
      }
    } else if (ok && (r.ops[3].u & ECIL_DELETE_VIRTUAL)) {
      uint32_t d0 = 0;
      uint32_t del = 0;

      if (!operand_id(r.ops[1], w) || !ids_.deleting_of(
              static_cast<uint32_t>(w), d0, del) || del != v) {
        problem = "DELETE VIRTUAL needs the virtual destructor of a "
                  "STRUCTOR_DELETING, and its g_delete as the function";
        ok = false;
      }
    }
    break;
  }
  default:
    break;
  }
  return ok;
}  /* a_ref_checker::check_v1 */

bool a_ref_checker::end_frame(std::string &problem)
/*
At the end of a frame no TY_RECORD_BODY may still await REC_FIELD records.
*/
{
  bool ok = fields_pending_ == 0;

  if (!ok) {
    problem = "a TY_RECORD_BODY is missing REC_FIELD records at the end of "
              "its frame";
    fields_pending_ = 0;
  } else if (layout_expect_ != 0) {
    problem = "the layout facts of class G-id " + number(layout_class_) +
              " (CLASS_MANGLE, CLASS_INFO, CLASS_BASES) are missing at the "
              "end of its frame";
    layout_expect_ = 0;
    ok = false;
  }
  return ok;
}  /* a_ref_checker::end_frame */

bool a_ref_checker::check_supp(const a_record &r, std::string &problem)
/*
The keys of a supp blob this reader understands.  A key appears at most once
per blob (any key: a blob may hold several, since 1.3).  unsupported_reason
(1.1) is UTF-8 text of at most 255 bytes on a DECL_FUNC or DECL_VAR that has
UNSUPPORTED set.  noinline (1.3) is `true` on a DECL_FUNC; used (1.3) is
`true` on a DECL_FUNC or DECL_VAR; always_inline, cold and hot (16 to 18) are
`true` on a DECL_FUNC; nonnull_ptrs (1.3) is an unsigned mask on
a TY_FUNC with no bit above the parameter count.  The 1.3 keys are checked in
streams of every minor (a reader that knows a key checks it whatever the
preamble says).  Unknown keys are skipped.
*/
{
  bool ok = true;
  std::unordered_set<uint64_t> seen;
  std::vector<std::pair<uint64_t, an_operand>> pairs;
  uint64_t flags = 0;
  bool unsupported_decl = false;

  supp_pairs(r, pairs);
  if (r.opcode == eop_decl_func || r.opcode == eop_decl_var) {
    operand_u64(r.ops[3], flags);
    unsupported_decl = (flags & (r.opcode == eop_decl_func
                                     ? ECIL_DECL_FUNC_UNSUPPORTED
                                     : ECIL_DECL_VAR_UNSUPPORTED)) != 0;
  }
  for (size_t k = 0; ok && k < pairs.size(); ++k) {
    const an_operand &value = pairs[k].second;
    uint64_t key = pairs[k].first;

    if (!seen.insert(key).second) {
      problem = "supp key " + number(key) + " appears twice in one blob";
      ok = false;
    } else if (key == esk_noinline) {
      if (r.opcode != eop_decl_func) {
        problem = "supp key noinline (14) is only valid on a DECL_FUNC";
        ok = false;
      } else if (!((value.kind == an_operand_kind::boolean ||
                    value.kind == an_operand_kind::uint) && value.u == 1)) {
        problem = "supp key noinline (14) is not true";
        ok = false;
      }
    } else if (key >= esk_always_inline && key <= esk_hot) {
      const char *name = key == esk_always_inline ? "always_inline (16)"
                         : key == esk_cold ? "cold (17)" : "hot (18)";

      if (r.opcode != eop_decl_func) {
        problem = std::string("supp key ") + name +
                  " is only valid on a DECL_FUNC";
        ok = false;
      } else if (!((value.kind == an_operand_kind::boolean ||
                    value.kind == an_operand_kind::uint) && value.u == 1)) {
        problem = std::string("supp key ") + name + " is not true";
        ok = false;
      }
    } else if (key == esk_used) {
      if (r.opcode != eop_decl_func && r.opcode != eop_decl_var) {
        problem = "supp key used (11) is only valid on a DECL_FUNC or "
                  "DECL_VAR";
        ok = false;
      } else if (!((value.kind == an_operand_kind::boolean ||
                    value.kind == an_operand_kind::uint) && value.u == 1)) {
        problem = "supp key used (11) is not true";
        ok = false;
      }
    } else if (key == esk_nonnull_ptrs) {
      if (r.opcode != eop_ty_func) {
        problem = "supp key nonnull_ptrs (15) is only valid on a TY_FUNC";
        ok = false;
      } else if (value.kind != an_operand_kind::uint) {
        problem = "supp key nonnull_ptrs (15) is not an unsigned integer";
        ok = false;
      } else {
        an_array_reader params = array_elements(r.ops[3]);
        uint64_t element = 0;
        uint64_t nparams = 0;

        while (next_uint_element(params, element))
          ++nparams;
        /* Bit 0 is the result, bit i+1 parameter i: bits 0..nparams. */
        if (nparams + 1 < 64 && (value.u >> (nparams + 1)) != 0) {
          problem = "supp key nonnull_ptrs (15) has a bit at or above " +
                    number(nparams + 1) + " (the function type has " +
                    number(nparams) + " parameters)";
          ok = false;
        }
      }
    } else if (key == esk_unsupported_reason) {
      if (!unsupported_decl) {
        problem = "supp key unsupported_reason (13) is only valid on a "
                  "DECL_FUNC or DECL_VAR with UNSUPPORTED";
        ok = false;
      } else if (value.kind != an_operand_kind::bin) {
        problem = "supp key unsupported_reason (13) is not a bin";
        ok = false;
      } else if (value.length > unsupported_reason_max) {
        problem = "supp key unsupported_reason (13) is " +
                  number(value.length) + " bytes (at most " +
                  number(unsupported_reason_max) + ")";
        ok = false;
      } else if (!is_valid_utf8(value.data, value.length)) {
        problem = "supp key unsupported_reason (13) is not valid UTF-8";
        ok = false;
      }
    }
  }
  return ok;
}  /* a_ref_checker::check_supp */

bool a_ref_checker::check(const a_record &r, std::string &problem)
/*
Apply the reference rules of one record.
*/
{
  bool ok = true;
  uint64_t v = 0;
  uint64_t w = 0;
  const a_gid_entry *e = nullptr;
  a_gid_entry *ne = nullptr;

  if (fields_pending_ != 0 && r.opcode != eop_rec_field &&
      r.opcode != eop_loc && r.opcode != eop_ext) {
    problem = "a TY_RECORD_BODY is missing REC_FIELD records";
    ok = false;
  }
  /* The layout facts of a dynamic class (an own vptr, a virtual base or a
     dynamic base) follow its body directly (12.2): CLASS_MANGLE, CLASS_INFO, CLASS_BASES, with only
     strings, locations and extensions in between. */
  if (ok && layout_expect_ != 0 && r.opcode != eop_str &&
      r.opcode != eop_loc && r.opcode != eop_ext) {
    uint64_t named = 0;
    bool mine = (r.opcode == eop_class_mangle ||
                 r.opcode == eop_class_info ||
                 r.opcode == eop_class_bases) &&
                operand_id(r.ops[0], named) && named == layout_class_;

    if (mine && r.opcode == eop_class_mangle && layout_expect_ == 1) {
      layout_expect_ = 2;
    } else if (mine && r.opcode == eop_class_info && layout_expect_ <= 2) {
      layout_expect_ = 3;
    } else if (mine && r.opcode == eop_class_bases && layout_expect_ == 3) {
      layout_expect_ = 0;
    } else {
      problem = "the layout facts (CLASS_MANGLE, CLASS_INFO, CLASS_BASES) of "
                "class G-id " + number(layout_class_) + ", which is "
                "dynamic, follow its TY_RECORD_BODY directly";
      ok = false;
    }
  }
  if (ok && r.supp != nullptr)
    ok = check_supp(r, problem);
  if (ok) switch (r.opcode) {
  case eop_str: {
    const uint8_t *data = nullptr;
    size_t length = 0;

    operand_u64(r.ops[0], w);
    if (r.ops[1].kind == an_operand_kind::bin && r.ops[1].data != nullptr) {
      data = r.ops[1].data;
      length = r.ops[1].length;
    }
    ok = ids_.add_string(w, data, length, problem);
    break;
  }
  case eop_loc:
    ok = need_sid(r.ops[0], true, "the file name", problem);
    break;
  case eop_hdr_target:
    ok = need_sid(r.ops[0], false, "the triple", problem) &&
         need_sid(r.ops[1], true, "the ABI", problem) &&
         need_sid(r.ops[2], true, "the data layout", problem);
    if (ok && operand_u64(r.ops[3], w))
      ptr_bytes_ = static_cast<uint32_t>(w / 8);
    break;
  case eop_hdr_producer:
    ok = need_sid(r.ops[0], false, "the producer", problem) &&
         need_sid(r.ops[1], true, "the source name", problem);
    break;
  case eop_hdr_type_role:
    ok = need_type(r.ops[1], false, problem);
    break;
  case eop_name_bind: {
    uint64_t linkage = 0;
    ok = operand_id(r.ops[0], v) && is_declaration(ids_.find_gid(v));
    if (!ok) {
      problem = "unknown declaration: NAME_BIND names a G-id that was not "
                "declared";
    } else {
      ok = need_sid(r.ops[1], false, "the name", problem);
    }
    if (ok) {
      a_gid_entry *target = ids_.find_gid(v);
      operand_u64(r.ops[2], linkage);
      operand_id(r.ops[1], w);
      if (!(target->decl_flags & decl_flag_nil_introduced)) {
        problem = "NAME_BIND names G-id " + number(v) + ", which was "
                  "introduced with a name";
        ok = false;
      } else {
        ok = check_comdat(target->decl_flags, linkage,
                          static_cast<a_gid_kind>(target->kind),
                          problem) &&
             apply_name(v, *target, w, linkage, problem);
      }
    }
    break;
  }
  case eop_retract: {
    const a_gid_entry *g = nullptr;
    bool unsupported;
    ok = operand_id(r.ops[0], v) && (g = ids_.find_gid(v)) != nullptr &&
         is_declaration(g);
    /* A definition dropped as unsupported may be retracted at any linkage
       (ECIL spec 11.10): it clears the obligation to report it. */
    unsupported = ok && (g->decl_flags &
        (g->kind == gk_func ? ECIL_DECL_FUNC_UNSUPPORTED
                            : ECIL_DECL_VAR_UNSUPPORTED)) != 0;
    if (!ok) {
      problem = "unknown declaration: RETRACT names a G-id that is not a "
                "declared function or variable";
    } else if (!unsupported && g->linkage != elk_internal &&
               g->linkage != elk_linkonce_odr) {
      problem = "RETRACT of G-id " + number(v) + ": only internal or "
                "linkonce_odr entities can be retracted (unless their "
                "definition was dropped as unsupported)";
      ok = false;
    } else if ((g->role & sr_complete) && g->kind == gk_func) {
      problem = "RETRACT of a STRUCTOR pair names g_base (G-id " +
                number(v) + " is the complete variant)";
      ok = false;
    } else if (!unsupported &&
               !(g->kind == gk_func ? (g->flags & gf_defined) != 0
                                    : (g->decl_flags & 1) != 0)) {
      problem = "RETRACT of G-id " + number(v) + ", which is not defined";
      ok = false;
    }
    break;
  }
  case eop_ty_void:
    ok = introduce(r.ops[0], gk_void, problem) != nullptr;
    break;
  case eop_ty_bool:
  case eop_ty_float:
    ne = introduce(r.ops[0], r.opcode == eop_ty_bool ? gk_bool : gk_float,
                   problem);
    ok = ne != nullptr;
    if (ok)
      operand_u64(r.ops[r.opcode == eop_ty_bool ? 1 : 2], ne->size);
    break;
  case eop_ty_int:
    ne = introduce(r.ops[0], gk_int, problem);
    ok = ne != nullptr;
    if (ok) {
      uint64_t is_signed = 0;

      operand_u64(r.ops[1], ne->extent);
      operand_u64(r.ops[3], ne->size);
      /* The signedness (the ABI decoder spells result types). */
      if (r.ops[2].kind == an_operand_kind::boolean)
        is_signed = r.ops[2].u;
      else
        operand_u64(r.ops[2], is_signed);
      ne->role = is_signed != 0 ? 1 : 0;
    }
    break;
  case eop_ty_opaque:
    ne = introduce(r.ops[0], gk_opaque, problem);
    ok = ne != nullptr;
    if (ok)
      operand_u64(r.ops[1], ne->size);
    break;
  case eop_ty_ptr:
    ok = need_type(r.ops[1], false, problem);
    if (ok) {
      ne = introduce(r.ops[0], gk_ptr, problem);
      ok = ne != nullptr;
      if (ok) {
        operand_u64(r.ops[1], w);
        ne->element = static_cast<uint32_t>(w);
      }
    }
    break;
  case eop_ty_array:
    ok = need_type(r.ops[1], false, problem);
    if (ok) {
      ne = introduce(r.ops[0], gk_array, problem);
      ok = ne != nullptr;
      if (ok) {
        operand_u64(r.ops[1], w);
        ne->element = static_cast<uint32_t>(w);
        if (operand_u64(r.ops[2], ne->extent))
          ne->flags |= gf_has_count;
      }
    }
    break;
  case eop_ty_func: {
    an_array_reader reader = array_elements(r.ops[3]);
    uint64_t element = 0;

    ok = need_type(r.ops[1], false, problem);
    while (ok && next_uint_element(reader, element)) {
      const a_gid_entry *pe = ids_.find_gid(element);
      if (element == 0) {
        problem = "missing type G-id";
        ok = false;
      } else if (pe == nullptr) {
        problem = "unknown type: G-id " + number(element) +
                  " was not introduced earlier in the stream";
        ok = false;
      } else if (not_a_type(pe) || pe->kind == gk_vptr) {
        problem = "G-id " + number(element) + " is a declaration, not a "
                  "type";
        ok = false;
      }
    }
    if (ok) {
      ne = introduce(r.ops[0], gk_func_type, problem);
      ok = ne != nullptr;
      if (ok) {
        an_array_reader again = array_elements(r.ops[3]);
        uint64_t first = 0;
        uint64_t count = 0;

        operand_u64(r.ops[1], w);
        ne->element = static_cast<uint32_t>(w);
        /* The parameter count and the first parameter (the validator of the
           member functions of 1.2 reads them). */
        while (next_uint_element(again, element)) {
          if (count == 0)
            first = element;
          ++count;
        }
        ne->extent = count;
        ne->size = first;
      }
    }
    break;
  }
  case eop_ty_record_decl: {
    uint64_t kind = 0;
    operand_u64(r.ops[1], kind);
    ok = need_sid(r.ops[2], true, "the tag name", problem);
    if (ok) {
      ne = introduce(r.ops[0], gk_record, problem);
      ok = ne != nullptr;
      if (ok && kind == erk_union)
        ne->flags |= gf_union;
    }
    break;
  }
  case eop_ty_record_body: {
    a_gid_entry *rec = nullptr;
    ok = operand_id(r.ops[0], v) &&
         (rec = ids_.find_gid(v)) != nullptr && rec->kind == gk_record;
    if (!ok) {
      problem = "unknown type: TY_RECORD_BODY names a G-id that is not a "
                "TY_RECORD_DECL";
    } else if (rec->flags & gf_complete) {
      problem = "TY_RECORD_BODY completes G-id " + number(v) + " twice";
      ok = false;
    } else {
      uint64_t size = 0;
      uint64_t data_size = 0;

      operand_u64(r.ops[1], size);
      operand_u64(r.ops[2], data_size);
      operand_u64(r.ops[5], w);
      if (data_size > size) {
        problem = "TY_RECORD_BODY: data_size " + number(data_size) +
                  " exceeds size " + number(size);
        ok = false;
      } else {
        rec->flags |= gf_complete;
        uint64_t record_flags = 0;

        operand_u64(r.ops[4], record_flags);
        if (record_flags & ECIL_RECORD_NONTRIVIAL_DTOR)
          rec->flags |= gf_nontrivial_dtor;
        rec->extent = w;
        rec->size = size;
        if (data_size != size)
          ids_.set_record_data_size(static_cast<uint32_t>(v), data_size);
        fields_pending_ = w;
        cur_record_ = static_cast<uint32_t>(v);
        cur_size_ = size;
        cur_data_size_ = data_size;
        cur_end_ = 0;
        cur_union_ = (rec->flags & gf_union) != 0;
        members_seen_ = false;
        vbase_seen_ = false;
        vptr_fields_ = 0;
        {
          a_class_facts &facts = ids_.add_class_facts(
                                     static_cast<uint32_t>(v));
          uint64_t align = 0;

          operand_u64(r.ops[3], align);
          facts.size = size;
          facts.data_size = data_size;
          facts.align = align;
          facts.nvsize = data_size;
          facts.fields.reserve(static_cast<size_t>(std::min<uint64_t>(w,
                                                                    4096)));
        }
        if (fields_pending_ == 0 && !finish_record_facts(problem))
          ok = false;
      }
    }
    break;
  }
  case eop_rec_field: {
    if (fields_pending_ == 0) {
      problem = "REC_FIELD outside a TY_RECORD_BODY";
      ok = false;
    } else {
      --fields_pending_;
      allow_vptr_ = true;
      ok = need_type(r.ops[0], false, problem);
      allow_vptr_ = false;
      ok = ok && need_sid(r.ops[4], true, "the field name", problem);
      if (ok) {
        operand_u64(r.ops[0], v);
        e = ids_.find_gid(v);
        if (r.ops[3].kind != an_operand_kind::nil && e->kind != gk_int &&
            e->kind != gk_bool) {
          problem = "bit-field member of non-integer type G-id " + number(v);
          ok = false;
        } else if (e->kind == gk_array) {
          const a_gid_entry *el = ids_.find_gid(e->element);
          if (el != nullptr && el->kind == gk_record &&
              !(el->flags & gf_complete) && (e->flags & gf_has_count) &&
              e->extent != 0) {
            problem = "incomplete record: array member of G-id " +
                      number(e->element) + " has no body yet";
            ok = false;
          }
        } else if (e->kind == gk_record && !(e->flags & gf_complete)) {
          problem = "incomplete record: member of G-id " + number(v) +
                    " has no body yet";
          ok = false;
        }
        if (ok) {
          uint64_t fflags = 0;
          uint64_t foffset = 0;
          uint64_t fend = 0;
          size_t index = ids_.class_facts(cur_record_)->fields.size();

          operand_u64(r.ops[5], fflags);
          operand_u64(r.ops[1], foffset);
          fend = field_end(r, e, v);
          cur_end_ = std::max(cur_end_, fend);
          {
            a_class_facts *facts = ids_.class_facts(cur_record_);
            a_class_field field = {static_cast<uint32_t>(v), foffset,
                                   static_cast<uint8_t>(fflags)};

            facts->fields.push_back(field);
            /* A [[no_unique_address]] member of an empty class is zero-size:
               it does not make the class non-empty (isZeroSize). */
            if (!(fflags & (ECIL_FIELD_BASE | ECIL_FIELD_VIRTUAL_BASE)) &&
                !((fflags & ECIL_FIELD_POTENTIALLY_OVERLAPPING) &&
                  (fflags & ECIL_FIELD_EMPTY_FOR_ABI))) {
              uint64_t width = 0;

              /* A zero-width bit-field is no data member (isEmpty). */
              if (r.ops[3].kind == an_operand_kind::nil ||
                  !operand_u64(r.ops[3], width) || width != 0)
                facts->has_data = true;
            }
            if (!(fflags & ECIL_FIELD_VIRTUAL_BASE))
              facts->non_vbase_end = std::max(facts->non_vbase_end, fend);
            if (!(fflags & ECIL_FIELD_VIRTUAL_BASE) &&
                !(fflags & ECIL_FIELD_EMPTY_FOR_ABI)) {
              uint64_t data_end = fend;

              if ((fflags & ECIL_FIELD_BASE) && e->kind == gk_record) {
                const a_class_facts *bf = ids_.class_facts(
                                              static_cast<uint32_t>(v));

                if (bf != nullptr && bf->complete)
                  data_end = foffset + bf->nvsize;
              }
              facts->nv_data_end = std::max(facts->nv_data_end, data_end);
            }
          }
          if (ok && (fflags & (ECIL_FIELD_BASE | ECIL_FIELD_VIRTUAL_BASE |
                               ECIL_FIELD_POTENTIALLY_OVERLAPPING)) &&
              e->kind == gk_record) {
            const a_class_facts *bf = ids_.class_facts(
                                          static_cast<uint32_t>(v));

            if (bf != nullptr && bf->complete &&
                bf->dynamic && !(bf->stage & cs_info)) {
              problem = "a TY_RECORD_BODY with a base or overlapping member "
                        "of class G-id " + number(v) + " (a dynamic class) "
                        "needs its CLASS_INFO first";
              ok = false;
            }
          }
          if (!ok) {
            /* The problem is set. */
          } else if (e->kind == gk_vptr &&
              (index != 0 || foffset != 0 ||
               !(fflags & ECIL_FIELD_UNNAMED) ||
               r.ops[3].kind != an_operand_kind::nil || ++vptr_fields_ > 1)) {
            problem = "a TY_VPTR field is the unnamed field 0 at offset 0 "
                      "(the class's own vptr), at most once";
            ok = false;
          } else if ((fflags & ECIL_FIELD_VIRTUAL_BASE) &&
                     ((fflags & (ECIL_FIELD_BASE |
                                 ECIL_FIELD_POTENTIALLY_OVERLAPPING)) ||
                      e->kind != gk_record ||
                      r.ops[3].kind != an_operand_kind::nil)) {
            problem = "a VIRTUAL_BASE field is a record-typed field that is "
                      "not BASE, POTENTIALLY_OVERLAPPING or a bit-field "
                      "(G-id " + number(v) + ")";
            ok = false;
          } else if (!(fflags & ECIL_FIELD_VIRTUAL_BASE) && vbase_seen_) {
            problem = "VIRTUAL_BASE fields come after the data members";
            ok = false;
          } else if ((fflags & (ECIL_FIELD_BASE |
                                ECIL_FIELD_POTENTIALLY_OVERLAPPING))
              && e->kind != gk_record) {
            problem = "REC_FIELD BASE/POTENTIALLY_OVERLAPPING needs a record "
                      "type (G-id " + number(v) + ")";
            ok = false;
          } else if (fflags & ECIL_FIELD_VIRTUAL_BASE) {
            vbase_seen_ = true;
            members_seen_ = true;
          } else if (fflags & ECIL_FIELD_BASE) {
            if (members_seen_ || r.ops[3].kind != an_operand_kind::nil) {
              problem = "a BASE field comes before the data members and is "
                        "not a bit-field";
              ok = false;
            } else {
              operand_u64(r.ops[1], w);
              ids_.add_base(cur_record_, static_cast<uint32_t>(v), w);
            }
          } else if ((e->kind != gk_opaque && e->kind != gk_vptr) ||
                     r.ops[3].kind != an_operand_kind::nil ||
                     !(fflags & ECIL_FIELD_UNNAMED)) {
            /* The vptr (an unnamed opaque field) is laid out first, ahead of
               the bases, so it does not end the run of bases. */
            members_seen_ = true;
          }
        }
      }
      if (ok && fields_pending_ == 0 && !finish_record_facts(problem))
        ok = false;
      if (ok && fields_pending_ == 0 && !cur_union_ &&
          cur_data_size_ < cur_end_) {
        problem = "TY_RECORD_BODY: data_size " + number(cur_data_size_) +
                  " of G-id " + number(cur_record_) + " is below the end "
                  "of its last field that is not EMPTY_FOR_ABI (" +
                  number(cur_end_) + ")";
        ok = false;
      }
    }
    break;
  }
  case eop_decl_func:
    ok = declare(r, gk_func, problem);
    break;
  case eop_decl_var:
    ok = declare(r, gk_var, problem);
    break;
  case eop_var_init:
    ok = operand_id(r.ops[0], v) && (ne = ids_.find_gid(v)) != nullptr &&
         ne->kind == gk_var;
    if (!ok) {
      problem = "unknown declaration: VAR_INIT names a G-id that is not a "
                "declared variable";
    } else {
      ne->role |= vr_var_init;
      /* The variable's type must be complete by now. */
      const a_gid_entry *t = ids_.find_gid(ne->element);
      while (t != nullptr && t->kind == gk_array)
        t = ids_.find_gid(t->element);
      if (t != nullptr && t->kind == gk_record && !(t->flags & gf_complete)) {
        problem = "incomplete record: VAR_INIT of G-id " + number(v) +
                  " whose type has no TY_RECORD_BODY";
        ok = false;
      }
    }
    break;
  case eop_func_begin:
    ok = operand_id(r.ops[0], v) && (ne = ids_.find_gid(v)) != nullptr &&
         ne->kind == gk_func;
    if (!ok) {
      problem = "unknown declaration: FUNC_BEGIN names a G-id that is not a "
                "declared function";
    } else if ((ne->flags & gf_defined) && !replaying) {
      problem = "function G-id " + number(v) + " is defined twice";
      ok = false;
    } else if ((ne->role & 1) && !(ne->role & 4)) {
      problem = "FUNC_BEGIN of complete structor G-id " + number(v) +
                " (the body is streamed under the base variant)";
      ok = false;
    } else {
      const a_gid_entry *ft = ids_.find_gid(ne->element);
      a_fn_state state = {ft != nullptr ? ft->element : 0u, 0};
      ne->flags |= gf_defined;
      ids_.begin_function();
      fns_.push_back(state);
      fn_info_ = a_function_info();
      fn_class_ = 0;
      fn_gid_ = static_cast<uint32_t>(v);
      if (ne->role & (sr_complete | sr_base)) {
        fn_info_.structor = (ne->role & sr_dtor) ? 2 : 1;
        fn_info_.complete = (ne->role & sr_complete) != 0;
        fn_info_.has_vbases = (ne->role & sr_vbases) != 0;
      }
      fn_info_.nothrow_decl = (ne->decl_flags & ECIL_DECL_FUNC_NOTHROW) != 0;
      /* The class of a structor: the has_vbases flag of its STRUCTOR must
         agree with the class's VIRTUAL_BASE fields. */
      if (fn_info_.structor != 0 && ft != nullptr && ft->extent != 0) {
        const a_gid_entry *pt = ids_.find_gid(ft->size);
        const a_class_facts *f = pt != nullptr && pt->kind == gk_ptr
                                     ? ids_.class_facts(pt->element)
                                     : nullptr;

        if (f != nullptr && f->complete &&
            f->vbases.empty() == fn_info_.has_vbases) {
          problem = "STRUCTOR has_vbases does not match the VIRTUAL_BASE "
                    "fields of the class (function G-id " + number(v) + ")";
          ok = false;
        }
      }
    }
    break;
  case eop_func_end:
    if (!ids_.in_function()) {
      problem = "FUNC_END without FUNC_BEGIN";
      ok = false;
    } else {
      size_t first = ids_.function_lid_begin();
      size_t count = ids_.lid_table_size();
      if (!fns_.empty())
        fns_.pop_back();
      for (size_t k = first + 1; k < count; ++k) {
        const a_lid_entry &entry = ids_.lid_at_index(k);
        if ((entry.flags & lf_goto_target) && entry.state != ls_label) {
          problem = "label L-id " + number(k - first) +
                    " is the target of a GOTO but never defined by LABEL";
          ok = false;
          break;
        }
      }
    }
    break;
  case eop_param: {
    uint64_t pflags = 0;
    operand_u64(r.ops[4], pflags);
    ok = need_object(r.ops[1], "PARAM", problem) &&
         need_object(r.ops[2], "PARAM", problem) &&
         need_sid(r.ops[3], true, "the parameter name", problem) &&
         introduce_local(r.ops[0], ls_value, problem,
                         operand_u64(r.ops[2], w) ? static_cast<uint32_t>(w)
                                                  : 0);
    if (ok && (pflags & ECIL_PARAM_THIS)) {
      const a_gid_entry *pt = ids_.find_gid(
                                 operand_u64(r.ops[2], w) ? w : 0);
      if ((!fns_.empty() && fns_.back().params != 0) ||
          pt == nullptr || pt->kind != gk_ptr) {
        problem = "PARAM THIS must be the first parameter and a pointer";
        ok = false;
      } else {
        /* The class `this` points to, for the structor rules of 1.2. */
        const a_class_facts *f = ids_.class_facts(pt->element);

        fn_class_ = pt->element;
        fn_info_.cls = pt->element;
        fn_info_.dynamic = f != nullptr && f->dynamic;
      }
    }
    if (ok && !fns_.empty())
      ++fns_.back().params;
    break;
  }
  case eop_local: {
    uint64_t lflags = 0;
    operand_u64(r.ops[4], lflags);
    ok = need_object(r.ops[1], "LOCAL", problem) &&
         need_sid(r.ops[2], true, "the local name", problem) &&
         introduce_local(r.ops[0], ls_value, problem,
                         operand_u64(r.ops[1], w) ? static_cast<uint32_t>(w)
                                                  : 0);
    if (ok && (lflags & ECIL_LOCAL_RETURN_SLOT) && !returns_aggregate()) {
      problem = "LOCAL RETURN_SLOT in a function that does not return a "
                "record or array";
      ok = false;
    }
    break;
  }
  case eop_materialize_temp:
    ok = need_object(r.ops[1], "MATERIALIZE_TEMP", problem) &&
         introduce_local(r.ops[0], ls_value, problem,
                         operand_u64(r.ops[1], w) ? static_cast<uint32_t>(w)
                                                  : 0);
    break;
  case eop_label:
    ok = need_sid(r.ops[1], true, "the label name", problem) &&
         introduce_local(r.ops[0], ls_label, problem);
    break;
  case eop_goto_stmt:
    if (!ids_.in_function() || !operand_id(r.ops[0], v)) {
      problem = "GOTO outside a function or without a label";
      ok = false;
    } else {
      a_lid_entry *entry = ids_.touch_lid(v, problem);
      if (entry == nullptr) {
        ok = false;
      } else if (entry->state == ls_value || entry->state == ls_dead) {
        problem = "GOTO target L-id " + number(v) + " is not a label";
        ok = false;
      } else {
        entry->flags |= lf_goto_target;
      }
    }
    break;
  case eop_lref:
    if (!ids_.in_function() || !operand_id(r.ops[0], v)) {
      problem = "LREF outside a function";
      ok = false;
    } else {
      a_lid_entry *entry = ids_.find_lid(v);
      if (entry != nullptr && entry->state == ls_dead) {
        problem = "local id out of scope: L-id " + number(v) +
                  " was declared in a scope region that has closed";
        ok = false;
      } else if (entry == nullptr || entry->state != ls_value) {
        problem = "unknown local id: L-id " + number(v) +
                  " was not introduced by PARAM, LOCAL or MATERIALIZE_TEMP "
                  "in this function";
        ok = false;
      }
    }
    break;
  case eop_gref:
    ok = operand_id(r.ops[0], v) && is_declaration(ids_.find_gid(v));
    if (!ok)
      problem = "unknown declaration: GREF names a G-id that was not declared";
    break;
  case eop_field:
  case eop_extract:
    ok = need_complete(r.ops[0], problem, e);
    if (ok && e->kind != gk_record) {
      problem = "selector on a type that is not a record";
      ok = false;
    }
    if (ok) {
      operand_u64(r.ops[1], w);
      if (w >= e->extent) {
        problem = "field index " + number(w) + " is out of range for a "
                  "record with " + number(e->extent) + " fields";
        ok = false;
      } else {
        operand_u64(r.ops[0], v);
        const a_class_facts *cf = ids_.class_facts(static_cast<uint32_t>(v));

        if (cf != nullptr && w < cf->fields.size() &&
            (cf->fields[w].flags & ECIL_FIELD_VIRTUAL_BASE)) {
          problem = std::string(r.info->name) + " of a VIRTUAL_BASE field "
                    "(reach a virtual base with VBASE_CAST)";
          ok = false;
        }
      }
    }
    break;
  case eop_agg_init_begin:
    ok = need_complete(r.ops[0], problem, e);
    if (ok && e->kind != gk_record && e->kind != gk_array) {
      problem = "AGG_INIT_BEGIN on a type that is neither record nor array";
      ok = false;
    }
    if (ok) {
      operand_u64(r.ops[0], v);
      agg_targets_.push_back(static_cast<uint32_t>(v));
    }
    break;
  case eop_agg_sel:
    operand_u64(r.ops[0], w);
    ok = check_selector(w, 1, false, problem);
    break;
  case eop_agg_init_end:
    if (agg_targets_.empty()) {
      problem = "AGG_INIT_END without AGG_INIT_BEGIN";
      ok = false;
    } else {
      agg_targets_.pop_back();
    }
    break;
  case eop_c_agg_begin:
    ok = need_complete(r.ops[0], problem, e);
    if (ok && e->kind != gk_record && e->kind != gk_array) {
      problem = "C_AGG_BEGIN on a type that is neither record nor array";
      ok = false;
    }
    if (ok) {
      operand_u64(r.ops[0], v);
      const a_class_facts *cf = ids_.class_facts(static_cast<uint32_t>(v));

      if (cf != nullptr && cf->complete && cf->dynamic)
        ok = need_class_facts(v, false, "a constant of the class "
                              "(C_AGG_BEGIN)", problem);
      if (ok)
        const_targets_.push_back(static_cast<uint32_t>(v));
    }
    break;
  case eop_c_sel:
    operand_u64(r.ops[0], w);
    ok = check_selector(w, 1, true, problem);
    break;
  case eop_c_repeat: {
    uint64_t n = 0;
    operand_u64(r.ops[0], w);
    operand_u64(r.ops[1], n);
    ok = check_selector(w, n == 0 ? 1 : n, true, problem);
    break;
  }
  case eop_c_agg_end:
    if (const_targets_.empty()) {
      problem = "C_AGG_END without C_AGG_BEGIN";
      ok = false;
    } else {
      const_targets_.pop_back();
    }
    break;
  case eop_const_int:
    ok = need_type(r.ops[0], false, problem);
    if (ok && operand_u64(r.ops[0], v) && ids_.find_gid(v)->kind == gk_ptr) {
      problem = "CONST_INT of pointer type G-id " + number(v) +
                " (use CONST_PTR or CONST_NULL)";
      ok = false;
    }
    if (ok)
      ok = check_int_value(r.ops[0], r.ops[1], "CONST_INT", problem);
    break;
  case eop_const_float:
  case eop_const_null:
  case eop_const_ptr:
  case eop_c_float:
  case eop_c_null:
  case eop_c_zero:
  case eop_c_string:
  case eop_c_ptr:
    ok = need_type(r.ops[0], false, problem);
    if (ok && r.opcode == eop_c_zero && operand_u64(r.ops[0], v)) {
      uint64_t rg = v;
      const a_gid_entry *zt = ids_.find_gid(rg);
      const a_class_facts *cf = nullptr;

      while (zt != nullptr && zt->kind == gk_array) {
        rg = zt->element;
        zt = ids_.find_gid(rg);
      }
      if (zt != nullptr && zt->kind == gk_record)
        cf = ids_.class_facts(static_cast<uint32_t>(rg));
      if (cf != nullptr && cf->dynamic) {
        problem = "C_ZERO of a dynamic class (a constant of a dynamic class "
                  "lists its members)";
        ok = false;
      }
    }
    break;
  case eop_c_int:
    ok = need_type(r.ops[0], false, problem) &&
         check_int_value(r.ops[0], r.ops[1], "C_INT", problem);
    break;
  case eop_c_addr:
    ok = need_type(r.ops[0], false, problem);
    if (ok) {
      ok = operand_id(r.ops[1], v) &&
           (is_declaration(ids_.find_gid(v)) ||
            (ids_.find_gid(v) != nullptr &&
             ids_.find_gid(v)->kind == gk_abi_data));
      if (!ok)
        problem = "unknown declaration: C_ADDR names a G-id that was not "
                  "declared";
    }
    break;
  case eop_addr_offset:
    ok = need_type(r.ops[0], false, problem);
    if (ok && operand_u64(r.ops[0], v) && ids_.find_gid(v)->kind != gk_ptr) {
      problem = "ADDR_OFFSET result type G-id " + number(v) +
                " is not a pointer type";
      ok = false;
    }
    break;
  case eop_cast:
  case eop_ptr_diff:
  case eop_va_arg:
  case eop_tern_begin:
    ok = need_type(r.ops[r.opcode == eop_cast ? 1 : 0],
                   r.opcode == eop_tern_begin, problem);
    break;
  case eop_builtin:
    ok = need_type(r.ops[2], true, problem);
    break;
  case eop_call:
    ok = operand_id(r.ops[0], v) && is_declaration(ids_.find_gid(v));
    if (!ok) {
      problem = "unknown declaration: CALL names a G-id that was not "
                "declared";
    } else if (ids_.find_gid(v)->kind != gk_func) {
      problem = "CALL names variable G-id " + number(v) +
                " (use CALL_INDIRECT through a pointer)";
      ok = false;
    } else {
      ok = need_type(r.ops[1], false, problem) &&
           check_call_flags(r, problem);
    }
    break;
  case eop_call_indirect:
    ok = need_type(r.ops[0], false, problem);
    break;
  case eop_hdr_lang: {
    uint64_t language = 0;
    uint64_t lang_flags = 0;

    operand_u64(r.ops[0], language);
    operand_u64(r.ops[2], lang_flags);
    ids_.set_lang(static_cast<uint32_t>(language),
                  static_cast<uint32_t>(lang_flags));
    break;
  }
  case eop_end:
    ok = check_end(problem);
    break;
  default:
    if (r.info->min_minor >= 2)
      ok = check_m4(r, problem);
    else if (r.info->section & eos_v1)
      ok = check_v1(r, problem);
    break;
  }
  return ok;
}  /* a_ref_checker::check */

}  /* namespace ecil */
