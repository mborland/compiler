/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_vtable.cpp -- the Itanium vtable layout builder.

Derived in part from clang/lib/AST/VTableBuilder.cpp (VCallOffsetMap,
VCallAndVBaseOffsetBuilder, ItaniumVTableBuilder, ItaniumVTableContext), which
is part of the LLVM Project, under the Apache License v2.0 with LLVM
Exceptions (https://llvm.org/LICENSE.txt, SPDX-License-Identifier:
Apache-2.0 WITH LLVM-exception).  What changed:
- the input is the dense class-fact tables of abi_facts.h instead of the
  AST; FinalOverriders became a lookup in the front end's per-class lists
  (class a_overriders keeps only the subobject offset computation of
  FinalOverriders::ComputeBaseOffsets);
- the Microsoft ABI, relative vtables, Apple kext and RTTI omission are gone;
- every recursion (AddMethods, LayoutPrimaryAndSecondaryVTables,
  LayoutSecondaryVTables, DeterminePrimaryVirtualBases,
  LayoutVTablesForVirtualBases, AddVCallAndVBaseOffsets, AddVCallOffsets,
  AddVBaseOffsets, ComputeBaseOffsets and the base path searches) runs on a
  heap stack, so the depth of an inheritance chain is not limited by the
  machine stack;
- failures are reported as messages (the original asserts).
*/

#include "abi_vtable.h"

#include <algorithm>
#include <set>

namespace edg_abi {

namespace {

std::string number(int64_t v)
/*
Decimal text of v.
*/
{
  return std::to_string(static_cast<long long>(v));
}  /* number */

/* The result of looking for a path from a derived class to a base (clang's
   BaseOffset): the last virtual base on the path and the non-virtual offset
   from there (or from the derived class). */
struct a_base_offset {
  a_class_id  derived = no_id;
  a_class_id  virtual_base = no_id;
  int64_t     non_virtual = 0;
  bool        found = false;   /* A path was found (the offset may still be
                                  empty). */

  bool is_empty() const
    { return non_virtual == 0 && virtual_base == no_id; }
};  /* a_base_offset */

void find_paths(const a_class_db &db, a_class_id derived, a_class_id base,
                std::vector<a_base_offset> &paths)
/*
Every path from `derived` down to `base` through the direct base lists, in
depth-first declaration order (CXXBasePaths of isDerivedFrom), as the
BaseOffset ComputeBaseOffset(Path) gives: the nearest-to-base virtual step
starts the non-virtual offset.  Explicit stack.
*/
{
  struct a_frame {
    a_class_id  cls;
    size_t      next;
    a_class_id  vbase;
    int64_t     nv;
  };  /* a_frame */
  std::vector<a_frame> stack;

  paths.clear();
  stack.push_back({derived, 0, no_id, 0});
  while (!stack.empty()) {
    a_frame &top = stack.back();
    const a_class_fact &c = db.cls(top.cls);

    if (top.next >= c.bases.size()) {
      stack.pop_back();
    } else {
      const a_base_fact &b = c.bases[top.next++];
      a_class_id vbase = b.is_virtual ? b.cls : top.vbase;
      int64_t nv = b.is_virtual ? 0 : top.nv + b.offset;

      if (b.cls == base) {
        a_base_offset result;

        result.derived = derived;
        result.virtual_base = vbase;
        result.non_virtual = nv;
        result.found = true;
        paths.push_back(result);
      } else {
        stack.push_back({b.cls, 0, vbase, nv});
      }
    }
  }
}  /* find_paths */

}  /* anonymous namespace */

/* ------------------------------------------------------------------ */
/* The final overriders' subobject offsets (FinalOverriders). */

namespace {

class a_overriders {
public:
  struct a_info {
    a_fn_id  method;
    int64_t  offset;   /* The overrider's subobject in the layout class. */
  };  /* a_info */

  a_overriders(const a_class_db &db, a_class_id most_derived,
               int64_t most_derived_offset, a_class_id layout_class);

  bool get(a_fn_id fn, int64_t base_offset, a_info &out, std::string &error)
    const;
  /* The final overrider of fn at the subobject at base_offset of the most
     derived class. */

  const std::string &error() const
    { return error_; }

private:
  const a_class_db                          &db_;
  a_class_id                                 md_;
  std::map<std::pair<a_class_id, int64_t>, int64_t>  layout_offsets_;
  std::string                                error_;
};  /* a_overriders */

a_overriders::a_overriders(const a_class_db &db, a_class_id most_derived,
                           int64_t most_derived_offset,
                           a_class_id layout_class)
  : db_(db), md_(most_derived)
/*
FinalOverriders::ComputeBaseOffsets, non-recursively: the offset in the layout
class of every subobject of the most derived class, keyed by (class, offset in
the most derived class).
*/
{
  struct a_frame {
    a_class_id  cls;
    int64_t     in_md;
    int64_t     in_layout;
    size_t      next;
  };  /* a_frame */
  std::vector<a_frame> stack;
  std::set<a_class_id> seen_vbases;

  layout_offsets_[std::make_pair(md_, int64_t(0))] = most_derived_offset;
  stack.push_back({md_, 0, most_derived_offset, 0});
  while (error_.empty() && !stack.empty()) {
    a_frame &top = stack.back();
    const a_class_fact &c = db_.cls(top.cls);

    if (top.next >= c.bases.size()) {
      stack.pop_back();
    } else {
      const a_base_fact &b = c.bases[top.next++];
      int64_t in_md = 0;
      int64_t in_layout = 0;
      bool skip = false;

      if (b.is_virtual) {
        if (seen_vbases.count(b.cls) != 0 ||
            !db_.find_vbase_offset(md_, b.cls, in_md) ||
            !db_.find_vbase_offset(layout_class, b.cls, in_layout)) {
          skip = true;
          if (seen_vbases.count(b.cls) == 0)
            error_ = "virtual base " + number(b.cls) + " is not a virtual "
                     "base of the most derived or layout class";
        } else {
          seen_vbases.insert(b.cls);
        }
      } else {
        in_md = top.in_md + b.offset;
        in_layout = top.in_layout + b.offset;
      }
      if (!skip) {
        layout_offsets_[std::make_pair(b.cls, in_md)] = in_layout;
        stack.push_back({b.cls, in_md, in_layout, 0});
      }
    }
  }
}  /* a_overriders::a_overriders */

bool a_overriders::get(a_fn_id fn, int64_t base_offset, a_info &out,
                       std::string &error) const
/*
The final overrider of fn at the subobject at base_offset (the function itself
when the front end lists no overrider).
*/
{
  a_fn_id ovr = fn;
  int64_t ovr_off = base_offset;
  bool ok = true;

  db_.find_override(md_, fn, base_offset, ovr, ovr_off);
  auto it = layout_offsets_.find(std::make_pair(db_.fn(ovr).owner, ovr_off));
  if (it == layout_offsets_.end()) {
    error = "no subobject of class " + number(db_.fn(ovr).owner) + " at "
            "offset " + number(ovr_off) + " in class " + number(md_) +
            " for the overrider of " + db_.fn(fn).name;
    ok = false;
  } else {
    out.method = ovr;
    out.offset = it->second;
  }
  return ok;
}  /* a_overriders::get */

}  /* anonymous namespace */

/* ------------------------------------------------------------------ */
/* VCallOffsetMap and VCallAndVBaseOffsetBuilder. */

namespace {

class a_vcall_offset_map {
public:
  explicit a_vcall_offset_map(const a_class_db &db)
    : db_(&db)
    {}
  a_vcall_offset_map()
    : db_(nullptr)
    {}

