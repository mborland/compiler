/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_builder.cpp -- the fact builder: dense ids for the stream's class and
function ids, forward references, the first problem kept.
*/

#include "abi_builder.h"

#include <algorithm>

#include "abi_print.h"

namespace edg_abi {

namespace {

std::string id_text(a_key v)
/*
Text of a stream id for a message.
*/
{
  return "#" + std::to_string(static_cast<unsigned long long>(v));
}  /* id_text */

}  /* anonymous namespace */

a_class_id a_fact_builder::class_of(a_key key)
/*
The dense id of a class; a class not seen yet gets an empty entry that
set_class must define before finish.
*/
{
  auto it = class_ids_.find(key);

  if (it == class_ids_.end()) {
    a_class_fact c;

    c.key = key;
    c.size = 0;
    c.nvsize = 0;
    c.is_dynamic = false;
    c.primary = no_id;
    c.primary_virtual = false;
    c.nslots = 0;
    db_.classes.push_back(c);
    class_keys_.push_back(key);
    class_defined_.push_back(0);
    primary_key_.push_back(0);
    it = class_ids_.emplace(key, static_cast<a_class_id>(
                                  db_.classes.size() - 1)).first;
  }
  return it->second;
}  /* a_fact_builder::class_of */

a_fn_id a_fact_builder::fn_of(a_key key)
/*
The dense id of a function; same forward-reference scheme.
*/
{
  auto it = fn_ids_.find(key);

  if (it == fn_ids_.end()) {
    a_fn_fact f;

    f.key = key;
    f.owner = no_id;
    f.slot = -1;
    f.is_pure = false;
    f.is_deleted = false;
    f.special = fs_none;
    f.is_implicit = false;
    f.ref_result = false;
    f.sig = 0;
    f.ret_class = no_id;
    db_.fns.push_back(f);
    fn_keys_.push_back(key);
    fn_defined_.push_back(0);
    ret_key_.push_back(0);
    it = fn_ids_.emplace(key, static_cast<a_fn_id>(
                               db_.fns.size() - 1)).first;
  }
  return it->second;
}  /* a_fact_builder::fn_of */

void a_fact_builder::set_class(a_key cls, const a_class_info &info)
/*
CLASS_INFO.
*/
{
  a_class_id id = class_of(cls);
  a_class_fact &c = db_.classes[id];

  if (class_defined_[id]) {
    if (problem_.empty())
      problem_ = "class " + id_text(cls) + " has two CLASS_INFO records";
  } else {
    class_defined_[id] = 1;
    c.name = info.name;
    c.mangled = info.mangled;
    c.size = info.size;
    c.nvsize = info.nvsize;
    c.nv_data_end = info.nv_data_end;
    c.is_dynamic = info.is_dynamic;
    c.is_empty = info.is_empty;
    c.layout_only = info.layout_only;
    c.primary_virtual = info.primary_virtual;
    c.nslots = info.nslots;
    primary_key_[id] = info.primary;
    c.primary = info.has_primary ? 0 : no_id;   /* Resolved in finish. */
  }
}  /* a_fact_builder::set_class */

void a_fact_builder::set_plain_class(a_key cls, const std::string &name,
                                     int64_t size, int64_t data_size,
                                     bool is_empty)
/*
A class without CLASS_INFO (see the header): not dynamic, no slots, its
non-virtual size is its data size.
*/
{
  a_class_info info;

  info.name = name;
  info.size = size;
  info.nvsize = data_size;
  info.nv_data_end = data_size;
  info.is_empty = is_empty;
  set_class(cls, info);
}  /* a_fact_builder::set_plain_class */

void a_fact_builder::add_base(a_key cls, a_key base, bool is_virtual,
                              bool is_public, int64_t offset)
/*
A direct base, declaration order.
*/
{
  a_class_id c = class_of(cls);
  a_class_id b = class_of(base);
  a_base_fact fact;

  fact.cls = b;
  fact.is_virtual = is_virtual;
  fact.offset = offset;
  fact.is_public = is_public;
  db_.classes[c].bases.push_back(fact);
}  /* a_fact_builder::add_base */

void a_fact_builder::add_vbase(a_key cls, a_key base, bool is_public,
                               int64_t offset)
/*
An entry of the virtual base list.
*/
{
  a_class_id c = class_of(cls);
  a_class_id b = class_of(base);
  a_base_fact fact;

  fact.cls = b;
  fact.is_virtual = true;
  fact.offset = offset;
  fact.is_public = is_public;
  db_.classes[c].vbases.push_back(fact);
}  /* a_fact_builder::add_vbase */

void a_fact_builder::add_vfunc(a_key cls, a_key fn, const a_vfunc_info &info)
/*
A virtual function declared in a class.
*/
{
  a_class_id c = class_of(cls);
  a_fn_id f = fn_of(fn);
  a_fn_fact &fact = db_.fns[f];

  if (fn_defined_[f]) {
    if (problem_.empty())
      problem_ = "function " + id_text(fn) + " is listed twice";
  } else {
    fn_defined_[f] = 1;
    fact.owner = c;
    fact.slot = info.slot;
    fact.is_pure = info.is_pure;
    fact.is_deleted = info.is_deleted;
    fact.special = info.special;
    fact.is_implicit = info.is_implicit;
    fact.ref_result = info.ref_result;
    fact.sig = info.sig;
    fact.name = info.name;
    fact.mangled = info.mangled;
    ret_key_[f] = info.ret_class;
    fact.ret_class = info.has_ret_class ? 0 : no_id;   /* Resolved later. */
    db_.classes[c].fns.push_back(f);
  }
}  /* a_fact_builder::add_vfunc */

void a_fact_builder::add_overridden(a_key fn, a_key overridden_fn)
/*
`fn` overrides `overridden_fn`.
*/
{
  a_fn_id f = fn_of(fn);
  a_fn_id g = fn_of(overridden_fn);

  db_.fns[f].overridden.push_back(g);
}  /* a_fact_builder::add_overridden */

void a_fact_builder::add_override(a_key cls, a_key fn, int64_t sub_off,
                                  a_key overrider, int64_t overrider_off)
/*
CLASS_OVERRIDES.
*/
{
  a_class_id c = class_of(cls);
  a_override_fact o;

  o.fn = fn_of(fn);
  o.sub_off = sub_off;
  o.ovr = fn_of(overrider);
  o.ovr_off = overrider_off;
  db_.classes[c].overrides.push_back(o);
}  /* a_fact_builder::add_override */

std::string a_fact_builder::finish(a_class_db &out)
/*
Resolve the references that were forward and run the deep check.
*/
{
  std::string problem = problem_;

  for (size_t c = 0; problem.empty() && c < db_.classes.size(); ++c) {
    if (!class_defined_[c]) {
      problem = "class " + id_text(class_keys_[c]) + " is used but has no "
                "CLASS_INFO";
    } else if (db_.classes[c].primary != no_id) {
      auto it = class_ids_.find(primary_key_[c]);

      if (it == class_ids_.end())
        problem = "class " + id_text(class_keys_[c]) + " names an unknown "
                  "primary base";
      else
        db_.classes[c].primary = it->second;
    }
  }
  for (size_t f = 0; problem.empty() && f < db_.fns.size(); ++f) {
    if (!fn_defined_[f]) {
      problem = "function " + id_text(fn_keys_[f]) + " is used but is not a "
                "listed virtual function";
    } else if (db_.fns[f].ret_class != no_id) {
      auto it = class_ids_.find(ret_key_[f]);

      if (it == class_ids_.end())
        problem = "function " + id_text(fn_keys_[f]) + " names an unknown "
                  "result class";
      else
        db_.fns[f].ret_class = it->second;
    }
  }
  if (problem.empty()) {
    /* The functions each function overrides (directly or not): a function
       that overrides g_fn is the final overrider of g_fn in its own class's
       list, so they come from CLASS_OVERRIDES. */
    for (size_t c = 0; c < db_.classes.size(); ++c) {
      for (const a_override_fact &o : db_.classes[c].overrides) {
        if (o.ovr < db_.fns.size() && db_.fns[o.ovr].owner == c)
          db_.fns[o.ovr].overridden.push_back(o.fn);
      }
    }
    for (a_fn_fact &fn : db_.fns) {
      std::sort(fn.overridden.begin(), fn.overridden.end());
      fn.overridden.erase(std::unique(fn.overridden.begin(),
                                      fn.overridden.end()),
                          fn.overridden.end());
    }
    problem = db_.validate_deep();
  }
  if (problem.empty())
    problem = check_all_slots(db_);
  if (problem.empty())
    out = std::move(db_);
  db_ = a_class_db();
  class_ids_.clear();
  fn_ids_.clear();
  class_keys_.clear();
  fn_keys_.clear();
  class_defined_.clear();
  fn_defined_.clear();
  primary_key_.clear();
  ret_key_.clear();
  problem_.clear();
  return problem;
}  /* a_fact_builder::finish */

}  /* namespace edg_abi */
