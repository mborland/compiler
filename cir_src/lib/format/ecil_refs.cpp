/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
ecil_refs.cpp -- reference checking of the ECIL stream validator: every
string id, G-id and L-id a record uses must have been introduced earlier,
selectors must name complete records with an index in range, and one
external symbol name must not be bound by two G-ids.
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

}  /* anonymous namespace */

a_ref_checker::a_ref_checker()
  : fields_pending_(0), opname_("")
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

const a_ref_checker::a_ent *a_ref_checker::find(uint64_t g) const
/*
The entity introduced for G-id g, or nullptr.
*/
{
  auto it = ents_.find(g);

  return it == ents_.end() ? nullptr : &it->second;
}  /* a_ref_checker::find */

bool a_ref_checker::need_sid(const an_operand &op, bool optional,
                             const char *what, std::string &problem)
/*
A string id operand: it must have been introduced by STR.
*/
{
  uint64_t v = 0;
  bool ok = true;

  if (operand_id(op, v)) {
    if (strings_.find(v) == strings_.end()) {
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
    const a_ent *e = find(v);
    if (e == nullptr) {
      problem = "unknown type: G-id " + number(v) +
                " was not introduced earlier in the stream";
      ok = false;
    } else if (e->kind == k_func || e->kind == k_var) {
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
                                  const a_ent *&out)
/*
A type operand that must be a complete record, or an array (for AGG_INIT and
C_AGG targets).
*/
{
  uint64_t v = 0;
  bool ok = need_type(op, false, problem);

  out = nullptr;
  if (ok && operand_id(op, v)) {
    out = find(v);
    if (out->kind == k_record && !out->complete) {
      problem = "incomplete record: G-id " + number(v) +
                " has no TY_RECORD_BODY yet";
      ok = false;
    }
  }
  return ok;
}  /* a_ref_checker::need_complete */

bool a_ref_checker::introduce(const an_operand &op, a_ent entity,
                              std::string &problem)
/*
A G-id introduced as a type: it must be new.
*/
{
  uint64_t v = 0;
  bool ok = operand_id(op, v);

  if (!ok) {
    problem = "G-id 0 cannot be introduced";
  } else if (ents_.find(v) != ents_.end()) {
    problem = "G-id " + number(v) + " is introduced twice";
    ok = false;
  } else {
    ents_[v] = entity;
  }
  return ok;
}  /* a_ref_checker::introduce */

bool a_ref_checker::bind_name(uint64_t g, const an_operand &sid,
                              uint64_t linkage, std::string &problem)
/*
One G-id per symbol: two G-ids must not bind one external symbol name.
*/
{
  uint64_t v = 0;
  bool ok = true;

  if (operand_id(sid, v) && is_external_linkage(linkage)) {
    auto s = strings_.find(v);
    if (s != strings_.end() && !s->second.empty()) {
      auto it = symbol_owner_.find(s->second);
      if (it == symbol_owner_.end())
        symbol_owner_[s->second] = g;
      else if (it->second != g) {
        problem = "one G-id per symbol: '" + s->second +
                  "' is bound by G-ids " + number(it->second) + " and " +
                  number(g);
        ok = false;
      }
    }
  }
  return ok;
}  /* a_ref_checker::bind_name */

bool a_ref_checker::declare(const a_record &r, a_kind kind,
                            std::string &problem)
/*
DECL_FUNC / DECL_VAR (g, t, linkage, flags, [align,] sid): introduce the
entity or re-issue it (the same kind only).
*/
{
  uint64_t g = 0;
  uint64_t t = 0;
  uint64_t linkage = 0;
  bool ok = operand_id(r.ops[0], g);
  const a_ent *e;
  const an_operand &name = r.ops[r.count - 1];

  if (!ok) {
    problem = "G-id 0 cannot be declared";
  } else {
    ok = need_type(r.ops[1], false, problem) &&
         need_sid(name, true, "the name", problem);
  }
  if (ok) {
    operand_id(r.ops[1], t);
    e = find(t);
    if (kind == k_func && e->kind != k_func_type) {
      problem = "DECL_FUNC type G-id " + number(t) +
                " is not a function type";
      ok = false;
    }
  }
  if (ok) {
    e = find(g);
    if (e != nullptr && e->kind != kind) {
      problem = "one G-id per symbol: G-id " + number(g) +
                " was introduced as something else and cannot be re-declared "
                "as a " + (kind == k_func ? "function" : "variable");
      ok = false;
    } else if (e == nullptr) {
      a_ent entity = {kind, false, 0, 0, false, 0, false};
      ents_[g] = entity;
    }
  }
  if (ok) {
    operand_u64(r.ops[2], linkage);
    ok = bind_name(g, name, linkage, problem);
  }
  return ok;
}  /* a_ref_checker::declare */

bool a_ref_checker::introduce_local(const an_operand &op, a_lkind kind,
                                    std::string &problem)
/*
PARAM / LOCAL / MATERIALIZE_TEMP / LABEL: a new L-id of the current function.
*/
{
  uint64_t v = 0;
  bool ok = true;

  if (functions_.empty()) {
    problem = "local id introduced outside a function";
    ok = false;
  } else if (!operand_id(op, v)) {
    problem = "L-id 0 cannot be introduced";
    ok = false;
  } else {
    auto &lids = functions_.back().lids;
    auto it = lids.find(v);
    if (it != lids.end()) {
      problem = "L-id " + number(v) + " is introduced twice in the function";
      ok = false;
    } else {
      lids[v] = kind;
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
  std::vector<uint64_t> &stack = is_const ? const_targets_ : agg_targets_;
  bool ok = !stack.empty();

  if (!ok) {
    problem = std::string("selector outside ") +
              (is_const ? "C_AGG_BEGIN" : "AGG_INIT_BEGIN");
  } else {
    const a_ent *t = find(stack.back());
    if (t->kind == k_record) {
      if (index >= t->nfields || repeat != 1) {
        problem = "field index " + number(index) + " is out of range for "
                  "record G-id " + number(stack.back()) + " with " +
                  number(t->nfields) + " fields";
        ok = false;
      }
    } else if (t->kind == k_array) {
      if (t->has_count && index + repeat > t->count) {
        problem = "element index " + number(index) + " (+" + number(repeat) +
                  ") is out of range for array G-id " +
                  number(stack.back()) + " of " + number(t->count);
        ok = false;
      }
    } else {
      problem = "selector on a target that is neither record nor array";
      ok = false;
    }
  }
  return ok;
}  /* a_ref_checker::check_selector */

bool a_ref_checker::check(const a_record &r, std::string &problem)
/*
Apply the reference rules of one record.
*/
{
  bool ok = true;
  uint64_t v = 0;
  uint64_t w = 0;
  const a_ent *e = nullptr;

  opname_ = r.info->name;
  if (fields_pending_ != 0 && r.opcode != eop_rec_field &&
      r.opcode != eop_loc && r.opcode != eop_ext) {
    problem = "a TY_RECORD_BODY is missing REC_FIELD records";
    ok = false;
  }
  if (ok) switch (r.opcode) {
  case eop_str: {
    std::string text;
    if (operand_u64(r.ops[0], v) && r.ops[1].kind == an_operand_kind::bin &&
        r.ops[1].data != nullptr)
      text.assign(reinterpret_cast<const char *>(r.ops[1].data),
                  r.ops[1].length);
    if (!operand_id(r.ops[0], v)) {
      problem = "string id 0 cannot be introduced";
      ok = false;
    } else if (strings_.find(v) != strings_.end() && strings_[v] != text) {
      problem = "string id " + number(v) + " is introduced twice with "
                "different contents";
      ok = false;
    } else {
      strings_[v] = text;
    }
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
    ok = operand_id(r.ops[0], v) && find(v) != nullptr &&
         (find(v)->kind == k_func || find(v)->kind == k_var);
    if (!ok)
      problem = "unknown declaration: NAME_BIND names a G-id that was not "
                "declared";
    else
      ok = need_sid(r.ops[1], false, "the name", problem);
    if (ok) {
      operand_u64(r.ops[2], linkage);
      ok = bind_name(v, r.ops[1], linkage, problem);
    }
    break;
  }
  case eop_retract:
    ok = operand_id(r.ops[0], v) && find(v) != nullptr;
    if (!ok)
      problem = "unknown declaration: RETRACT names a G-id that was not "
                "introduced";
    break;
  case eop_ty_void: {
    a_ent t = {k_void, false, 0, 0, false, 0, false};
    ok = introduce(r.ops[0], t, problem);
    break;
  }
  case eop_ty_bool:
  case eop_ty_int:
  case eop_ty_float: {
    a_ent t = {r.opcode == eop_ty_bool ? k_bool
               : r.opcode == eop_ty_int ? k_int : k_float,
               false, 0, 0, false, 0, false};
    ok = introduce(r.ops[0], t, problem);
    break;
  }
  case eop_ty_opaque: {
    a_ent t = {k_opaque, false, 0, 0, false, 0, false};
    ok = introduce(r.ops[0], t, problem);
    break;
  }
  case eop_ty_ptr: {
    a_ent t = {k_ptr, false, 0, 0, false, 0, false};
    ok = need_type(r.ops[1], false, problem) &&
         introduce(r.ops[0], t, problem);
    break;
  }
  case eop_ty_array: {
    a_ent t = {k_array, false, 0, 0, false, 0, false};
    ok = need_type(r.ops[1], false, problem);
    if (ok) {
      operand_u64(r.ops[1], t.element);
      t.has_count = operand_u64(r.ops[2], t.count);
      ok = introduce(r.ops[0], t, problem);
    }
    break;
  }
  case eop_ty_func: {
    a_ent t = {k_func_type, false, 0, 0, false, 0, false};
    an_array_reader reader = array_elements(r.ops[3]);
    an_operand element;

    ok = need_type(r.ops[1], false, problem);
    while (ok && next_array_element(reader, element))
      ok = need_type(element, false, problem);
    if (ok)
      ok = introduce(r.ops[0], t, problem);
    break;
  }
  case eop_ty_record_decl: {
    a_ent t = {k_record, false, 0, 0, false, 0, false};
    uint64_t kind = 0;
    operand_u64(r.ops[1], kind);
    t.is_union = kind == erk_union;
    ok = need_sid(r.ops[2], true, "the tag name", problem) &&
         introduce(r.ops[0], t, problem);
    break;
  }
  case eop_ty_record_body: {
    ok = operand_id(r.ops[0], v) && find(v) != nullptr &&
         find(v)->kind == k_record;
    if (!ok) {
      problem = "unknown type: TY_RECORD_BODY names a G-id that is not a "
                "TY_RECORD_DECL";
    } else if (find(v)->complete) {
      problem = "TY_RECORD_BODY completes G-id " + number(v) + " twice";
      ok = false;
    } else {
      a_ent &rec = ents_[v];
      operand_u64(r.ops[5], w);
      rec.complete = true;
      rec.nfields = w;
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
        e = find(v);
        if (e->kind == k_array) {
          const a_ent *el = find(e->element);
          if (el != nullptr && el->kind == k_record && !el->complete &&
              e->has_count && e->count != 0) {
            problem = "incomplete record: array member of G-id " +
                      number(e->element) + " has no body yet";
            ok = false;
          }
        } else if (e->kind == k_record && !e->complete) {
          problem = "incomplete record: member of G-id " + number(v) +
                    " has no body yet";
          ok = false;
        }
      }
    }
    break;
  }
  case eop_decl_func:
    ok = declare(r, k_func, problem);
    break;
  case eop_decl_var:
    ok = declare(r, k_var, problem);
    break;
  case eop_var_init:
    ok = operand_id(r.ops[0], v) && find(v) != nullptr &&
         find(v)->kind == k_var;
    if (!ok)
      problem = "unknown declaration: VAR_INIT names a G-id that is not a "
                "declared variable";
    break;
  case eop_func_begin: {
    ok = operand_id(r.ops[0], v) && find(v) != nullptr &&
         find(v)->kind == k_func;
    if (!ok)
      problem = "unknown declaration: FUNC_BEGIN names a G-id that is not a "
                "declared function";
    else
      functions_.emplace_back();
    break;
  }
  case eop_func_end:
    if (functions_.empty()) {
      problem = "FUNC_END without FUNC_BEGIN";
      ok = false;
    } else {
      for (uint64_t target : functions_.back().goto_targets) {
        auto it = functions_.back().lids.find(target);
        if (it == functions_.back().lids.end() || it->second != l_label) {
          problem = "label L-id " + number(target) +
                    " is the target of a GOTO but never defined by LABEL";
          ok = false;
          break;
        }
      }
      functions_.pop_back();
    }
    break;
  case eop_param:
    ok = need_type(r.ops[1], false, problem) &&
         need_type(r.ops[2], false, problem) &&
         need_sid(r.ops[3], true, "the parameter name", problem) &&
         introduce_local(r.ops[0], l_value, problem);
    break;
  case eop_local:
    ok = need_type(r.ops[1], false, problem) &&
         need_sid(r.ops[2], true, "the local name", problem) &&
         introduce_local(r.ops[0], l_value, problem);
    break;
  case eop_materialize_temp:
    ok = need_type(r.ops[1], false, problem) &&
         introduce_local(r.ops[0], l_value, problem);
    break;
  case eop_label:
    ok = need_sid(r.ops[1], true, "the label name", problem) &&
         introduce_local(r.ops[0], l_label, problem);
    break;
  case eop_goto_stmt:
    if (functions_.empty() || !operand_id(r.ops[0], v)) {
      problem = "GOTO outside a function or without a label";
      ok = false;
    } else {
      auto &fs = functions_.back();
      auto it = fs.lids.find(v);
      if (it != fs.lids.end() && it->second != l_label) {
        problem = "GOTO target L-id " + number(v) + " is not a label";
        ok = false;
      } else {
        fs.goto_targets.insert(v);
      }
    }
    break;
  case eop_lref:
    if (functions_.empty() || !operand_id(r.ops[0], v)) {
      problem = "LREF outside a function";
      ok = false;
    } else {
      auto &lids = functions_.back().lids;
      auto it = lids.find(v);
      if (it == lids.end() || it->second != l_value) {
        problem = "unknown local id: L-id " + number(v) +
                  " was not introduced by PARAM, LOCAL or MATERIALIZE_TEMP "
                  "in this function";
        ok = false;
      }
    }
    break;
  case eop_gref:
    ok = operand_id(r.ops[0], v) && find(v) != nullptr &&
         (find(v)->kind == k_func || find(v)->kind == k_var);
    if (!ok)
      problem = "unknown declaration: GREF names a G-id that was not declared";
    break;
  case eop_field:
  case eop_extract:
    ok = need_complete(r.ops[0], problem, e);
    if (ok && e->kind != k_record) {
      problem = "selector on a type that is not a record";
      ok = false;
    }
    if (ok) {
      operand_u64(r.ops[1], w);
      if (w >= e->nfields) {
        problem = "field index " + number(w) + " is out of range for a "
                  "record with " + number(e->nfields) + " fields";
        ok = false;
      }
    }
    break;
  case eop_agg_init_begin:
    ok = need_complete(r.ops[0], problem, e);
    if (ok && e->kind != k_record && e->kind != k_array) {
      problem = "AGG_INIT_BEGIN on a type that is neither record nor array";
      ok = false;
    }
    if (ok) {
      operand_u64(r.ops[0], v);
      agg_targets_.push_back(v);
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
    if (ok && e->kind != k_record && e->kind != k_array) {
      problem = "C_AGG_BEGIN on a type that is neither record nor array";
      ok = false;
    }
    if (ok) {
      operand_u64(r.ops[0], v);
      const_targets_.push_back(v);
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
      ok = operand_id(r.ops[1], v) && find(v) != nullptr &&
           (find(v)->kind == k_func || find(v)->kind == k_var);
      if (!ok)
        problem = "unknown declaration: C_ADDR names a G-id that was not "
                  "declared";
    }
    break;
  case eop_addr_offset:
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
    ok = operand_id(r.ops[0], v) && find(v) != nullptr &&
         (find(v)->kind == k_func || find(v)->kind == k_var);
    if (!ok)
      problem = "unknown declaration: CALL names a G-id that was not declared";
    else
      ok = need_type(r.ops[1], false, problem);
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