  bool add(a_fn_id fn, int64_t offset_offset);
  /* False if a method with the same signature already has an entry. */
  bool find(a_fn_id fn, int64_t &offset_offset) const;
  bool empty() const
    { return offsets_.empty(); }

private:
  bool can_share(a_fn_id lhs, a_fn_id rhs) const;

  const a_class_db                          *db_;
  std::vector<std::pair<a_fn_id, int64_t>>   offsets_;
};  /* a_vcall_offset_map */

bool a_vcall_offset_map::can_share(a_fn_id lhs, a_fn_id rhs) const
/*
VCallOffsetMap::MethodsCanShareVCallOffset: two destructors, or the same name
and virtual signature (the front end's sig class).
*/
{
  const a_fn_fact &a = db_->fn(lhs);
  const a_fn_fact &b = db_->fn(rhs);
  bool result;

  if (a.special == fs_destructor)
    result = b.special == fs_destructor;
  else
    result = b.special != fs_destructor && a.sig == b.sig;
  return result;
}  /* a_vcall_offset_map::can_share */

bool a_vcall_offset_map::add(a_fn_id fn, int64_t offset_offset)
/*
Add an entry unless a method that can share one already has it.
*/
{
  bool added = true;

  for (const auto &entry : offsets_) {
    if (can_share(entry.first, fn))
      added = false;
  }
  if (added)
    offsets_.push_back(std::make_pair(fn, offset_offset));
  return added;
}  /* a_vcall_offset_map::add */

bool a_vcall_offset_map::find(a_fn_id fn, int64_t &offset_offset) const
/*
The vcall offset offset of the entry fn shares.
*/
{
  bool found = false;

  for (const auto &entry : offsets_) {
    if (!found && can_share(entry.first, fn)) {
      offset_offset = entry.second;
      found = true;
    }
  }
  return found;
}  /* a_vcall_offset_map::find */

class a_vcall_builder {
public:
  a_vcall_builder(const a_class_db &db, a_class_id most_derived,
                  a_class_id layout_class, const a_overriders *overriders,
                  const a_subobject &base, bool base_is_virtual,
                  int64_t real_base_offset);

  std::vector<a_component> components() const;
  /* In vtable order (the original collects them backwards). */

  const a_vcall_offset_map &vcall_offsets() const
    { return vcall_offsets_; }
  const std::map<a_class_id, int64_t> &vbase_offset_offsets() const
    { return vbase_offset_offsets_; }
  const std::string &error() const
    { return error_; }

private:
  void add_vcall_and_vbase_offsets(const a_subobject &base,
                                   bool base_is_virtual,
                                   int64_t real_base_offset);
  void add_vcall_offsets(const a_subobject &base, int64_t vbase_offset);
  void add_vbase_offsets(a_class_id rd, int64_t offset_in_layout_class);
  int64_t current_offset_offset() const;
  void fail(const std::string &message);

