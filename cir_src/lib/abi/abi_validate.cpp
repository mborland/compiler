/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_validate.cpp -- the deep consistency check of the class facts (what the
ECIL 1.2 decoder will require of CLASS_INFO, CLASS_BASES, CLASS_VFUNCS,
CLASS_OVERRIDES and the virtual base fields).  Non-recursive.
*/

#include <set>

#include "abi_facts.h"

namespace edg_abi {

namespace {

std::string number(int64_t v)
/*
Decimal text of v.
*/
{
  return std::to_string(static_cast<long long>(v));
}  /* number */

void collect_subobjects(const a_class_db &db, a_class_id root,
                        std::set<std::pair<a_class_id, int64_t>> &out)
/*
The (class, offset) of every subobject of `root`, the class itself first: the
non-virtual bases at their offsets and each virtual base once at the class's
virtual base offset.  Explicit stack.
*/
{
  struct a_frame {
    a_class_id  cls;
    int64_t     offset;
    size_t      next;
  };  /* a_frame */
  std::vector<a_frame> stack;
  std::set<a_class_id> seen_vbases;

  out.insert(std::make_pair(root, int64_t(0)));
  stack.push_back({root, 0, 0});
  while (!stack.empty()) {
    a_frame &top = stack.back();
    const a_class_fact &c = db.cls(top.cls);

    if (top.next >= c.bases.size()) {
      stack.pop_back();
    } else {
      const a_base_fact &b = c.bases[top.next++];
      int64_t offset = 0;
      bool skip = false;

      if (b.is_virtual) {
        if (!seen_vbases.insert(b.cls).second ||
            !db.find_vbase_offset(root, b.cls, offset))
          skip = true;
      } else {
        offset = top.offset + b.offset;
      }
      if (!skip) {
        out.insert(std::make_pair(b.cls, offset));
        stack.push_back({b.cls, offset, 0});
      }
    }
  }
}  /* collect_subobjects */

/* "Is class a an ancestor of class d": a bit matrix over the classes, built in
   one pass over the inheritance graph (explicit stack).  Chains of n levels
   need n * n / 8 bytes, so it is built only up to a bound; beyond it the
   caller relies on the existence checks of the override lists. */
struct a_ancestry {
  bool                  enabled = false;
  size_t                words = 0;
  std::vector<uint64_t> bits;

