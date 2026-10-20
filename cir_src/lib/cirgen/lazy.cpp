/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
lazy.cpp -- lazy emission of function bodies.

A function that every translation unit that needs it defines itself (internal
or linkonce_odr linkage) is emitted only if the program can reach it: its
records are validated on arrival and kept (deferred), and at finish the
reachable ones are replayed through the emitter in stream order into the
cir.func that its DECL_FUNC created.  What nothing reaches is never emitted;
GlobalDCE would have erased it, after the library had paid for its emission,
verification and lowering.  EDG_CIR_FLAG_EAGER_BODIES keeps the eager
behavior.

Finish runs the phases in this order (lazy_finish, then finish_classes and
finish_entities as before):
  1. the functions each class's vtable group and VTT can name, from the
     shared class facts, without emitting anything;
  2. the reachability closure: the roots, the symbol uses of the module as
     emitted so far, and the edges of the deferred bodies;
  3. the replay of the reachable bodies, in stream order.
*/

#include <algorithm>
#include <cstdlib>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "abi_facts.h"
#include "ecil_abi_decode.h"
#include "emitter.h"
#include "symbol_uses.h"

namespace edg_cir {

namespace {

class a_replay_sink : public ecil::a_stream_handler {
public:
  explicit a_replay_sink(a_emitter &emitter) : emitter_(emitter) {}
  bool on_preamble(const ecil::a_preamble &) override
    { return true; }
  bool on_frame_begin(const ecil::a_frame_info &) override
    { return true; }
  bool on_record(const ecil::a_record &record,
                 const ecil::a_record_context &context) override
    { return emitter_.on_record(record, context); }
  bool on_frame_end(const ecil::a_frame_info &) override
    { return true; }
  bool on_extension_frame(const ecil::a_frame_info &) override
    { return true; }
private:
  a_emitter &emitter_;
};  /* a_replay_sink */

}  /* anonymous namespace */

bool a_emitter::lazy_can_defer(const a_entity &e,
                               const ecil::a_record_context &context) const
/*
The deferral rule: a function of a function-region frame that every
translation unit that needs it defines itself (internal or linkonce_odr),
that is not `used` (GNU used: kept even when nothing names it) and has no
dropped definition.  A C99 inline-only definition (no external definition in
this translation unit) counts as discardable: every translation unit that
calls it must have a definition of its own.  Its entity must not have a body
already.
*/
{
  return e.is_function && !e.body_arrived && !e.used &&
         context.frame != nullptr && context.frame->kind == efk_region &&
         (e.linkage == elk_internal || e.linkage == elk_private ||
          e.linkage == elk_linkonce_odr ||
          (e.flags & ECIL_DECL_FUNC_INLINE_ONLY) != 0) &&
         (e.flags & ECIL_DECL_FUNC_UNSUPPORTED) == 0;
}  /* a_emitter::lazy_can_defer */

void a_emitter::lazy_begin(const ecil::a_record &r, a_entity &e,
                           const ecil::a_record_context &context)
/*
FUNC_BEGIN of a deferred function: remember where its records start, and
collect the G-ids that the validator looks up while it reads them.  The
entity has no body yet (has_body) but the stream's own checks must treat it
as defined (body_arrived).
*/
{
  deferred_.emplace_back();
  a_deferred_fn &d = deferred_.back();

  d.frame = *context.frame;
  d.frame.payload = nullptr;
  ecil::operand_u64(r.ops[0], d.gid);
  d.entity = &e;
  d.base_offset = context.frame->offset + ECIL_FRAME_HEADER_SIZE +
                  static_cast<uint64_t>(r.start - context.frame->payload);
  e.body_arrived = true;
  e.deferred = static_cast<int64_t>(deferred_.size() - 1);
  deferring_ = &d;
  defer_begin_ = r.start;
  skipping_ = true;
  sink_refs_.clear();
  ids_.ref_sink = &sink_refs_;
  session_.stats.lazy_deferred += 1;
}  /* a_emitter::lazy_begin */

void a_emitter::lazy_note(const ecil::a_record &r)
/*
A record of a deferred body: note what finish needs to know about it (a
vptr store or VTT use names the vtable and VTT of the class; an aggregate of
a class type names its vtable; a static local's initializer ties the
variable to this function); at FUNC_END keep the bytes.
*/
{
  a_deferred_fn &d = *deferring_;
  uint64_t g = 0;

  switch (r.opcode) {
  case eop_vptr_init:
    d.vptr_init = true;
    break;
  case eop_vtt_addr:
    d.vtt_use = true;
    break;
  case eop_c_agg_begin:
    if (ecil::operand_u64(r.ops[0], g))
      d.agg_classes.push_back(g);
    break;
  case eop_static_init_begin:
    if (ecil::operand_u64(r.ops[0], g))
      d.statics.push_back(g);
    break;
  case eop_call:
    if ((r.ops[3].u & ECIL_CALL_VTT) != 0)
      d.vtt_use = true;
    break;
  case eop_const_method:
  case eop_c_method:
    /* The member function of a method-pointer constant. */
    if (ecil::operand_u64(r.ops[1], g) && g < 0xFFFFFFFFull)
      sink_refs_.push_back(static_cast<uint32_t>(g));
    break;
  default:
    break;
  }
  if (r.opcode == eop_func_end) {
    d.bytes.assign(defer_begin_, r.start + r.size);
    std::sort(sink_refs_.begin(), sink_refs_.end());
    sink_refs_.erase(std::unique(sink_refs_.begin(), sink_refs_.end()),
                     sink_refs_.end());
    d.refs = sink_refs_;
    d.this_class = session_.reader->last_function_info().cls;
    ids_.ref_sink = nullptr;
    session_.stats.lazy_bytes += d.bytes.size();
    deferring_ = nullptr;
    skipping_ = false;
  }
}  /* a_emitter::lazy_note */

bool a_emitter::replay_function(a_deferred_fn &d)
/*
Emit a deferred body from its saved records.  The location of the stream
(for the diagnostics of finish) is saved and restored around it.
*/
{
  bool ok = true;

  if (!d.replayed) {
    uint64_t start = session_.stats_on ? stats_now_ns() : 0;
    std::string saved_file = session_.last_file;
    uint32_t saved_line = session_.last_line;
    uint32_t saved_column = session_.last_column;
    mlir::Location saved_loc = loc_;
    a_replay_sink sink(*this);

    d.replayed = true;
    replaying_ = true;
    ok = session_.reader->replay_body(d.bytes.data(),
                                      d.bytes.data() + d.bytes.size(),
                                      d.base_offset, d.frame, sink);
    replaying_ = false;
    cur_context_ = nullptr;   /* It pointed into the replay's own stack. */
    session_.last_file = saved_file;
    session_.last_line = saved_line;
    session_.last_column = saved_column;
    loc_ = saved_loc;
    if (!ok && session_.status == EDG_CIR_OK)
      session_.fail(EDG_CIR_ERROR_INTERNAL, edc_internal,
                    "replay of a deferred body failed: " +
                    session_.reader->error().message);
    if (ok && (in_function_ || !frames_.empty()))
      ok = internal("a replayed body left a function open");
    session_.stats.lazy_replayed += 1;
    if (session_.stats_on)
      session_.stats.lazy_replay_ns += stats_now_ns() - start;
  }
  return ok && session_.status == EDG_CIR_OK;
}  /* a_emitter::replay_function */

bool a_emitter::inline_only_declaration(const a_entity &e) const
/*
A C99 inline-only function that stays a declaration: at -O0, and a
`noinline` one (at -O1 and up the body of another is kept as an
available_externally definition, for the inliner; a trivially recursive one
is dropped at finish, see finish_entities).
*/
{
  return (session_.opt_level <= 0 || e.no_inline) &&
         (e.flags & ECIL_DECL_FUNC_INLINE_ONLY) != 0;
}  /* a_emitter::inline_only_declaration */

bool a_emitter::lazy_finish()
/*
Phases 1 to 3 of the design: the function sets of the classes' vtable
groups, the reachability closure (roots, the symbol uses of the emitted
module, the edges of the deferred bodies) and the replay of the reachable
bodies in stream order.  A body that is not reachable stays a shell (a
declaration) that finish_entities and lazy_erase_dropped dispose of.
*/
{
  bool ok = true;
  uint64_t start = session_.stats_on ? stats_now_ns() : 0;
  std::unordered_map<const a_entity *, std::vector<a_entity *>> extra;
  std::vector<a_entity *> extra_roots;
  std::unordered_set<const a_entity *> live;
  std::unordered_map<const a_entity *, std::vector<a_entity *>> uses;
  std::unordered_map<mlir::Operation *, a_entity *> by_op;
  edg_abi::a_class_db db;
  std::map<uint64_t, edg_abi::a_class_id> by_key;
  std::string problem;
  bool have_db = false;

  for (a_entity *entity : entity_order_) {
    if (entity->op != nullptr)
      by_op[entity->op] = entity;
  }
  if (ecil::build_abi_facts(ids_, db, problem)) {
    have_db = true;
    for (edg_abi::a_class_id c = 0; c < db.classes.size(); ++c)
      by_key[db.classes[c].key] = c;
  }
  auto owner_of = [&](mlir::Operation *op) -> a_entity * {
    mlir::Operation *top = op;
    a_entity *result = nullptr;

    while (top != nullptr && top->getParentOp() != module_.getOperation())
      top = top->getParentOp();
    auto found = by_op.find(top);
    if (found != by_op.end())
      result = found->second;
    return result;
  };
  auto entity_of_gid = [&](uint64_t g) -> a_entity * {
    const ecil::a_gid_entry *entry = ids_.find_gid(g);
    a_entity *result = nullptr;

    if (entry != nullptr && entry->payload != nullptr) {
      if (entry->kind == ecil::gk_func || entry->kind == ecil::gk_var)
        result = static_cast<a_entity *>(entry->payload);
      else if (entry->kind == ecil::gk_abi_data)
        result = static_cast<a_abi_entity *>(entry->payload)->entity;
    }
    return result;
  };
  /* The functions a class's vtable group (and VTT) can name: its own virtual
     functions, their final overriders and the same of every base, with the
     deleting destructors' functions. */
  auto class_functions = [&](uint64_t class_gid,
                             std::vector<a_entity *> &out) {
    auto root = by_key.find(class_gid);
    std::vector<edg_abi::a_class_id> stack;
    std::unordered_set<edg_abi::a_class_id> seen;

    if (!have_db || root == by_key.end())
      return;
    stack.push_back(root->second);
    while (!stack.empty()) {
      edg_abi::a_class_id c = stack.back();
      const edg_abi::a_class_fact &cf = db.cls(c);

      stack.pop_back();
      if (!seen.insert(c).second)
        continue;
      auto add_fn = [&](edg_abi::a_fn_id f) {
        const edg_abi::a_fn_fact &fn = db.fn(f);
        a_entity *e = function_of_key(fn.key);
        uint32_t d0 = 0;
        uint32_t del = 0;

        if (e != nullptr)
          out.push_back(e);
        if (fn.special == edg_abi::fs_destructor &&
            ids_.deleting_of(static_cast<uint32_t>(fn.key), d0, del)) {
          if (a_entity *d = function_of_key(d0))
            out.push_back(d);
          if (a_entity *o = entity_of_gid(del))
            out.push_back(o);
        }
      };
      for (edg_abi::a_fn_id f : cf.fns)
        add_fn(f);
      for (const edg_abi::a_override_fact &ov : cf.overrides) {
        add_fn(ov.fn);
        add_fn(ov.ovr);
      }
      for (const edg_abi::a_base_fact &b : cf.bases)
        stack.push_back(b.cls);
      for (const edg_abi::a_base_fact &b : cf.vbases)
        stack.push_back(b.cls);
    }
  };

  /* The edges of the deferred bodies. */
  for (a_deferred_fn &d : deferred_) {
    std::vector<a_entity *> &to = extra[d.entity];

    for (uint32_t g : d.refs) {
      if (a_entity *target = entity_of_gid(g))
        to.push_back(target);
    }
    for (uint64_t g : d.agg_classes) {
      if (a_abi_entity *abi = abi_entity_of(eadk_vtable, g))
        to.push_back(abi->entity);
    }
    if ((d.vptr_init || d.vtt_use) && d.this_class != 0) {
      for (unsigned kind : {static_cast<unsigned>(eadk_vtable),
                            static_cast<unsigned>(eadk_vtt)}) {
        if (a_abi_entity *abi = abi_entity_of(kind, d.this_class))
          to.push_back(abi->entity);
      }
    }
    /* A static local the function initializes is used only through the
       function (its guard comes from the function's STATIC_INIT): when the
       variable is needed the function is. */
    for (uint64_t g : d.statics) {
      if (a_entity *local = entity_of_gid(g))
        extra[local].push_back(d.entity);
    }
  }
  /* The vtable and VTT of a class name the functions of its group. */
  for (a_abi_entity *abi : abi_order_) {
    if (abi->kind == eadk_vtable || abi->kind == eadk_vtt)
      class_functions(abi->type, extra[abi->entity]);
  }
  /* Pending placeholder uses of the emitted code. */
  for (const a_vptr_use &use : vptr_uses_) {
    a_abi_entity *vtable = abi_entity_of(eadk_vtable, use.vtable_class);
    a_entity *owner = owner_of(use.op);

    if (vtable != nullptr) {
      if (owner != nullptr)
        extra[owner].push_back(vtable->entity);
      else
        extra_roots.push_back(vtable->entity);
    }
  }
  for (const a_late_use &use : late_uses_) {
    if (use.names_vtt) {
      a_abi_entity *vtt = abi_entity_of(eadk_vtt, use.owner);
      a_entity *owner = owner_of(use.op);

      if (vtt != nullptr) {
        if (owner != nullptr)
          extra[owner].push_back(vtt->entity);
        else
          extra_roots.push_back(vtt->entity);
      }
    }
  }
  for (const a_vptr_marker &marker : vptr_markers_) {
    if (a_abi_entity *vtable = abi_entity_of(eadk_vtable,
                                             marker.vtable_class))
      extra_roots.push_back(vtable->entity);
  }
  for (a_deleting_dtor &dd : deleting_dtors_) {
    if (dd.d1 != nullptr)
      extra[dd.d0].push_back(dd.d1);
    if (dd.del != nullptr)
      extra[dd.d0].push_back(dd.del);
  }
  for (const auto &use : address_uses_) {
    if (use.first != nullptr)
      extra[use.first].push_back(use.second);
    else
      extra_roots.push_back(use.second);
  }
  /* A deferred function that is no longer discardable (its final linkage)
     or that became `used` after its body arrived (the key is sticky and a
     later declaration may carry it) is a root. */
  for (a_deferred_fn &d : deferred_) {
    if (d.entity->op != nullptr &&
        (!is_discardable(*d.entity) || d.entity->used))
      extra_roots.push_back(d.entity);
  }
#if EDG_CIR_TEST_HOOKS
  /* Test hook: EDG_CIR_TEST_LAZY_MISS=<symbol> drops every edge to that
     function from the deferred bodies, which simulates a missed edge. */
  if (const char *miss = std::getenv("EDG_CIR_TEST_LAZY_MISS")) {
    for (auto &item : extra) {
      std::vector<a_entity *> &targets = item.second;

      targets.erase(
          std::remove_if(targets.begin(), targets.end(),
                         [&](a_entity *t) {
            return t->op != nullptr &&
                   mlir::cast<mlir::SymbolOpInterface>(t->op).getName() ==
                       miss;
          }), targets.end());
    }
  }
#endif
  entity_liveness(live, uses, &extra, &extra_roots);
  if (session_.stats_on)
    session_.stats.lazy_resolve_ns += stats_now_ns() - start;
  /* Replay the reachable bodies in stream order. */
  for (a_deferred_fn &d : deferred_) {
    /* At -O0 an inline-only function stays a declaration, as clang makes
       it (the body would only be an available_externally definition for
       the inliner). */
    if (ok && live.count(d.entity) != 0 &&
        !inline_only_declaration(*d.entity))
      ok = replay_function(d);
  }
  for (a_deferred_fn &d : deferred_) {
    if (d.replayed)
      continue;
    session_.stats.lazy_dropped += 1;
    /* A variable that only dropped bodies named (a string literal, a static
       local, a temporary of the function) is dead with them. */
    for (uint32_t g : d.refs) {
      a_entity *target = entity_of_gid(g);

      if (target != nullptr && !target->is_function &&
          is_discardable(*target) && live.count(target) == 0)
        lazy_dead_vars_.push_back(target);
    }
  }
  std::sort(lazy_dead_vars_.begin(), lazy_dead_vars_.end());
  lazy_dead_vars_.erase(std::unique(lazy_dead_vars_.begin(),
                                    lazy_dead_vars_.end()),
                        lazy_dead_vars_.end());
  return ok;
}  /* a_emitter::lazy_finish */

bool a_emitter::lazy_erase_dropped()
/*
The deferred functions that were never replayed, and the variables that only
they named, are declarations nothing reachable names: erase them (their
lowering would only declare them).  An entity can still name one without being
reachable itself: a linkonce vtable or typeinfo of a class nothing uses,
which GlobalDCE would remove later.  Such a dead namer is erased as well, and
so, to a fixed point, is every dead entity that names an erased symbol, so
that the output has no declaration of a function it does not define and does
not depend on GlobalDCE (EDG_CIR_FLAG_NO_LLVM_PASSES runs none).  If a
reachable entity names a dropped function, the closure of the reachability
missed an edge, and the function would be an undefined reference: that is an
internal error.  Runs at the end of finish_entities, when the names are final.
*/
{
  bool ok = true;
  std::vector<a_entity *> candidates;
  std::unordered_map<mlir::Operation *, a_entity *> by_op;
  std::unordered_map<std::string_view, a_entity *> by_name;
  /* Entity -> the entities whose operation names it (null: an operation of
     no entity). */
  std::unordered_map<a_entity *, std::vector<a_entity *>> named_by;
  std::unordered_set<a_entity *> erase;
  std::unordered_set<a_entity *> dropped_functions;
  std::vector<a_entity *> queue;
  std::unordered_set<const a_entity *> live;
  std::unordered_map<const a_entity *, std::vector<a_entity *>> uses;
  bool named_dead = false;

  for (const a_deferred_fn &d : deferred_) {
    if (d.entity->op != nullptr)
      lazy_symbols_.emplace_back(
          std::string(mlir::cast<mlir::SymbolOpInterface>(d.entity->op)
                          .getName()), d.replayed);
    if (!d.replayed && d.entity->op != nullptr) {
      candidates.push_back(d.entity);
      dropped_functions.insert(d.entity);
    }
  }
  for (a_entity *variable : lazy_dead_vars_) {
    if (variable->op != nullptr)
      candidates.push_back(variable);
  }
  if (candidates.empty())
    return true;
  for (a_entity *entity : entity_order_) {
    if (entity->op == nullptr)
      continue;
    by_op[entity->op] = entity;
    by_name[std::string_view(
        mlir::cast<mlir::SymbolOpInterface>(entity->op).getName())] = entity;
  }
  for (mlir::Operation &top : *module_.getBody()) {
    auto owner = by_op.find(&top);
    a_entity *from = owner == by_op.end() ? nullptr : owner->second;

    for_each_symbol_use(&top, [&](mlir::Operation *, mlir::StringAttr name) {
      auto target = by_name.find(std::string_view(name.getValue()));

      if (target != by_name.end() && target->second != from)
        named_by[target->second].push_back(from);
    });
  }
  for (a_entity *candidate : candidates) {
    if (named_by.count(candidate) != 0)
      named_dead = true;
  }
  if (named_dead)
    entity_liveness(live, uses);
  /* The candidates to erase: what nothing names, and what only dead
     entities name (the fixed point follows). */
  for (a_entity *candidate : candidates) {
    bool is_function = dropped_functions.count(candidate) != 0;

    if (named_dead && live.count(candidate) != 0) {
      /* (An inline-only function at -O0 is a declaration that reachable
         code legitimately names.) */
      if (is_function && !inline_only_declaration(*candidate))
        ok = internal("lazy emission missed an edge to '" +
                      std::string(mlir::cast<mlir::SymbolOpInterface>(
                          candidate->op).getName()) +
                      "': a reachable entity names a function whose body "
                      "was dropped");
      continue;
    }
    if (erase.insert(candidate).second)
      queue.push_back(candidate);
  }
  while (ok && !queue.empty()) {
    a_entity *current = queue.back();
    auto found = named_by.find(current);

    queue.pop_back();
    if (found == named_by.end())
      continue;
    for (a_entity *namer : found->second) {
      if (namer == nullptr || live.count(namer) != 0) {
        /* A reachable namer: a variable stays (it is not a function whose
           body we dropped), a dropped function is a missed edge. */
        if (dropped_functions.count(current) != 0 &&
            !inline_only_declaration(*current)) {
          ok = internal("lazy emission missed an edge to '" +
                        std::string(mlir::cast<mlir::SymbolOpInterface>(
                            current->op).getName()) +
                        "': a reachable entity names a function whose body "
                        "was dropped");
          break;
        }
        erase.erase(current);
        break;
      }
      if (erase.insert(namer).second)
        queue.push_back(namer);
    }
  }
  if (ok) {
    for (a_entity *entity : erase) {
      if (dropped_functions.count(entity) != 0) {
        if (named_by.count(entity) != 0) {
          session_.stats.lazy_kept += 1;
          lazy_kept_.emplace_back(std::string(
              mlir::cast<mlir::SymbolOpInterface>(entity->op).getName()));
        }
      } else if (entity->is_function ||
                 std::find(lazy_dead_vars_.begin(), lazy_dead_vars_.end(),
                           entity) == lazy_dead_vars_.end()) {
        session_.stats.lazy_namers += 1;
      } else {
        session_.stats.lazy_vars += 1;
      }
      entity->op->erase();
      entity->op = nullptr;
    }
  }
  return ok;
}  /* a_emitter::lazy_erase_dropped */

}  /* namespace edg_cir */