  const a_class_db            &db_;
  a_class_id                   md_;
  a_class_id                   lc_;
  const a_overriders          *overriders_;
  std::vector<a_component>     components_;
  std::set<a_class_id>         visited_vbases_;
  a_vcall_offset_map           vcall_offsets_;
  std::map<a_class_id, int64_t> vbase_offset_offsets_;
  std::string                  error_;
};  /* a_vcall_builder */

a_vcall_builder::a_vcall_builder(const a_class_db &db, a_class_id most_derived,
                                 a_class_id layout_class,
                                 const a_overriders *overriders,
                                 const a_subobject &base,
                                 bool base_is_virtual,
                                 int64_t real_base_offset)
  : db_(db), md_(most_derived), lc_(layout_class), overriders_(overriders),
    vcall_offsets_(db)
{
  add_vcall_and_vbase_offsets(base, base_is_virtual, real_base_offset);
}  /* a_vcall_builder::a_vcall_builder */

void a_vcall_builder::fail(const std::string &message)
/*
Remember the first problem.
*/
{
  if (error_.empty())
    error_ = message;
}  /* a_vcall_builder::fail */

std::vector<a_component> a_vcall_builder::components() const
/*
The collected components, last first (the order the vtable has them).
*/
{
  return std::vector<a_component>(components_.rbegin(), components_.rend());
}  /* a_vcall_builder::components */

int64_t a_vcall_builder::current_offset_offset() const
/*
getCurrentOffsetOffset: the offset, from the address point, of the entry the
next component will occupy (three components lie above the address point:
vcall and vbase offsets are below it, then offset to top, RTTI).
*/
{
  int64_t index = -static_cast<int64_t>(3 + components_.size());

  return static_cast<int64_t>(db_.pointer_size) * index;
}  /* a_vcall_builder::current_offset_offset */

void a_vcall_builder::add_vcall_and_vbase_offsets(const a_subobject &base,
                                                  bool base_is_virtual,
                                                  int64_t real_base_offset)
/*
AddVCallAndVBaseOffsets: the primary base chain first (deepest first), each
with its virtual base offsets and, for a virtual one, its vcall offsets.
*/
{
  struct a_item {
    a_class_id  cls;
    int64_t     offset;
    bool        is_virtual;
  };  /* a_item */
  std::vector<a_item> chain;

  chain.push_back({base.cls, base.offset, base_is_virtual});
  while (error_.empty()) {
    const a_item cur = chain.back();
    const a_class_fact &c = db_.cls(cur.cls);

    if (c.primary == no_id)
      break;
    if (c.primary_virtual) {
      int64_t off = 0;

      if (!db_.find_vbase_offset(md_, c.primary, off))
        fail("primary virtual base " + number(c.primary) + " is not a "
             "virtual base of class " + number(md_));
      chain.push_back({c.primary, off, true});
    } else {
      chain.push_back({c.primary, cur.offset, false});
    }
  }
  for (size_t i = chain.size(); error_.empty() && i > 0; --i) {
    const a_item &item = chain[i - 1];

    add_vbase_offsets(item.cls, real_base_offset);
    if (item.is_virtual && error_.empty())
      add_vcall_offsets(a_subobject{item.cls, item.offset},
                        real_base_offset);
  }
}  /* a_vcall_builder::add_vcall_and_vbase_offsets */

void a_vcall_builder::add_vcall_offsets(const a_subobject &base,
                                        int64_t vbase_offset)
/*
AddVCallOffsets: one vcall offset per method (that cannot share) of the
primary non-virtual base chain, the class and the other non-virtual bases.
*/
{
  struct a_frame {
    a_class_id  cls;
    int64_t     offset;
    int         phase;
    size_t      next;
  };  /* a_frame */
  std::vector<a_frame> stack;

  stack.push_back({base.cls, base.offset, 0, 0});
  while (error_.empty() && !stack.empty()) {
    a_frame &top = stack.back();
    const a_class_fact &c = db_.cls(top.cls);

    if (top.phase == 0) {
      top.phase = 1;
      if (c.primary != no_id && !c.primary_virtual)
        stack.push_back({c.primary, top.offset, 0, 0});
    } else if (top.phase == 1) {
      int64_t offset = top.offset;

      top.phase = 2;
      for (size_t k = 0; error_.empty() && k < c.fns.size(); ++k) {
        a_fn_id md = c.fns[k];
        int64_t oo = current_offset_offset();

        if (!vcall_offsets_.add(md, oo))
          continue;
        int64_t off = 0;

        if (overriders_ != nullptr) {
          a_overriders::a_info ovr;
          std::string err;

          if (!overriders_->get(md, offset, ovr, err))
            fail(err);
          else
            off = ovr.offset - vbase_offset;
        }
        a_component comp = {ck_vcall_offset, off, no_id};

        components_.push_back(comp);
      }
    } else if (top.next >= c.bases.size()) {
      stack.pop_back();
    } else {
      const a_base_fact &b = c.bases[top.next++];

      if (!b.is_virtual && b.cls != c.primary)
        stack.push_back({b.cls, top.offset + b.offset, 0, 0});
    }
  }
}  /* a_vcall_builder::add_vcall_offsets */

void a_vcall_builder::add_vbase_offsets(a_class_id rd,
                                        int64_t offset_in_layout_class)
/*
AddVBaseOffsets: the vbase offset of every not yet seen virtual base of rd and
of all its bases.
*/
{
  struct a_frame {
    a_class_id  cls;
    size_t      next;
  };  /* a_frame */
  std::vector<a_frame> stack;

  stack.push_back({rd, 0});
  while (error_.empty() && !stack.empty()) {
    a_frame &top = stack.back();
    const a_class_fact &c = db_.cls(top.cls);

    if (top.next >= c.bases.size()) {
      stack.pop_back();
    } else {
      const a_base_fact &b = c.bases[top.next++];

      if (b.is_virtual && visited_vbases_.insert(b.cls).second) {
        int64_t off = 0;

        if (!db_.find_vbase_offset(lc_, b.cls, off)) {
          fail("virtual base " + number(b.cls) + " is missing from class " +
               number(lc_));
        } else {
          vbase_offset_offsets_[b.cls] = current_offset_offset();
          components_.push_back(
              {ck_vbase_offset, off - offset_in_layout_class, no_id});
        }
      }
      stack.push_back({b.cls, 0});
    }
  }
}  /* a_vcall_builder::add_vbase_offsets */

}  /* anonymous namespace */

/* ------------------------------------------------------------------ */
/* ItaniumVTableBuilder. */

namespace {

class a_vtable_builder {
public:
  a_vtable_builder(a_vtable_context &ctx, a_class_id most_derived,
                   int64_t most_derived_offset, bool most_derived_is_virtual,
                   a_class_id layout_class);

  bool run(std::string &error);
  /* LayoutVTable. */

  /* Results. */
  std::vector<size_t>                              vtable_indices;
  std::vector<a_component>                         components;
  std::map<a_subobject, a_address_point>           address_points;
  std::map<uint64_t, a_thunk_info>                 vtable_thunks;
  std::map<a_fn_id, std::vector<a_thunk_info>>     thunks;
  std::map<a_fn_id, int64_t>                       method_indices;
  std::map<a_class_id, int64_t>                    vbase_offset_offsets;

private:
  struct a_method_info {
    int64_t   base_offset;
    int64_t   base_offset_in_layout_class;
    uint64_t  vtable_index;
  };  /* a_method_info */

  const a_class_db &db() const
    { return ctx_.db(); }
  bool building_construction_vtable() const
    { return md_ != lc_; }
  void fail(const std::string &message);

