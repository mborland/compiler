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
  {eop_array_dtor_end,    1, 0, rk_none},
  {eop_try_begin,         0, 1, rk_try_body},
  {eop_catch_begin,       1, 1, rk_catch},
  {eop_try_end,           1, 0, rk_none}
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
    "ARRAY", "TRY"
  };
  return kind < sizeof names / sizeof names[0] ? names[kind] : "?";
}  /* bracket_name */

a_bracket_tracker::a_bracket_tracker()
  : switch_depth_(0), cleanup_code_depth_(0), static_init_depth_(0),
    fullexpr_depth_(0), conditional_depth_(0), body_started_(false), meminit_count_(0),
    meminit_delegation_(false), meminit_last_idx_(0),
    kill_from_(std::numeric_limits<size_t>::max()), ended_function_(false),
    lang_flags_(0), eh_spec_seen_(false), function_try_seen_(false),
    vptr_seen_(false), prologue_checked_(false), meminit_rank_(0),
    meminit_group_rank_(255), catch_head_(0), catch_local_(0),
    next_region_id_(0)
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
  fn_ = a_function_info();
  eh_spec_seen_ = false;
  function_try_seen_ = false;
  vptr_seen_ = false;
  prologue_checked_ = false;
  meminit_rank_ = 0;
  meminit_group_rank_ = 255;
  catch_head_ = 0;
  cleanups_.clear();
  throws_.clear();
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
      while (!cleanups_.empty() &&
             cleanups_.back().region == regions_.back().id) {
        if (cleanups_.back().deactivatable && cleanups_.back().active) {
          problem = "a DEACTIVATABLE cleanup is still active at the end of "
                    "its range (CLEANUP_END must deactivate it first)";
          ok = false;
        }
        cleanups_.pop_back();
      }
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
      a_region region = {rule.open_kind, first, ++next_region_id_};
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

bool a_bracket_tracker::at_prologue_level() const
/*
At the level where MEMINIT and VPTR_INIT may stand: directly in the function
body, or directly in the body of its function-try-block.
*/
{
  bool result = false;

  if (!function_bases_.empty()) {
    size_t base = function_bases_.back();

    if (brackets_.size() == base) {
      result = true;
    } else if (brackets_.size() == base + 1) {
      const a_bracket &b = brackets_.back();

      result = b.kind == bk_try && b.phase == 0 &&
               (b.flags & abf_function_try) != 0;
    }
  }
  return result;
}  /* a_bracket_tracker::at_prologue_level */

bool a_bracket_tracker::end_prologue(std::string &problem)
/*
The constructor/destructor prologue ends (the first record that is not part
of it): a dynamic class's structor must have stored its vptrs by now.
*/
{
  bool ok = true;

  if (!prologue_checked_) {
    prologue_checked_ = true;
    if (fn_.structor != 0 && fn_.dynamic && !fn_.synthesized_d1() &&
        !vptr_seen_ && !(fn_.structor == 1 && meminit_delegation_)) {
      problem = "VPTR_INIT is missing from the prologue of a constructor or "
                "destructor of a dynamic class";
      ok = false;
    }
  }
  return ok;
}  /* a_bracket_tracker::end_prologue */

uint32_t a_bracket_tracker::fullexpr_identity() const
/*
Index+1 of the innermost FULLEXPR bracket of the current function; 0 outside
one.
*/
{
  uint32_t id = 0;
  size_t base = function_bases_.empty() ? 0 : function_bases_.back();

  for (size_t k = brackets_.size(); k > base && id == 0; --k) {
    if (brackets_[k - 1].kind == bk_fullexpr)
      id = static_cast<uint32_t>(k);
  }
  return id;
}  /* a_bracket_tracker::fullexpr_identity */

uint32_t a_bracket_tracker::arm_identity() const
/*
Identify the innermost conditional arm (a TERN arm, the right operand of
LAND/LOR) of the current function; 0 outside one.
*/
{
  uint32_t result = 0;
  size_t base = function_bases_.empty() ? 0 : function_bases_.back();

  for (size_t k = brackets_.size(); k > base && result == 0; --k) {
    const a_bracket &b = brackets_[k - 1];

    if (b.kind == bk_tern)
      result = static_cast<uint32_t>(k) * 2 + b.phase;
    else if (b.kind == bk_land || b.kind == bk_lor)
      result = static_cast<uint32_t>(k) * 2;
  }
  return result;
}  /* a_bracket_tracker::arm_identity */

bool a_bracket_tracker::check_v1(const a_record &record,
                                 std::string &problem)
