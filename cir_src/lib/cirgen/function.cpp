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

#include "llvm/ADT/bit.h"

#include "emitter.h"

namespace edg_cir {

/* Dispatch. */

namespace {

bool v1_implemented(uint8_t opcode)
/*
The C++ (ECIL v1) records that have an implementation; every other one is
reported as not supported yet.
*/
{
  bool result;

  switch (opcode) {
  case eop_return_slot:
  case eop_return_from_slot:
  case eop_base_cast:
  case eop_derived_cast:
  case eop_zero_bytes:
  case eop_cmp3way:
  case eop_meminit_begin:
  case eop_meminit_end:
  case eop_cleanup_begin:
  case eop_cleanup_body:
  case eop_fullexpr_begin:
  case eop_fullexpr_end:
  case eop_array_ctor_begin:
  case eop_array_ctor_end:
  case eop_array_dtor_begin:
  case eop_array_dtor_end:
  case eop_static_init_begin:
  case eop_static_init_end:
  case eop_new_array_size:
  case eop_new_array_cookie:
  case eop_delete:
    result = true;
    break;
  default:
    result = false;
    break;
  }
  return result;
}  /* v1_implemented */

}  /* anonymous namespace */

bool a_emitter::on_record(const ecil::a_record &r,
                          const ecil::a_record_context &context)
/*
Process one decoded record.  Inside a construct the library drops (an eh
cleanup, an empty array loop) the reader has decoded and validated the
records and they are ignored here, up to the one that closes it.
*/
{
  bool ok = true;

  record_depth_ = context.scope.bracket_depth;
  if (in_function_ && !entry_mark_set_ && r.opcode != eop_param &&
      r.opcode != eop_func_eh_spec && !frames_.empty() &&
      frames_.front().kind == a_frame_kind::function && fn_entry_ != nullptr) {
    /* The parameters are done: where the body starts. */
    entry_mark_set_ = true;
    entry_mark_ = fn_entry_->empty() ? nullptr : &fn_entry_->back();
  }
  if (drop_depth_ == 0) {
    if (pending_head_ >= 0 && !flush_catch_head(r)) {
      ok = false;
    } else {
      /* The free of a thrown object under initialization starts before a
         construct that opens a region of its own. */
      if (!throws_.empty()) {
        switch (r.opcode) {
        case eop_tern_begin:
        case eop_land_begin:
        case eop_lor_begin:
        case eop_array_ctor_begin:
        case eop_array_dtor_begin:
        case eop_fullexpr_begin:
        case eop_static_init_begin:
        case eop_block_begin:
        case eop_if_begin:
        case eop_while_begin:
        case eop_do_begin:
        case eop_for_begin:
        case eop_switch_begin:
        case eop_try_begin:
          ok = materialize_throws();
          break;
        default:
          break;
        }
      }
      if (ok)
        ok = dispatch_record(r, context);
    }
  } else if (record_depth_ < drop_depth_) {
    drop_depth_ = 0;
    if (frames_.empty() || frames_.back().kind != a_frame_kind::dropped)
      ok = internal("a dropped construct ended out of step");
    else
      ok = pop_frame();
    if (ok && frames_.size() + const_aggs_.size() !=
                  context.scope.bracket_depth + (in_function_ ? 1u : 0u))
      ok = internal("the emitter's frames and the reader's brackets differ "
                    "after a dropped construct");
  }
  return ok && session_.status == EDG_CIR_OK;
}  /* a_emitter::on_record */

bool a_emitter::dispatch_record(const ecil::a_record &r,
                                const ecil::a_record_context &context)
/*
Process one decoded record outside any dropped construct.
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
    break;
  case eop_retract:
    ok = retract_record(r);
    break;
  case eop_loc: {
    bool good = true;
    uint64_t line = 0;
    uint64_t col = 0;
    std::string_view file = string_for(r.ops[0], good);
    uint_operand(r.ops[1], line);
    uint_operand(r.ops[2], col);
    if (!file.empty())
      session_.last_file.assign(file.data(), file.size());
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
  case eop_ty_vptr:
  case eop_ty_data_member:
  case eop_ty_method:
    ok = type_record(r);
    break;
  case eop_class_vfuncs:
    /* A class with vtable facts takes part in finish even when the stream
       has no vtable or VPTR_INIT: the thunks to the overriders defined here
       are emitted for it. */
    have_vtable_facts_ = true;
    break;
  case eop_class_mangle:
  case eop_class_bases:
  case eop_class_overrides:
    /* The validator keeps these facts (ecil_ids.h); finish decodes them. */
    break;
  case eop_class_info:
    ok = class_info_record(r);
    break;
  case eop_structor_deleting:
    ok = structor_deleting_record(r);
    break;
  case eop_decl_abi_data:
    ok = abi_data_record(r);
    break;
  case eop_vptr_init:
    ok = in_function_ ? vptr_init_record()
                      : format_error("VPTR_INIT outside a function body");
    break;
  case eop_c_data_member:
  case eop_c_method:
    ok = constant_record(r);
    break;
  case eop_typeinfo_def:
    ok = typeinfo_def_record(r);
    break;
  case eop_const_data_member:
  case eop_const_method:
  case eop_pm_field:
  case eop_pm_call:
  case eop_pm_cast:
    ok = in_function_ ? member_pointer_record(r)
                      : format_error(std::string(r.info->name) +
                                     " outside a function body");
    break;
  case eop_new_array_size_throw:
    ok = in_function_ ? new_array_record(r)
                      : format_error("NEW_ARRAY_SIZE_THROW outside a "
                                     "function body");
    break;
  case eop_typeid:
    ok = in_function_ ? typeid_record(r)
                      : format_error("TYPEID outside a function body");
    break;
  case eop_dynamic_cast:
    ok = in_function_ ? dynamic_cast_record(r)
                      : format_error("DYNAMIC_CAST outside a function body");
    break;
  case eop_try_begin:
  case eop_catch_begin:
  case eop_catch_param:
  case eop_try_end:
  case eop_throw_begin:
  case eop_throw_end:
  case eop_rethrow:
  case eop_func_eh_spec:
  case eop_array_partial_dtor:
  case eop_cleanup_end:
    ok = in_function_ ? exception_record(r)
                      : format_error(std::string(r.info->name) +
                                     " outside a function body");
    break;
  case eop_vbase_cast:
    ok = in_function_ ? vbase_cast_record(r)
                      : format_error("VBASE_CAST outside a function body");
    break;
  case eop_vtt_addr:
    ok = in_function_ ? vtt_addr_record(r)
                      : format_error("VTT_ADDR outside a function body");
    break;
  case eop_init_zero_base:
    ok = in_function_ ? init_zero_base_record()
                      : format_error("INIT_ZERO_BASE outside a function "
                                     "body");
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
  case eop_structor:
    ok = structor_record(r);
    break;
  case eop_static_init_begin:
  case eop_static_init_end:
    ok = static_init_record(r);
    break;
  case eop_cleanup_begin:
  case eop_cleanup_body:
  case eop_fullexpr_begin:
  case eop_fullexpr_end:
  case eop_array_ctor_begin:
  case eop_array_ctor_end:
  case eop_array_dtor_begin:
  case eop_array_dtor_end:
    if (!in_function_)
      ok = format_error(std::string(r.info->name) + " outside a function "
                        "body or a STATIC_INIT");
    else if (r.opcode == eop_cleanup_begin || r.opcode == eop_cleanup_body)
      ok = cleanup_record(r);
    else if (r.opcode == eop_fullexpr_begin || r.opcode == eop_fullexpr_end)
      ok = fullexpr_record(r);
    else
      ok = array_record(r);
    break;
  default:
    /* The class of every other record comes from the schema's section
       bits (ecil_schema.cpp), not from opcode ranges. */
    if (r.info->min_minor >= 2)
      ok = unsupported("ECIL 1.2 record " + std::string(r.info->name) +
                       " is not supported yet");
    else if ((r.info->section & ecil::eos_v1) && !v1_implemented(r.opcode))
      ok = unsupported(std::string(r.info->name) + ": C++ records (ECIL v1) "
                       "are not supported yet");
    else if (r.info->section & ecil::eos_const)
      ok = constant_record(r);
    else if (!in_function_ && r.opcode != eop_func_begin)
      ok = format_error(std::string(r.info->name) +
                        " outside a function body");
    else if (r.info->section & ecil::eos_stmt)
      ok = statement(r);
    else
      ok = expression(r);
    break;
  }
  /* The reader's bracket tracker is the only bracket matcher; the emitter's
     frame stacks must follow it exactly (function bodies are not brackets,
     constant aggregates have a stack of their own). */
  if (ok && session_.status == EDG_CIR_OK &&
      frames_.size() + const_aggs_.size() !=
          context.scope.bracket_depth + (in_function_ ? 1u : 0u))
    ok = internal("the emitter's frames and the reader's brackets differ "
                  "after " + std::string(r.info->name));
  return ok && session_.status == EDG_CIR_OK;
}  /* a_emitter::dispatch_record */

