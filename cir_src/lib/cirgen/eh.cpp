/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
eh.cpp -- exceptions (ECIL 1.2, M4 slice S4): try and catch, throw and
rethrow, the exception specifications of a function, the partial destructor
of an array constructor and the deactivation of cleanups.

A try is emitted in pieces: its body and each handler go into placeholder
cir.scope regions, since a cir.try has all its handler regions from the start;
at TRY_END the cir.try is made and the placeholders' regions move into it.

Derived in part from llvm-project clang/lib/CIR/CodeGen (CIRGenException.cpp,
CIRGenCleanup.cpp, CIRGenExprCXX.cpp), Apache-2.0 WITH LLVM-exception; see
CIRCompat.h for the upstream API surface used.
*/

#include "emitter.h"

namespace edg_cir {

bool a_emitter::is_nothrow_call(uint64_t flags, const a_entity *callee) const
/*
A call (or function) cannot throw: always with exceptions off; else when the
call has the NOTHROW flag or its callee's DECL_FUNC does.
*/
{
  return !exceptions_ || (flags & ECIL_CALL_NOTHROW) != 0 ||
         (callee != nullptr && (callee->flags & ECIL_DECL_FUNC_NOTHROW) != 0);
}  /* a_emitter::is_nothrow_call */

mlir::Type a_emitter::eh_token_type()
/*
!cir.eh_token: the in-flight exception that a handler region receives.
*/
{
  return cir::EhTokenType::get(ctx_);
}  /* a_emitter::eh_token_type */

bool a_emitter::exception_record(const ecil::a_record &r)
/*
The records of ECIL 1.2 exceptions.
*/
{
  bool ok = true;

  if (!exceptions_ && r.opcode != eop_cleanup_end) {
    ok = format_error(std::string(r.info->name) + " needs exceptions = 1");
  } else {
    switch (r.opcode) {
    case eop_try_begin:
      ok = try_begin_record(r);
      break;
    case eop_catch_begin:
      ok = catch_begin_record(r);
      break;
    case eop_catch_param:
      ok = catch_param_record(r);
      break;
    case eop_try_end:
      ok = try_end_record();
      break;
    case eop_throw_begin:
      ok = throw_begin_record(r);
      break;
    case eop_throw_end:
      ok = throw_end_record(r);
      break;
    case eop_rethrow:
      ok = rethrow_record();
      break;
    case eop_func_eh_spec:
      ok = eh_spec_record(r);
      break;
    case eop_array_partial_dtor:
      ok = array_partial_dtor_record(r);
      break;
    case eop_cleanup_end:
      ok = cleanup_end_record();
      break;
    default:
      ok = internal("not an exception record");
      break;
    }
  }
  return ok;
}  /* a_emitter::exception_record */

bool a_emitter::cleanup_end_record()
/*
CLEANUP_END: the innermost active DEACTIVATABLE cleanup stops being active.
*/
{
  deactivate_cleanup();
  return true;
}  /* a_emitter::cleanup_end_record */

/* try and catch. */

bool a_emitter::try_begin_record(const ecil::a_record &r)
/*
TRY_BEGIN(flags): the try body goes into a placeholder scope.
*/
{
  a_try t;
  cir::ScopeOp body = compat::open_scope(builder_, loc());
  uint64_t flags = 0;

  uint_operand(r.ops[0], flags);
  t.body = body;
  t.function_try = (flags & ECIL_TRY_FUNCTION_TRY) != 0;
  tries_.push_back(t);
  open_frame(a_frame_kind::try_body, body.getOperation());
  frames_.back().try_index = tries_.size() - 1;
  builder_.setInsertionPointToStart(&body.getScopeRegion().front());
  return true;
}  /* a_emitter::try_begin_record */

bool a_emitter::catch_begin_record(const ecil::a_record &r)
/*
CATCH_BEGIN(g_ti|nil): closes the try body or the previous handler and opens
a handler (a placeholder scope with the !cir.eh_token block argument).  Its
head, which needs the catch parameter that may follow, is made later
(flush_catch_head and catch_param_record).
*/
{
  bool ok = true;
  a_frame *f = &frames_.back();
  size_t index = f->try_index;
  a_try &t = tries_[index];
  cir::ScopeOp previous = f->kind == a_frame_kind::try_body
      ? t.body : t.handlers.back().scope;
  a_handler h;

  close_cleanups(frames_.size() - 1, -1);
  if (values_.size() != f->value_base)
    ok = format_error("values left on the stack at CATCH_BEGIN");
  if (ok) {
    terminate_region(previous.getScopeRegion());
    builder_.setInsertionPointAfter(previous.getOperation());
    h.scope = compat::open_scope(builder_, loc());
    h.scope.getScopeRegion().front().addArgument(eh_token_type(), loc());
    if (r.ops[0].kind != ecil::an_operand_kind::nil) {
      uint64_t g = 0;
      ecil::a_gid_entry *entry;

      uint_operand(r.ops[0], g);
      entry = ids_.find_gid(g);
      if (entry == nullptr || entry->payload == nullptr) {
        ok = internal("CATCH_BEGIN of an unknown typeinfo");
      } else {
        h.ti = static_cast<a_abi_entity *>(entry->payload);
        h.type = symbol_pointer(*h.ti->entity);
        h.ti->entity->referenced = true;
      }
    }
  }
  if (ok) {
    h.head_pending = true;
    t.handlers.push_back(h);
    frames_.pop_back();
    open_frame(a_frame_kind::catch_body, h.scope.getOperation());
    frames_.back().try_index = index;
    pending_head_ = static_cast<int64_t>(index);
    builder_.setInsertionPointToStart(&h.scope.getScopeRegion().front());
  }
  return ok;
}  /* a_emitter::catch_begin_record */

bool a_emitter::flush_catch_head(const ecil::a_record &next)
/*
The record after CATCH_BEGIN: a LOCAL (the catch parameter's storage) waits;
CATCH_PARAM makes the head with the parameter; anything else makes the head
of a handler without one.
*/
{
  bool ok = true;

  if (next.opcode != eop_local && next.opcode != eop_catch_param) {
    a_handler &h = tries_[static_cast<size_t>(pending_head_)].handlers.back();

    ok = make_catch_head(h, nullptr, 0, nullptr);
  }
  return ok;
}  /* a_emitter::flush_catch_head */

a_entity *a_emitter::catch_copy_thunk(a_handler &h, a_entity &copy,
                                      uint64_t class_gid)
/*
The thunk of a catch by value of a class with a copy constructor:
__clang_cir_catch_copy_<_ZTS mangling>(dst, src) calls the copy constructor
(CIRGen's __clang_cir_catch_copy; hidden and vague, or internal for a
TU-local type).
*/
{
  a_entity *result = nullptr;
  const ecil::a_class_facts *facts =
      ids_.class_facts(static_cast<uint32_t>(class_gid));
  a_type *cls = type_at(class_gid);

  if (facts == nullptr || cls == nullptr || facts->mangle_sid == 0) {
    format_error("a catch of a class by value needs the CLASS_MANGLE of the "
                 "class");
  } else {
    std::string name = "__clang_cir_catch_copy__ZTS" +
                       std::string(ids_.string(facts->mangle_sid));
    auto found = library_names_.find(name);

    if (found != library_names_.end()) {
      result = found->second;
    } else {
      mlir::Type pointer = compat::pointer_to(cls->cir);
      mlir::Type type = compat::function_type({pointer, pointer},
                                              compat::void_type(ctx_), false);
      bool tu_local = h.ti != nullptr &&
                      (h.ti->flags & ECIL_ABI_DATA_TU_LOCAL) != 0;
      cir::FuncOp fn;
      mlir::Block *entry;
      mlir::OpBuilder::InsertionGuard guard(builder_);
      llvm::SmallVector<mlir::NamedAttribute> attrs;

      result = define_library_function(name, type,
                                       tu_local ? elk_internal
                                                : elk_linkonce_odr, false);
      library_names_[result->want] = result;
      fn = mlir::cast<cir::FuncOp>(result->op);
      compat::mark_catch_copy_thunk(fn);
      if (!tu_local)
        compat::set_hidden(fn);
      entry = fn.addEntryBlock();
      builder_.setInsertionPointToStart(entry);
      if (is_nothrow_call(0, &copy))
        attrs.push_back(nothrow_attr());
      copy.calls.push_back(builder_.createCallOp(
          loc(), mlir::cast<cir::FuncOp>(copy.op),
          llvm::ArrayRef<mlir::Value>{entry->getArgument(0),
                                      entry->getArgument(1)}, attrs));
      copy.referenced = true;
      cir::ReturnOp::create(builder_, loc());
    }
  }
  return result;
}  /* a_emitter::catch_copy_thunk */

bool a_emitter::make_catch_head(a_handler &h, a_local *param, uint64_t kind,
                                a_entity *copy)
/*
The head of a handler: construct_catch_param for kinds 5 and 6, begin_catch,
the cir.cleanup.scope that ends the catch (end_catch in its cleanup region)
and init_catch_param.  The rest of the handler goes in the scope's body.
*/
{
  bool ok = true;
  mlir::Block *block = &h.scope.getScopeRegion().front();
  mlir::Value token = block->getArgument(0);
  mlir::Type void_pointer = compat::pointer_to(compat::void_type(ctx_));

  builder_.setInsertionPointToStart(block);
  h.head_pending = false;
  pending_head_ = -1;
  if (param != nullptr && (kind == ecat_non_trivial_copy ||
                           kind == ecat_reference_to_pointer)) {
    mlir::Attribute copy_fn;

    if (kind == ecat_non_trivial_copy) {
      a_entity *thunk = copy != nullptr
          ? catch_copy_thunk(h, *copy, type_at(param->type)->record->gid)
          : nullptr;

      if (thunk == nullptr)
        ok = copy == nullptr
            ? format_error("CATCH_PARAM of kind non_trivial_copy needs the "
                           "copy constructor") : false;
      else
        copy_fn = mlir::cast<cir::FuncOp>(thunk->op).getSymNameAttr();
    }
    if (ok)
      compat::construct_catch_param(builder_, loc(), kind, token,
                                    param->address, copy_fn);
  }
  if (ok) {
    mlir::Value exception;
    mlir::Value catch_token;
    cir::CleanupScopeOp scope;
    a_cleanup_scope entry;

    compat::begin_catch(builder_, loc(), token, void_pointer, catch_token,
                        exception);
    scope = compat::open_cleanup_scope(builder_, loc(),
                                       cir::CleanupKind::All);
    builder_.setInsertionPointToStart(&scope.getCleanupRegion().front());
    cir::EndCatchOp::create(builder_, loc(), catch_token);
    terminate_region(scope.getCleanupRegion());
    entry.op = scope;
    entry.owner = frames_.size() - 1;
    entry.owner_phase = frames_.back().phase;
    entry.conditional = false;
    cleanups_.push_back(entry);
    builder_.setInsertionPointToStart(&scope.getBodyRegion().front());
    if (param != nullptr)
      compat::init_catch_param(builder_, loc(), kind, exception,
                               param->address);
  }
  return ok;
}  /* a_emitter::make_catch_head */

bool a_emitter::catch_param_record(const ecil::a_record &r)
/*
CATCH_PARAM(l, kind, g_copy|nil): the parameter of the handler whose head is
waiting.
*/
{
  bool ok = true;
  uint64_t l = 0;
  uint64_t kind = 0;
  uint64_t g = 0;
  a_local *local;
  a_entity *copy = nullptr;

  uint_operand(r.ops[0], l);
  uint_operand(r.ops[1], kind);
  local = local_at(l);
  if (r.ops[2].kind != ecil::an_operand_kind::nil) {
    uint_operand(r.ops[2], g);
    copy = entity_for(g, true);
  }
  if (pending_head_ < 0) {
    ok = internal("CATCH_PARAM without a handler head waiting");
  } else if (local == nullptr || (g != 0 && copy == nullptr)) {
    ok = false;
  } else {
    a_handler &h = tries_[static_cast<size_t>(pending_head_)].handlers.back();

    ok = make_catch_head(h, local, kind, copy);
  }
  return ok;
}  /* a_emitter::catch_param_record */

bool a_emitter::assemble_try(a_try &t)
/*
TRY_END: make the cir.try from the placeholders.  Without a catch-all handler
there is an unwind handler that resumes.
*/
{
  bool ok = true;
  bool has_all = false;
  mlir::MLIRContext *mctx = ctx_;
  std::vector<mlir::Attribute> types;
  mlir::OperationState state(loc(), cir::TryOp::getOperationName());
  mlir::Operation *op;
  size_t count;

  for (const a_handler &h : t.handlers) {
    if (!h.type)
      has_all = true;
    types.push_back(h.type ? h.type
                           : static_cast<mlir::Attribute>(
                                 cir::CatchAllAttr::get(mctx)));
  }
  if (!has_all)
    types.push_back(cir::UnwindAttr::get(mctx));
  count = types.size();
  state.addRegion();
  for (size_t k = 0; k < count; ++k)
    state.addRegion();
  state.addAttribute(cir::TryOp::getHandlerTypesAttrName(state.name),
                     builder_.getArrayAttr(types));
  builder_.setInsertionPoint(t.body.getOperation());
  op = builder_.create(state);
  op->getRegion(0).takeBody(t.body.getScopeRegion());
  for (size_t k = 0; k < t.handlers.size(); ++k)
    op->getRegion(static_cast<unsigned>(k + 1)).takeBody(
        t.handlers[k].scope.getScopeRegion());
  if (!has_all) {
    mlir::Block *block = builder_.createBlock(
        &op->getRegion(static_cast<unsigned>(count)), {},
        {eh_token_type()}, {loc()});

    builder_.setInsertionPointToStart(block);
    cir::ResumeOp::create(builder_, loc(), block->getArgument(0));
  }
  for (a_handler &h : t.handlers)
    h.scope->erase();
  t.body->erase();
  builder_.setInsertionPointAfter(op);
  return ok;
}  /* a_emitter::assemble_try */

bool a_emitter::try_end_record()
/*
TRY_END: the last handler (or the body) ends and the try is assembled.
*/
{
  bool ok = true;
  a_frame *f = &frames_.back();
  size_t index = f->try_index;
  a_try &t = tries_[index];
  cir::ScopeOp last = f->kind == a_frame_kind::try_body
      ? t.body : t.handlers.back().scope;

  close_cleanups(frames_.size() - 1, -1);
  if (values_.size() != f->value_base)
    ok = format_error("values left on the stack at TRY_END");
  if (ok) {
    terminate_region(last.getScopeRegion());
    frames_.pop_back();
    a_try done = t;

    tries_.pop_back();
    ok = assemble_try(done);
  }
  return ok;
}  /* a_emitter::try_end_record */

/* throw. */

bool a_emitter::throw_begin_record(const ecil::a_record &r)
/*
THROW_BEGIN(t): allocate the exception object (cir.alloc.exception).  The
implicit DEACTIVATABLE eh cleanup that frees it (__cxa_free_exception) if the
initialization throws (K10) is made only when something that can throw
follows (materialize_throws), as CIRGen does: `throw k;` has no cleanup.  The
glvalue of the object is pushed for the initializer.
*/
{
  bool ok = true;
  uint64_t tgid = 0;
  a_type *t;

  uint_operand(r.ops[0], tgid);
  t = type_at(tgid);
  if (t == nullptr) {
    ok = false;
  } else if (!type_is_complete(t->cir)) {
    ok = internal("THROW_BEGIN of an incomplete record type");
  } else {
    mlir::Type void_pointer = compat::pointer_to(compat::void_type(ctx_));
    mlir::Value object = compat::alloc_exception(
        builder_, loc(), compat::pointer_to(t->cir), type_size(t->cir));
    mlir::Value slot = alloca_here(void_pointer, "exn.slot", 8);
    a_throw entry;

    builder_.createStore(
        loc(), builder_.createBitcast(loc(), object, void_pointer), slot,
        false, false, alignment_attr(8));
    entry.object = object;
    entry.slot = slot;
    throws_.push_back(entry);
    push_glvalue(object, static_cast<uint32_t>(
                             std::max<uint64_t>(type_align(t->cir), 16)));
  }
  return ok;
}  /* a_emitter::throw_begin_record */

bool a_emitter::materialize_throws(bool skip_last)
/*
Make the free-the-exception cleanup of every open THROW (but the innermost
with skip_last) that has none yet, outermost first so that they nest in
order.  It is needed from the first thing that can throw: a call that is not
nothrow, a cleanup begun, a nested throw, a new region.  Everything before
that cannot throw, so the cleanup starts there.
*/
{
  bool ok = true;
  size_t count = throws_.size() - (skip_last && !throws_.empty() ? 1 : 0);

  for (size_t k = 0; ok && k < count; ++k) {
    if (throws_[k].materialized)
      continue;
    a_entity *release = library_entity(
        "__cxa_free_exception", true,
        compat::function_type({compat::pointer_to(compat::void_type(ctx_))},
                              compat::void_type(ctx_), false), false);

    if (release == nullptr) {
      ok = false;
    } else {
      mlir::Value slot = throws_[k].slot;

      throws_[k].materialized = true;
      ok = begin_cleanup_code(ecl_eh, true);
      if (ok) {
        mlir::Value pointer = builder_.createLoad(loc(), slot, false, 8);

        release->referenced = true;
        release->calls.push_back(builder_.createCallOp(
            loc(), mlir::cast<cir::FuncOp>(release->op),
            llvm::ArrayRef<mlir::Value>{pointer},
            llvm::ArrayRef<mlir::NamedAttribute>{nothrow_attr()}));
        ok = end_cleanup_code();
        if (ok && !deacts_.empty())
          throws_[k].flag = deacts_.back().flag;
      }
    }
  }
  return ok;
}  /* a_emitter::materialize_throws */

bool a_emitter::throw_end_record(const ecil::a_record &r)
/*
THROW_END(g_ti, g_dtor|nil): the exception object is complete: deactivate the
free (the runtime owns the object from here on) and cir.throw it, then
cir.unreachable.  What follows is in a new block.
*/
{
  bool ok = true;
  uint64_t g = 0;
  ecil::a_gid_entry *entry;
  a_throw th;

  uint_operand(r.ops[0], g);
  entry = ids_.find_gid(g);
  if (throws_.empty() || entry == nullptr || entry->payload == nullptr) {
    ok = internal("THROW_END out of step with its THROW_BEGIN");
  } else {
    a_abi_entity &ti = *static_cast<a_abi_entity *>(entry->payload);
    mlir::StringAttr dtor;

    /* A throw nested in the initializer is a thing that can throw. */
    ok = materialize_throws(true);
    th = throws_.back();
    throws_.pop_back();
    if (ok && r.ops[1].kind != ecil::an_operand_kind::nil) {
      uint64_t gd = 0;
      a_entity *d;

      uint_operand(r.ops[1], gd);
      d = entity_for(gd, true);
      if (d == nullptr) {
        ok = false;
      } else {
        d->referenced = true;
        dtor = mlir::cast<cir::FuncOp>(d->op).getSymNameAttr();
      }
    }
    if (ok) {
      ti.entity->referenced = true;
      if (th.materialized && th.flag)
        deactivate_flag(th.flag);
      compat::throw_op(builder_, loc(), th.object,
                       mlir::cast<cir::GlobalOp>(ti.entity->op)
                           .getSymNameAttr(),
                       dtor);
      cir::UnreachableOp::create(builder_, loc());
      start_new_block();
    }
  }
  return ok;
}  /* a_emitter::throw_end_record */

bool a_emitter::rethrow_record()
/*
RETHROW: cir.throw without operands, then cir.unreachable.
*/
{
  bool ok = materialize_throws();

  if (ok) {
    compat::throw_op(builder_, loc(), mlir::Value(), mlir::StringAttr(),
                     mlir::StringAttr());
    cir::UnreachableOp::create(builder_, loc());
    start_new_block();
  }
  return ok;
}  /* a_emitter::rethrow_record */

/* Exception specifications. */

bool a_emitter::eh_spec_record(const ecil::a_record &r)
/*
FUNC_EH_SPEC(kind, [g_ti...]): the whole body of the function goes in a
placeholder scope; FUNC_END makes the cir.try (close_eh_spec): noexcept is a
catch-all that terminates, a dynamic specification a filter plus an
unexpected handler.
*/
{
  bool ok = true;
  uint64_t kind = 0;
  ecil::an_array_reader reader = ecil::array_elements(r.ops[1]);
  ecil::an_operand element;

  uint_operand(r.ops[0], kind);
  eh_spec_kind_ = static_cast<unsigned>(kind);
  eh_spec_types_.clear();
  while (ok && ecil::next_array_element(reader, element)) {
    uint64_t g = 0;
    ecil::a_gid_entry *entry;

    uint_operand(element, g);
    entry = ids_.find_gid(g);
    if (entry == nullptr || entry->payload == nullptr) {
      ok = internal("FUNC_EH_SPEC of an unknown typeinfo");
    } else {
      a_abi_entity *ti = static_cast<a_abi_entity *>(entry->payload);

      ti->entity->referenced = true;
      eh_spec_types_.push_back(symbol_pointer(*ti->entity));
    }
  }
  if (ok) {
    eh_spec_body_ = compat::open_scope(builder_, loc());
    eh_spec_active_ = true;
    builder_.setInsertionPointToStart(&eh_spec_body_.getScopeRegion().front());
  }
  return ok;
}  /* a_emitter::eh_spec_record */

bool a_emitter::close_eh_spec()
/*
The end of a function with an exception specification: make its cir.try.
*/
{
  bool ok = true;
  mlir::MLIRContext *mctx = ctx_;
  std::vector<mlir::Attribute> types;
  mlir::OperationState state(loc(), cir::TryOp::getOperationName());
  mlir::Operation *op;

  terminate_region(eh_spec_body_.getScopeRegion());
  if (eh_spec_kind_ == eesk_noexcept) {
    types.push_back(cir::CatchAllAttr::get(mctx));
  } else {
    types.push_back(cir::EhFilterAttr::get(
        mctx, builder_.getArrayAttr(eh_spec_types_)));
    types.push_back(cir::EhUnexpectedAttr::get(mctx));
  }
  state.addRegion();
  for (size_t k = 0; k < types.size(); ++k)
    state.addRegion();
  state.addAttribute(cir::TryOp::getHandlerTypesAttrName(state.name),
                     builder_.getArrayAttr(types));
  builder_.setInsertionPoint(eh_spec_body_.getOperation());
  op = builder_.create(state);
  op->getRegion(0).takeBody(eh_spec_body_.getScopeRegion());
  for (size_t k = 0; k < types.size(); ++k) {
    mlir::Block *block = builder_.createBlock(
        &op->getRegion(static_cast<unsigned>(k + 1)), {}, {eh_token_type()},
        {loc()});

    builder_.setInsertionPointToStart(block);
    if (eh_spec_kind_ == eesk_noexcept)
      compat::eh_terminate(builder_, loc(), block->getArgument(0));
    else if (k == 1)
      compat::eh_unexpected(builder_, loc(), block->getArgument(0));
    else if (eh_spec_types_.empty())
      cir::UnreachableOp::create(builder_, loc());
    else
      cir::ResumeOp::create(builder_, loc(), block->getArgument(0));
  }
  eh_spec_body_->erase();
  builder_.setInsertionPointAfter(op);
  eh_spec_body_ = cir::ScopeOp();
  eh_spec_active_ = false;
  return ok;
}  /* a_emitter::close_eh_spec */

bool a_emitter::array_partial_dtor_record(const ecil::a_record &r)
/*
ARRAY_PARTIAL_DTOR(g_dtor): the partial_dtor region of the cir.array.ctor
just begun: destroy one element (called for the elements constructed before
a constructor throws).
*/
{
  bool ok = true;
  uint64_t g = 0;
  a_entity *dtor;
  a_frame *f = frames_.empty() ? nullptr : &frames_.back();

  uint_operand(r.ops[0], g);
  dtor = entity_for(g, true);
  if (dtor == nullptr) {
    ok = false;
  } else if (f == nullptr || f->kind != a_frame_kind::array_body ||
             f->op == nullptr) {
    ok = internal("ARRAY_PARTIAL_DTOR outside an ARRAY_CTOR");
  } else {
    mlir::Operation *op = f->op;
    mlir::Type element = op->getRegion(0).front().getArgument(0).getType();
    mlir::OpBuilder::InsertionGuard guard(builder_);
    mlir::Block *block = builder_.createBlock(&op->getRegion(1));
    mlir::Value pointer = block->addArgument(element, loc());
    llvm::SmallVector<mlir::NamedAttribute> attrs;

    builder_.setInsertionPointToStart(block);
    if (is_nothrow_call(0, dtor))
      attrs.push_back(nothrow_attr());
    dtor->referenced = true;
    dtor->calls.push_back(builder_.createCallOp(
        loc(), mlir::cast<cir::FuncOp>(dtor->op), pointer, attrs));
    builder_.createYield(loc());
  }
  return ok;
}  /* a_emitter::array_partial_dtor_record */

}  /* namespace edg_cir */