  bool layout_primary_and_secondary(const a_subobject &base,
                                    bool morally_virtual,
                                    bool base_is_virtual_in_layout_class,
                                    int64_t offset_in_layout_class);
  void primary_work(const a_subobject &base, bool base_is_virtual_in_lc,
                    int64_t offset_in_lc);
  void determine_primary_virtual_bases(int64_t offset_in_lc);
  void layout_vtables_for_virtual_bases();
  void add_methods(const a_subobject &base, int64_t base_offset_in_lc,
                   a_class_id first_base, int64_t first_base_offset_in_lc,
                   std::vector<a_class_id> &primary_bases);
  void process_methods(a_class_id cls, int64_t base_offset,
                       int64_t base_offset_in_lc, a_class_id first_base,
                       int64_t first_base_offset_in_lc,
                       const std::vector<a_class_id> &primary_bases);
  void add_method(a_fn_id fn, const a_adjustment &return_adjustment);
  bool is_overrider_used(a_fn_id overrider, int64_t base_offset_in_lc,
                         a_class_id first_base,
                         int64_t first_base_offset_in_lc);
  a_fn_id find_nearest_overridden(a_fn_id fn,
                                  const std::vector<a_class_id> &bases);
  bool overrides_in_bases(a_fn_id fn, const std::set<a_class_id> &bases);
  a_base_offset compute_return_adjustment_base_offset(a_fn_id derived_fn,
                                                      a_fn_id base_fn);
  a_adjustment compute_return_adjustment(const a_base_offset &offset);
  a_base_offset compute_this_adjustment_base_offset(
      const a_subobject &base, const a_subobject &derived);
  a_adjustment compute_this_adjustment(a_fn_id md,
                                       int64_t base_offset_in_lc,
                                       const a_overriders::a_info &overrider);
  void compute_this_adjustments();
  void add_thunk(a_fn_id md, const a_thunk_info &thunk);

