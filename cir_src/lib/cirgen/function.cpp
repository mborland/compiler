/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
function.cpp -- the record dispatcher, function bodies and statements: the
non-recursive frame machine.  Every structured op (scope, if, loops, switch,
ternary) is created with empty regions; the insertion point is then moved
into the region of the current phase and a frame remembers what to do when
the matching ECIL record arrives.  Shapes follow clang/lib/CIR/CodeGen
(CIRGenStmt.cpp, CIRGenFunction.cpp) except where the ECIL spec differs
(marker cir.case operations, direct cir.return).
*/

#include <cstring>

#include "emitter.h"

namespace edg_cir {

/* Dispatch. */

bool a_emitter::on_record(const ecil::a_record &r,
                          const ecil::a_record_context &)
/*
Process one decoded record.
*/
{
  bool ok = true;

  switch (r.opcode) {
  case eop_str:
  case eop_hdr_target:
  case eop_hdr_lang:
  case eop_hdr_producer:
  case eop_hdr_type_role:
  case eop_ext:
  case eop_refs:
  case eop_end:
  case eop_retract:
    break;
  case eop_loc: {
    bool good = true;
    uint64_t line = 0;
    uint64_t col = 0;
    std::string file = string_for(r.ops[0], good);
    uint_operand(r.ops[1], line);
    uint_operand(r.ops[2], col);
    if (!file.empty())
      session_.last_file = file;
    loc_ = mlir::FileLineColLoc::get(ctx_, session_.last_file,
                                     static_cast<unsigned>(line),
                                     static_cast<unsigned>(col));
    break;
  }
  case eop_ty_void:
  case eop_ty_bool:
  case eop_ty_int:
  case eop_ty_float:
  case eop_ty_ptr:
  case eop_ty_array:
  case eop_ty_func:
  case eop_ty_record_decl:
  case eop_ty_opaque:
    ok = type_record(r);
    break;
  case eop_ty_record_body:
    ok = record_body(r);
    break;
  case eop_rec_field:
    ok = record_field(r);
    break;
  case eop_decl_func:
    ok = decl_function(r);
    break;
  case eop_decl_var:
    ok = decl_variable(r);
    break;
  case eop_var_init:
    ok = var_init(r);
    break;
  case eop_name_bind:
    ok = name_bind(r);
    break;
  case eop_c_int:
  case eop_c_float:
  case eop_c_null:
  case eop_c_zero:
  case eop_c_string:
  case eop_c_addr:
  case eop_c_ptr:
  case eop_c_agg_begin:
  case eop_c_sel:
  case eop_c_repeat:
  case eop_c_agg_end:
    ok = constant_record(r);
    break;
  default:
    if (!in_function_ && r.opcode != eop_func_begin)
      ok = format_error(std::string(r.info->name) +
                        " outside a function body");
    else if (r.opcode >= eop_func_begin && r.opcode <= eop_unreachable)
      ok = statement(r);
    else
      ok = expression(r);
    break;
  }
  return ok && session_.status == EDG_CIR_OK;
}  /* a_emitter::on_record */

bool a_emitter::on_frame_end(uint8_t)
/*
Spec rule F4: every stack is empty at the end of a frame, and no body or
type definition is left half way.
*/
{
  bool ok = true;

  if (pending_body_gid_ != 0)
    ok = format_error("a TY_RECORD_BODY is missing REC_FIELD records at the "
                      "end of its frame");
  else if (in_function_ || !frames_.empty())
    ok = format_error("a function body is open at the end of its frame");
  else if (!values_.empty() || !consts_.empty() || !const_aggs_.empty() ||
           !targets_.empty())
    ok = format_error("a stack is not empty at the end of a frame");
  return ok;
}  /* a_emitter::on_frame_end */

/* Frame helpers. */

bool a_emitter::open_frame(a_frame_kind kind, mlir::Operation *op,
                           mlir::Block *alloca_block)
/*
Push a frame.
*/
{
  a_frame f;

  f.kind = kind;
  f.op = op;
  f.alloca_block = alloca_block;
  f.value_base = values_.size();
  f.target_base = targets_.size();
  frames_.push_back(f);
  return true;
}  /* a_emitter::open_frame */

a_frame *a_emitter::top_frame(a_frame_kind kind, const char *what)
/*
Return the innermost frame if it has the kind; else report a format error.
*/
{
  a_frame *result = nullptr;

  if (!frames_.empty() && frames_.back().kind == kind)
    result = &frames_.back();
  else
    format_error(std::string(what) + " does not match the open construct");
  return result;
}  /* a_emitter::top_frame */

bool a_emitter::pop_frame()
/*
Pop the innermost frame, checking the value stack height.
*/
{
  bool ok = true;

  if (values_.size() != frames_.back().value_base)
    ok = format_error("values left on the stack at the end of a construct");
  frames_.pop_back();
  return ok;
}  /* a_emitter::pop_frame */

mlir::Block *a_emitter::current_alloca_block()
/*
The block that receives allocas: the first block of the innermost region.
*/
{
  return frames_.empty() ? fn_entry_ : frames_.back().alloca_block;
}  /* a_emitter::current_alloca_block */

mlir::Value a_emitter::alloca_in(mlir::Block *block, mlir::Type type,
                                 const std::string &name, uint32_t align)
/*
Create an alloca at the best alloca point of block (the builder's own point is
kept).
*/
{
  mlir::OpBuilder::InsertionGuard guard(builder_);

  builder_.restoreInsertionPoint(
      compat::a_builder::getBestAllocaInsertPoint(block));
  return builder_.createAlloca(loc(), cir::PointerType::get(type), name,
                               builder_.getAlignmentAttr(align));
}  /* a_emitter::alloca_in */

mlir::Value a_emitter::alloca_here(mlir::Type type, const std::string &name,
                                   uint32_t align)
/*
An alloca in the function's entry block.  The front end's BLOCK brackets do
not follow variable lifetime (the variable of a for-init is declared in a
block that ends before the condition uses it), so an alloca placed in the
innermost scope would not dominate later uses.
*/
{
  return alloca_in(fn_entry_, type, name, align);
}  /* a_emitter::alloca_here */

bool a_emitter::terminate_region(mlir::Region &region, bool)
/*
End a structured region: give every unterminated block a cir.yield and erase
empty blocks nothing can reach (CIRGenFunction::terminateStructuredRegionBody).
*/
{
  llvm::SmallVector<mlir::Block *> dead;
  size_t blocks = region.getBlocks().size();

  for (mlir::Block &block : region.getBlocks()) {
    if (blocks != 1 && &block != &region.front() && block.empty() &&
        block.hasNoPredecessors())
      dead.push_back(&block);
    if (block.empty() ||
        !block.back().hasTrait<mlir::OpTrait::IsTerminator>()) {
      mlir::OpBuilder::InsertionGuard guard(builder_);
      builder_.setInsertionPointToEnd(&block);
      builder_.createYield(loc());
    }
  }
  for (mlir::Block *block : dead)
    block->erase();
  return true;
}  /* a_emitter::terminate_region */

void a_emitter::start_new_block()
/*
After a terminator, open a fresh block so that (dead) code that follows has an
insertion point.  Empty ones are erased when the region closes.
*/
{
  builder_.createBlock(builder_.getBlock()->getParent());
}  /* a_emitter::start_new_block */

std::string a_emitter::label_name(uint64_t l)
/*
Label names are hints; use a unique, stable spelling per label id.
*/
{
  return "L" + std::to_string(l);
}  /* a_emitter::label_name */

mlir::Value a_emitter::pop_bool_condition()
/*
Pop a condition and make sure it is a !cir.bool.
*/
{
  mlir::Value v;

  if (!pop_prvalue(v))
    v = mlir::Value();
  else if (!mlir::isa<cir::BoolType>(v.getType()))
    v = to_bool(v);
  return v;
}  /* a_emitter::pop_bool_condition */

/* Functions. */

bool a_emitter::func_begin(const ecil::a_record &r)
/*
FUNC_BEGIN: create the entry block and open the function frame.
*/
{
  bool ok = true;
  uint64_t gid = 0;
  a_entity *e;
  a_type *t;
  cir::FuncOp fn;

  uint_operand(r.ops[0], gid);
  e = entity_for(gid, true);
  if (in_function_)
    ok = unsupported("nested function bodies (lambdas) are not supported "
                     "in ECIL v0");
  else if (e == nullptr)
    ok = format_error("FUNC_BEGIN for a function that was not declared");
  else if (e->has_body)
    ok = format_error("function " + e->name + " defined twice");
  if (ok) {
    t = type_at(e->type);
    fn = mlir::cast<cir::FuncOp>(e->op);
    e->has_body = true;
    e->referenced = true;
    apply_function_attributes(*e, fn);
    /* As CIRGen: a definition compiled without exceptions cannot throw. */
    if (!(session_.header.lang_flags & ECIL_LANG_EXCEPTIONS))
      fn->setAttr(cir::CIRDialect::getNoThrowAttrName(),
                  builder_.getUnitAttr());
    compat::set_dso_local(fn.getOperation(), true);
    fn_ = fn;
    fn_entity_ = e;
    fn_entry_ = fn.addEntryBlock();
    fn_return_type_ = t->target;
    builder_.setInsertionPointToStart(fn_entry_);
    locals_.clear();
    params_seen_ = 0;
    temp_counter_ = 0;
    in_function_ = true;
    values_.clear();
    open_frame(a_frame_kind::function, fn.getOperation(), fn_entry_);
  }
  return ok;
}  /* a_emitter::func_begin */

bool a_emitter::param(const ecil::a_record &r)
/*
PARAM: an alloca of the declared type holding the (cast) argument.
*/
{
  bool ok = true;
  uint64_t l = 0;
  uint64_t t_abi = 0;
  uint64_t t_decl = 0;
  bool good = true;
  std::string name;
  a_type *abi;
  a_type *decl;
  mlir::Value argument;
  mlir::Value address;
  uint32_t align = 1;

  uint_operand(r.ops[0], l);
  uint_operand(r.ops[1], t_abi);
  uint_operand(r.ops[2], t_decl);
  name = string_for(r.ops[3], good);
  abi = type_at(t_abi);
  decl = type_at(t_decl);
  if (!good || abi == nullptr || decl == nullptr)
    ok = format_error("PARAM names an unknown type or string");
  else if (frames_.size() != 1 ||
           params_seen_ >= fn_entry_->getNumArguments())
    ok = format_error("PARAM out of place or too many parameters");
  else if (fn_entry_->getArgument(params_seen_).getType() != abi->cir)
    ok = format_error("PARAM type differs from the function's parameter "
                      "type");
  if (ok) {
    argument = fn_entry_->getArgument(params_seen_);
    ++params_seen_;
    if (name.empty())
      name = "arg" + std::to_string(params_seen_);
    align = static_cast<uint32_t>(type_align(decl->cir));
    address = alloca_in(fn_entry_, decl->cir, name, align);
    if (abi->cir != decl->cir) {
      mlir::Value converted;
      uint32_t kind = mlir::isa<cir::IntType>(decl->cir) ? eck_integral
                                                         : eck_floating;
      ok = cast_value(kind, argument, decl->cir, converted);
      argument = converted;
    }
  }
  if (ok) {
    a_local local;
    builder_.createStore(loc(), argument, address, false, false,
                         builder_.getAlignmentAttr(align));
    local.address = address;
    local.align = align;
    local.type = t_decl;
    locals_[l] = local;
  }
  return ok;
}  /* a_emitter::param */

bool a_emitter::local_decl(const ecil::a_record &r)
/*
LOCAL: an automatic variable at this point of the body.
*/
{
  bool ok = true;
  uint64_t l = 0;
  uint64_t tgid = 0;
  uint64_t align = 0;
  bool good = true;
  std::string name;
  a_type *t;
  a_local local;

  uint_operand(r.ops[0], l);
  uint_operand(r.ops[1], tgid);
  name = string_for(r.ops[2], good);
  uint_operand(r.ops[3], align);
  t = type_at(tgid);
  if (!good || t == nullptr) {
    ok = format_error("LOCAL names an unknown type or string");
  } else {
    if (name.empty())
      name = "tmp" + std::to_string(l);
    if (align == 0)
      align = type_align(t->cir);
    local.address = alloca_here(t->cir, name, static_cast<uint32_t>(align));
    local.align = static_cast<uint32_t>(align);
    local.type = tgid;
    locals_[l] = local;
  }
  return ok;
}  /* a_emitter::local_decl */

void a_emitter::implicit_return(bool is_main)
/*
Control may fall off the end of the function (spec 8.3 table).
*/
{
  a_type *ret = type_at(fn_return_type_);

  if (ret->kind == a_type_kind::void_type) {
    cir::ReturnOp::create(builder_, loc());
  } else if (is_main) {
    cir::ReturnOp::create(builder_, loc(),
                          mlir::ValueRange{builder_.getNullValue(ret->cir,
                                                                 loc())});
  } else if (c_language_) {
    uint32_t align = static_cast<uint32_t>(type_align(ret->cir));
    mlir::Value slot = alloca_in(fn_entry_, ret->cir, "__retval", align);
    mlir::Value v = builder_.createLoad(loc(), slot, false, align);
    cir::ReturnOp::create(builder_, loc(), mlir::ValueRange{v});
  } else {
    cir::TrapOp::create(builder_, loc());
  }
}  /* a_emitter::implicit_return */

bool a_emitter::func_end(const ecil::a_record &r)
/*
FUNC_END: apply the fall-off-the-end rule and close the function.
*/
{
  bool ok = true;
  uint64_t flags = 0;
  mlir::Block *block;
  bool terminated;

  if (frames_.size() != 1 || frames_.back().kind != a_frame_kind::function)
    ok = format_error("FUNC_END inside an open construct");
  else if (params_seen_ != fn_entry_->getNumArguments())
    ok = format_error("fewer PARAM records than the function has "
                      "parameters");
  else
    ok = pop_frame();
  if (ok) {
    uint_operand(r.ops[0], flags);
    block = builder_.getBlock();
    terminated = !block->empty() &&
                 block->back().hasTrait<mlir::OpTrait::IsTerminator>();
    if (!terminated) {
      if (flags & ECIL_FUNC_END_REACHABLE) {
        implicit_return((fn_entity_->flags & ECIL_DECL_FUNC_IS_MAIN) != 0);
      } else if (block->empty() && block != fn_entry_ &&
                 block->hasNoPredecessors()) {
        builder_.clearInsertionPoint();
        block->erase();
      } else {
        cir::UnreachableOp::create(builder_, loc());
      }
    }
    /* Drop empty blocks nothing branches to (code after a return). */
    {
      llvm::SmallVector<mlir::Block *> dead;
      for (mlir::Block &b : fn_.getBody().getBlocks()) {
        if (&b != fn_entry_ && b.empty() && b.use_empty())
          dead.push_back(&b);
      }
      for (mlir::Block *b : dead)
        b->erase();
    }
    builder_.clearInsertionPoint();
    builder_.setInsertionPointToEnd(module_.getBody());
    in_function_ = false;
    fn_entity_ = nullptr;
    locals_.clear();
  }
  return ok;
}  /* a_emitter::func_end */

/* Statements. */

bool a_emitter::emit_return(const ecil::a_record &r)
/*
RETURN_STMT: cir.return directly at the statement (no shared return block).
*/
{
  bool ok = true;
  bool has_value = r.ops[0].u != 0;
  a_type *ret = type_at(fn_return_type_);

  if (has_value) {
    mlir::Value v;
    ok = pop_prvalue(v);
    if (ok && ret->kind == a_type_kind::void_type)
      ok = format_error("RETURN_STMT with a value in a void function");
    if (ok && v.getType() != ret->cir) {
      mlir::Value converted;
      uint32_t kind = mlir::isa<cir::PointerType>(ret->cir) ? eck_bitcast
          : mlir::isa<cir::IntType>(ret->cir) ? eck_integral : eck_floating;
      ok = cast_value(kind, v, ret->cir, converted);
      v = converted;
    }
    if (ok)
      cir::ReturnOp::create(builder_, loc(), mlir::ValueRange{v});
  } else if (ret->kind != a_type_kind::void_type) {
    /* Legal in C (GCC accepts it): it behaves like falling off the end. */
    implicit_return((fn_entity_->flags & ECIL_DECL_FUNC_IS_MAIN) != 0);
  } else {
    cir::ReturnOp::create(builder_, loc());
  }
  if (ok)
    start_new_block();
  return ok;
}  /* a_emitter::emit_return */

bool a_emitter::statement(const ecil::a_record &r)
/*
Function-level records and statements.
*/
{
  bool ok = true;

  switch (r.opcode) {
  case eop_func_begin:
    ok = func_begin(r);
    break;
  case eop_func_end:
    ok = func_end(r);
    break;
  case eop_param:
    ok = param(r);
    break;
  case eop_local:
    ok = local_decl(r);
    break;
  case eop_block_begin: {
    cir::ScopeOp scope = compat::open_scope(builder_, loc());
    mlir::Block *block = &scope.getScopeRegion().front();
    open_frame(a_frame_kind::scope, scope.getOperation(), block);
    builder_.setInsertionPointToStart(block);
    break;
  }
  case eop_block_end: {
    a_frame *f = top_frame(a_frame_kind::scope, "BLOCK_END");
    if (f == nullptr) {
      ok = false;
    } else {
      cir::ScopeOp scope = mlir::cast<cir::ScopeOp>(f->op);
      terminate_region(scope.getScopeRegion());
      builder_.setInsertionPointAfter(scope.getOperation());
      ok = pop_frame();
    }
    break;
  }
  case eop_if_begin: {
    mlir::Value cond = pop_bool_condition();
    if (!cond) {
      ok = false;
    } else {
      cir::IfOp op = compat::open_if(builder_, loc(), cond);
      mlir::Block *block = &op.getThenRegion().front();
      open_frame(a_frame_kind::if_stmt, op.getOperation(), block);
      builder_.setInsertionPointToStart(block);
    }
    break;
  }
  case eop_if_else: {
    a_frame *f = top_frame(a_frame_kind::if_stmt, "IF_ELSE");
    if (f == nullptr) {
      ok = false;
    } else if (values_.size() != f->value_base) {
      ok = format_error("values left on the stack at IF_ELSE");
    } else {
      cir::IfOp op = mlir::cast<cir::IfOp>(f->op);
      terminate_region(op.getThenRegion());
      f->alloca_block = builder_.createBlock(&op.getElseRegion());
      f->phase = 1;
    }
    break;
  }
  case eop_if_end: {
    a_frame *f = top_frame(a_frame_kind::if_stmt, "IF_END");
    if (f == nullptr) {
      ok = false;
    } else {
      cir::IfOp op = mlir::cast<cir::IfOp>(f->op);
      terminate_region(f->phase == 0 ? op.getThenRegion()
                                     : op.getElseRegion());
      builder_.setInsertionPointAfter(op.getOperation());
      ok = pop_frame();
    }
    break;
  }
  case eop_while_begin: {
    cir::WhileOp op = compat::open_while(builder_, loc());
    mlir::Block *block = &op.getCond().front();
    open_frame(a_frame_kind::while_stmt, op.getOperation(), block);
    builder_.setInsertionPointToStart(block);
    break;
  }
  case eop_while_body: {
    a_frame *f = top_frame(a_frame_kind::while_stmt, "WHILE_BODY");
    mlir::Value cond;
    if (f == nullptr) {
      ok = false;
    } else if (!(cond = pop_bool_condition())) {
      ok = false;
    } else if (values_.size() != f->value_base) {
      ok = format_error("values left on the stack at WHILE_BODY");
    } else {
      cir::WhileOp op = mlir::cast<cir::WhileOp>(f->op);
      builder_.createCondition(cond);
      f->alloca_block = &op.getBody().front();
      f->phase = 1;
      builder_.setInsertionPointToStart(f->alloca_block);
    }
    break;
  }
  case eop_while_end: {
    a_frame *f = top_frame(a_frame_kind::while_stmt, "WHILE_END");
    if (f == nullptr) {
      ok = false;
    } else {
      cir::WhileOp op = mlir::cast<cir::WhileOp>(f->op);
      terminate_region(op.getBody());
      builder_.setInsertionPointAfter(op.getOperation());
      ok = pop_frame();
    }
    break;
  }
  case eop_do_begin: {
    cir::DoWhileOp op = compat::open_do(builder_, loc());
    mlir::Block *block = &op.getBody().front();
    open_frame(a_frame_kind::do_stmt, op.getOperation(), block);
    builder_.setInsertionPointToStart(block);
    break;
  }
  case eop_do_cond: {
    a_frame *f = top_frame(a_frame_kind::do_stmt, "DO_COND");
    if (f == nullptr) {
      ok = false;
    } else if (values_.size() != f->value_base) {
      ok = format_error("values left on the stack at DO_COND");
    } else {
      cir::DoWhileOp op = mlir::cast<cir::DoWhileOp>(f->op);
      terminate_region(op.getBody());
      f->alloca_block = &op.getCond().front();
      f->phase = 1;
      builder_.setInsertionPointToStart(f->alloca_block);
    }
    break;
  }
  case eop_do_end: {
    a_frame *f = top_frame(a_frame_kind::do_stmt, "DO_END");
    mlir::Value cond;
    if (f == nullptr) {
      ok = false;
    } else if (!(cond = pop_bool_condition())) {
      ok = false;
    } else {
      cir::DoWhileOp op = mlir::cast<cir::DoWhileOp>(f->op);
      builder_.createCondition(cond);
      builder_.setInsertionPointAfter(op.getOperation());
      ok = pop_frame();
    }
    break;
  }
  case eop_for_begin: {
    cir::ScopeOp scope = compat::open_scope(builder_, loc());
    mlir::Block *block = &scope.getScopeRegion().front();
    open_frame(a_frame_kind::for_stmt, nullptr, block);
    frames_.back().scope_op = scope.getOperation();
    builder_.setInsertionPointToStart(block);
    break;
  }
  case eop_for_cond: {
    a_frame *f = top_frame(a_frame_kind::for_stmt, "FOR_COND");
    if (f == nullptr) {
      ok = false;
    } else if (f->phase != 0) {
      ok = format_error("FOR_COND out of order");
    } else if (values_.size() != f->value_base) {
      ok = format_error("values left on the stack at FOR_COND");
    } else {
      cir::ForOp op = compat::open_for(builder_, loc());
      f->op = op.getOperation();
      f->has_cond = r.ops[0].u != 0;
      f->alloca_block = &op.getCond().front();
      f->phase = 1;
      builder_.setInsertionPointToStart(f->alloca_block);
    }
    break;
  }
  case eop_for_step: {
    a_frame *f = top_frame(a_frame_kind::for_stmt, "FOR_STEP");
    mlir::Value cond;
    if (f == nullptr) {
      ok = false;
    } else if (f->phase != 1) {
      ok = format_error("FOR_STEP out of order");
    } else {
      if (f->has_cond) {
        cond = pop_bool_condition();
        ok = static_cast<bool>(cond);
      } else {
        cond = builder_.getTrue(loc());
      }
      if (ok && values_.size() != f->value_base)
        ok = format_error("values left on the stack at FOR_STEP");
      if (ok) {
        cir::ForOp op = mlir::cast<cir::ForOp>(f->op);
        builder_.createCondition(cond);
        f->alloca_block = &op.getStep().front();
        f->phase = 2;
        builder_.setInsertionPointToStart(f->alloca_block);
      }
    }
    break;
  }
  case eop_for_body: {
    a_frame *f = top_frame(a_frame_kind::for_stmt, "FOR_BODY");
    if (f == nullptr) {
      ok = false;
    } else if (f->phase != 2) {
      ok = format_error("FOR_BODY out of order");
    } else if (values_.size() != f->value_base) {
      ok = format_error("values left on the stack at FOR_BODY");
    } else {
      cir::ForOp op = mlir::cast<cir::ForOp>(f->op);
      terminate_region(op.getStep());
      f->alloca_block = &op.getBody().front();
      f->phase = 3;
      builder_.setInsertionPointToStart(f->alloca_block);
    }
    break;
  }
  case eop_for_end: {
    a_frame *f = top_frame(a_frame_kind::for_stmt, "FOR_END");
    if (f == nullptr) {
      ok = false;
    } else if (f->phase != 3) {
      ok = format_error("FOR_END out of order");
    } else {
      cir::ForOp op = mlir::cast<cir::ForOp>(f->op);
      cir::ScopeOp scope = mlir::cast<cir::ScopeOp>(f->scope_op);
      terminate_region(op.getBody());
      builder_.setInsertionPointAfter(op.getOperation());
      terminate_region(scope.getScopeRegion());
      builder_.setInsertionPointAfter(scope.getOperation());
      ok = pop_frame();
    }
    break;
  }
  case eop_switch_begin: {
    mlir::Value cond;
    ok = pop_prvalue(cond);
    if (ok && mlir::isa<cir::BoolType>(cond.getType()))
      cond = builder_.createBoolToInt(cond, builder_.getUIntNTy(1));
    if (ok && !mlir::isa<cir::IntType>(cond.getType()))
      ok = format_error("switch condition is not an integer");
    if (ok) {
      cir::SwitchOp op = compat::open_switch(builder_, loc(), cond);
      mlir::Block *block = &op.getBody().front();
      open_frame(a_frame_kind::switch_stmt, op.getOperation(), block);
      frames_.back().switch_type = cond.getType();
      builder_.setInsertionPointToStart(block);
    }
    break;
  }
  case eop_case_value:
  case eop_case_range:
  case eop_case_default: {
    a_frame *sw = nullptr;
    llvm::SmallVector<mlir::Attribute, 2> values;
    cir::CaseOpKind kind = cir::CaseOpKind::Equal;

    for (size_t k = frames_.size(); k > 0 && sw == nullptr; --k) {
      if (frames_[k - 1].kind == a_frame_kind::switch_stmt)
        sw = &frames_[k - 1];
    }
    if (sw == nullptr) {
      ok = format_error("case label outside a switch");
    } else {
      auto cond_type = mlir::cast<cir::IntType>(sw->switch_type);
      if (r.opcode != eop_case_default) {
        unsigned n = r.opcode == eop_case_range ? 2 : 1;
        for (unsigned k = 0; ok && k < n; ++k) {
          llvm::APInt v;
          if (!int_operand_apint(r.ops[k], cond_type.getWidth(), v))
            ok = format_error("case value is not an integer");
          else
            values.push_back(cir::IntAttr::get(cond_type, v));
        }
        kind = n == 2 ? cir::CaseOpKind::Range : cir::CaseOpKind::Equal;
      } else {
        kind = cir::CaseOpKind::Default;
      }
      if (ok)
        compat::create_marker_case(builder_, loc(), kind, values);
    }
    break;
  }
  case eop_switch_end: {
    a_frame *f = top_frame(a_frame_kind::switch_stmt, "SWITCH_END");
    if (f == nullptr) {
      ok = false;
    } else {
      cir::SwitchOp op = mlir::cast<cir::SwitchOp>(f->op);
      terminate_region(op.getBody());
      builder_.setInsertionPointAfter(op.getOperation());
      ok = pop_frame();
    }
    break;
  }
  case eop_break_stmt:
    builder_.createBreak(loc());
    start_new_block();
    break;
  case eop_continue_stmt:
    builder_.createContinue(loc());
    start_new_block();
    break;
  case eop_return_stmt:
    ok = emit_return(r);
    break;
  case eop_goto_stmt: {
    uint64_t l = 0;
    uint_operand(r.ops[0], l);
    cir::GotoOp::create(builder_, loc(), label_name(l));
    start_new_block();
    break;
  }
  case eop_label: {
    uint64_t l = 0;
    mlir::Block *current = builder_.getBlock();
    mlir::Block *label_block = current;
    uint_operand(r.ops[0], l);
    if (!current->empty() || current->isEntryBlock()) {
      {
        mlir::OpBuilder::InsertionGuard guard(builder_);
        label_block = builder_.createBlock(current->getParent());
      }
      cir::BrOp::create(builder_, loc(), label_block);
    }
    builder_.setInsertionPointToEnd(label_block);
    cir::LabelOp::create(builder_, loc(), label_name(l));
    break;
  }
  case eop_unreachable:
    cir::UnreachableOp::create(builder_, loc());
    start_new_block();
    break;
  default:
    ok = format_error(std::string(r.info->name) + " is not a statement");
    break;
  }
  return ok;
}  /* a_emitter::statement */

}  /* namespace edg_cir */