  bool is_ancestor(a_class_id ancestor, a_class_id derived) const
    {
      return (bits[static_cast<size_t>(derived) * words + ancestor / 64] >>
              (ancestor % 64) & 1) != 0;
    }
};  /* a_ancestry */

const size_t ancestry_class_limit = 16384;

a_ancestry build_ancestry(const a_class_db &db)
/*
The ancestor matrix of the classes (a post-order walk: a class's row is the
union of its bases' rows and the bases themselves).
*/
{
  a_ancestry result;
  const size_t n = db.classes.size();

  if (n <= ancestry_class_limit) {
    struct a_frame {
      a_class_id  cls;
      size_t      next;
    };  /* a_frame */
    std::vector<uint8_t> done(n, 0);
    std::vector<a_frame> stack;

    result.enabled = true;
    result.words = (n + 63) / 64;
    result.bits.assign(n * result.words, 0);
    for (size_t root = 0; root < n; ++root) {
      if (done[root])
        continue;
      stack.push_back({static_cast<a_class_id>(root), 0});
      while (!stack.empty()) {
        a_frame &top = stack.back();
        const a_class_fact &c = db.cls(top.cls);

        if (top.next < c.bases.size()) {
          a_class_id b = c.bases[top.next++].cls;

          if (!done[b])
            stack.push_back({b, 0});
        } else {
          uint64_t *row = &result.bits[static_cast<size_t>(top.cls) *
                                       result.words];

          for (const a_base_fact &b : c.bases) {
            const uint64_t *brow = &result.bits[static_cast<size_t>(b.cls) *
                                                result.words];

            for (size_t w = 0; w < result.words; ++w)
              row[w] |= brow[w];
            row[b.cls / 64] |= uint64_t(1) << (b.cls % 64);
          }
          done[top.cls] = 1;
          stack.pop_back();
        }
      }
    }
  }
  return result;
}  /* build_ancestry */

bool has_own_storage(const a_class_db &db, a_class_id c, a_class_id query)
/*
clang's CodeGenUtils::hasOwnStorage(Decl = c, Query = query): false when some
class on the way down the bases (c included) has `query` as its primary
virtual base, which then lives inside that base and has no storage of its
own.  Explicit stack.
*/
{
  std::vector<a_class_id> stack(1, c);
  std::set<a_class_id> seen;
  bool own = true;

  while (own && !stack.empty()) {
    a_class_id x = stack.back();

    stack.pop_back();
    if (!seen.insert(x).second)
      continue;
    const a_class_fact &cf = db.cls(x);

    if (cf.primary_virtual && cf.primary == query)
      own = false;
    for (const a_base_fact &b : cf.bases)
      stack.push_back(b.cls);
  }
  return own;
}  /* has_own_storage */

}  /* anonymous namespace */

std::string a_class_db::validate_deep()
/*
The consistency checks of the class facts.
*/
{
  std::string problem = validate();
  const size_t nclasses = classes.size();
  a_ancestry ancestry;

  if (problem.empty())
    ancestry = build_ancestry(*this);

  for (size_t f = 0; problem.empty() && f < fns.size(); ++f) {
    const a_fn_fact &fn = fns[f];

    if ((fn.special == fs_destructor) != (fn.sig == 0))
      problem = "function " + fn.name + ": sig 0 is for destructors only";
    else if (!classes[fn.owner].is_dynamic)
      problem = "function " + fn.name + " is declared in a class without a "
                "vptr";
    for (size_t k = 0; problem.empty() && k < fn.overridden.size(); ++k) {
      a_class_id g = fns[fn.overridden[k]].owner;

      if (g == fn.owner ||
          (ancestry.enabled && !ancestry.is_ancestor(g, fn.owner)))
        problem = "function " + fn.name + " overrides a function of a "
                  "class that is not a base of its class";
    }
  }
  for (size_t c = 0; problem.empty() && c < nclasses; ++c) {
    const a_class_fact &cf = classes[c];
    std::set<std::pair<a_class_id, int64_t>> subobjects;
    bool have_subobjects = false;

    if (cf.nvsize < 0 || cf.size < cf.nvsize)
      problem = "class " + cf.name + ": bad size/nvsize";
    if (!cf.is_dynamic && (!cf.fns.empty() || cf.nslots != 0 ||
                           cf.primary != no_id || !cf.vbases.empty()))
      problem = "class " + cf.name + " has virtual functions, a primary base "
                "or virtual bases but no vptr";
    for (size_t k = 0; problem.empty() && k < cf.bases.size(); ++k) {
      const a_base_fact &b = cf.bases[k];
      const a_class_fact &bf = classes[b.cls];

      /* An empty class has no storage to fit or to keep out of the
         non-virtual part. */
      if (bf.is_empty)
        continue;
      if (b.offset < 0 || b.offset + bf.nvsize > cf.size)
        problem = "class " + cf.name + ": base " + bf.name + " at offset " +
                  number(b.offset) + " does not fit";
      else if (b.is_virtual && b.offset < cf.nvsize && bf.nvsize != 0 &&
               has_own_storage(*this, static_cast<a_class_id>(c), b.cls))
        problem = "class " + cf.name + ": virtual base " + bf.name +
                  " is inside the non-virtual part";
    }
    for (size_t k = 0; problem.empty() && k < cf.vbases.size(); ++k) {
      const a_base_fact &b = cf.vbases[k];

      if (!classes[b.cls].is_empty &&
          (b.offset < 0 || b.offset + classes[b.cls].nvsize > cf.size))
        problem = "class " + cf.name + ": virtual base " +
                  classes[b.cls].name + " does not fit";
    }
    if (problem.empty() && cf.primary != no_id) {
      int64_t off = -1;

      if (!cf.is_dynamic || !classes[cf.primary].is_dynamic) {
        problem = "class " + cf.name + ": the primary base has no vptr";
      } else if (cf.primary_virtual) {
        if (!find_vbase_offset(c, cf.primary, off) || off != 0)
          problem = "class " + cf.name + ": the primary virtual base is not "
                    "a virtual base at offset 0";
      } else if (!find_base_offset(c, cf.primary, off) || off != 0) {
        problem = "class " + cf.name + ": the primary base is not a direct "
                  "base at offset 0";
      }
      if (problem.empty() && cf.nslots < classes[cf.primary].nslots)
        problem = "class " + cf.name + ": fewer slots than its primary base";
    }
    for (size_t k = 0; problem.empty() && k < cf.fns.size(); ++k) {
      const a_fn_fact &fn = fns[cf.fns[k]];
      int64_t last = fn.slot + (fn.special == fs_destructor ? 1 : 0);

      if (fn.slot < 0 || last >= static_cast<int64_t>(cf.nslots))
        problem = "function " + fn.name + ": slot outside [0, nslots)";
    }
    for (size_t k = 0; problem.empty() && k < cf.overrides.size(); ++k) {
      const a_override_fact &o = cf.overrides[k];

      if (!have_subobjects) {
        collect_subobjects(*this, static_cast<a_class_id>(c), subobjects);
        have_subobjects = true;
      }
      if (subobjects.count(std::make_pair(fns[o.fn].owner, o.sub_off)) == 0)
        problem = "class " + cf.name + ": no subobject of " +
                  classes[fns[o.fn].owner].name + " at offset " +
                  number(o.sub_off) + " for the override of " + fns[o.fn].name;
      else if (subobjects.count(std::make_pair(fns[o.ovr].owner,
                                               o.ovr_off)) == 0)
        problem = "class " + cf.name + ": no subobject of " +
                  classes[fns[o.ovr].owner].name + " at offset " +
                  number(o.ovr_off) + " for the overrider " + fns[o.ovr].name;
    }
  }
  return problem;
}  /* a_class_db::validate_deep */

}  /* namespace edg_abi */
