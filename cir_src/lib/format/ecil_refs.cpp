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

}  /* anonymous namespace */

a_ref_checker::a_ref_checker(a_id_tables &ids, a_bracket_tracker &tracker)
  : ids_(ids), tracker_(tracker), fields_pending_(0)
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
    } else if (is_declaration(e)) {
      problem = "G-id " + number(v) + " is a declaration, not a type";
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
    operand_u64(r.ops[2], linkage);
    operand_id(name_op, name);
    e->element = static_cast<uint32_t>(t);
    ok = apply_name(g, *e, name, linkage, problem);
  }
  return ok;
}  /* a_ref_checker::declare */

bool a_ref_checker::introduce_local(const an_operand &op, a_lid_state state,
                                    std::string &problem)
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
  }
  return ok;
}  /* a_ref_checker::end_frame */

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
      operand_u64(r.ops[2], linkage);
      operand_id(r.ops[1], w);
      ok = apply_name(v, *ids_.find_gid(v), w, linkage, problem);
    }
    break;
  }
  case eop_retract:
    ok = operand_id(r.ops[0], v) && ids_.find_gid(v) != nullptr;
    if (!ok)
      problem = "unknown declaration: RETRACT names a G-id that was not "
                "introduced";
    break;
  case eop_ty_void:
    ok = introduce(r.ops[0], gk_void, problem) != nullptr;
    break;
  case eop_ty_bool:
  case eop_ty_float:
    ok = introduce(r.ops[0], r.opcode == eop_ty_bool ? gk_bool : gk_float,
                   problem) != nullptr;
    break;
  case eop_ty_int:
    ne = introduce(r.ops[0], gk_int, problem);
    ok = ne != nullptr;
    if (ok)
      operand_u64(r.ops[1], ne->extent);
    break;
  case eop_ty_opaque:
    ok = introduce(r.ops[0], gk_opaque, problem) != nullptr;
    break;
  case eop_ty_ptr:
    ok = need_type(r.ops[1], false, problem) &&
         introduce(r.ops[0], gk_ptr, problem) != nullptr;
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
      } else if (is_declaration(pe)) {
        problem = "G-id " + number(element) + " is a declaration, not a "
                  "type";
        ok = false;
      }
    }
    if (ok)
      ok = introduce(r.ops[0], gk_func_type, problem) != nullptr;
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
      operand_u64(r.ops[5], w);
      rec->flags |= gf_complete;
      rec->extent = w;
      fields_pending_ = w;
    }
    break;
  }
  case eop_rec_field: {
    if (fields_pending_ == 0) {
      problem = "REC_FIELD outside a TY_RECORD_BODY";
      ok = false;
    } else {
      --fields_pending_;
      ok = need_type(r.ops[0], false, problem) &&
           need_sid(r.ops[4], true, "the field name", problem);
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
    } else if (ne->flags & gf_defined) {
      problem = "function G-id " + number(v) + " is defined twice";
      ok = false;
    } else {
      ne->flags |= gf_defined;
      ids_.begin_function();
    }
    break;
  case eop_func_end:
    if (!ids_.in_function()) {
      problem = "FUNC_END without FUNC_BEGIN";
      ok = false;
    } else {
      size_t first = ids_.function_lid_begin();
      size_t count = ids_.lid_table_size();
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
  case eop_param:
    ok = need_object(r.ops[1], "PARAM", problem) &&
         need_object(r.ops[2], "PARAM", problem) &&
         need_sid(r.ops[3], true, "the parameter name", problem) &&
         introduce_local(r.ops[0], ls_value, problem);
    break;
  case eop_local:
    ok = need_object(r.ops[1], "LOCAL", problem) &&
         need_sid(r.ops[2], true, "the local name", problem) &&
         introduce_local(r.ops[0], ls_value, problem);
    break;
  case eop_materialize_temp:
    ok = need_object(r.ops[1], "MATERIALIZE_TEMP", problem) &&
         introduce_local(r.ops[0], ls_value, problem);
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
    break;
  case eop_const_float:
  case eop_const_null:
  case eop_const_ptr:
  case eop_c_int:
  case eop_c_float:
  case eop_c_null:
  case eop_c_zero:
  case eop_c_string:
  case eop_c_ptr:
    ok = need_type(r.ops[0], false, problem);
    break;
  case eop_c_addr:
    ok = need_type(r.ops[0], false, problem);
    if (ok) {
      ok = operand_id(r.ops[1], v) && is_declaration(ids_.find_gid(v));
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
      ok = need_type(r.ops[1], false, problem);
    }
    break;
  case eop_call_indirect:
    ok = need_type(r.ops[0], false, problem);
    break;
  default:
    break;
  }
  return ok;
}  /* a_ref_checker::check */

}  /* namespace ecil */
