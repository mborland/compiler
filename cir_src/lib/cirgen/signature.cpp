/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
signature.cpp -- the LLVM attributes of parameters and results.

CIRGen (clang/lib/CIR/CodeGen/CIRGenCall.cpp, constructAttributeList) marks
the pointers of references and of `this` nonnull, noundef,
dereferenceable(size of the pointee) and align(alignment of the pointee), and
the scalar parameters and results noundef.  The facts come from the stream:
the key nonnull_ptrs of a function type says which positions are references
or the object parameter (ECIL 1.3, spec 6); the class facts give the sizes.
The attributes are the LLVM dialect's (llvm.noundef, llvm.nonnull,
llvm.dereferenceable, llvm.align) in the argument and result dictionaries of
cir.func and cir.call.
*/

#include "emitter.h"

namespace edg_cir {

mlir::DictionaryAttr a_emitter::noundef_attributes(mlir::Type type)
/*
{llvm.noundef} for a scalar (an integer, bool, floating-point or pointer
type: CIRGen's determineNoUndef is true for every scalar but a member pointer
and nullptr_t, and false for a record), else the empty dictionary.
*/
{
  llvm::SmallVector<mlir::NamedAttribute> attrs;

  if (mlir::isa<cir::IntType, cir::BoolType, cir::PointerType,
                cir::FPTypeInterface>(type))
    attrs.emplace_back(mlir::StringAttr::get(ctx_, "llvm.noundef"),
                       unit_attr_);
  return mlir::DictionaryAttr::get(ctx_, attrs);
}  /* a_emitter::noundef_attributes */

bool a_emitter::replaceable_operator(const a_entity &e, bool *is_new) const
/* Whether the function is a replaceable global allocation or deallocation
   function, by its name. */
{
  return e.is_function &&
         is_replaceable_operator_name(e.named ? e.want : e.symbol, is_new);
}  /* a_emitter::replaceable_operator */

llvm::SmallVector<mlir::NamedAttribute>
a_emitter::new_result_attributes(const a_entity &e) const
/* What clang knows of the result of a replaceable operator new: it does not
   alias anything (malloc-like) and, unless it is the nothrow form, is not
   null.  These go on the result of the declaration or definition and on the
   result of every direct call (noalias, like allocsize(0), is a property of
   the callee that LLVM reads at either place).  The `__hot_cold_t` forms and
   the __size_returning_new family are covered by the same name list: LLVM's
   TargetLibraryInfo treats them as allocators anyway, and so does the
   attribute. */
{
  llvm::SmallVector<mlir::NamedAttribute> attrs;
  std::string_view name = e.named ? e.want : e.symbol;

  attrs.emplace_back(mlir::StringAttr::get(ctx_, "llvm.noalias"), unit_attr_);
  if (name.find("RKSt9nothrow_t") == std::string_view::npos)
    attrs.emplace_back(mlir::StringAttr::get(ctx_, "llvm.nonnull"),
                       unit_attr_);
  return attrs;
}  /* a_emitter::new_result_attributes */

namespace {

mlir::DictionaryAttr with_attributes(
    mlir::MLIRContext *ctx, mlir::DictionaryAttr base,
    llvm::ArrayRef<mlir::NamedAttribute> more)
/* The dictionary with more attributes added. */
{
  llvm::SmallVector<mlir::NamedAttribute> all;

  if (base)
    all.assign(base.begin(), base.end());
  all.append(more.begin(), more.end());
  return mlir::DictionaryAttr::get(ctx, all);
}  /* with_attributes */

}  /* anonymous namespace */

bool a_emitter::strict_result(const a_entity *callee) const
/*
Whether the result of a function may be noundef (CIRGen's hasStrictReturn):
only in C++, and not for an extern "C" function (a null callee, an indirect
call, counts as C++).  The function must also not return without a value
anywhere (the library's own guard, not clang's: `return;` in a function with
a result is a GNU extension here).
*/
{
  return !c_language_ &&
         (callee == nullptr ||
          ((callee->flags & ECIL_DECL_FUNC_EXTERN_C) == 0 &&
           !callee->valueless_return));
}  /* a_emitter::strict_result */

mlir::DictionaryAttr a_emitter::pointer_attributes(uint64_t pointer_gid,
                                                  bool with_noundef)
/*
The attributes of a reference or `this`: noundef and nonnull; and, for a
complete pointee, dereferenceable(size) and align(alignment).  The size of a
class is its non-virtual size (the pointee may be a base subobject) and the
alignment its non-virtual alignment; a class without CLASS_INFO, and every
other object type, uses the complete size and alignment.  An incomplete
pointee has neither, a function pointee neither.  (The stream has no `final`:
for a final class the non-virtual size is a smaller, still valid, bound.)
*/
{
  llvm::SmallVector<mlir::NamedAttribute> attrs;
  const a_type *pointer = type_at(pointer_gid);
  auto name = [&](const char *text) {
    return mlir::StringAttr::get(ctx_, text);
  };

  if (pointer != nullptr && pointer->kind == a_type_kind::pointer_type) {
    const a_type *pointee = type_at(pointer->target);
    uint64_t size = 0;
    uint64_t align = 0;

    if (with_noundef)
      attrs.emplace_back(name("llvm.noundef"), unit_attr_);
    attrs.emplace_back(name("llvm.nonnull"), unit_attr_);
    if (pointee != nullptr && pointee->kind != a_type_kind::function_type &&
        pointee->kind != a_type_kind::void_type &&
        pointee->kind != a_type_kind::opaque_type &&
        type_is_complete(pointee->cir)) {
      if (pointee->kind == a_type_kind::record_type &&
          pointee->record->has_class_info) {
        size = pointee->record->nvsize;
        align = pointee->record->nvalign;
      } else {
        size = type_size(pointee->cir);
        align = type_align(pointee->cir);
      }
    }
    if (size != 0)
      attrs.emplace_back(name("llvm.dereferenceable"),
                         builder_.getI64IntegerAttr(
                             static_cast<int64_t>(size)));
    if (align != 0)
      attrs.emplace_back(name("llvm.align"),
                         builder_.getI64IntegerAttr(
                             static_cast<int64_t>(align)));
  }
  return mlir::DictionaryAttr::get(ctx_, attrs);
}  /* a_emitter::pointer_attributes */

void a_emitter::apply_signature_attributes()
/*
Put the argument and result attributes on every function and on the direct
calls of it.  Run at finish, when every class is complete and every body is
emitted.  A base constructor with a VTT takes it as a second parameter, which
shifts the parameters after `this` by one.  The result of a function is
noundef only under strict_result; the result of a direct call too, judged by
the callee (the callee's body is known only now).
*/
{
  for (a_entity *entity : entity_order_) {
    a_entity &e = *entity;
    const a_type *t = e.is_function && e.op != nullptr && e.type != 0
                          ? type_at(e.type) : nullptr;
    cir::FuncOp fn;
    llvm::SmallVector<mlir::Attribute> args;
    mlir::DictionaryAttr result;
    bool any = false;
    bool strict;

    if (t == nullptr || t->kind != a_type_kind::function_type)
      continue;
    fn = mlir::cast<cir::FuncOp>(e.op);
    strict = strict_result(&e);
    cir::FuncType fn_type = fn.getFunctionType();

    for (mlir::Type input : fn_type.getInputs())
      args.push_back(noundef_attributes(input));
    for (size_t i = 0; i < t->params.size(); ++i) {
      size_t position = i + (e.has_vtt && i >= 1 ? 1 : 0);

      if (((t->nonnull_mask >> (i + 1)) & 1) != 0 && position < args.size())
        args[position] = pointer_attributes(t->params[i], true);
    }
    for (mlir::Attribute a : args)
      any = any || !mlir::cast<mlir::DictionaryAttr>(a).empty();
    if (any)
      fn.setArgAttrsAttr(mlir::ArrayAttr::get(ctx_, args));
    if (!fn_type.hasVoidReturn()) {
      if ((t->nonnull_mask & 1) != 0)
        result = pointer_attributes(t->target, strict);
      else if (strict)
        result = noundef_attributes(fn_type.getReturnType());
      bool is_new = false;

      if (replaceable_operator(e, &is_new) && is_new)
        result = with_attributes(ctx_, result, new_result_attributes(e));
      if (result && !result.empty())
        fn.setResAttrsAttr(mlir::ArrayAttr::get(ctx_, {result}));
    }
  }
  /* The direct calls: the result attribute of the callee's kind (strict
     result), now that the callee's body is known.  (Walked, not taken from
     the entities' call lists: bodies were erased since.) */
  std::unordered_map<std::string_view, const a_entity *> by_name;

  for (a_entity *entity : entity_order_) {
    if (entity->is_function && entity->op != nullptr && entity->type != 0)
      by_name[std::string_view(mlir::cast<mlir::SymbolOpInterface>(entity->op)
                                   .getName())] = entity;
  }
  module_.walk([&](cir::CallOp call) {
    auto callee = call.getCalleeAttr();
    if (!callee)
      return;
    auto found = by_name.find(std::string_view(callee.getValue()));
    if (found == by_name.end())
      return;
    const a_entity *target = found->second;

    /* A call the library made itself (the operator delete of a deleting
       destructor, the call of a thunk or of a destructor from D0) has the
       argument attributes of the callee's signature, which is what CIRGen's
       call sites carry (noundef, and nonnull, dereferenceable, align of a
       reference or `this`). */
    if (!call.getArgAttrsAttr()) {
      auto fn = mlir::cast<cir::FuncOp>(target->op);

      if (fn.getArgAttrsAttr() &&
          fn.getFunctionType().getNumInputs() == call.getNumOperands())
        call.setArgAttrsAttr(fn.getArgAttrsAttr());
    }
    if (call.getNumResults() != 1)
      return;
    bool is_new = false;
    const a_type *t = type_at(target->type);
    mlir::DictionaryAttr one;

    if (replaceable_operator(*target, &is_new) && is_new &&
        !call->hasAttr(allocsize_name_))
      call->setAttr(allocsize_name_, builder_.getDenseI32ArrayAttr({0}));
    if (call.getResAttrs())
      return;
    if (strict_result(target))
      one = t != nullptr && (t->nonnull_mask & 1) != 0
                ? pointer_attributes(t->target, true)
                : noundef_attributes(call.getResult().getType());
    if (is_new)
      one = with_attributes(ctx_, one, new_result_attributes(*target));
    if (one && !one.empty())
      call.setResAttrsAttr(mlir::ArrayAttr::get(ctx_, {one}));
  });
  /* A thunk has noundef on its scalar parameters and result (and no
     nonnull, dereferenceable or align: its `this` is a plain adjusted
     pointer), and its target's inline hint and nounwind (clang builds the
     thunk from the target's declaration). */
  for (const auto &pair : thunk_pairs_) {
    a_entity *thunk = pair.first;
    a_entity *target = pair.second;

    /* The complete variant of a structor pair that is an alias (or was
       redirected) has no function of its own: the base variant has the
       facts. */
    if (target->op == nullptr) {
      for (const a_structor_pair &structor : structors_) {
        if (structor.complete == target)
          target = structor.base;
      }
    }
    if (thunk->op == nullptr || target->op == nullptr)
      continue;
    auto thunk_fn = mlir::cast<cir::FuncOp>(thunk->op);
    auto target_fn = mlir::cast<cir::FuncOp>(target->op);

    {
      cir::FuncType type = thunk_fn.getFunctionType();
      llvm::SmallVector<mlir::Attribute> args;

      for (mlir::Type input : type.getInputs())
        args.push_back(noundef_attributes(input));
      thunk_fn.setArgAttrsAttr(mlir::ArrayAttr::get(ctx_, args));
      /* (The result is noundef only where a call's would be: a GNU
         value-less `return;` in the target must not hand an undefined value
         to a noundef return.) */
      if (!type.hasVoidReturn() && strict_result(target)) {
        mlir::DictionaryAttr result = noundef_attributes(type.getReturnType());

        if (!result.empty())
          thunk_fn.setResAttrsAttr(mlir::ArrayAttr::get(ctx_, {result}));
      }
    }
    if (target_fn.getInlineKind() == cir::InlineKind::InlineHint)
      thunk_fn.setInlineKind(cir::InlineKind::InlineHint);
    if (target_fn->hasAttr(nothrow_name_))
      thunk_fn->setAttr(nothrow_name_, unit_attr_);
  }
}  /* a_emitter::apply_signature_attributes */

void a_emitter::call_signature_attributes(cir::CallOp op,
                                          const a_type &call_type,
                                          const a_entity *callee,
                                          bool with_vtt)
/*
The argument attributes of a call: noundef for every scalar argument
(variadic ones too), and for a reference or `this` marked in the call's
function type nonnull, dereferenceable and align as well.  An indirect call
also gets the result attributes here (noundef, or those of a reference); the
result of a direct call gets them at finish, when the callee's body is known.
*/
{
  llvm::SmallVector<mlir::Attribute> args;
  bool any = false;

  for (mlir::Value argument : op.getArgOperands())
    args.push_back(noundef_attributes(argument.getType()));
  for (size_t i = 0; i < call_type.params.size(); ++i) {
    size_t position = i + (with_vtt && i >= 1 ? 1 : 0);

    if (((call_type.nonnull_mask >> (i + 1)) & 1) != 0 &&
        position < args.size())
      args[position] = pointer_attributes(call_type.params[i], true);
  }
  for (mlir::Attribute a : args)
    any = any || !mlir::cast<mlir::DictionaryAttr>(a).empty();
  if (any)
    op.setArgAttrsAttr(mlir::ArrayAttr::get(ctx_, args));
  if (callee == nullptr && op.getNumResults() == 1 && strict_result(nullptr)) {
    mlir::DictionaryAttr one =
        (call_type.nonnull_mask & 1) != 0
            ? pointer_attributes(call_type.target, true)
            : noundef_attributes(op.getResult().getType());

    if (!one.empty())
      op.setResAttrsAttr(mlir::ArrayAttr::get(ctx_, {one}));
  }
}  /* a_emitter::call_signature_attributes */

}  /* namespace edg_cir */