bool a_emitter::on_frame_end(uint8_t)
/*
Spec rule F4: every stack is empty at the end of a frame, and no body or
type definition is left half way.
*/
{
  bool ok = true;

  if (pending_body_gid_ != 0)
    ok = internal("a TY_RECORD_BODY is missing REC_FIELD records at the "
                      "end of its frame");
  else if (pending_info_gid_ != 0)
    ok = internal("a dynamic class is missing its CLASS_INFO at the end of "
                  "its frame");
  else if (in_function_ || !frames_.empty())
    ok = internal("a function body is open at the end of its frame");
  else if (!values_.empty() || !consts_.empty() || !const_aggs_.empty() ||
           !targets_.empty())
    ok = format_error("a stack is not empty at the end of a frame");
  return ok;
}  /* a_emitter::on_frame_end */

/* Frame helpers. */

bool a_emitter::open_frame(a_frame_kind kind, mlir::Operation *op)
/*
Push a frame.
*/
{
  a_frame f;

  f.kind = kind;
  f.op = op;
  f.value_base = values_.size();
  f.target_base = targets_.size();
  frames_.push_back(f);
  return true;
}  /* a_emitter::open_frame */

a_frame *a_emitter::top_frame(a_frame_kind kind, const char *what)
/*
Return the innermost frame if it has the kind.  The bracket tracker already
matched every closing record against its opening one, so a mismatch is an
internal error.
*/
{
  a_frame *result = nullptr;

  if (!frames_.empty() && frames_.back().kind == kind)
    result = &frames_.back();
  else
    internal(std::string(what) + " does not match the open construct");
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

mlir::Value a_emitter::alloca_in(mlir::Block *block, mlir::Type type,
                                 const std::string &name, uint32_t align)
/*
Create an alloca at the alloca point of block (the builder's own point is
kept).  The function's entry block tracks its last alloca explicitly, so the
cost is constant; any other block uses the upstream backward scan.
*/
{
  mlir::OpBuilder::InsertionGuard guard(builder_);
  mlir::Value result;

  if (block == fn_entry_)
    builder_.restoreInsertionPoint(mlir::OpBuilder::InsertPoint(
        block, last_alloca_ != nullptr
                   ? ++mlir::Block::iterator(last_alloca_) : block->begin()));
  else
    builder_.restoreInsertionPoint(
        compat::a_builder::getBestAllocaInsertPoint(block));
  result = builder_.createAlloca(loc(), cir::PointerType::get(type), name,
                                 alignment_attr(align));
  if (block == fn_entry_)
    last_alloca_ = result.getDefiningOp();
  return result;
}  /* a_emitter::alloca_in */

mlir::IntegerAttr a_emitter::alignment_attr(uint64_t align)
/*
The i64 alignment attribute for align (empty for 0), cached for the powers
of two up to 32768.
*/
{
  mlir::IntegerAttr result;
  unsigned index = 0;

  if (align != 0 && (align & (align - 1)) == 0 &&
      (index = static_cast<unsigned>(llvm::countr_zero(align))) < 16) {
    if (!align_attrs_[index])
      align_attrs_[index] = builder_.getAlignmentAttr(
                                              static_cast<int64_t>(align));
    result = align_attrs_[index];
  } else {
    result = builder_.getAlignmentAttr(static_cast<int64_t>(align));
  }
  return result;
}  /* a_emitter::alignment_attr */

cir::GlobalLinkageKindAttr a_emitter::linkage_attr(
                                                cir::GlobalLinkageKind kind)
/*
The linkage attribute for kind, cached.
*/
{
  unsigned index = static_cast<unsigned>(kind);
  cir::GlobalLinkageKindAttr result;

  if (index < 16) {
    if (!linkage_attrs_[index])
      linkage_attrs_[index] = cir::GlobalLinkageKindAttr::get(ctx_, kind);
    result = linkage_attrs_[index];
  } else {
    result = cir::GlobalLinkageKindAttr::get(ctx_, kind);
  }
  return result;
}  /* a_emitter::linkage_attr */

mlir::NamedAttribute a_emitter::nothrow_attr()
/*
The call-site / function attribute meaning "cannot unwind".
*/
{
  return mlir::NamedAttribute(nothrow_name_, unit_attr_);
}  /* a_emitter::nothrow_attr */

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

bool a_emitter::terminate_region(mlir::Region &region, bool keep_dead)
/*
End a structured region: give every unterminated block a cir.yield and erase
empty blocks nothing can reach (CIRGenFunction::terminateStructuredRegionBody).
The body of a cleanup scope whose paths all end in cir.unreachable (a throw)
keeps its dead block, which holds the yield that gives the body an exit:
FlattenCFG does not accept a cleanup scope body without one.
*/
{
  llvm::SmallVector<mlir::Block *> dead;
  size_t blocks = region.getBlocks().size();
  bool unreachable = false;

  for (mlir::Block &block : region.getBlocks()) {
    if (!block.empty() && mlir::isa<cir::UnreachableOp>(block.back()))
      unreachable = true;
  }
  for (mlir::Block &block : region.getBlocks()) {
    if (blocks != 1 && &block != &region.front() && block.empty() &&
        block.hasNoPredecessors() && !(keep_dead && unreachable))
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
    ok = false;
  else if (e->has_body)
    ok = internal("function " + std::string(e->want) + " defined twice");
  if (ok) {
    t = type_at(e->type);
    fn = mlir::cast<cir::FuncOp>(e->op);
    e->has_body = true;
    e->referenced = true;
    apply_function_attributes(*e, fn);
    /* As CIRGen: a definition compiled without exceptions cannot throw. */
    if (is_nothrow_call(0, e))
      fn->setAttr(nothrow_name_, unit_attr_);
    compat::set_dso_local(fn.getOperation(), true);
    compat::set_unwind_tables(fn);
    this_local_ = nullptr;
    this_class_ = 0;
    vtt_arg_ = mlir::Value();
    entry_mark_ = nullptr;
    entry_mark_set_ = false;
    vptr_set_early_ = false;
    vptr_init_seen_ = false;
    fn_ = fn;
    fn_entity_ = e;
    fn_entry_ = fn.addEntryBlock();
    fn_return_type_ = t->target;
    builder_.setInsertionPointToStart(fn_entry_);
    local_store_.clear();
    last_alloca_ = nullptr;
    params_seen_ = 0;
    fn_retval_ = mlir::Value();
    fn_nrvo_flag_ = mlir::Value();
    cleanups_.clear();
    temp_counter_ = 0;
    in_function_ = true;
    values_.clear();
    open_frame(a_frame_kind::function, fn.getOperation());
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
  name = std::string(string_for(r.ops[3], good));
  abi = type_at(t_abi);
  decl = type_at(t_decl);
  if (!good || abi == nullptr || decl == nullptr)
    ok = false;
  else if (!type_is_complete(abi->cir) || !type_is_complete(decl->cir))
    ok = internal("PARAM of an incomplete record type");
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
      name = (r.ops[4].u & ECIL_PARAM_THIS) != 0
          ? "this" : "arg" + std::to_string(params_seen_);
    align = static_cast<uint32_t>(type_align(decl->cir));
    address = alloca_in(fn_entry_, decl->cir, name, align);
    if (abi->cir != decl->cir) {
      mlir::Value converted;
      uint32_t kind = eck_integral;
      mlir::Type from = abi->cir;
      mlir::Type to = decl->cir;
      if (mlir::isa<cir::IntType>(from) && mlir::isa<cir::IntType>(to))
        kind = eck_integral;
      else if (mlir::isa<cir::IntType>(from) && mlir::isa<cir::BoolType>(to))
        kind = eck_int_to_bool;
      else if (cir::isAnyFloatingPointType(from) &&
               cir::isAnyFloatingPointType(to))
        kind = eck_floating;
      else
        ok = format_error("PARAM ABI type cannot be narrowed to its "
                          "declared type");
      if (ok)
        ok = cast_value(kind, argument, decl->cir, converted);
      argument = converted;
    }
  }
  if (ok) {
    a_local *local = new_local(l);
    builder_.createStore(loc(), argument, address, false, false,
                         alignment_attr(align));
    if (local == nullptr) {
      ok = false;
    } else {
      local->address = address;
      local->align = align;
      local->type = t_decl;
      if ((r.ops[4].u & ECIL_PARAM_THIS) != 0 &&
          decl->kind == a_type_kind::pointer_type) {
        /* The object of VPTR_INIT: this and its class. */
        this_local_ = local;
        this_class_ = decl->target;
        /* The hidden VTT parameter of a C2/D2 of a class with virtual bases
           follows it (12.3): no PARAM record names it. */
        if (fn_entity_ != nullptr && fn_entity_->has_vtt) {
          if (params_seen_ < fn_entry_->getNumArguments()) {
            vtt_arg_ = fn_entry_->getArgument(params_seen_);
            ++params_seen_;
          } else {
            ok = internal("the VTT parameter is missing from the entry "
                          "block");
          }
        }
      }
    }
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
  uint64_t flags = 0;
  bool good = true;
  std::string name;
  a_type *t;
  a_local *local;

  uint_operand(r.ops[0], l);
  uint_operand(r.ops[1], tgid);
  name = std::string(string_for(r.ops[2], good));
  uint_operand(r.ops[3], align);
  uint_operand(r.ops[4], flags);
  t = type_at(tgid);
  if (!good || t == nullptr) {
    ok = false;
  } else if (!type_is_complete(t->cir)) {
    ok = internal("LOCAL of an incomplete record type");
  } else if ((flags & ECIL_LOCAL_RETURN_SLOT) &&
             type_at(fn_return_type_)->cir != t->cir) {
    ok = format_error("LOCAL(RETURN_SLOT) does not have the function's "
                      "return type");
  } else if ((local = new_local(l)) == nullptr) {
    ok = false;
  } else if (flags & ECIL_LOCAL_RETURN_SLOT) {
    /* The NRVO variable is the function's __retval. */
    local->address = return_slot();
    local->align = static_cast<uint32_t>(type_align(t->cir));
    local->type = tgid;
    local->nrvo = true;
  } else {
    if (name.empty())
      name = ((flags & ECIL_LOCAL_TEMPORARY) != 0 ? "agg.tmp" : "tmp") +
             std::to_string(l);
    if (align == 0)
      align = type_align(t->cir);
    local->address = alloca_here(t->cir, name, static_cast<uint32_t>(align));
    local->align = static_cast<uint32_t>(align);
    local->type = tgid;
  }
  return ok;
}  /* a_emitter::local_decl */

mlir::Value a_emitter::return_slot()
/*
The function's single __retval alloca.  Every return of a value goes through
it, as CIRGen does: CallConvLowering's named return value optimization can
only forward one alloca to the sret pointer, so returns of different locals
would leave all but one of them uncopied.
*/
{
  a_type *ret = type_at(fn_return_type_);

  if (!fn_retval_)
    fn_retval_ = alloca_in(fn_entry_, ret->cir, "__retval",
                           static_cast<uint32_t>(type_align(ret->cir)));
  return fn_retval_;
}  /* a_emitter::return_slot */

mlir::Value a_emitter::nrvo_flag()
/*
The "nrvo" flag of the function (CIRGen's): false from the entry, true once
the NRVO variable is returned.  The destruction of that variable on the other
exits (a goto out of its scope) is guarded by it.
*/
{
  if (!fn_nrvo_flag_) {
    mlir::OpBuilder::InsertPoint here = builder_.saveInsertionPoint();

    fn_nrvo_flag_ = alloca_in(fn_entry_, builder_.getBoolTy(), "nrvo", 1);
    builder_.setInsertionPointAfter(fn_nrvo_flag_.getDefiningOp());
    builder_.createStore(loc(), builder_.getFalse(loc()), fn_nrvo_flag_,
                         false, false, alignment_attr(1));
    builder_.restoreInsertionPoint(here);
  }
  return fn_nrvo_flag_;
}  /* a_emitter::nrvo_flag */

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
    mlir::Value slot = return_slot();
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

  if (!frames_.empty() && frames_.front().kind == a_frame_kind::function)
    close_cleanups(0, -1);
  if (eh_spec_active_ && frames_.size() == 1)
    close_eh_spec();
  if (frames_.size() != 1 || frames_.back().kind != a_frame_kind::function)
    ok = internal("FUNC_END inside an open construct");
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
    local_store_.clear();
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
    else if (ok && is_indirect_class(ret->cir))
      ok = format_error("RETURN_STMT with a value in a function returning a "
                        "NONTRIVIAL_ABI record: use RETURN_SLOT and "
                        "RETURN_FROM_SLOT");
    if (ok && v.getType() != ret->cir) {
      mlir::Value converted;
      uint32_t kind = mlir::isa<cir::PointerType>(ret->cir) ? eck_bitcast
          : mlir::isa<cir::IntType>(ret->cir) ? eck_integral : eck_floating;
      ok = cast_value(kind, v, ret->cir, converted);
      v = converted;
    }
    /* An aggregate always returns through __retval; so does every value
       while a cleanup is open: a bare load of an entry alloca would be sunk
       past the cleanup (spike 2).  Likewise while a try or an exception
       specification is open. */
    if (ok && (mlir::isa<cir::RecordType>(ret->cir) ||
               mlir::isa<cir::ArrayType>(ret->cir) || !cleanups_.empty() ||
               try_depth() > 0)) {
      uint32_t align = static_cast<uint32_t>(type_align(ret->cir));
      mlir::Value slot = return_slot();
      builder_.createStore(loc(), v, slot, false, false,
                           alignment_attr(align));
      v = builder_.createLoad(loc(), slot, false, align);
    }
    if (ok)
      cir::ReturnOp::create(builder_, loc(), mlir::ValueRange{v});
  } else if (ret->kind != a_type_kind::void_type) {
    /* Legal in C and a GNU extension in C++ (a warning): the function
       returns whatever its return slot holds, as GCC's code does. */
    if ((fn_entity_->flags & ECIL_DECL_FUNC_IS_MAIN) != 0) {
      implicit_return(true);
    } else {
      uint32_t align = static_cast<uint32_t>(type_align(ret->cir));
      mlir::Value slot = return_slot();
      mlir::Value v = builder_.createLoad(loc(), slot, false, align);
      cir::ReturnOp::create(builder_, loc(), mlir::ValueRange{v});
    }
  } else {
    cir::ReturnOp::create(builder_, loc());
  }
  if (ok)
    start_new_block();
  return ok;
}  /* a_emitter::emit_return */

/* Cleanups, full expressions and array loops. */

namespace {

bool is_region_frame(a_frame_kind kind)
/*
The frames that are scope regions of the ECIL spec (a cleanup begun in one
lasts to its end).
*/
{
  return kind == a_frame_kind::function || kind == a_frame_kind::scope ||
         kind == a_frame_kind::if_stmt || kind == a_frame_kind::while_stmt ||
         kind == a_frame_kind::do_stmt || kind == a_frame_kind::for_stmt ||
         kind == a_frame_kind::switch_stmt || kind == a_frame_kind::meminit ||
         kind == a_frame_kind::fullexpr || kind == a_frame_kind::array_body ||
         kind == a_frame_kind::static_init ||
         kind == a_frame_kind::try_body || kind == a_frame_kind::catch_body;
}  /* is_region_frame */

}  /* anonymous namespace */

size_t a_emitter::region_owner() const
/*
The index of the innermost scope-region frame.
*/
{
  size_t k = frames_.size();

  while (k > 0 && !is_region_frame(frames_[k - 1].kind))
    --k;
  return k == 0 ? 0 : k - 1;
}  /* a_emitter::region_owner */

void a_emitter::close_cleanups(size_t owner, int phase)
/*
Close the open cleanup scopes that end with the region of frame owner (in
phase, or in any phase when phase is negative), innermost first: the body is
terminated, the cleanup region (already complete for an ordinary cleanup)
too, and emission continues after the scope.
*/
{
  while (!cleanups_.empty() && cleanups_.back().owner == owner &&
         (phase < 0 || cleanups_.back().owner_phase == phase)) {
    a_cleanup_scope scope = cleanups_.back();

    cleanups_.pop_back();
    terminate_region(scope.op.getBodyRegion(), true);
    if (scope.conditional)
      terminate_region(scope.op.getCleanupRegion());
    builder_.setInsertionPointAfter(scope.op.getOperation());
  }
  prune_deacts();
}  /* a_emitter::close_cleanups */

void a_emitter::close_cleanups_down_to(size_t count)
/*
Close the innermost cleanup scopes until count remain.
*/
{
  while (cleanups_.size() > count) {
    a_cleanup_scope scope = cleanups_.back();

    cleanups_.pop_back();
    terminate_region(scope.op.getBodyRegion(), true);
    if (scope.conditional)
      terminate_region(scope.op.getCleanupRegion());
    builder_.setInsertionPointAfter(scope.op.getOperation());
  }
  prune_deacts();
}  /* a_emitter::close_cleanups_down_to */

bool a_emitter::in_conditional_region(size_t &outermost) const
/*
True when the innermost scope region is reached through a conditional record
(a ?: arm or the right operand of && or ||) inside a FULLEXPR; outermost is
the frame index of the outermost such record.  The reader has checked that a
FULLEXPR encloses it.
*/
{
  bool found = false;
  size_t k = frames_.size();

  outermost = 0;
  while (k > 0) {
    a_frame_kind kind = frames_[k - 1].kind;
    if (kind == a_frame_kind::ternary || kind == a_frame_kind::land ||
        kind == a_frame_kind::lor) {
      found = true;
      outermost = k - 1;
    } else if (is_region_frame(kind)) {
      break;
    }
    --k;
  }
  return found;
}  /* a_emitter::in_conditional_region */

void a_emitter::open_conditional_scope(size_t outermost)
/*
The first conditional cleanup of a conditional expression: wrap that
expression (the outermost conditional record) in a flag-guarded cleanup
scope that lasts to the end of the full expression (spike 3).  The scope is
made right before the cir.ternary and the ternary moves into its body, so
the insertion point inside the arm stays valid and the code that follows the
expression lands in the scope body.  Unconditional cleanups begun later nest
inside it.
*/
{
  mlir::OpBuilder::InsertionGuard guard(builder_);
  mlir::Operation *conditional = frames_[outermost].op;
  size_t fullexpr = outermost;
  cir::CleanupScopeOp scope;
  mlir::Block *body;
  a_cleanup_scope entry;

  while (fullexpr > 0 && frames_[fullexpr].kind != a_frame_kind::fullexpr)
    --fullexpr;
  builder_.setInsertionPoint(conditional);
  scope = compat::open_cleanup_scope(
      builder_, loc(),
      exceptions_ ? cir::CleanupKind::All : cir::CleanupKind::Normal);
  body = &scope.getBodyRegion().front();
  conditional->moveBefore(body, body->end());
  entry.op = scope;
  entry.owner = fullexpr;
  entry.owner_phase = 0;
  entry.conditional = true;
  frames_[outermost].cond_scope = cleanups_.size();
  cleanups_.push_back(entry);
}  /* a_emitter::open_conditional_scope */

cir::CleanupKind a_emitter::cleanup_kind(uint64_t kind) const
/*
The kind of cir.cleanup.scope of an ECIL cleanup kind: with exceptions off an
all cleanup is a normal one (an eh cleanup is dropped before it gets here).
*/
{
  cir::CleanupKind result = cir::CleanupKind::Normal;

  if (kind == ecl_eh)
    result = cir::CleanupKind::EH;
  else if (kind == ecl_all && exceptions_)
    result = cir::CleanupKind::All;
  return result;
}  /* a_emitter::cleanup_kind */

void a_emitter::prune_deacts()
/*
Forget the deactivatable cleanups whose scope is closed.
*/
{
  while (!deacts_.empty() && deacts_.back().depth > cleanups_.size())
    deacts_.pop_back();
}  /* a_emitter::prune_deacts */

void a_emitter::deactivate_flag(mlir::Value flag)
/*
THROW_END: the implicit free cleanup of the throw stops being active.  It is
the cleanup with this flag, not the innermost active one (a statement
expression in the initializer may have begun others, and the throw may sit in
a new-expression whose own cleanup is active around it).
*/
{
  for (size_t k = deacts_.size(); k > 0; --k) {
    if (deacts_[k - 1].flag == flag) {
      deacts_.erase(deacts_.begin() + (k - 1));
      break;
    }
  }
  builder_.createStore(loc(), builder_.getFalse(loc()), flag, false, false,
                       alignment_attr(1));
}  /* a_emitter::deactivate_flag */

void a_emitter::deactivate_cleanup()
/*
CLEANUP_END: the innermost active deactivatable cleanup stops being active
(its flag turns false: neither the normal nor the exception path runs it).
*/
{
  prune_deacts();
  if (!deacts_.empty()) {
    a_deact entry = deacts_.back();

    deacts_.pop_back();
    builder_.createStore(loc(), builder_.getFalse(loc()), entry.flag, false,
                         false, alignment_attr(1));
  }
}  /* a_emitter::deactivate_cleanup */

bool a_emitter::begin_cleanup_code(uint64_t kind, bool deactivatable)
/*
The start of a cleanup (CLEANUP_BEGIN of a normal, eh or all cleanup): the
insertion point moves into the code of its cleanup region.  A normal cleanup
is a cir.cleanup.scope whose cleanup region fills with that code and whose
body takes the rest of the enclosing scope region.  An eh cleanup is dropped
when exceptions are off.  A DEACTIVATABLE one has a flag (set true where its
body starts) and its code runs under that flag.  Inside a conditional record
the cleanup is guarded by a flag in the full expression's flag-guarded scope
(open_conditional_scope); a DEACTIVATABLE one uses that flag as its active
flag.
*/
{
  bool ok = true;
  size_t outermost = 0;

  if (kind == ecl_eh && !exceptions_) {
    open_frame(a_frame_kind::dropped, nullptr);
    drop_depth_ = record_depth_;
  } else if (!in_conditional_region(outermost)) {
    cir::CleanupScopeOp scope = compat::open_cleanup_scope(
        builder_, loc(), cleanup_kind(kind));

    open_frame(a_frame_kind::cleanup, scope.getOperation());
    frames_.back().cleanup_op = scope.getOperation();
    builder_.setInsertionPointToStart(&scope.getCleanupRegion().front());
    if (deactivatable) {
      mlir::Value flag = alloca_here(builder_.getBoolTy(), "cleanup.isactive",
                                     1);
      mlir::Value set = builder_.createLoad(loc(), flag, false, 1);
      cir::IfOp guard = compat::open_if(builder_, loc(), set);

      frames_.back().deactivatable = true;
      frames_.back().deact_flag = flag;
      frames_.back().deact_guard = guard;
      builder_.setInsertionPointToStart(&guard.getThenRegion().front());
    }
  } else if (kind == ecl_eh && !deactivatable) {
    ok = unsupported("a conditional eh cleanup that is not DEACTIVATABLE");
  } else {
    if (frames_[outermost].cond_scope == static_cast<size_t>(-1))
      open_conditional_scope(outermost);
    a_cleanup_scope &entry = cleanups_[frames_[outermost].cond_scope];
    cir::CleanupScopeOp scope = entry.op;
    mlir::Value flag = alloca_here(builder_.getBoolTy(), "cleanup.cond", 1);
    mlir::OpBuilder::InsertPoint here = builder_.saveInsertionPoint();

    /* The flag starts false at the top of the scope body and turns true
       where the object is constructed. */
    builder_.setInsertionPointToStart(&scope.getBodyRegion().front());
    builder_.createStore(loc(), builder_.getFalse(loc()), flag, false, false,
                         alignment_attr(1));
    /* Its destruction goes first in the cleanup region: reverse order. */
    builder_.setInsertionPointToStart(&scope.getCleanupRegion().front());
    mlir::Value set = builder_.createLoad(loc(), flag, false, 1);
    cir::IfOp guard = compat::open_if(builder_, loc(), set);
    open_frame(a_frame_kind::cleanup, guard.getOperation());
    a_frame &f = frames_.back();
    f.cleanup_mode = 1;
    f.saved_ip = here;
    f.flag = flag;
    f.cleanup_op = guard.getOperation();
    f.deactivatable = deactivatable;
    f.cond_index = frames_[outermost].cond_scope;
    builder_.setInsertionPointToStart(&guard.getThenRegion().front());
  }
  return ok;
}  /* a_emitter::begin_cleanup_code */

bool a_emitter::end_cleanup_code()
/*
CLEANUP_BODY: the cleanup code is complete.
*/
{
  bool ok = true;
  a_frame *f = top_frame(a_frame_kind::cleanup, "CLEANUP_BODY");

  if (f == nullptr) {
    ok = false;
  } else if (f->cleanup_mode >= 2) {
    mlir::OpBuilder::InsertPoint here = f->saved_ip;
    mlir::Operation *code_op = f->cleanup_op;

    mlir::Block *block = builder_.getBlock();

    /* LoweringPrepare outlines the destruction code and requires it to
       start with the get_global of the object. */
    if (block->empty() || !mlir::isa<cir::GetGlobalOp>(block->front()))
      ok = format_error("the exit cleanup code of a static-duration "
                        "object must start with GREF of that object");
    terminate_region(*block->getParent());
    if (ok)
      ok = pop_frame();
    if (ok) {
      if (code_op != nullptr)
        builder_.setInsertionPointAfter(code_op);
      else
        builder_.restoreInsertionPoint(here);
    }
  } else if (f->cleanup_mode == 0) {
    cir::CleanupScopeOp scope = mlir::cast<cir::CleanupScopeOp>(
                                                         f->cleanup_op);
    bool deactivatable = f->deactivatable;
    mlir::Value flag = f->deact_flag;

    if (f->nrvo_guard != nullptr) {
      cir::IfOp guard = mlir::cast<cir::IfOp>(f->nrvo_guard);

      terminate_region(guard.getThenRegion());
      builder_.setInsertionPointAfter(guard.getOperation());
    }
    if (deactivatable)
      terminate_region(f->deact_guard.getThenRegion());
    terminate_region(scope.getCleanupRegion());
    ok = pop_frame();
    if (ok) {
      a_cleanup_scope entry;
      entry.op = scope;
      entry.owner = region_owner();
      entry.owner_phase = frames_[entry.owner].phase;
      entry.conditional = false;
      cleanups_.push_back(entry);
      builder_.setInsertionPointToStart(&scope.getBodyRegion().front());
      if (deactivatable) {
        builder_.createStore(loc(), builder_.getTrue(loc()), flag, false,
                             false, alignment_attr(1));
        deacts_.push_back({flag, cleanups_.size()});
      }
    }
  } else {
    cir::IfOp guard = mlir::cast<cir::IfOp>(f->cleanup_op);
    mlir::OpBuilder::InsertPoint here = f->saved_ip;
    mlir::Value flag = f->flag;
    bool deactivatable = f->deactivatable;
    size_t scope_index = f->cond_index;

    terminate_region(guard.getThenRegion());
    ok = pop_frame();
    if (ok) {
      builder_.restoreInsertionPoint(here);
      builder_.createStore(loc(), builder_.getTrue(loc()), flag, false, false,
                           alignment_attr(1));
      if (deactivatable)
        deacts_.push_back({flag, scope_index + 1});
    }
  }
  return ok;
}  /* a_emitter::end_cleanup_code */

bool a_emitter::cleanup_record(const ecil::a_record &r)
/*
CLEANUP_BEGIN .. CLEANUP_BODY.  An exit cleanup registers code for the exit
of a static-duration object; the others are begin_cleanup_code and
end_cleanup_code.
*/
{
  bool ok = true;

  if (r.opcode == eop_cleanup_begin) {
    uint64_t kind = 0;
    uint64_t flags = 0;

    uint_operand(r.ops[0], kind);
    uint_operand(r.ops[2], flags);
    if (kind == ecl_exit) {
      /* The destruction of a static-duration object at program exit: the
         dtor region of the variable being initialized, or, for another
         variable (a lifetime-extended _ZGR temporary), a
         cir.register_exit_dtor in the ctor region. */
      uint64_t g = 0;
      size_t k = frames_.size();
      a_entity *target;

      uint_operand(r.ops[1], g);
      target = entity_for(g, false);
      while (k > 0 && frames_[k - 1].kind != a_frame_kind::static_init)
        --k;
      if (target == nullptr) {
        ok = false;
      } else if (k == 0) {
        ok = internal("CLEANUP_BEGIN(exit) outside a STATIC_INIT");
      } else {
        a_frame &si = frames_[k - 1];
        mlir::OpBuilder::InsertPoint here = builder_.saveInsertionPoint();
        mlir::Operation *init_op = si.op;
        a_entity *self = si.init_entity;
        mlir::Operation *code_op = nullptr;
        mlir::Block *block;

        if (target == self) {
          mlir::Region &dtor = mlir::isa<cir::GlobalOp>(init_op)
              ? mlir::cast<cir::GlobalOp>(init_op).getDtorRegion()
              : mlir::cast<cir::LocalInitOp>(init_op).getDtorRegion();
          block = builder_.createBlock(&dtor);
        } else {
          cir::RegisterExitDtorOp reg = compat::open_register_exit_dtor(
              builder_, loc(), mlir::cast<cir::GlobalOp>(target->op));
          target->referenced = true;
          code_op = reg.getOperation();
          block = builder_.createBlock(&reg.getBody());
        }
        open_frame(a_frame_kind::cleanup, code_op);
        frames_.back().cleanup_mode = code_op == nullptr ? 2 : 3;
        frames_.back().saved_ip = here;
        frames_.back().cleanup_op = code_op;
        builder_.setInsertionPointToStart(block);
      }
    } else {
      /* A cleanup begun in a thrown object's initializer nests inside the
         cleanup that frees the object. */
      if (!throws_.empty())
        ok = materialize_throws();
      if (ok)
        ok = begin_cleanup_code(kind,
                                (flags & ECIL_CLEANUP_DEACTIVATABLE) != 0);
    }
  } else {
    ok = end_cleanup_code();
  }
  return ok;
}  /* a_emitter::cleanup_record */

bool a_emitter::fullexpr_record(const ecil::a_record &r)
/*
FULLEXPR_BEGIN / FULLEXPR_END: the cleanups begun inside end here.  The one
value the full expression may carry out (a scalar prvalue or a glvalue) is
stored in a tmp.exprcleanup slot before the cleanup scopes close and loaded
after them, as CIRGen does: a cir.cleanup.scope has no results.
*/
{
  bool ok = true;

  if (r.opcode == eop_fullexpr_begin) {
    open_frame(a_frame_kind::fullexpr, nullptr);
    frames_.back().cleanups_base = cleanups_.size();
  } else {
    a_frame *f = top_frame(a_frame_kind::fullexpr, "FULLEXPR_END");

    if (f == nullptr) {
      ok = false;
    } else if (values_.size() > f->value_base + 1) {
      ok = format_error("a full expression carries out at most one value");
    } else {
      size_t base = f->cleanups_base;
      bool opened = cleanups_.size() > base;
      bool carried = values_.size() == f->value_base + 1;
      a_value value;
      mlir::Value slot;
      uint32_t align = 1;

      if (carried) {
        value = values_.back();
        values_.pop_back();
        if (opened && value.kind == a_value_kind::bitfield) {
          ok = unsupported("a bit-field glvalue carried out of a full "
                           "expression with cleanups");
        } else if (opened) {
          mlir::Type type = value.value.getType();
          align = static_cast<uint32_t>(type_align(type));
          slot = alloca_here(type, "tmp.exprcleanup", align);
          builder_.createStore(loc(), value.value, slot, false, false,
                               alignment_attr(align));
        }
      }
      if (ok) {
        close_cleanups_down_to(base);
        ok = pop_frame();
      }
      if (ok && carried) {
        if (opened)
          value.value = builder_.createLoad(loc(), slot, false, align);
        values_.push_back(value);
      }
    }
  }
  return ok;
}  /* a_emitter::fullexpr_record */

bool a_emitter::array_record(const ecil::a_record &r)
/*
ARRAY_CTOR / ARRAY_DTOR: cir.array.ctor / cir.array.dtor whose body region
holds the code for one element; the element pointer is its block argument and
is pushed for the code.  A multi-dimensional array is one flat array of the
innermost element type.  An empty array drops the loop.
*/
{
  bool ok = true;
  bool begin = r.opcode == eop_array_ctor_begin ||
               r.opcode == eop_array_dtor_begin;

  if (begin) {
    bool is_dtor = r.opcode == eop_array_dtor_begin;
    uint64_t tgid = 0;
    uint64_t flags = 0;
    a_type *element;
    mlir::Value address;
    mlir::Value count;
    bool empty = false;

    uint_operand(r.ops[0], tgid);
    uint_operand(r.ops[1], flags);
    element = type_at(tgid);
    if (element == nullptr) {
      ok = false;
    } else if (!type_is_complete(element->cir)) {
      ok = internal("array loop over an incomplete record type");
    } else if (flags & ECIL_ARRAY_LOOP_FIXED) {
      a_value array;
      ok = pop_glvalue(array);
      if (ok && array.kind != a_value_kind::glvalue)
        ok = format_error("array loop over a bit-field");
      if (ok) {
        mlir::Type pointee = mlir::cast<cir::PointerType>(
                                 array.value.getType()).getPointee();
        uint64_t total = 1;
        mlir::Type inner = pointee;
        while (auto nested = mlir::dyn_cast<cir::ArrayType>(inner)) {
          total *= nested.getSize();
          inner = nested.getElementType();
        }
        if (!mlir::isa<cir::ArrayType>(pointee) || inner != element->cir) {
          ok = format_error("a fixed array loop needs a glvalue of an array "
                            "of its element type");
        } else if (total == 0) {
          empty = true;
        } else {
          mlir::Type flat = compat::array_of(element->cir, total);
          address = array.value;
          if (pointee != flat)
            address = builder_.createBitcast(loc(), address,
                                             compat::pointer_to(flat));
        }
      }
    } else {
      mlir::Value pointer;
      ok = pop_prvalue(count) && pop_prvalue(pointer);
      if (ok && (!mlir::isa<cir::IntType>(count.getType()) ||
                 !mlir::isa<cir::PointerType>(pointer.getType()))) {
        ok = format_error("an array loop needs an element pointer and an "
                          "integer count");
      } else if (ok) {
        address = pointer;
        if (mlir::cast<cir::PointerType>(pointer.getType()).getPointee() !=
            element->cir)
          address = builder_.createBitcast(loc(), pointer,
                                           compat::pointer_to(element->cir));
      }
    }
    if (ok && empty) {
      open_frame(a_frame_kind::dropped, nullptr);
      drop_depth_ = record_depth_;
    } else if (ok) {
      mlir::Operation *op = compat::array_ctor_dtor(builder_, loc(), is_dtor,
                                                    address, count);
      mlir::Block *body = &op->getRegion(0).front();
      mlir::Value pointer = body->addArgument(
          compat::pointer_to(element->cir), loc());

      open_frame(a_frame_kind::array_body, op);
      builder_.setInsertionPointToStart(body);
      push_prvalue(pointer);
    }
  } else {
    a_frame *f = top_frame(a_frame_kind::array_body, "ARRAY_END");

    if (f == nullptr) {
      ok = false;
    } else {
      mlir::Operation *op = f->op;
      close_cleanups(frames_.size() - 1, -1);
      terminate_region(op->getRegion(0));
      /* The array.dtor is never marked dtor_may_throw: the lowering would
         put an EH cleanup scope (the loop that destroys the elements before
         the one that threw) inside the cleanup region that holds the loop,
         and the pin's cleanup flattening asserts on it ("cleanup region has
         exits in non-final blocks").  As with clang -fclangir, the elements
         before one whose destructor throws are not destroyed; the cleanups
         of the enclosing scopes do run. */
      builder_.setInsertionPointAfter(op);
      ok = pop_frame();
    }
  }
  return ok;
}  /* a_emitter::array_record */

bool a_emitter::static_init_record(const ecil::a_record &r)
/*
STATIC_INIT_BEGIN .. STATIC_INIT_END (ECIL spec 11.7).  In a function it is a
cir.local_init static_local whose ctor region holds the initialization code
and whose dtor region the exit cleanup.  In the F_GLOBALS body section it
fills the ctor and dtor regions of the cir.global, which moves to the end of
the module (initialization order is module order), with a fresh L-id space
whose allocas go in the ctor region.  A variable that has a VAR_INIT already
(constant initialization) has only its exit cleanup: a dtor-only region, or a
local_init with only a dtor region, as CIRGen emits it.
*/
{
  bool ok = true;

  if (r.opcode == eop_static_init_begin) {
    uint64_t gid = 0;
    uint64_t flags = 0;
    a_entity *e;

    uint_operand(r.ops[0], gid);
    uint_operand(r.ops[1], flags);
    e = entity_for(gid, false);
    if (e == nullptr) {
      ok = false;
    } else if ((flags & ECIL_STATIC_INIT_PARTIAL) && !e->has_init) {
      ok = internal("PARTIAL STATIC_INIT out of step with the validator");
    } else {
      cir::GlobalOp g = mlir::cast<cir::GlobalOp>(e->op);
      /* A variable with a VAR_INIT has only its exit cleanup here, unless
         PARTIAL (1.1): the constant elements stay the global's initial value
         and the region stores the dynamic ones.  The pin's verifier even
         requires an initial value next to a ctor region. */
      bool partial = (flags & ECIL_STATIC_INIT_PARTIAL) != 0;
      bool exit_only = e->has_init && !partial;
      bool global_init = !in_function_;

      e->dyn_init = true;
      e->init_exit_only = exit_only;
      e->init_inline = (flags & ECIL_STATIC_INIT_INLINE_VAR) != 0;
      e->init_template = (flags & ECIL_STATIC_INIT_TEMPLATE_INSTANCE) != 0;
      e->referenced = true;
      if (global_init) {
        /* A fresh "function": the ctor region is its entry block. */
        g->moveBefore(module_.getBody(), module_.getBody()->end());
        in_function_ = true;
        values_.clear();
        local_store_.clear();
        open_frame(a_frame_kind::function, nullptr);
        open_frame(a_frame_kind::static_init, g.getOperation());
        a_frame &f = frames_.back();
        f.init_entity = e;
        f.global_init = true;
        f.saved_entry = fn_entry_;
        f.saved_last_alloca = last_alloca_;
        last_alloca_ = nullptr;
        if (!exit_only) {
          fn_entry_ = builder_.createBlock(&g.getCtorRegion());
        } else {
          fn_entry_ = nullptr;
          builder_.setInsertionPointToEnd(module_.getBody());
        }
        if (!exit_only)
          builder_.setInsertionPointToStart(fn_entry_);
      } else {
        cir::LocalInitOp op = compat::open_local_init(
            builder_, loc(), g, g.getTlsModel().has_value());
        open_frame(a_frame_kind::static_init, op.getOperation());
        frames_.back().init_entity = e;
        if (exit_only) {
          /* CIRGen's shape: a dtor-only local_init, no ctor region. */
          builder_.setInsertionPointAfter(op.getOperation());
        } else {
          mlir::Block *block = builder_.createBlock(&op.getCtorRegion());

          builder_.setInsertionPointToStart(block);
        }
      }
    }
  } else {
    a_frame *f = top_frame(a_frame_kind::static_init, "STATIC_INIT_END");

    if (f == nullptr) {
      ok = false;
    } else {
      mlir::Operation *op = f->op;
      bool global_init = f->global_init;
      a_entity *e = f->init_entity;
      mlir::Block *entry = f->saved_entry;
      mlir::Operation *last = f->saved_last_alloca;

      close_cleanups(frames_.size() - 1, -1);
      if (!e->init_exit_only) {
        mlir::Region &ctor = global_init
            ? mlir::cast<cir::GlobalOp>(op).getCtorRegion()
            : mlir::cast<cir::LocalInitOp>(op).getCtorRegion();
        terminate_region(ctor);
      }
      ok = pop_frame();
      if (ok && global_init) {
        /* Pop the pseudo function frame. */
        ok = frames_.size() == 1 && pop_frame();
        in_function_ = false;
        fn_entry_ = entry;
        last_alloca_ = last;
        local_store_.clear();
        builder_.setInsertionPointToEnd(module_.getBody());
      } else if (ok) {
        builder_.setInsertionPointAfter(op);
      }
    }
  }
  return ok;
}  /* a_emitter::static_init_record */

bool a_emitter::return_from_slot()
/*
RETURN_FROM_SLOT: return the object that was constructed in __retval
(ECIL spec 11.6; CIRGen's return of a record or array loads __retval).
*/
{
  bool ok = true;
  a_type *ret = type_at(fn_return_type_);
  uint32_t align = static_cast<uint32_t>(type_align(ret->cir));
  mlir::Value slot;
  mlir::Value v;

  if (!mlir::isa<cir::RecordType>(ret->cir) &&
      !mlir::isa<cir::ArrayType>(ret->cir)) {
    ok = format_error("RETURN_FROM_SLOT in a function that does not "
                      "return a record or array");
  } else {
    slot = return_slot();
    if (fn_nrvo_flag_)
      builder_.createStore(loc(), builder_.getTrue(loc()), fn_nrvo_flag_,
                           false, false, alignment_attr(1));
    v = builder_.createLoad(loc(), slot, false, align);
    cir::ReturnOp::create(builder_, loc(), mlir::ValueRange{v});
    start_new_block();
  }
  return ok;
}  /* a_emitter::return_from_slot */

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
    open_frame(a_frame_kind::scope, scope.getOperation());
    builder_.setInsertionPointToStart(block);
    break;
  }
  case eop_block_end: {
    a_frame *f = top_frame(a_frame_kind::scope, "BLOCK_END");
    if (f == nullptr) {
      ok = false;
    } else {
      cir::ScopeOp scope = mlir::cast<cir::ScopeOp>(f->op);
      close_cleanups(frames_.size() - 1, -1);
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
      open_frame(a_frame_kind::if_stmt, op.getOperation());
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
      close_cleanups(frames_.size() - 1, 0);
      terminate_region(op.getThenRegion());
      builder_.createBlock(&op.getElseRegion());
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
      close_cleanups(frames_.size() - 1, f->phase);
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
    open_frame(a_frame_kind::while_stmt, op.getOperation());
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
      f->phase = 1;
      builder_.setInsertionPointToStart(&op.getBody().front());
    }
    break;
  }
  case eop_while_end: {
    a_frame *f = top_frame(a_frame_kind::while_stmt, "WHILE_END");
    if (f == nullptr) {
      ok = false;
    } else {
      cir::WhileOp op = mlir::cast<cir::WhileOp>(f->op);
      close_cleanups(frames_.size() - 1, 1);
      terminate_region(op.getBody());
      builder_.setInsertionPointAfter(op.getOperation());
      ok = pop_frame();
    }
    break;
  }
  case eop_do_begin: {
    cir::DoWhileOp op = compat::open_do(builder_, loc());
    mlir::Block *block = &op.getBody().front();
    open_frame(a_frame_kind::do_stmt, op.getOperation());
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
      close_cleanups(frames_.size() - 1, 0);
      terminate_region(op.getBody());
      f->phase = 1;
      builder_.setInsertionPointToStart(&op.getCond().front());
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
    open_frame(a_frame_kind::for_stmt, nullptr);
    frames_.back().scope_op = scope.getOperation();
    builder_.setInsertionPointToStart(block);
    break;
  }
  case eop_for_cond: {
    a_frame *f = top_frame(a_frame_kind::for_stmt, "FOR_COND");
    if (f == nullptr) {
      ok = false;
    } else if (f->phase != 0) {
      ok = internal("FOR_COND out of order");
    } else if (values_.size() != f->value_base) {
      ok = format_error("values left on the stack at FOR_COND");
    } else {
      cir::ForOp op = compat::open_for(builder_, loc());
      f->op = op.getOperation();
      f->has_cond = r.ops[0].u != 0;
      f->phase = 1;
      builder_.setInsertionPointToStart(&op.getCond().front());
    }
    break;
  }
  case eop_for_step: {
    a_frame *f = top_frame(a_frame_kind::for_stmt, "FOR_STEP");
    mlir::Value cond;
    if (f == nullptr) {
      ok = false;
    } else if (f->phase != 1) {
      ok = internal("FOR_STEP out of order");
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
        f->phase = 2;
        builder_.setInsertionPointToStart(&op.getStep().front());
      }
    }
    break;
  }
  case eop_for_body: {
    a_frame *f = top_frame(a_frame_kind::for_stmt, "FOR_BODY");
    if (f == nullptr) {
      ok = false;
    } else if (f->phase != 2) {
      ok = internal("FOR_BODY out of order");
    } else if (values_.size() != f->value_base) {
      ok = format_error("values left on the stack at FOR_BODY");
    } else {
      cir::ForOp op = mlir::cast<cir::ForOp>(f->op);
      terminate_region(op.getStep());
      f->phase = 3;
      builder_.setInsertionPointToStart(&op.getBody().front());
    }
    break;
  }
  case eop_for_end: {
    a_frame *f = top_frame(a_frame_kind::for_stmt, "FOR_END");
    if (f == nullptr) {
      ok = false;
    } else if (f->phase != 3) {
      ok = internal("FOR_END out of order");
    } else {
      cir::ForOp op = mlir::cast<cir::ForOp>(f->op);
      cir::ScopeOp scope = mlir::cast<cir::ScopeOp>(f->scope_op);
      close_cleanups(frames_.size() - 1, 3);
      terminate_region(op.getBody());
      builder_.setInsertionPointAfter(op.getOperation());
      close_cleanups(frames_.size() - 1, 0);
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
      open_frame(a_frame_kind::switch_stmt, op.getOperation());
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
      ok = internal("case label outside a switch");
    } else {
      auto cond_type = mlir::cast<cir::IntType>(sw->switch_type);
      if (r.opcode != eop_case_default) {
        unsigned n = r.opcode == eop_case_range ? 2 : 1;
        for (unsigned k = 0; ok && k < n; ++k) {
          llvm::APInt v;
          if (!int_operand_apint(r.ops[k], cond_type.getWidth(), v))
            ok = format_error("case value is not an integer");
          else if (cond_type.getWidth() > 64 && v.getActiveBits() > 64)
            /* The MLIR LLVM switch translation at the pin reads a case
               value as an unsigned 64-bit number: a negative or wider one
               would collide with others. */
            ok = unsupported("a case label of a switch on a 128-bit "
                             "integer that is negative or wider than 64 "
                             "bits");
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
      close_cleanups(frames_.size() - 1, -1);
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
  case eop_return_from_slot:
    ok = return_from_slot();
    break;
  case eop_meminit_begin:
    /* A marker: member initializers are ordinary code (ECIL spec 11.4). */
    open_frame(a_frame_kind::meminit, nullptr);
    break;
  case eop_meminit_end: {
    a_frame *f = top_frame(a_frame_kind::meminit, "MEMINIT_END");
    if (f != nullptr)
      close_cleanups(frames_.size() - 1, -1);
    ok = f != nullptr && pop_frame();
    break;
  }
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
