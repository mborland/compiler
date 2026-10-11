/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
ecil_brackets.cpp -- a_bracket_tracker: the one matcher of FUNC_BEGIN/FUNC_END
and the BEGIN/ELSE/END brackets, plus the scope regions of spec 8.3 (the
dead-local rule now, the cleanup ranges of C++ later).  Heap stacks only.
*/

#include <algorithm>
#include <limits>

#include "ecil_reader.h"

namespace ecil {

namespace {

/* Which record opens and closes which scope region.  A region is what a LOCAL
   or MATERIALIZE_TEMP belongs to (spec 8.3); an `if` arm, a loop body, a
   switch body, a block, the for-init scope and the function body are the
   scope regions, a bracket that is not listed here (a condition, a ternary,
   an initializer) is not one. */
struct a_region_rule {
  uint8_t  opcode;
  uint8_t  closes;     /* Regions closed (innermost first). */
  uint8_t  opens;      /* Regions opened (after the closes). */
  uint8_t  open_kind;  /* a_region_kind of the opened region. */
};  /* a_region_rule */

const a_region_rule region_rules[] = {
  {eop_func_begin,   0, 1, rk_function},
  {eop_func_end,     1, 0, rk_none},
  {eop_block_begin,  0, 1, rk_block},
  {eop_block_end,    1, 0, rk_none},
  {eop_for_begin,    0, 1, rk_for_init},
  {eop_for_body,     0, 1, rk_loop_body},
  {eop_for_end,      2, 0, rk_none},       /* The body, then the init scope. */
  {eop_if_begin,     0, 1, rk_if_arm},
  {eop_if_else,      1, 1, rk_if_arm},
  {eop_if_end,       1, 0, rk_none},
  {eop_while_body,   0, 1, rk_loop_body},
  {eop_while_end,    1, 0, rk_none},
  {eop_do_begin,     0, 1, rk_loop_body},
  {eop_do_cond,      1, 0, rk_none},
  {eop_switch_begin, 0, 1, rk_switch_body},
  {eop_switch_end,   1, 0, rk_none},
  {eop_fullexpr_begin,    0, 1, rk_fullexpr},
  {eop_fullexpr_end,      1, 0, rk_none},
  {eop_static_init_begin, 0, 1, rk_static_init},
  {eop_static_init_end,   1, 0, rk_none},
  {eop_meminit_begin,     0, 1, rk_meminit},
  {eop_meminit_end,       1, 0, rk_none},
  {eop_array_ctor_begin,  0, 1, rk_array_body},
  {eop_array_ctor_end,    1, 0, rk_none},
  {eop_array_dtor_begin,  0, 1, rk_array_body},
  {eop_array_dtor_end,    1, 0, rk_none}
};

}  /* anonymous namespace */

const char *bracket_name(uint8_t kind)
/*
Return a readable name for a bracket kind.
*/
{
  static const char *const names[] = {
    "BLOCK", "IF", "WHILE", "DO", "FOR", "SWITCH", "TERN", "LAND", "LOR",
    "AGG_INIT", "C_AGG", "CLEANUP", "FULLEXPR", "STATIC_INIT", "MEMINIT",
    "ARRAY"
  };
  return kind < sizeof names / sizeof names[0] ? names[kind] : "?";
}  /* bracket_name */

a_bracket_tracker::a_bracket_tracker()
  : switch_depth_(0), cleanup_code_depth_(0), static_init_depth_(0),
    fullexpr_depth_(0), conditional_depth_(0), body_started_(false), meminit_count_(0),
    meminit_delegation_(false), meminit_last_idx_(0),
    kill_from_(std::numeric_limits<size_t>::max()), ended_function_(false)
{
}  /* a_bracket_tracker::a_bracket_tracker */

void a_bracket_tracker::begin_frame()
/*
Every frame starts with nothing open.
*/
{
  brackets_.clear();
  function_bases_.clear();
  regions_.clear();
  scope_locals_.clear();
  switch_depth_ = 0;
  cleanup_code_depth_ = 0;
  static_init_depth_ = 0;
  fullexpr_depth_ = 0;
  conditional_depth_ = 0;
  body_started_ = false;
  meminit_count_ = 0;
  meminit_delegation_ = false;
  meminit_last_idx_ = 0;
  kill_from_ = std::numeric_limits<size_t>::max();
  ended_function_ = false;
}  /* a_bracket_tracker::begin_frame */

void a_bracket_tracker::note_local(size_t lid_index)
/*
A local belongs to the innermost open scope region.
*/
{
  scope_locals_.push_back(lid_index);
}  /* a_bracket_tracker::note_local */

bool a_bracket_tracker::apply_regions(uint8_t opcode, std::string &problem,
                                      a_scope_event &event)
/*
Open and close the scope regions of a record that has a rule.
*/
{
  bool ok = true;

  for (const a_region_rule &rule : region_rules) {
    if (rule.opcode != opcode)
      continue;
    if (regions_.size() < rule.closes) {
      problem = "scope region stack underflow";
      ok = false;
      break;
    }
    for (uint8_t k = 0; k < rule.closes; ++k) {
      event.closed_kind[event.closed++] = regions_.back().kind;
      /* A FULLEXPR is a scope region for cleanups only: the locals
         introduced inside it belong to the enclosing region. */
      if (regions_.back().kind != rk_fullexpr &&
          regions_.back().first < kill_from_)
        kill_from_ = regions_.back().first;
      regions_.pop_back();
    }
    if (rule.opens != 0) {
      /* The regions this record closed give up their locals at commit(), so
         a region opened by the same record (IF_ELSE) starts where they
         started, not at the current end of the list. */
      size_t first = kill_from_ != std::numeric_limits<size_t>::max()
                         ? kill_from_ : scope_locals_.size();
      a_region region = {rule.open_kind, first};
      regions_.push_back(region);
      ++event.opened;
    }
    break;
  }
  return ok;
}  /* a_bracket_tracker::apply_regions */

void a_bracket_tracker::commit(a_id_tables &ids)
/*
Apply what the last record closed to the id tables.
*/
{
  if (kill_from_ != std::numeric_limits<size_t>::max()) {
    for (size_t k = kill_from_; k < scope_locals_.size(); ++k) {
      a_lid_entry &entry = ids.lid_at_index(scope_locals_[k]);
      if (entry.state == ls_value)
        entry.state = ls_dead;
    }
    if (kill_from_ < scope_locals_.size())
      scope_locals_.resize(kill_from_);
    kill_from_ = std::numeric_limits<size_t>::max();
  }
  if (ended_function_) {
    ids.end_function();
    ended_function_ = false;
  }
}  /* a_bracket_tracker::commit */

bool a_bracket_tracker::conditional_without_fullexpr() const
/*
True if a conditional region (TERN arms, the right operand of LAND/LOR) is
open and no FULLEXPR encloses the outermost one in this function.
*/
{
  size_t base = function_bases_.empty() ? 0 : function_bases_.back();
  size_t outermost = brackets_.size();
  bool covered = false;

  for (size_t k = base; k < brackets_.size(); ++k) {
    uint8_t kind = brackets_[k].kind;
    if (kind == bk_tern || kind == bk_land || kind == bk_lor) {
      outermost = k;
      break;
    }
  }
  for (size_t k = base; k < outermost; ++k) {
    if (brackets_[k].kind == bk_fullexpr)
      covered = true;
  }
  return !covered;
}  /* a_bracket_tracker::conditional_without_fullexpr */

bool a_bracket_tracker::in_loop_condition() const
/*
True if the innermost scope region is reached directly through the condition
or step of a loop (no FULLEXPR, block or other region in between): a cleanup
begun there would never be closed by the loop's end.
*/
{
  bool result = false;
  size_t base = function_bases_.empty() ? 0 : function_bases_.back();

  for (size_t k = brackets_.size(); k > base; --k) {
    const a_bracket &b = brackets_[k - 1];
    if (b.kind == bk_tern || b.kind == bk_land || b.kind == bk_lor ||
        b.kind == bk_agg_init || b.kind == bk_const_agg ||
        b.kind == bk_cleanup)
      continue;
    result = (b.kind == bk_while && b.phase == 0) ||
             (b.kind == bk_do && b.phase == 1) ||
             (b.kind == bk_for && (b.phase == 1 || b.phase == 2));
    break;
  }
  return result;
}  /* a_bracket_tracker::in_loop_condition */

bool a_bracket_tracker::check_v1(const a_record &record,
                                 std::string &problem)
/*
The structural rules of the C++ records (spec 11): what may appear inside
cleanup code and static initializers, where exit and conditional cleanups
may be, CLEANUP_END, the MEMINIT prologue.  Value-stack rules are the
consumer's.
*/
{
  bool ok = true;
  uint8_t op = record.opcode;
  bool at_function_level = !function_bases_.empty() &&
                           brackets_.size() == function_bases_.back();
  bool jump = op == eop_return_stmt || op == eop_return_from_slot ||
              op == eop_goto_stmt ||
              op == eop_label || op == eop_break_stmt ||
              op == eop_continue_stmt;
  bool statement = jump || op == eop_block_begin || op == eop_if_begin ||
                   op == eop_while_begin || op == eop_do_begin ||
                   op == eop_for_begin || op == eop_switch_begin ||
                   op == eop_case_value || op == eop_case_range ||
                   op == eop_case_default;
  const a_bracket *group = brackets_.empty() ? nullptr : &brackets_.back();
  uint64_t kind_value = 0;
  std::string name = record.info->name;

  if (cleanup_code_depth_ != 0 &&
      (jump || op == eop_local || op == eop_materialize_temp ||
       op == eop_cleanup_begin || op == eop_fullexpr_begin)) {
    problem = name + " is not allowed inside cleanup code";
    ok = false;
  } else if (fullexpr_depth_ != 0 && statement) {
    problem = name + " is not allowed inside a FULLEXPR (it holds an "
              "expression)";
    ok = false;
  } else if (group != nullptr && group->kind == bk_static_init &&
             (group->flags & abf_exit_only) != 0 &&
             op != eop_cleanup_begin && op != eop_static_init_end &&
             op != eop_loc && op != eop_ext) {
    problem = name + " is not allowed in a STATIC_INIT of a variable that "
              "has a VAR_INIT (only its exit cleanup)";
    ok = false;
  } else if (group != nullptr && group->kind == bk_static_init &&
             (group->flags & abf_exit_only) != 0 &&
             op == eop_cleanup_begin &&
             !(operand_u64(record.ops[0], kind_value) &&
               kind_value == ecl_exit)) {
    problem = "only exit cleanups are allowed in a STATIC_INIT of a "
              "variable that has a VAR_INIT";
    ok = false;
  } else if (static_init_depth_ != 0 && jump) {
    problem = name + " is not allowed inside a static initializer (it is a "
              "single block)";
    ok = false;
  } else if (static_init_depth_ != 0 &&
             (op == eop_return_slot ||
              (op == eop_local && (record.ops[4].u & ECIL_LOCAL_RETURN_SLOT)))) {
    problem = name + " is not allowed inside a static initializer (it has "
              "no return object)";
    ok = false;
  } else if (op == eop_cleanup_begin) {
    uint64_t kind = 0;
    operand_u64(record.ops[0], kind);
    if (kind != ecl_exit && in_loop_condition()) {
      problem = "CLEANUP_BEGIN directly in a loop condition or step needs a "
                "FULLEXPR";
      ok = false;
    } else if (kind == ecl_exit && static_init_depth_ == 0) {
      problem = "CLEANUP_BEGIN(exit) outside STATIC_INIT";
      ok = false;
    } else if (kind != ecl_exit && conditional_depth_ != 0 &&
               conditional_without_fullexpr()) {
      problem = "a conditional cleanup needs an enclosing FULLEXPR";
      ok = false;
    }
  } else if (op == eop_cleanup_end) {
    problem = "CLEANUP_END is reserved for M4 (v1 cleanups end with their "
              "scope region or FULLEXPR)";
    ok = false;
  } else if (op == eop_meminit_begin) {
    uint64_t kind = 0;
    uint64_t idx = 0;
    operand_u64(record.ops[0], kind);
    operand_u64(record.ops[1], idx);
    if (!at_function_level || body_started_) {
      problem = "MEMINIT_BEGIN only starts a constructor body";
      ok = false;
    } else if (kind == emk_vbase) {
      problem = "MEMINIT_BEGIN of a virtual base is not part of ECIL v1";
      ok = false;
    } else if (meminit_delegation_ ||
               (kind == emk_delegation && meminit_count_ != 0)) {
      problem = "a delegating MEMINIT must be the only initializer group";
      ok = false;
    } else if (kind != emk_delegation && meminit_count_ != 0 &&
               idx <= meminit_last_idx_) {
      problem = "MEMINIT idx must strictly increase";
      ok = false;
    } else {
      ++meminit_count_;
      meminit_last_idx_ = idx;
      meminit_delegation_ = kind == emk_delegation;
    }
  }
  /* A function's constructor prologue ends at its first other record. */
  if (ok && at_function_level && op != eop_param && op != eop_loc &&
      op != eop_ext && op != eop_meminit_begin && op != eop_cleanup_begin)
    body_started_ = true;
  return ok;
}  /* a_bracket_tracker::check_v1 */

bool a_bracket_tracker::check(const a_record &record,
                              std::string &problem_text,
                              a_scope_event &event)
/*
Track FUNC_BEGIN/FUNC_END and the BEGIN/ELSE/END brackets with explicit
stacks, and reject mismatches.
*/
{
  bool result = true;
  uint8_t op = record.opcode;
  const char *problem = nullptr;
  a_bracket *top = brackets_.empty() ? nullptr : &brackets_.back();
  size_t depth_before = brackets_.size();
  uint8_t top_kind = top != nullptr ? top->kind : 0;
  uint8_t action = eba_none;
  uint8_t kind = 0;

  bool v1_ok = check_v1(record, problem_text);

  if (!v1_ok)
    problem = "";
  if (v1_ok) switch (op) {
  case eop_func_begin:
    if (!function_bases_.empty())
      problem = "nested FUNC_BEGIN (function bodies are never nested)";
    else {
      function_bases_.push_back(brackets_.size());
      body_started_ = false;
      meminit_count_ = 0;
      meminit_delegation_ = false;
      meminit_last_idx_ = 0;
    }
    break;
  case eop_func_end:
    if (function_bases_.empty())
      problem = "FUNC_END without FUNC_BEGIN";
    else if (brackets_.size() != function_bases_.back())
      problem = "unclosed bracket at FUNC_END";
    else
      function_bases_.pop_back();
    break;
  case eop_block_begin: {
    a_bracket b = {bk_block, 0, 0,
                   static_cast<uint8_t>(top != nullptr && top->case_ok)};
    brackets_.push_back(b);
    break;
  }
  case eop_block_end:
    if (top && top->kind == bk_block)
      brackets_.pop_back();
    else
      problem = "BLOCK_END without BLOCK_BEGIN";
    break;
  case eop_if_begin: {
    uint64_t has_else = 0;
    operand_u64(record.ops[0], has_else);
    if (record.ops[0].kind == an_operand_kind::boolean)
      has_else = record.ops[0].u;
    a_bracket b = {bk_if, 0, static_cast<uint8_t>(has_else), 0};
    brackets_.push_back(b);
    break;
  }
  case eop_if_else:
    if (top && top->kind == bk_if && top->phase == 0 && top->has_else)
      top->phase = 1;
    else
      problem = "IF_ELSE without a matching IF_BEGIN(has_else)";
    break;
  case eop_if_end:
    if (top && top->kind == bk_if && (top->has_else ? top->phase == 1
                                                    : top->phase == 0))
      brackets_.pop_back();
    else
      problem = "IF_END does not match IF_BEGIN/IF_ELSE";
    break;
  case eop_while_begin: {
    a_bracket b = {bk_while, 0, 0, 0};
    brackets_.push_back(b);
    break;
  }
  case eop_while_body:
    if (top && top->kind == bk_while && top->phase == 0)
      top->phase = 1;
    else
      problem = "WHILE_BODY without WHILE_BEGIN";
    break;
  case eop_while_end:
    if (top && top->kind == bk_while && top->phase == 1)
      brackets_.pop_back();
    else
      problem = "WHILE_END without WHILE_BODY";
    break;
  case eop_do_begin: {
    a_bracket b = {bk_do, 0, 0, 0};
    brackets_.push_back(b);
    break;
  }
  case eop_do_cond:
    if (top && top->kind == bk_do && top->phase == 0)
      top->phase = 1;
    else
      problem = "DO_COND without DO_BEGIN";
    break;
  case eop_do_end:
    if (top && top->kind == bk_do && top->phase == 1)
      brackets_.pop_back();
    else
      problem = "DO_END without DO_COND";
    break;
  case eop_for_begin: {
    a_bracket b = {bk_for, 0, 0, 0};
    brackets_.push_back(b);
    break;
  }
  case eop_for_cond:
    if (top && top->kind == bk_for && top->phase == 0)
      top->phase = 1;
    else
      problem = "FOR_COND out of order";
    break;
  case eop_for_step:
    if (top && top->kind == bk_for && top->phase == 1)
      top->phase = 2;
    else
      problem = "FOR_STEP out of order";
    break;
  case eop_for_body:
    if (top && top->kind == bk_for && top->phase == 2)
      top->phase = 3;
    else
      problem = "FOR_BODY out of order";
    break;
  case eop_for_end:
    if (top && top->kind == bk_for && top->phase == 3)
      brackets_.pop_back();
    else
      problem = "FOR_END without FOR_BODY";
    break;
  case eop_switch_begin: {
    a_bracket b = {bk_switch, 0, 0, 1};
    brackets_.push_back(b);
    ++switch_depth_;
    break;
  }
  case eop_switch_end:
    if (top && top->kind == bk_switch) {
      brackets_.pop_back();
      --switch_depth_;
    } else {
      problem = "SWITCH_END without SWITCH_BEGIN";
    }
    break;
  case eop_case_value:
  case eop_case_range:
  case eop_case_default:
    if (switch_depth_ == 0)
      problem = "case label outside a switch";
    else if (!(top != nullptr && top->case_ok))
      problem = "case label not directly in a SWITCH body (or in BLOCKs "
                "nested directly in it)";
    break;
  case eop_tern_begin: {
    a_bracket b = {bk_tern, 0, 0, 0};
    brackets_.push_back(b);
    ++conditional_depth_;
    break;
  }
  case eop_tern_else:
    if (top && top->kind == bk_tern && top->phase == 0)
      top->phase = 1;
    else
      problem = "TERN_ELSE without TERN_BEGIN";
    break;
  case eop_tern_end:
    if (top && top->kind == bk_tern && top->phase == 1) {
      brackets_.pop_back();
      --conditional_depth_;
    } else
      problem = "TERN_END without TERN_ELSE";
    break;
  case eop_land_begin:
  case eop_lor_begin: {
    a_bracket b = {static_cast<uint8_t>(op == eop_land_begin ? bk_land
                                                              : bk_lor),
                   0, 0, 0};
    brackets_.push_back(b);
    ++conditional_depth_;
    break;
  }
  case eop_land_end:
    if (top && top->kind == bk_land) {
      brackets_.pop_back();
      --conditional_depth_;
    } else
      problem = "LAND_END without LAND_BEGIN";
    break;
  case eop_lor_end:
    if (top && top->kind == bk_lor) {
      brackets_.pop_back();
      --conditional_depth_;
    } else
      problem = "LOR_END without LOR_BEGIN";
    break;
  case eop_agg_init_begin: {
    a_bracket b = {bk_agg_init, 0, 0, 0};
    brackets_.push_back(b);
    break;
  }
  case eop_agg_sel:
    if (!(top && top->kind == bk_agg_init))
      problem = "AGG_SEL outside AGG_INIT_BEGIN";
    break;
  case eop_agg_init_end:
    if (top && top->kind == bk_agg_init)
      brackets_.pop_back();
    else
      problem = "AGG_INIT_END without AGG_INIT_BEGIN";
    break;
  case eop_c_agg_begin: {
    a_bracket b = {bk_const_agg, 0, 0, 0};
    brackets_.push_back(b);
    break;
  }
  case eop_c_sel:
  case eop_c_repeat:
    if (!(top && top->kind == bk_const_agg))
      problem = "C_SEL/C_REPEAT outside C_AGG_BEGIN";
    break;
  case eop_c_agg_end:
    if (top && top->kind == bk_const_agg)
      brackets_.pop_back();
    else
      problem = "C_AGG_END without C_AGG_BEGIN";
    break;
  case eop_cleanup_begin: {
    uint64_t ckind = 0;
    operand_u64(record.ops[0], ckind);
    a_bracket b = {bk_cleanup, static_cast<uint8_t>(ckind), 0, 0};
    brackets_.push_back(b);
    ++cleanup_code_depth_;
    break;
  }
  case eop_cleanup_body:
    if (top && top->kind == bk_cleanup) {
      brackets_.pop_back();
      --cleanup_code_depth_;
    } else {
      problem = "CLEANUP_BODY without CLEANUP_BEGIN";
    }
    break;
  case eop_fullexpr_begin: {
    a_bracket b = {bk_fullexpr, 0, 0, 0, 0};
    brackets_.push_back(b);
    ++fullexpr_depth_;
    break;
  }
  case eop_fullexpr_end:
    if (top && top->kind == bk_fullexpr) {
      brackets_.pop_back();
      --fullexpr_depth_;
    } else
      problem = "FULLEXPR_END without FULLEXPR_BEGIN";
    break;
  case eop_static_init_begin: {
    /* Outside a function (F_GLOBALS) the group has an L-id space of its
       own, closed by STATIC_INIT_END. */
    uint8_t own = function_bases_.empty() ? abf_own_ids : 0;
    a_bracket b = {bk_static_init, 0, 0, 0, own};
    brackets_.push_back(b);
    ++static_init_depth_;
    break;
  }
  case eop_static_init_end:
    if (top && top->kind == bk_static_init) {
      if (top->flags & abf_own_ids)
        ended_function_ = true;
      brackets_.pop_back();
      --static_init_depth_;
    } else {
      problem = "STATIC_INIT_END without STATIC_INIT_BEGIN";
    }
    break;
  case eop_meminit_begin: {
    a_bracket b = {bk_meminit, 0, 0, 0};
    brackets_.push_back(b);
    break;
  }
  case eop_meminit_end:
    if (top && top->kind == bk_meminit)
      brackets_.pop_back();
    else
      problem = "MEMINIT_END without MEMINIT_BEGIN";
    break;
  case eop_array_ctor_begin:
  case eop_array_dtor_begin: {
    a_bracket b = {bk_array,
                   static_cast<uint8_t>(op == eop_array_dtor_begin), 0, 0};
    brackets_.push_back(b);
    break;
  }
  case eop_array_ctor_end:
    if (top && top->kind == bk_array && top->phase == 0)
      brackets_.pop_back();
    else
      problem = "ARRAY_CTOR_END without ARRAY_CTOR_BEGIN";
    break;
  case eop_array_dtor_end:
    if (top && top->kind == bk_array && top->phase == 1)
      brackets_.pop_back();
    else
      problem = "ARRAY_DTOR_END without ARRAY_DTOR_BEGIN";
    break;
  default:
    break;
  }
  if (problem != nullptr) {
    if (v1_ok)
      problem_text = problem;
    if (top)
      problem_text += std::string(" (innermost open bracket: ") +
                      bracket_name(top_kind) + ")";
    result = false;
  } else {
    if (brackets_.size() > depth_before) {
      action = eba_open;
      kind = brackets_.back().kind;
    } else if (brackets_.size() < depth_before) {
      action = eba_close;
      kind = top_kind;
    } else if (top != nullptr && brackets_.size() == depth_before &&
               (op == eop_if_else || op == eop_while_body ||
                op == eop_do_cond || op == eop_for_cond ||
                op == eop_for_step || op == eop_for_body ||
                op == eop_tern_else)) {
      action = eba_advance;
      kind = top_kind;
    }
    event.bracket_action = action;
    event.bracket_kind = kind;
    event.bracket_depth = static_cast<uint32_t>(brackets_.size());
    result = apply_regions(op, problem_text, event);
    if (result && op == eop_func_end)
      ended_function_ = true;
  }
  return result;
}  /* a_bracket_tracker::check */

}  /* namespace ecil */