/*
The structural rules of the C++ records (spec 11 and 12): what may appear
inside cleanup code and static initializers, where exit and conditional
cleanups may be, CLEANUP_END, the constructor/destructor prologue (MEMINIT,
VPTR_INIT), the exception brackets.  Value-stack rules are the consumer's.
*/
{
  bool ok = true;
  uint8_t op = record.opcode;
  bool prologue = at_prologue_level() && !body_started_;
  bool at_function_level = !function_bases_.empty() &&
                           brackets_.size() == function_bases_.back();
  bool jump = op == eop_return_stmt || op == eop_return_from_slot ||
              op == eop_goto_stmt ||
              op == eop_label || op == eop_break_stmt ||
              op == eop_continue_stmt;
  bool try_op = op == eop_try_begin || op == eop_catch_begin ||
                op == eop_try_end;
  bool statement = jump || try_op || op == eop_block_begin ||
                   op == eop_if_begin ||
                   op == eop_while_begin || op == eop_do_begin ||
                   op == eop_for_begin || op == eop_switch_begin ||
                   op == eop_case_value || op == eop_case_range ||
                   op == eop_case_default;
  bool exception_op = try_op || op == eop_catch_param ||
                      op == eop_throw_begin || op == eop_throw_end ||
                      op == eop_rethrow || op == eop_func_eh_spec ||
                      op == eop_array_partial_dtor;
  const a_bracket *group = brackets_.empty() ? nullptr : &brackets_.back();
  uint64_t kind_value = 0;
  std::string name = record.info->name;

  /* The handler head: CATCH_BEGIN [LOCAL l] CATCH_PARAM l. */
  if (catch_head_ != 0) {
    if (op == eop_loc || op == eop_ext) {
      /* Transparent. */
    } else if (op == eop_local && catch_head_ == 1) {
      catch_head_ = 2;
      operand_u64(record.ops[0], catch_local_);
    } else if (op == eop_catch_param) {
      uint64_t l = 0;
      operand_u64(record.ops[0], l);
      if (catch_head_ != 2 || l != catch_local_) {
        problem = "CATCH_PARAM must directly follow the LOCAL of its "
                  "parameter, which directly follows CATCH_BEGIN";
        ok = false;
      }
      catch_head_ = 0;
    } else {
      catch_head_ = 0;
    }
  } else if (op == eop_catch_param) {
    problem = "CATCH_PARAM must directly follow the LOCAL of its parameter, "
              "which directly follows CATCH_BEGIN";
    ok = false;
  }
  if (ok && exception_op && !(lang_flags_ & ECIL_LANG_EXCEPTIONS)) {
    problem = name + " needs exceptions = 1 in HDR_LANG";
    ok = false;
  }
  if (!ok) {
    /* A rule above failed. */
  } else if (cleanup_code_depth_ != 0 &&
      (jump || try_op || op == eop_local || op == eop_materialize_temp ||
       op == eop_cleanup_begin || op == eop_cleanup_end ||
       op == eop_fullexpr_begin || op == eop_throw_begin ||
       op == eop_rethrow)) {
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
  } else if (static_init_depth_ != 0 && (jump || try_op)) {
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
    } else if ((record.ops[2].u & ECIL_CLEANUP_DEACTIVATABLE) != 0 &&
               kind != ecl_eh && kind != ecl_all) {
      problem = "CLEANUP_BEGIN DEACTIVATABLE is only for eh and all "
                "cleanups";
      ok = false;
    }
  } else if (op == eop_cleanup_end) {
    size_t k = cleanups_.size();

    while (k > 0 && !(cleanups_[k - 1].deactivatable &&
                      cleanups_[k - 1].active))
      --k;
    if (k == 0) {
      problem = "CLEANUP_END without an active DEACTIVATABLE cleanup";
      ok = false;
    } else if (cleanups_[k - 1].fullexpr != fullexpr_identity() ||
               cleanups_[k - 1].arm != arm_identity()) {
      problem = "CLEANUP_END is in another FULLEXPR or conditional arm than "
                "the CLEANUP_BEGIN it would deactivate";
      ok = false;
    } else {
      cleanups_[k - 1].active = false;
    }
  } else if (op == eop_func_eh_spec) {
    if (!(at_function_level && !body_started_ && !function_try_seen_)) {
      problem = "FUNC_EH_SPEC is a header record: after the PARAMs, before "
                "any body record";
      ok = false;
    } else if (eh_spec_seen_) {
      problem = "FUNC_EH_SPEC appears twice in a function";
      ok = false;
    } else if (record.ops[0].u == eesk_noexcept && !fn_.nothrow_decl) {
      problem = "FUNC_EH_SPEC noexcept in a function whose DECL_FUNC has no "
                "NOTHROW";
      ok = false;
    } else {
      eh_spec_seen_ = true;
    }
  } else if (op == eop_try_begin &&
             (record.ops[0].u & ECIL_TRY_FUNCTION_TRY) != 0) {
    if (!(at_function_level && !body_started_ && !function_try_seen_ &&
          meminit_count_ == 0 && cleanups_.empty())) {
      problem = "TRY_BEGIN FUNCTION_TRY comes right after the function "
                "header";
      ok = false;
    } else if (fn_.structor == 0 || fn_.synthesized_d1()) {
      problem = "TRY_BEGIN FUNCTION_TRY is only for the body of a "
                "constructor or destructor (C1, C2, D2 or the only body)";
      ok = false;
    }
  } else if (op == eop_throw_begin) {
    if (conditional_depth_ != 0 && conditional_without_fullexpr()) {
      problem = "THROW_BEGIN in a conditional region needs an enclosing "
                "FULLEXPR";
      ok = false;
    } else {
      a_throw t = {brackets_.size(), cleanups_.size()};
      throws_.push_back(t);
    }
  } else if (op == eop_throw_end) {
    if (throws_.empty()) {
      problem = "THROW_END without THROW_BEGIN";
      ok = false;
    } else if (brackets_.size() != throws_.back().brackets) {
      problem = "THROW_END is in another bracket than its THROW_BEGIN";
      ok = false;
    } else {
      for (size_t k = throws_.back().cleanups; k < cleanups_.size(); ++k) {
        if (cleanups_[k].deactivatable && cleanups_[k].active) {
          problem = "a DEACTIVATABLE cleanup begun after THROW_BEGIN is "
                    "still active at THROW_END (CLEANUP_END it first)";
          ok = false;
          break;
        }
      }
      if (ok)
        throws_.pop_back();
    }
  } else if (op == eop_vptr_init) {
    if (!prologue) {
      problem = "VPTR_INIT only in the prologue of a constructor or "
                "destructor";
      ok = false;
    } else if (fn_.structor == 0 || !fn_.dynamic) {
      problem = "VPTR_INIT outside a constructor or destructor of a "
                "dynamic class";
      ok = false;
    } else if (fn_.synthesized_d1()) {
      problem = "VPTR_INIT in the synthesized D1 (D2 stores the vptrs)";
      ok = false;
    } else if (vptr_seen_) {
      problem = "VPTR_INIT appears twice";
      ok = false;
    } else if (fn_.structor == 1 && meminit_delegation_) {
      problem = "VPTR_INIT in a delegating constructor";
      ok = false;
    } else {
      vptr_seen_ = true;
      meminit_rank_ = 2;
    }
  } else if (op == eop_meminit_begin) {
    uint64_t kind = 0;
    uint64_t idx = 0;
    operand_u64(record.ops[0], kind);
    operand_u64(record.ops[1], idx);
    if (!prologue) {
      problem = "MEMINIT_BEGIN only starts a constructor body";
      ok = false;
    } else if (kind == emk_vbase &&
               !(fn_.structor == 1 && fn_.complete && fn_.has_vbases)) {
      problem = "MEMINIT_BEGIN of a virtual base is only for the complete "
                "constructor (C1) of a class with virtual bases";
      ok = false;
    } else if (meminit_delegation_ ||
               (kind == emk_delegation &&
                (meminit_count_ != 0 || vptr_seen_))) {
      problem = "a delegating MEMINIT must be the only initializer group";
      ok = false;
    } else if (kind != emk_delegation && kind < meminit_rank_) {
      problem = "MEMINIT kinds appear in the order vbase, base, field (and "
                "VPTR_INIT follows the base groups)";
      ok = false;
    } else if (kind != emk_delegation && kind == meminit_group_rank_ &&
               idx <= meminit_last_idx_) {
      problem = "MEMINIT idx must strictly increase within a kind";
      ok = false;
    } else if (kind == emk_field && fn_.structor == 1 && fn_.dynamic &&
               !vptr_seen_) {
      problem = "VPTR_INIT comes before the first field MEMINIT";
      ok = false;
    } else {
      ++meminit_count_;
      meminit_last_idx_ = idx;
      if (kind != emk_delegation) {
        meminit_group_rank_ = static_cast<uint8_t>(kind);
        if (kind > meminit_rank_)
          meminit_rank_ = static_cast<uint8_t>(kind);
      }
      meminit_delegation_ = kind == emk_delegation;
    }
  }
  /* The prologue of a constructor or destructor ends at its first other
     record. */
  if (ok && prologue && op != eop_param && op != eop_loc && op != eop_ext &&
      op != eop_meminit_begin && op != eop_cleanup_begin &&
      op != eop_func_eh_spec &&
      !(op == eop_try_begin &&
        (record.ops[0].u & ECIL_TRY_FUNCTION_TRY) != 0) &&
      !(op == eop_vptr_init && fn_.structor == 1)) {
    body_started_ = true;
    ok = end_prologue(problem);
  }
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

  /* A PARTIAL static initializer must store something: note the first record
     that is not its exit cleanup (CLEANUP_BEGIN, its code up to CLEANUP_BODY,
     which the cleanup bracket and the code depth both exclude). */
  if (v1_ok && top != nullptr && top->kind == bk_static_init &&
      cleanup_code_depth_ == 0 &&
      (top->flags & abf_partial) != 0 && op != eop_cleanup_begin &&
      op != eop_static_init_end && op != eop_loc && op != eop_ext)
    top->flags |= abf_content;

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
      fn_ = a_function_info();
      eh_spec_seen_ = false;
      function_try_seen_ = false;
      vptr_seen_ = false;
      prologue_checked_ = false;
      meminit_rank_ = 0;
      meminit_group_rank_ = 255;
      catch_head_ = 0;
      cleanups_.clear();
      throws_.clear();
    }
    break;
  case eop_func_end:
    if (function_bases_.empty())
      problem = "FUNC_END without FUNC_BEGIN";
    else if (brackets_.size() != function_bases_.back())
      problem = "unclosed bracket at FUNC_END";
    else if (!throws_.empty())
      problem = "THROW_BEGIN without THROW_END at FUNC_END";
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
    a_bracket b = {bk_cleanup, static_cast<uint8_t>(ckind), 0, 0,
                   static_cast<uint8_t>(record.ops[2].u &
                                        ECIL_CLEANUP_DEACTIVATABLE)};
    brackets_.push_back(b);
    ++cleanup_code_depth_;
    break;
  }
  case eop_cleanup_body:
    if (top && top->kind == bk_cleanup) {
      uint8_t ckind = top->phase;
      bool deactivatable = (top->flags & ECIL_CLEANUP_DEACTIVATABLE) != 0;

      brackets_.pop_back();
      --cleanup_code_depth_;
      if (ckind != ecl_exit && !regions_.empty()) {
        a_cleanup c = {regions_.back().id, deactivatable, true,
                       fullexpr_identity(), arm_identity()};
        cleanups_.push_back(c);
      }
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
    if (own)
      cleanups_.clear();
    break;
  }
  case eop_static_init_end:
    if (top && top->kind == bk_static_init) {
      if ((top->flags & abf_partial) && !(top->flags & abf_content))
        problem = "a PARTIAL STATIC_INIT holds only an exit cleanup (a "
                  "variable with a VAR_INIT and only its exit cleanup is the "
                  "non-PARTIAL form)";
      if (!throws_.empty() && throws_.back().brackets >= brackets_.size())
        problem = "THROW_BEGIN without THROW_END in a static initializer";
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
  case eop_try_begin: {
    bool function_try = (record.ops[0].u & ECIL_TRY_FUNCTION_TRY) != 0;
    a_bracket b = {bk_try, 0, 0, 0,
                   static_cast<uint8_t>(function_try ? abf_function_try
                                                     : 0)};
    brackets_.push_back(b);
    if (function_try)
      function_try_seen_ = true;
    break;
  }
  case eop_catch_begin:
    if (top && top->kind == bk_try && top->phase != 2) {
      top->phase = record.ops[0].kind == an_operand_kind::nil ? 2 : 1;
      if (top->has_else < 255)
        ++top->has_else;
      catch_head_ = 1;
    } else {
      problem = "CATCH_BEGIN without TRY_BEGIN, or after catch (...)";
    }
    break;
  case eop_try_end:
    if (top && top->kind == bk_try && top->has_else != 0)
      brackets_.pop_back();
    else
      problem = "TRY_END without TRY_BEGIN, or a TRY without a CATCH_BEGIN";
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
                op == eop_tern_else || op == eop_catch_begin)) {
      action = eba_advance;
      kind = top_kind;
    }
    event.bracket_action = action;
    event.bracket_kind = kind;
    event.bracket_depth = static_cast<uint32_t>(brackets_.size());
    result = apply_regions(op, problem_text, event);
    if (result && !throws_.empty() &&
        brackets_.size() < throws_.back().brackets) {
      problem_text = "a bracket was closed between THROW_BEGIN and "
                     "THROW_END";
      result = false;
    }
    if (result && op == eop_func_end)
      ended_function_ = true;
  }
  return result;
}  /* a_bracket_tracker::check */

}  /* namespace ecil */
