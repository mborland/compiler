/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_facts.cpp -- validation and lookup tables of the class facts.  Nothing
here recurses: the inheritance graph is checked for cycles with a heap stack.
*/

#include "abi_facts.h"

namespace edg_abi {

namespace {

std::string number(uint64_t v)
/*
Decimal text of v.
*/
{
  return std::to_string(static_cast<unsigned long long>(v));
}  /* number */

}  /* anonymous namespace */

std::string a_class_db::validate()
/*
Check ids, the layout of the inheritance graph and the override facts, and
build the lookup tables.
*/
{
  std::string problem;
  const size_t nclasses = classes.size();
  const size_t nfns = fns.size();

  base_offsets_.clear();
  vbase_offsets_.clear();
  overrides_.clear();
  for (size_t f = 0; problem.empty() && f < nfns; ++f) {
    const a_fn_fact &fact = fns[f];

    if (fact.owner >= nclasses)
      problem = "function " + number(f) + " has an unknown owner class";
    else if (fact.ret_class != no_id && fact.ret_class >= nclasses)
      problem = "function " + number(f) + " has an unknown result class";
    for (size_t k = 0; problem.empty() && k < fact.overridden.size(); ++k) {
      if (fact.overridden[k] >= nfns)
        problem = "function " + number(f) + " overrides an unknown function";
    }
  }
  for (size_t c = 0; problem.empty() && c < nclasses; ++c) {
    const a_class_fact &fact = classes[c];

    if (fact.primary != no_id && fact.primary >= nclasses)
      problem = "class " + number(c) + " has an unknown primary base";
    if (fact.nvsize > fact.size)
      problem = "class " + number(c) + ": nvsize exceeds size";
    for (size_t k = 0; problem.empty() && k < fact.bases.size(); ++k) {
      const a_base_fact &b = fact.bases[k];

      if (b.cls >= nclasses || b.cls == c)
        problem = "class " + number(c) + " has an unknown or own base";
      else if (!b.is_virtual)
        base_offsets_[a_pair(c, b.cls)] = b.offset;
    }
    for (size_t k = 0; problem.empty() && k < fact.vbases.size(); ++k) {
      const a_base_fact &b = fact.vbases[k];

      if (b.cls >= nclasses || b.cls == c || !b.is_virtual)
        problem = "class " + number(c) + " has a bad virtual base entry";
      else
        vbase_offsets_[a_pair(c, b.cls)] = b.offset;
    }
    for (size_t k = 0; problem.empty() && k < fact.bases.size(); ++k) {
      const a_base_fact &b = fact.bases[k];

      if (b.is_virtual &&
          vbase_offsets_.find(a_pair(c, b.cls)) == vbase_offsets_.end())
        problem = "class " + number(c) + ": a direct virtual base is missing "
                  "from its virtual base list";
    }
    for (size_t k = 0; problem.empty() && k < fact.fns.size(); ++k) {
      if (fact.fns[k] >= nfns || fns[fact.fns[k]].owner != c)
        problem = "class " + number(c) + " lists a function it does not own";
    }
    for (size_t k = 0; problem.empty() && k < fact.overrides.size(); ++k) {
      const a_override_fact &o = fact.overrides[k];

      if (o.fn >= nfns || o.ovr >= nfns) {
        problem = "class " + number(c) + " has an override of an unknown "
                  "function";
      } else {
        a_override_value value = {o.ovr, o.ovr_off};
        auto key = std::make_pair(a_pair(c, o.fn),
                                  static_cast<uint64_t>(o.sub_off));

        if (!overrides_.emplace(key, value).second)
          problem = "class " + number(c) + " has two final overriders for "
                    "one function at one subobject";
      }
    }
  }
  if (problem.empty()) {
    /* Cycles: a depth-first walk with colors on a heap stack. */
    std::vector<uint8_t> color(nclasses, 0);   /* 0 new, 1 open, 2 done */
    std::vector<std::pair<a_class_id, size_t>> stack;

    for (size_t root = 0; problem.empty() && root < nclasses; ++root) {
      if (color[root] != 0)
        continue;
      stack.push_back(std::make_pair(static_cast<a_class_id>(root), 0));
      color[root] = 1;
      while (problem.empty() && !stack.empty()) {
        a_class_id c = stack.back().first;
        size_t index = stack.back().second;

        if (index < classes[c].bases.size()) {
          a_class_id b = classes[c].bases[index].cls;

          stack.back().second = index + 1;
          if (color[b] == 1) {
            problem = "the inheritance graph has a cycle at class " +
                      number(b);
          } else if (color[b] == 0) {
            color[b] = 1;
            stack.push_back(std::make_pair(b, 0));
          }
        } else {
          color[c] = 2;
          stack.pop_back();
        }
      }
    }
  }
  return problem;
}  /* a_class_db::validate */

bool a_class_db::find_base_offset(a_class_id c, a_class_id base,
                                  int64_t &offset) const
/*
The offset of the direct non-virtual base `base` in class c.
*/
{
  auto it = base_offsets_.find(a_pair(c, base));
  bool found = it != base_offsets_.end();

  if (found)
    offset = it->second;
  return found;
}  /* a_class_db::find_base_offset */

bool a_class_db::find_vbase_offset(a_class_id c, a_class_id vbase,
                                   int64_t &offset) const
/*
The offset of the virtual base `vbase` (direct or indirect) in class c.
*/
{
  auto it = vbase_offsets_.find(a_pair(c, vbase));
  bool found = it != vbase_offsets_.end();

  if (found)
    offset = it->second;
  return found;
}  /* a_class_db::find_vbase_offset */

bool a_class_db::find_override(a_class_id c, a_fn_id f, int64_t sub_off,
                               a_fn_id &ovr, int64_t &ovr_off) const
/*
The final overrider of f at the subobject at sub_off of class c.
*/
{
  auto it = overrides_.find(std::make_pair(a_pair(c, f),
                                           static_cast<uint64_t>(sub_off)));
  bool found = it != overrides_.end();

  if (found) {
    ovr = it->second.ovr;
    ovr_off = it->second.ovr_off;
  }
  return found;
}  /* a_class_db::find_override */

}  /* namespace edg_abi */
