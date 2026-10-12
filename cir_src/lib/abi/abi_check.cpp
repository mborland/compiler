/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_check.cpp -- checks of the front end's virtual function slot numbers.

The numbers (number.virtual_function) are authoritative: they are clang's
indices from the primary address point (research/m4_fe_facts.md, section 2).
The structural check needs no layout, the second compares them with what the
builder's own AddMethods decisions give.
*/

#include "abi_print.h"

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

}  /* anonymous namespace */

std::string check_slots(a_vtable_context &ctx, a_class_id c)
/*
Prefix chain, contiguity, destructor pairs and coverage of [0, nslots) for the
slots of class c.
*/
{
  const a_class_db &db = ctx.db();
  const a_class_fact &cf = db.cls(c);
  std::string problem;
  uint32_t base_slots = 0;
  std::set<a_class_id> chain;
  std::vector<a_class_id> chain_order;   /* Nearest primary base first. */
  std::vector<int> owner;   /* New slots: which function (index + 1). */
  bool cf_skip = false;

  for (a_class_id p = cf.primary; cf.is_dynamic && p != no_id &&
                                  chain.insert(p).second;
       p = db.cls(p).primary)
    chain_order.push_back(p);
  if (cf.primary != no_id)
    base_slots = db.cls(cf.primary).nslots;
  if (!cf.is_dynamic)
    cf_skip = true;
  if (!cf_skip && cf.nslots < base_slots)
    problem = "class " + cf.name + ": nslots " + number(cf.nslots) +
              " is below its primary base's " + number(base_slots) +
              " (the primary base's slots are a prefix)";
  owner.assign(cf.nslots, 0);
  for (size_t k = 0; !cf_skip && problem.empty() && k < cf.fns.size(); ++k) {
    const a_fn_fact &f = db.fn(cf.fns[k]);
    bool dtor = f.special == fs_destructor;
    int64_t last = f.slot + (dtor ? 1 : 0);
    bool reuses = false;

    if (f.slot < 0) {
      problem = f.name + " has no slot";
      break;
    }
    if (last >= static_cast<int64_t>(cf.nslots)) {
      problem = f.name + ": slot " + number(f.slot) + " is outside "
                "[0, nslots=" + number(cf.nslots) + ")";
      break;
    }
    {
      /* The function it replaces: the nearest overridden one declared in a
         class of the primary chain. */
      const std::vector<a_fn_id> &all = ctx.all_overridden(cf.fns[k]);
      a_fn_id nearest = no_id;

      for (size_t ci = 0; nearest == no_id && ci < chain_order.size(); ++ci) {
        for (a_fn_id g : all) {
          if (nearest == no_id && db.fn(g).owner == chain_order[ci])
            nearest = g;
        }
      }
      if (nearest != no_id) {
        const a_fn_fact &gf = db.fn(nearest);

        if (f.slot == gf.slot) {
          reuses = true;
        } else if (f.slot < static_cast<int64_t>(base_slots)) {
          problem = f.name + ": slot " + number(f.slot) + " differs from "
                    "the slot " + number(gf.slot) + " of " + gf.name +
                    " it overrides in the primary chain";
        } else if (f.ret_class == gf.ret_class) {
          problem = f.name + ": a new slot " + number(f.slot) + " although "
                    "it overrides " + gf.name + " (slot " + number(gf.slot) +
                    ") with the same result class (only a covariant "
                    "override can need a new slot)";
        }
      }
    }
    if (!problem.empty())
      break;
    if (!reuses && f.slot < static_cast<int64_t>(base_slots)) {
      problem = f.name + ": slot " + number(f.slot) + " is in the primary "
                "base's prefix but overrides nothing there";
      break;
    }
    if (f.slot >= static_cast<int64_t>(base_slots)) {
      for (int64_t s = f.slot; s <= last; ++s) {
        if (owner[static_cast<size_t>(s)] != 0)
          problem = f.name + ": slot " + number(s) + " is also used by " +
                    db.fn(cf.fns[static_cast<size_t>(owner[
                        static_cast<size_t>(s)] - 1)]).name;
        owner[static_cast<size_t>(s)] = static_cast<int>(k) + 1;
      }
    } else if (dtor && last >= static_cast<int64_t>(base_slots)) {
      problem = f.name + ": a destructor's two slots straddle the end of the "
                "primary base's prefix";
    }
  }
  for (size_t s = base_slots; !cf_skip && problem.empty() && s < cf.nslots;
       ++s) {
    if (owner[s] == 0)
      problem = "class " + cf.name + ": slot " + number(s) + " of " +
                number(cf.nslots) + " has no function (the new slots must be "
                "contiguous from " + number(base_slots) + ")";
  }
  return problem;
}  /* check_slots */

std::string check_slots_against_builder(a_vtable_context &ctx, a_class_id c)
/*
The front end's slot of each function against the index the builder gives the
vtable entry of the function (covariant functions that get a new slot
included).
*/
{
  const a_class_db &db = ctx.db();
  std::string problem;
  const a_vtable_info &info = ctx.vtable_info(c, problem);

  if (problem.empty()) {
    for (const auto &entry : info.method_indices) {
      const a_fn_fact &f = db.fn(entry.first);

      if (f.slot != entry.second) {
        problem = "class " + db.cls(c).name + ": " + f.name + " has front "
                  "end slot " + number(f.slot) + ", the builder puts it at " +
                  number(entry.second);
        break;
      }
    }
  }
  return problem;
}  /* check_slots_against_builder */

std::string check_all_slots(const a_class_db &db)
/*
check_slots and check_slots_against_builder for every dynamic class that is
not the primary base of another one (see below): the
finish-time check of the CLASS_VFUNCS slots (a fatal diagnostic, because
CALL VIRTUAL uses the front end's slot).
*/
{
  a_vtable_context ctx(db);
  std::string problem;
  std::vector<uint8_t> is_primary(db.classes.size(), 0);

  /* The vtable of a class holds the slots of its whole primary chain, so the
     classes that are the primary base of another class are covered by it:
     only the others are laid out (a deep chain costs one layout, not one
     per level). */
  for (const a_class_fact &cf : db.classes) {
    if (cf.is_dynamic && cf.primary != no_id)
      is_primary[cf.primary] = 1;
  }
  for (a_class_id c = 0; problem.empty() && c < db.classes.size(); ++c) {
    if (!db.cls(c).is_dynamic)
      continue;
    problem = check_slots(ctx, c);
    if (problem.empty() && !is_primary[c])
      problem = check_slots_against_builder(ctx, c);
  }
  return problem;
}  /* check_all_slots */

}  /* namespace edg_abi */
