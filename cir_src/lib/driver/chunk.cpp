/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
chunk.cpp -- chunked lowering of the CIR module (design (a), see chunk.h and
doc/research/m5_design.md section 3).

What makes a chunk lowerable on its own, and what is put right after the
merge (every item was found by a failing case in the L3 spike):

- A chunk declares the symbols its operations name.  The names are found
  by a walk over the attributes of every nested operation (symbol_uses.h):
  mlir::SymbolTable::getSymbolUses does not see the calls inside cir.func
  nor nested references such as #cir.method.
- A symbol with internal or private linkage is made external while the
  chunks are lowered (a chunk cannot see another chunk's internal symbol)
  and gets its linkage back in the merged module; its declarations are
  dso_local, else an access to a private constant would go through the GOT.
- An available_externally function (S2) is made external for the same
  reason: llvm::Linker does not link the body of a function that is only an
  available_externally definition (it counts as a declaration) over a
  declaration.  It gets the linkage back after the merge, with no comdat and
  without dso_local.
- A function alias cannot cross a link boundary: the aliasee attribute is
  stripped and the llvm::GlobalAlias is created in the merged module.
- The linker keeps the first definition of a comdat group, so the members
  of a group split over chunks would disappear: the comdat of every function
  body is stripped and re-attached after the merge.
- The module attributes cir.global_ctors and cir.global_dtors belong to the
  globals chunk only.
- Lowering cir.construct_catch_param inlines the body of its catch-copy
  thunk, so every chunk that names a thunk gets a clone of it with its body.
- The globals chunk is lowered and linked first.

A chunk that fails to lower is diagnosed.  There is no in-process fallback
to the whole module: the CIR of a lowered chunk has been erased.
*/

#include <algorithm>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/raw_ostream.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"

#include "chunk.h"
#include "cir_module.h"
#include "symbol_uses.h"

namespace edg_cir {

namespace {

cir::GlobalLinkageKind linkage_of(mlir::Operation *op)
/* The linkage of a cir.func or cir.global. */
{
  cir::GlobalLinkageKind result;

  if (auto fn = mlir::dyn_cast<cir::FuncOp>(op))
    result = fn.getLinkage();
  else
    result = mlir::cast<cir::GlobalOp>(op).getLinkage();
  return result;
}  /* linkage_of */

void set_linkage_of(mlir::Operation *op, cir::GlobalLinkageKind kind)
/* Set the linkage of a cir.func or cir.global. */
{
  if (auto fn = mlir::dyn_cast<cir::FuncOp>(op))
    fn.setLinkage(kind);
  else
    mlir::cast<cir::GlobalOp>(op).setLinkage(kind);
}  /* set_linkage_of */

llvm::GlobalValue::LinkageTypes alias_linkage(cir::GlobalLinkageKind kind)
/* The LLVM linkage of an alias of a function with the given CIR linkage. */
{
  llvm::GlobalValue::LinkageTypes result =
      llvm::GlobalValue::ExternalLinkage;

  switch (kind) {
  case cir::GlobalLinkageKind::LinkOnceODRLinkage:
    result = llvm::GlobalValue::LinkOnceODRLinkage;
    break;
  case cir::GlobalLinkageKind::WeakODRLinkage:
    result = llvm::GlobalValue::WeakODRLinkage;
    break;
  case cir::GlobalLinkageKind::InternalLinkage:
    result = llvm::GlobalValue::InternalLinkage;
    break;
  default:
    break;
  }
  return result;
}  /* alias_linkage */

struct an_alias {
  mlir::StringAttr        name;
  mlir::StringAttr        target;
  cir::GlobalLinkageKind  linkage;
};  /* an_alias */

class a_chunker {
public:
  a_chunker(an_edg_cir_session &session, mlir::ModuleOp module,
            llvm::LLVMContext &llvm_context, const a_lower_function &lower)
    : session_(session), cm_(*session.module), module_(module),
      llvm_context_(llvm_context), lower_(lower),
      size_(session.stats.chunk_size), ok_(true), index_(0),
      location_(mlir::UnknownLoc::get(&cm_.context)) {}

  std::unique_ptr<llvm::Module> run();

private:
  void scan();
  void copy_attributes(mlir::ModuleOp to, bool first) const;
  void add_declarations(mlir::ModuleOp chunk);
  void lower_chunk(mlir::ModuleOp chunk);
  void restore();

  an_edg_cir_session &session_;
  a_cir_module       &cm_;
  mlir::ModuleOp      module_;
  llvm::LLVMContext  &llvm_context_;
  const a_lower_function &lower_;
  unsigned            size_;
  bool                ok_;
  size_t              index_;
  mlir::Location      location_;
  size_t              total_ = 0;       /* Chunks, the globals chunk too. */

  /* A declaration of every symbol, made before anything moves. */
  llvm::DenseMap<mlir::StringAttr, mlir::Operation *> templates_;
  /* The catch-copy thunks, with their bodies. */
  llvm::DenseMap<mlir::StringAttr, mlir::Operation *> inline_bodies_;
  std::vector<mlir::Operation *> bodies_;
  std::vector<mlir::Operation *> rest_;
  /* To put right in the merged module. */
  std::vector<std::pair<mlir::StringAttr, cir::GlobalLinkageKind>> locals_;
  std::vector<mlir::StringAttr> available_;
  std::vector<an_alias> aliases_;
  std::vector<std::pair<mlir::StringAttr, std::string>> comdats_;
  llvm::DenseSet<mlir::StringAttr> alias_targets_;
  std::unique_ptr<llvm::Module> merged_;
  /* The global variables in the order the globals chunk had them (the
     order of the whole module). */
  std::vector<std::string> global_order_;
};  /* a_chunker */

void a_chunker::scan()
/*
Classify the operations into function bodies and the rest, make a
declaration template of every symbol, and strip the attributes that cannot
cross a link boundary (see the file comment).
*/
{
  mlir::MLIRContext *ctx = &cm_.context;
  mlir::StringAttr dso_local = mlir::StringAttr::get(ctx, "dso_local");
  mlir::StringAttr comdat = mlir::StringAttr::get(ctx, "comdat");
  mlir::StringAttr aliasee = mlir::StringAttr::get(ctx, "aliasee");
  mlir::StringAttr initial = mlir::StringAttr::get(ctx, "initial_value");
  mlir::StringAttr thunk = mlir::StringAttr::get(ctx,
                                                 "cir.eh.catch_copy_thunk");

  for (mlir::Operation &op : *module_.getBody()) {
    mlir::StringAttr name;
    bool is_body = false;

    if (auto fn = mlir::dyn_cast<cir::FuncOp>(&op)) {
      name = fn.getSymNameAttr();
      if (fn.getAliasee()) {
        aliases_.push_back({name, mlir::StringAttr::get(ctx, *fn.getAliasee()),
                            fn.getLinkage()});
        op.removeAttr(aliasee);
      }
      if (!fn.isDeclaration() && op.getAttr(comdat)) {
        auto text = mlir::dyn_cast<mlir::StringAttr>(op.getAttr(comdat));

        comdats_.emplace_back(name, text && !text.getValue().empty()
                                        ? text.getValue().str()
                                        : name.getValue().str());
        op.removeAttr(comdat);
      }
      is_body = !fn.isDeclaration();
    } else if (auto global = mlir::dyn_cast<cir::GlobalOp>(&op)) {
      name = global.getSymNameAttr();
      if (global.getAliasee())
        alias_targets_.insert(
            mlir::StringAttr::get(ctx, *global.getAliasee()));
    } else {
      rest_.push_back(&op);
      continue;
    }
    (is_body ? bodies_ : rest_).push_back(&op);
    if (op.hasAttr(thunk))
      inline_bodies_[name] = op.clone();
    mlir::Operation *decl = op.cloneWithoutRegions();

    decl->removeAttr(comdat);
    decl->removeAttr(aliasee);
    decl->removeAttr(initial);
    cir::GlobalLinkageKind kind = linkage_of(&op);

    /* (A weak declaration stays weak: the code must not assume the symbol
       is defined.) */
    if (kind != cir::GlobalLinkageKind::ExternalWeakLinkage)
      set_linkage_of(decl, cir::GlobalLinkageKind::ExternalLinkage);
    /* A declaration is private (the verifier rejects a public one), as
       the declarations the emitter makes. */
    mlir::SymbolTable::setSymbolVisibility(
        decl, mlir::SymbolTable::Visibility::Private);
    templates_[name] = decl;

    if (kind == cir::GlobalLinkageKind::InternalLinkage ||
        kind == cir::GlobalLinkageKind::PrivateLinkage) {
      locals_.emplace_back(name, kind);
      decl->setAttr(dso_local, mlir::UnitAttr::get(ctx));
      set_linkage_of(&op, cir::GlobalLinkageKind::ExternalLinkage);
    } else if (kind == cir::GlobalLinkageKind::AvailableExternallyLinkage) {
      available_.push_back(name);
      decl->removeAttr(dso_local);
      op.removeAttr(dso_local);
      set_linkage_of(&op, cir::GlobalLinkageKind::ExternalLinkage);
    }
  }
  /* A global alias and its aliasee share a module: the aliasees of globals
     are never function bodies. */
  std::vector<mlir::Operation *> keep;

  for (mlir::Operation *op : bodies_) {
    mlir::StringAttr name = mlir::cast<cir::FuncOp>(op).getSymNameAttr();

    (alias_targets_.count(name) != 0 ? rest_ : keep).push_back(op);
  }
  bodies_.swap(keep);
}  /* a_chunker::scan */

void a_chunker::copy_attributes(mlir::ModuleOp to, bool first) const
/*
Give a chunk the module attributes of the module; the constructor and
destructor lists and the module-level inline assembly belong to the globals
chunk (llvm::Linker concatenates the module asm of the chunks, so file-scope
asm would be repeated once per chunk).

Not done: with debug information every chunk would make its own
DICompileUnit.  The library emits none today.
*/
{
  for (mlir::NamedAttribute attr : module_->getAttrs()) {
    if (!first &&
        (attr.getName() == cir::CIRDialect::getGlobalCtorsAttrName() ||
         attr.getName() == cir::CIRDialect::getGlobalDtorsAttrName() ||
         attr.getName() == cir::CIRDialect::getModuleLevelAsmAttrName()))
      continue;
    to->setAttr(attr.getName(), attr.getValue());
  }
}  /* a_chunker::copy_attributes */

void a_chunker::add_declarations(mlir::ModuleOp chunk)
/*
Declare in the chunk every symbol its operations name that it does not
define; a catch-copy thunk is cloned with its body (and what the clone
names).
*/
{
  llvm::DenseSet<mlir::StringAttr> defined;
  llvm::DenseSet<mlir::StringAttr> declared;
  std::vector<mlir::StringAttr> names;
  auto collect = [&](mlir::Operation *root) {
    for_each_symbol_use(root, [&](mlir::Operation *, mlir::StringAttr name) {
      names.push_back(name);
    });
  };

  for (mlir::Operation &op : *chunk.getBody()) {
    if (auto symbol = mlir::dyn_cast<mlir::SymbolOpInterface>(&op))
      defined.insert(symbol.getNameAttr());
  }
  collect(chunk.getOperation());
  for (size_t at = 0; at < names.size(); ++at) {
    mlir::StringAttr name = names[at];

    if (defined.count(name) != 0 || !declared.insert(name).second)
      continue;
    auto thunk = inline_bodies_.find(name);
    auto found = templates_.find(name);

    if (thunk != inline_bodies_.end()) {
      mlir::Operation *copy = thunk->second->clone();

      chunk.getBody()->push_back(copy);
      collect(copy);
    } else if (found != templates_.end()) {
      chunk.getBody()->push_back(found->second->clone());
    }
  }
}  /* a_chunker::add_declarations */

void a_chunker::lower_chunk(mlir::ModuleOp chunk)
/*
Lower one chunk, erase its CIR and link the result into the merged module.
*/
{
  std::unique_ptr<llvm::Module> lowered = lower_(chunk);

#if EDG_CIR_TEST_HOOKS
  /* Test hook: chunk N (1 is the globals chunk) fails to lower. */
  if (const char *fail = std::getenv("EDG_CIR_TEST_CHUNK_FAIL")) {
    if (static_cast<size_t>(std::atoi(fail)) == index_ + 1)
      lowered.reset();
  }
#endif
  if (!lowered) {
    session_.fail(EDG_CIR_ERROR_PIPELINE, edc_pipeline,
                  "lowering chunk " + std::to_string(index_ + 1) + " of " +
                  std::to_string(total_) + " to LLVM IR failed");
    ok_ = false;
  } else {
    lowered->setTargetTriple(cm_.triple);
    lowered->setDataLayout(cm_.layout);
#if EDG_CIR_TEST_HOOKS
    /* Test hook: print every lowered chunk (before the link). */
    if (std::getenv("EDG_CIR_CHUNK_DUMP") != nullptr)
      lowered->print(llvm::errs(), nullptr);
#endif
    chunk.erase();
    if (!merged_) {
      merged_ = std::move(lowered);
      for (const llvm::GlobalVariable &global : merged_->globals())
        global_order_.push_back(global.getName().str());
    } else if (llvm::Linker::linkModules(*merged_, std::move(lowered))) {
      session_.fail(EDG_CIR_ERROR_PIPELINE, edc_pipeline,
                    "linking chunk " + std::to_string(index_ + 1) + " of " +
                    std::to_string(total_) + " failed");
      ok_ = false;
    }
#if defined(__GLIBC__)
    /* The heap keeps what the erased chunk held: hand it back. */
    malloc_trim(0);
#endif
  }
  ++index_;
  session_.stats.chunks = index_;
}  /* a_chunker::lower_chunk */

void a_chunker::restore()
/*
Put right the merged module: comdats, aliases, linkage of the local and
available_externally functions, the order of the global variables and of
the functions the lowering made (llvm::Linker moves the variables a source
module names to the end of the destination, in the source's order; the
whole module has them in the order of the globals chunk, which is the code
generator's data order).
*/
{
  {
    std::vector<llvm::GlobalVariable *> ordered;
    llvm::DenseSet<llvm::GlobalVariable *> placed;
    std::vector<llvm::GlobalVariable *> added;

    for (const std::string &name : global_order_) {
      if (llvm::GlobalVariable *global = merged_->getNamedGlobal(name)) {
        ordered.push_back(global);
        placed.insert(global);
      }
    }
    for (llvm::GlobalVariable &global : merged_->globals()) {
      if (placed.count(&global) == 0)
        added.push_back(&global);
    }
    ordered.insert(ordered.end(), added.begin(), added.end());
    for (llvm::GlobalVariable *global : ordered) {
      global->removeFromParent();
      merged_->insertGlobalVariable(global);
    }
  }
  {
    /* The definitions the lowering makes (__clang_call_terminate) come
       last in the whole module; here each chunk made its own and the first
       stayed where the link put it. */
    std::vector<llvm::Function *> made;

    for (llvm::Function &fn : *merged_) {
      if (!fn.isDeclaration() &&
          templates_.count(
              mlir::StringAttr::get(&cm_.context, fn.getName())) == 0)
        made.push_back(&fn);
    }
    for (llvm::Function *fn : made) {
      fn->removeFromParent();
      merged_->getFunctionList().push_back(fn);
    }
  }
  for (const auto &item : comdats_) {
    if (llvm::Function *fn = merged_->getFunction(item.first.getValue())) {
      if (!fn->isDeclaration() && !fn->hasAvailableExternallyLinkage())
        fn->setComdat(merged_->getOrInsertComdat(item.second));
    }
  }
  for (const an_alias &alias : aliases_) {
    llvm::GlobalValue *decl = merged_->getNamedValue(alias.name.getValue());
    llvm::GlobalValue *target =
        merged_->getNamedValue(alias.target.getValue());

    if (decl != nullptr && target != nullptr &&
        llvm::isa<llvm::Function>(decl)) {
      std::string name = decl->getName().str();
      llvm::GlobalAlias *created = llvm::GlobalAlias::create(
          llvm::cast<llvm::Function>(decl)->getFunctionType(), 0,
          alias_linkage(alias.linkage), "", target, merged_.get());

      created->setVisibility(decl->getVisibility());
      created->setDSOLocal(decl->isDSOLocal());
      created->setUnnamedAddr(decl->getUnnamedAddr());
      decl->replaceAllUsesWith(created);
      decl->eraseFromParent();
      created->setName(name);
    }
  }
  for (const auto &item : locals_) {
    llvm::GlobalValue *gv = merged_->getNamedValue(item.first.getValue());

    if (gv != nullptr && !gv->isDeclaration())
      gv->setLinkage(item.second == cir::GlobalLinkageKind::InternalLinkage
                         ? llvm::GlobalValue::InternalLinkage
                         : llvm::GlobalValue::PrivateLinkage);
  }
  for (mlir::StringAttr name : available_) {
    llvm::GlobalValue *gv = merged_->getNamedValue(name.getValue());

    if (gv != nullptr && !gv->isDeclaration()) {
      gv->setLinkage(llvm::GlobalValue::AvailableExternallyLinkage);
      gv->setDSOLocal(false);
    }
  }
}  /* a_chunker::restore */

std::unique_ptr<llvm::Module> a_chunker::run()
/*
Cut the module into the globals chunk and the chunks of function bodies,
lower and link them in that order, and put the merged module right.
*/
{
  std::unique_ptr<llvm::Module> result;

  scan();
  total_ = 1 + (bodies_.size() + size_ - 1) / size_;
  {
    /* The globals chunk: everything that is not a function body, and a
       declaration of every body function (the functions keep their
       places in the merged module). */
    mlir::ModuleOp chunk = mlir::ModuleOp::create(location_);

    copy_attributes(chunk, true);
    for (mlir::Operation *op : rest_)
      op->moveBefore(chunk.getBody(), chunk.getBody()->end());
    for (mlir::Operation *op : bodies_) {
      mlir::StringAttr name = mlir::cast<cir::FuncOp>(op).getSymNameAttr();

      chunk.getBody()->push_back(templates_.find(name)->second->clone());
    }
    add_declarations(chunk);
    lower_chunk(chunk);
  }
  for (size_t at = 0; ok_ && at < bodies_.size(); at += size_) {
    mlir::ModuleOp chunk = mlir::ModuleOp::create(location_);

    copy_attributes(chunk, false);
    for (size_t k = at; k < std::min(bodies_.size(), at + size_); ++k)
      bodies_[k]->moveBefore(chunk.getBody(), chunk.getBody()->end());
    add_declarations(chunk);
    lower_chunk(chunk);
  }
  for (auto &item : templates_)
    item.second->erase();
  for (auto &item : inline_bodies_)
    item.second->erase();
  if (ok_ && merged_) {
    restore();
    result = std::move(merged_);
  }
  return result;
}  /* a_chunker::run */

}  /* anonymous namespace */

bool chunking_wanted(const an_edg_cir_session &session, uint64_t bodies)
/*
Whether the module is lowered in chunks: it has at least chunk_threshold
function bodies (1 forces chunking whenever there is one; UINT32_MAX never
chunks).  The count is the emitter's (no walk over the module: below the
threshold chunking costs nothing).
*/
{
  uint32_t threshold = session.chunk_threshold == 0
                           ? k_default_chunk_threshold
                           : session.chunk_threshold;

  return threshold != k_chunk_never && bodies >= threshold;
}  /* chunking_wanted */

std::unique_ptr<llvm::Module> lower_in_chunks(
    an_edg_cir_session &session, mlir::ModuleOp module,
    llvm::LLVMContext &llvm_context, const a_lower_function &lower)
/*
Lower the module in chunks (see the file comment); null if a chunk failed
(the failure is reported).
*/
{
  a_chunker chunker(session, module, llvm_context, lower);

  return chunker.run();
}  /* lower_in_chunks */

}  /* namespace edg_cir */