  a_vtable_context                    &ctx_;
  a_class_id                           md_;
  int64_t                              md_offset_;
  bool                                 md_is_virtual_;
  a_class_id                           lc_;
  std::unique_ptr<a_overriders>        overriders_;
  std::map<a_class_id, a_vcall_offset_map>  vcall_offsets_for_vbases_;
  std::map<a_fn_id, a_method_info>     method_info_;
  std::set<a_class_id>                 primary_virtual_bases_;
  std::string                          error_;
};  /* a_vtable_builder */

a_vtable_builder::a_vtable_builder(a_vtable_context &ctx,
                                   a_class_id most_derived,
                                   int64_t most_derived_offset,
                                   bool most_derived_is_virtual,
                                   a_class_id layout_class)
  : ctx_(ctx), md_(most_derived), md_offset_(most_derived_offset),
    md_is_virtual_(most_derived_is_virtual), lc_(layout_class)
{
  overriders_.reset(new a_overriders(ctx.db(), md_, md_offset_, lc_));
  if (!overriders_->error().empty())
    error_ = overriders_->error();
}  /* a_vtable_builder::a_vtable_builder */

void a_vtable_builder::fail(const std::string &message)
/*
Remember the first problem.
*/
{
  if (error_.empty())
    error_ = message;
}  /* a_vtable_builder::fail */

bool a_vtable_builder::run(std::string &error)
/*
LayoutVTable: the primary and secondary vtables of the class, then the
vtables of the virtual bases that are not primary bases of another base.
*/
{
  if (error_.empty()) {
    layout_primary_and_secondary(a_subobject{md_, 0}, false,
                                 md_is_virtual_, md_offset_);
    if (error_.empty())
      determine_primary_virtual_bases(md_offset_);
    if (error_.empty())
      layout_vtables_for_virtual_bases();
  }
  error = error_;
  return error_.empty();
}  /* a_vtable_builder::run */

bool a_vtable_builder::layout_primary_and_secondary(
    const a_subobject &base, bool morally_virtual,
    bool base_is_virtual_in_layout_class, int64_t offset_in_layout_class)
/*
LayoutPrimaryAndSecondaryVTables with LayoutSecondaryVTables: the vtable of
the base subobject, then (depth first) the vtables of its non-virtual bases
that are not its primary base.
*/
{
  struct a_frame {
    a_class_id  cls;
    int64_t     base_offset;
    int64_t     offset_in_lc;
    size_t      next;
  };  /* a_frame */
  std::vector<a_frame> stack;

  primary_work(base, base_is_virtual_in_layout_class,
               offset_in_layout_class);
  stack.push_back({base.cls, base.offset, offset_in_layout_class, 0});
  while (error_.empty() && !stack.empty()) {
    a_frame &top = stack.back();
    const a_class_fact &c = db().cls(top.cls);

    if (top.next >= c.bases.size()) {
      stack.pop_back();
    } else {
      const a_base_fact &b = c.bases[top.next++];

      if (b.is_virtual || !db().cls(b.cls).is_dynamic)
        continue;
      if (building_construction_vtable() && !morally_virtual &&
          db().cls(b.cls).vbases.empty())
        continue;
      int64_t base_offset = top.base_offset + b.offset;
      int64_t offset_in_lc = top.offset_in_lc + b.offset;

      if (b.cls == c.primary) {
        stack.push_back({b.cls, base_offset, offset_in_lc, 0});
      } else {
        primary_work(a_subobject{b.cls, base_offset}, false, offset_in_lc);
        stack.push_back({b.cls, base_offset, offset_in_lc, 0});
      }
    }
  }
  return error_.empty();
}  /* a_vtable_builder::layout_primary_and_secondary */

void a_vtable_builder::primary_work(const a_subobject &base,
                                    bool base_is_virtual_in_lc,
                                    int64_t offset_in_lc)
/*
The part of LayoutPrimaryAndSecondaryVTables that is not the recursion: start
a vtable, its vcall and vbase offsets, offset to top, RTTI, the methods, the
vtable indices (for the most derived class) and the address points.
*/
{
  unsigned vtable_index = static_cast<unsigned>(components.size());
  uint64_t address_point;
  a_class_id rd = base.cls;
  std::vector<a_class_id> primary_bases;

  vtable_indices.push_back(vtable_index);
  a_vcall_builder builder(db(), md_, lc_, overriders_.get(), base,
                          base_is_virtual_in_lc, offset_in_lc);
  if (!builder.error().empty()) {
    fail(builder.error());
  } else {
    std::vector<a_component> head = builder.components();

    components.insert(components.end(), head.begin(), head.end());
    if (base_is_virtual_in_lc && !builder.vcall_offsets().empty()) {
      a_vcall_offset_map &slot = vcall_offsets_for_vbases_[base.cls];

      if (slot.empty())
        slot = builder.vcall_offsets();
    }
    if (base.cls == md_)
      vbase_offset_offsets = builder.vbase_offset_offsets();
    components.push_back({ck_offset_to_top, md_offset_ - offset_in_lc, no_id});
    components.push_back({ck_rtti, 0, md_});
    address_point = components.size();
    add_methods(base, offset_in_lc, base.cls, offset_in_lc, primary_bases);
    if (error_.empty()) {
      if (rd == md_) {
        for (const auto &entry : method_info_) {
          method_indices[entry.first] =
              static_cast<int64_t>(entry.second.vtable_index) -
              static_cast<int64_t>(address_point);
        }
      }
      compute_this_adjustments();
    }
    while (error_.empty()) {
      const a_class_fact &c = db().cls(rd);
      a_address_point point;

      point.vtable_index = static_cast<unsigned>(vtable_indices.size() - 1);
      point.address_point_index =
          static_cast<unsigned>(address_point - vtable_index);
      address_points.insert(
          std::make_pair(a_subobject{rd, offset_in_lc}, point));
      if (c.primary == no_id)
        break;
      if (c.primary_virtual) {
        int64_t off = 0;

        if (!db().find_vbase_offset(lc_, c.primary, off))
          fail("primary virtual base missing from the layout class");
        else if (off != offset_in_lc)
          break;
      }
      rd = c.primary;
    }
  }
}  /* a_vtable_builder::primary_work */

void a_vtable_builder::determine_primary_virtual_bases(
    int64_t offset_in_lc)
/*
DeterminePrimaryVirtualBases: note every virtual base that is the primary base
of some class in the hierarchy (in a construction vtable only where the base
sits where the layout class puts it).
*/
{
  struct a_frame {
    a_class_id  cls;
    int64_t     offset_in_lc;
    size_t      next;
    bool        entered;
  };  /* a_frame */
  std::vector<a_frame> stack;
  std::set<a_class_id> vbases;

  stack.push_back({md_, offset_in_lc, 0, false});
  while (error_.empty() && !stack.empty()) {
    a_frame &top = stack.back();
    const a_class_fact &c = db().cls(top.cls);

    if (!top.entered) {
      top.entered = true;
      if (c.primary != no_id && c.primary_virtual) {
        bool is_primary = true;

        if (building_construction_vtable()) {
          int64_t off = 0;

          if (!db().find_vbase_offset(lc_, c.primary, off))
            fail("primary virtual base missing from the layout class");
          else if (off != top.offset_in_lc)
            is_primary = false;
        }
        if (is_primary)
          primary_virtual_bases_.insert(c.primary);
      }
    } else if (top.next >= c.bases.size()) {
      stack.pop_back();
    } else {
      const a_base_fact &b = c.bases[top.next++];
      int64_t off = 0;
      bool skip = false;

      if (b.is_virtual) {
        if (!vbases.insert(b.cls).second)
          skip = true;
        else if (!db().find_vbase_offset(lc_, b.cls, off))
          fail("virtual base missing from the layout class");
      } else {
        off = top.offset_in_lc + b.offset;
      }
      if (!skip && error_.empty())
        stack.push_back({b.cls, off, 0, false});
    }
  }
}  /* a_vtable_builder::determine_primary_virtual_bases */

void a_vtable_builder::layout_vtables_for_virtual_bases()
/*
LayoutVTablesForVirtualBases: the vtable group of each dynamic virtual base
that is not the primary base of another base (once each).
*/
{
  struct a_frame {
    a_class_id  cls;
    size_t      next;
  };  /* a_frame */
  std::vector<a_frame> stack;
  std::set<a_class_id> vbases;

  stack.push_back({md_, 0});
  while (error_.empty() && !stack.empty()) {
    a_frame &top = stack.back();
    const a_class_fact &c = db().cls(top.cls);

    if (top.next >= c.bases.size()) {
      stack.pop_back();
    } else {
      const a_base_fact &b = c.bases[top.next++];

      if (b.is_virtual && db().cls(b.cls).is_dynamic &&
          primary_virtual_bases_.count(b.cls) == 0 &&
          vbases.insert(b.cls).second) {
        int64_t base_offset = 0;
        int64_t offset_in_lc = 0;

        if (!db().find_vbase_offset(md_, b.cls, base_offset) ||
            !db().find_vbase_offset(lc_, b.cls, offset_in_lc))
          fail("virtual base missing from the most derived or layout class");
        else
          layout_primary_and_secondary(a_subobject{b.cls, base_offset}, true,
                                       true, offset_in_lc);
      }
      if (error_.empty() && !db().cls(b.cls).vbases.empty())
        stack.push_back({b.cls, 0});
    }
  }
}  /* a_vtable_builder::layout_vtables_for_virtual_bases */

void a_vtable_builder::add_methods(const a_subobject &base,
                                   int64_t base_offset_in_lc,
                                   a_class_id first_base,
                                   int64_t first_base_offset_in_lc,
                                   std::vector<a_class_id> &primary_bases)
/*
AddMethods: the methods of the base subobject and of its primary bases, the
deepest primary base first.  The recursion down the primary chain became a
loop over the chain, run backwards.
*/
{
  struct a_item {
    a_class_id  cls;
    int64_t     offset;
    int64_t     offset_in_lc;
  };  /* a_item */
  std::vector<a_item> chain;

  chain.push_back({base.cls, base.offset, base_offset_in_lc});
  while (error_.empty()) {
    const a_item cur = chain.back();
    const a_class_fact &c = db().cls(cur.cls);

    if (c.primary == no_id)
      break;
    if (c.primary_virtual) {
      int64_t off = 0;
      int64_t off_lc = 0;

      if (!db().find_vbase_offset(md_, c.primary, off) ||
          !db().find_vbase_offset(lc_, c.primary, off_lc))
        fail("primary virtual base missing from the most derived or layout "
             "class");
      chain.push_back({c.primary, off, off_lc});
    } else {
      chain.push_back({c.primary, cur.offset, cur.offset_in_lc});
    }
  }
  for (size_t i = chain.size(); error_.empty() && i > 0; --i) {
    const a_item &item = chain[i - 1];

    if (i < chain.size()) {
      a_class_id pb = chain[i].cls;

      if (std::find(primary_bases.begin(), primary_bases.end(), pb) !=
          primary_bases.end())
        fail("duplicate primary base " + number(pb));
      primary_bases.push_back(pb);
    }
    process_methods(item.cls, item.offset, item.offset_in_lc, first_base,
                    first_base_offset_in_lc, primary_bases);
  }
}  /* a_vtable_builder::add_methods */

void a_vtable_builder::process_methods(
    a_class_id cls, int64_t base_offset, int64_t base_offset_in_lc,
    a_class_id first_base, int64_t first_base_offset_in_lc,
    const std::vector<a_class_id> &primary_bases)
/*
The part of AddMethods that handles the virtual functions a class declares:
an overrider of a function of a primary base reuses its slot (and may need a
thunk), the others get new slots (implicit ones in clang's order).
*/
{
  const a_class_fact &c = db().cls(cls);
  std::vector<a_fn_id> new_functions;
  std::vector<a_fn_id> implicit_functions;

  for (size_t k = 0; error_.empty() && k < c.fns.size(); ++k) {
    a_fn_id md = c.fns[k];
    a_overriders::a_info overrider;
    std::string err;
    a_fn_id overridden;
    bool reused = false;

    if (!overriders_->get(md, base_offset, overrider, err)) {
      fail(err);
      continue;
    }
    overridden = find_nearest_overridden(md, primary_bases);
    if (overridden != no_id &&
        compute_return_adjustment_base_offset(md, overridden).is_empty()) {
      auto found = method_info_.find(overridden);

      reused = true;
      ctx_.set_original_method(md, overridden);
      if (found == method_info_.end()) {
        fail("did not find the overridden method of " + db().fn(md).name);
      } else {
        a_method_info info = {base_offset, base_offset_in_lc,
                              found->second.vtable_index};

        if (method_info_.count(md) != 0)
          fail("method info for " + db().fn(md).name + " exists already");
        method_info_[md] = info;
        method_info_.erase(overridden);
        if (!building_construction_vtable() && overridden != md) {
          a_adjustment this_adj =
              compute_this_adjustment(overridden, base_offset_in_lc,
                                      overrider);

          if (this_adj.offset_offset != 0 &&
              db().fn(overrider.method).owner == md_) {
            a_base_offset ra_offset = compute_return_adjustment_base_offset(
                overrider.method, md);
            a_adjustment return_adj = compute_return_adjustment(ra_offset);
            a_thunk_info thunk;

            thunk.this_adj = this_adj;
            thunk.return_adj = return_adj;
            thunk.this_class = db().fn(overridden).owner;
            add_thunk(overrider.method, thunk);
          }
        }
      }
    }
    if (!reused) {
      if (db().fn(md).is_implicit)
        implicit_functions.push_back(md);
      else
        new_functions.push_back(md);
    }
  }
  /* The implicit ones: copy assignment, move assignment, destructor, then
     the others, each group in declaration order. */
  std::stable_sort(implicit_functions.begin(), implicit_functions.end(),
                   [this](a_fn_id a, a_fn_id b) {
                     auto rank = [this](a_fn_id f) {
                       a_fn_special s = db().fn(f).special;

                       return s == fs_copy_assign ? 0 : s == fs_move_assign
                                  ? 1 : s == fs_destructor ? 2 : 3;
                     };
                     return rank(a) < rank(b);
                   });
  new_functions.insert(new_functions.end(), implicit_functions.begin(),
                       implicit_functions.end());
  for (size_t k = 0; error_.empty() && k < new_functions.size(); ++k) {
    a_fn_id md = new_functions[k];
    a_overriders::a_info overrider;
    std::string err;
    a_method_info info = {base_offset, base_offset_in_lc,
                          static_cast<uint64_t>(components.size())};
    a_fn_id overrider_md;

    if (!overriders_->get(md, base_offset, overrider, err)) {
      fail(err);
      continue;
    }
    if (method_info_.count(md) != 0)
      fail("method info for " + db().fn(md).name + " exists already");
    method_info_[md] = info;
    overrider_md = overrider.method;
    if (!is_overrider_used(overrider_md, base_offset_in_lc, first_base,
                           first_base_offset_in_lc)) {
      components.push_back({ck_unused_function, 0, overrider_md});
    } else {
      a_base_offset ra_offset;
      a_adjustment return_adj;

      if (!db().fn(overrider_md).is_pure)
        ra_offset = compute_return_adjustment_base_offset(overrider_md, md);
      return_adj = compute_return_adjustment(ra_offset);
      if (!return_adj.is_empty()) {
        a_thunk_info &thunk = vtable_thunks[components.size()];

        thunk.method = md;
        thunk.this_class = db().fn(md).owner;
      }
      add_method(overrider_md, return_adj);
    }
  }
}  /* a_vtable_builder::process_methods */

void a_vtable_builder::add_method(a_fn_id fn,
                                  const a_adjustment &return_adjustment)
/*
AddMethod: one component for a function, two for a destructor.
*/
{
  if (db().fn(fn).special == fs_destructor) {
    if (!return_adjustment.is_empty())
      fail("a destructor cannot have a return adjustment");
    components.push_back({ck_complete_dtor, 0, fn});
    components.push_back({ck_deleting_dtor, 0, fn});
  } else {
    if (!return_adjustment.is_empty())
      vtable_thunks[components.size()].return_adj = return_adjustment;
    components.push_back({ck_function, 0, fn});
  }
}  /* a_vtable_builder::add_method */

bool a_vtable_builder::overrides_in_bases(a_fn_id fn,
                                          const std::set<a_class_id> &bases)
/*
OverridesIndirectMethodInBases: fn, or a function it overrides, is declared
in one of the classes.
*/
{
  bool result = bases.count(db().fn(fn).owner) != 0;

  for (a_fn_id f : ctx_.all_overridden(fn)) {
    if (bases.count(db().fn(f).owner) != 0)
      result = true;
  }
  return result;
}  /* a_vtable_builder::overrides_in_bases */

bool a_vtable_builder::is_overrider_used(a_fn_id overrider,
                                         int64_t base_offset_in_lc,
                                         a_class_id first_base,
                                         int64_t first_base_offset_in_lc)
/*
IsOverriderUsed: whether the overrider is ever called through this part of the
vtable (Itanium C++ ABI 2.5.2: the copy of a virtual primary base's vtable that
is not primary in the layout class is never referenced).
*/
{
  bool used = true;

  if (base_offset_in_lc != first_base_offset_in_lc &&
      db().fn(overrider).owner != first_base) {
    std::set<a_class_id> primary_bases;
    a_class_id rd = first_base;
    bool go = true;

    primary_bases.insert(rd);
    while (go) {
      const a_class_fact &c = db().cls(rd);

      if (c.primary == no_id) {
        go = false;
      } else {
        if (c.primary_virtual) {
          int64_t off = 0;

          if (!db().find_vbase_offset(lc_, c.primary, off))
            fail("primary virtual base missing from the layout class");
          if (off != first_base_offset_in_lc)
            go = false;
        }
        if (go) {
          if (!primary_bases.insert(c.primary).second)
            fail("found a duplicate primary base");
          rd = c.primary;
          if (!error_.empty())
            go = false;
        }
      }
    }
    used = overrides_in_bases(overrider, primary_bases);
  }
  return used;
}  /* a_vtable_builder::is_overrider_used */

a_fn_id a_vtable_builder::find_nearest_overridden(
    a_fn_id fn, const std::vector<a_class_id> &bases)
/*
FindNearestOverriddenMethod: the function fn overrides that is declared in the
most derived of the primary bases seen so far (bases are in order, the most
derived last).  One pass over the overridden functions with a position map.
*/
{
  a_fn_id result = no_id;
  const std::vector<a_fn_id> &all = ctx_.all_overridden(fn);
  std::unordered_map<a_class_id, size_t> position;
  size_t best = 0;

  if (!all.empty()) {
    for (size_t i = 0; i < bases.size(); ++i)
      position[bases[i]] = i;
    for (a_fn_id f : all) {
      auto found = position.find(db().fn(f).owner);

      if (found != position.end() && (result == no_id || found->second > best)) {
        result = f;
        best = found->second;
      }
    }
  }
  return result;
}  /* a_vtable_builder::find_nearest_overridden */

a_base_offset a_vtable_builder::compute_return_adjustment_base_offset(
    a_fn_id derived_fn, a_fn_id base_fn)
/*
ComputeReturnAdjustmentBaseOffset: for a covariant result, the path from the
overrider's result class to the overridden function's.
*/
{
  a_class_id rd = db().fn(derived_fn).ret_class;
  a_class_id rb = db().fn(base_fn).ret_class;
  a_base_offset result;

  if (rd != no_id && rb != no_id && rd != rb) {
    std::vector<a_base_offset> paths;

    find_paths(db(), rd, rb, paths);
    if (paths.empty())
      fail("class " + number(rd) + " is not derived from " + number(rb) +
           " (covariant result of " + db().fn(derived_fn).name + ")");
    else
      result = paths.front();
  }
  return result;
}  /* a_vtable_builder::compute_return_adjustment_base_offset */

a_adjustment a_vtable_builder::compute_return_adjustment(
    const a_base_offset &offset)
/*
ComputeReturnAdjustment: the adjustment (with the vbase offset offset when the
path goes through a virtual base).
*/
{
  a_adjustment adjustment;

  if (!offset.is_empty()) {
    if (offset.virtual_base != no_id) {
      if (offset.derived == md_) {
        auto it = vbase_offset_offsets.find(offset.virtual_base);

        if (it == vbase_offset_offsets.end())
          fail("no vbase offset offset for virtual base " +
               number(offset.virtual_base));
        else
          adjustment.offset_offset = it->second;
      } else {
        std::string err;

        adjustment.offset_offset = ctx_.virtual_base_offset_offset(
            offset.derived, offset.virtual_base, err);
        if (!err.empty())
          fail(err);
      }
    }
    adjustment.non_virtual = offset.non_virtual;
  }
  return adjustment;
}  /* a_vtable_builder::compute_return_adjustment */

a_base_offset a_vtable_builder::compute_this_adjustment_base_offset(
    const a_subobject &base, const a_subobject &derived)
/*
ComputeThisAdjustmentBaseOffset: the path from the derived subobject's class to
the base's class that leads to the base subobject at its offset; the
non-virtual offset is inverted (the adjustment goes from base to derived).
*/
{
  std::vector<a_base_offset> paths;
  a_base_offset result;

  find_paths(db(), derived.cls, base.cls, paths);
  for (const a_base_offset &path : paths) {
    int64_t to_base = path.non_virtual;

    if (result.found)
      continue;
    if (path.virtual_base != no_id) {
      int64_t off = 0;

      if (!db().find_vbase_offset(lc_, path.virtual_base, off))
        fail("virtual base missing from the layout class");
      to_base += off;
    } else {
      to_base += derived.offset;
    }
    if (to_base == base.offset) {
      result = path;
      result.found = true;
      result.non_virtual = -path.non_virtual;
    }
  }
  return result;
}  /* a_vtable_builder::compute_this_adjustment_base_offset */

a_adjustment a_vtable_builder::compute_this_adjustment(
    a_fn_id md, int64_t base_offset_in_lc,
    const a_overriders::a_info &overrider)
/*
ComputeThisAdjustment: the this adjustment of a virtual function whose final
overrider is in another subobject.
*/
{
  a_adjustment adjustment;

  if (!db().fn(overrider.method).is_pure) {
    a_subobject overridden{db().fn(md).owner, base_offset_in_lc};
    a_subobject overrider_base{db().fn(overrider.method).owner,
                               overrider.offset};
    a_base_offset offset = compute_this_adjustment_base_offset(
        overridden, overrider_base);

    if (!offset.is_empty()) {
      if (offset.virtual_base != no_id) {
        a_vcall_offset_map &map = vcall_offsets_for_vbases_[offset.virtual_base];
        int64_t oo = 0;

        if (map.empty()) {
          a_vcall_builder builder(db(), md_, md_, nullptr,
                                  a_subobject{offset.virtual_base, 0}, true,
                                  0);

          if (!builder.error().empty())
            fail(builder.error());
          map = builder.vcall_offsets();
        }
        if (!map.find(md, oo))
          fail("no vcall offset for " + db().fn(md).name);
        adjustment.offset_offset = oo;
      }
      adjustment.non_virtual = offset.non_virtual;
    }
  }
  return adjustment;
}  /* a_vtable_builder::compute_this_adjustment */

void a_vtable_builder::add_thunk(a_fn_id md, const a_thunk_info &thunk)
/*
AddThunk: remember a thunk of the most derived class's function once.
*/
{
  std::vector<a_thunk_info> &list = thunks[md];

  if (building_construction_vtable())
    fail("cannot add thunks for a construction vtable");
  if (std::find(list.begin(), list.end(), thunk) == list.end())
    list.push_back(thunk);
}  /* a_vtable_builder::add_thunk */

void a_vtable_builder::compute_this_adjustments()
/*
ComputeThisAdjustments: after a vtable's methods are in, find the entries
whose final overrider is in another subobject and record their thunks.
*/
{
  for (const auto &entry : method_info_) {
    a_fn_id md = entry.first;
    const a_method_info &info = entry.second;
    uint64_t index = info.vtable_index;
    a_overriders::a_info overrider;
    std::string err;
    bool skip = false;

    if (components[index].kind == ck_unused_function)
      continue;
    if (!overriders_->get(md, info.base_offset, overrider, err)) {
      fail(err);
      continue;
    }
    if (info.base_offset_in_layout_class == overrider.offset) {
      auto it = vtable_thunks.find(index);

      /* A return thunk needed by a class that overrides a virtual base gets a
         virtual this adjustment too (to match gcc). */
      if (it == vtable_thunks.end() || it->second.return_adj.is_empty())
        skip = true;
    }
    if (skip)
      continue;
    a_adjustment this_adj = compute_this_adjustment(
        md, info.base_offset_in_layout_class, overrider);

    if (this_adj.is_empty())
      continue;
    auto set_thunk = [&](uint64_t idx) {
      auto inserted = vtable_thunks.emplace(idx, a_thunk_info());

      if (inserted.second) {
        a_fn_id original = ctx_.find_original_method(md);

        inserted.first->second.method = original;
        inserted.first->second.this_class = db().fn(original).owner;
      }
      inserted.first->second.this_adj = this_adj;
    };

    set_thunk(index);
    if (db().fn(md).special == fs_destructor)
      set_thunk(index + 1);
  }
  method_info_.clear();
  if (!building_construction_vtable() && error_.empty()) {
    for (const auto &entry : vtable_thunks) {
      const a_component &comp = components[entry.first];
      a_fn_id md = comp.id;

      if (comp.kind == ck_deleting_dtor)
        continue;
      if (comp.kind != ck_function && comp.kind != ck_complete_dtor) {
        fail("unexpected component kind for a thunk");
        continue;
      }
      if (db().fn(md).owner == md_)
        add_thunk(md, entry.second);
    }
  }
}  /* a_vtable_builder::compute_this_adjustments */

}  /* anonymous namespace */

/* ------------------------------------------------------------------ */
/* The context (ItaniumVTableContext). */

a_vtable_context::a_vtable_context(const a_class_db &db)
  : db_(db)
{
}  /* a_vtable_context::a_vtable_context */

a_vtable_context::~a_vtable_context()
{
}  /* a_vtable_context::~a_vtable_context */

a_fn_id a_vtable_context::find_original_method(a_fn_id f) const
/*
findOriginalMethodInMap: follow the chain of overridden methods to the one
that added the slot.
*/
{
  auto it = original_.find(f);

  while (it != original_.end()) {
    f = it->second;
    it = original_.find(f);
  }
  return f;
}  /* a_vtable_context::find_original_method */

const std::vector<a_fn_id> &a_vtable_context::all_overridden(a_fn_id f)
/*
Every function f overrides, directly or not.  The facts' lists are the whole
set already (abi_facts.h: a function overrides what it overrides indirectly
too; the fact builder derives them that way from CLASS_OVERRIDES), so there is
no closure to compute: clang's ComputeAllOverriddenMethods runs on lists of
direct overrides, which would make this cubic on a deep chain.
*/
{
  return db_.fn(f).overridden;
}  /* a_vtable_context::all_overridden */

int64_t a_vtable_context::virtual_base_offset_offset(a_class_id c,
                                                     a_class_id vbase,
                                                     std::string &error)
/*
getVirtualBaseOffsetOffset: where the offset of virtual base `vbase` is stored
in the vtable of class c.
*/
{
  auto key = std::make_pair(c, vbase);
  auto it = vbase_oo_.find(key);

  if (it == vbase_oo_.end()) {
    a_vcall_builder builder(db_, c, c, nullptr, a_subobject{c, 0}, false, 0);

    if (!builder.error().empty())
      error = builder.error();
    for (const auto &entry : builder.vbase_offset_offsets())
      vbase_oo_[std::make_pair(c, entry.first)] = entry.second;
    it = vbase_oo_.find(key);
    if (it == vbase_oo_.end() && error.empty())
      error = "class " + number(vbase) + " is not a virtual base of class " +
              number(c);
  }
  return it != vbase_oo_.end() ? it->second : 0;
}  /* a_vtable_context::virtual_base_offset_offset */

const a_vtable_info &a_vtable_context::vtable_info(a_class_id c,
                                                   std::string &error)
/*
computeVTableRelatedInformation: lay out the vtable group of class c once.
*/
{
  auto found = info_.find(c);
  const a_vtable_info *result = &empty_info_;

  if (found != info_.end()) {
    result = &found->second;
  } else if (c >= db_.classes.size() || !db_.cls(c).is_dynamic) {
    error = "class " + number(c) + " has no vtable";
  } else {
    a_vtable_builder builder(*this, c, 0, false, c);

    if (builder.run(error)) {
      a_vtable_info info;

      info.layout.vtable_indices = builder.vtable_indices;
      info.layout.components = builder.components;
      info.layout.address_points = builder.address_points;
      for (const auto &entry : builder.vtable_thunks)
        info.layout.thunks.push_back(entry);
      info.method_indices = builder.method_indices;
      info.vbase_offset_offsets = builder.vbase_offset_offsets;
      info.thunks = builder.thunks;
      for (const auto &entry : builder.vbase_offset_offsets)
        vbase_oo_[std::make_pair(c, entry.first)] = entry.second;
      result = &info_.emplace(c, std::move(info)).first->second;
    }
  }
  return *result;
}  /* a_vtable_context::vtable_info */

std::unique_ptr<a_vtable_layout> a_vtable_context::construction_vtable_layout(
    a_class_id most_derived, int64_t most_derived_offset,
    bool most_derived_is_virtual, a_class_id layout_class,
    std::string &error)
/*
createConstructionVTableLayout.
*/
{
  std::unique_ptr<a_vtable_layout> result;
  a_vtable_builder builder(*this, most_derived, most_derived_offset,
                           most_derived_is_virtual, layout_class);

  if (builder.run(error)) {
    result.reset(new a_vtable_layout);
    result->vtable_indices = builder.vtable_indices;
    result->components = builder.components;
    result->address_points = builder.address_points;
    for (const auto &entry : builder.vtable_thunks)
      result->thunks.push_back(entry);
  }
  return result;
}  /* a_vtable_context::construction_vtable_layout */

}  /* namespace edg_abi */
