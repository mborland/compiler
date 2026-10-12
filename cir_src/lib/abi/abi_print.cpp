/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_print.cpp -- vtable layout text and the front-end slot checks.

The printers are derived in part from ItaniumVTableBuilder::dumpLayout in
clang/lib/AST/VTableBuilder.cpp (Apache-2.0 WITH LLVM-exception); the function
names are the facts' display names (no result type).
*/

#include "abi_print.h"

#include <algorithm>
#include <cstdio>
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

std::string index_prefix(uint64_t index)
/*
"%4d | ".
*/
{
  char buffer[32];

  std::snprintf(buffer, sizeof buffer, "%4llu | ",
                static_cast<unsigned long long>(index));
  return buffer;
}  /* index_prefix */

std::string adjustment_text(const a_adjustment &a, const char *offset_name)
/*
"N non-virtual[, M <offset_name>]".
*/
{
  std::string text = number(a.non_virtual) + " non-virtual";

  if (a.offset_offset != 0)
    text += ", " + number(a.offset_offset) + " " + offset_name;
  return text;
}  /* adjustment_text */

void print_components(std::string &out, const a_class_db &db,
                      const a_vtable_layout &layout)
/*
The component lines with the address point notes between them.
*/
{
  std::multimap<uint64_t, a_subobject> by_index;
  std::map<uint64_t, const a_thunk_info *> thunks;

  for (const auto &ap : layout.address_points) {
    uint64_t index = layout.vtable_indices[ap.second.vtable_index] +
                     ap.second.address_point_index;

    by_index.insert(std::make_pair(index, ap.first));
  }
  for (const auto &t : layout.thunks)
    thunks[t.first] = &t.second;
  for (size_t i = 0; i < layout.components.size(); ++i) {
    const a_component &comp = layout.components[i];
    auto thunk_it = thunks.find(i);
    const a_thunk_info *thunk = thunk_it != thunks.end() ? thunk_it->second
                                                         : nullptr;
    std::string line = index_prefix(i);

    switch (comp.kind) {
    case ck_vcall_offset:
      line += "vcall_offset (" + number(comp.offset) + ")";
      break;
    case ck_vbase_offset:
      line += "vbase_offset (" + number(comp.offset) + ")";
      break;
    case ck_offset_to_top:
      line += "offset_to_top (" + number(comp.offset) + ")";
      break;
    case ck_rtti:
      line += db.cls(comp.id).name + " RTTI";
      break;
    case ck_function: {
      const a_fn_fact &f = db.fn(comp.id);

      line += f.name;
      if (f.is_pure)
        line += " [pure]";
      if (f.is_deleted)
        line += " [deleted]";
      if (thunk != nullptr && !thunk->is_empty()) {
        if (!thunk->return_adj.is_empty()) {
          line += "\n       [return adjustment: " +
                  adjustment_text(thunk->return_adj, "vbase offset offset") +
                  "]";
          if (thunk->method != no_id)
            line += " method: " + db.fn(thunk->method).name;
        }
        if (!thunk->this_adj.is_empty()) {
          line += "\n       [this adjustment: " +
                  adjustment_text(thunk->this_adj, "vcall offset offset") +
                  "]";
          if (thunk->method != no_id)
            line += " method: " + db.fn(thunk->method).name;
        }
      }
      break;
    }
    case ck_complete_dtor:
    case ck_deleting_dtor: {
      const a_fn_fact &f = db.fn(comp.id);

      line += f.name + (comp.kind == ck_complete_dtor ? " [complete]"
                                                      : " [deleting]");
      if (f.is_pure)
        line += " [pure]";
      if (thunk != nullptr && !thunk->is_empty()) {
        if (!thunk->this_adj.is_empty())
          line += "\n       [this adjustment: " +
                  adjustment_text(thunk->this_adj, "vcall offset offset") +
                  "]";
        if (thunk->method != no_id)
          line += " method: " + db.fn(thunk->method).name;
      }
      break;
    }
    case ck_unused_function: {
      const a_fn_fact &f = db.fn(comp.id);

      line += "[unused] " + f.name;
      if (f.is_pure)
        line += " [pure]";
      break;
    }
    }
    out += line + "\n";
    uint64_t next = i + 1;
    size_t count = by_index.count(next);

    if (count == 1) {
      const a_subobject &b = by_index.find(next)->second;

      out += "       -- (" + db.cls(b.cls).name + ", " + number(b.offset) +
             ") vtable address --\n";
    } else if (count > 1) {
      int64_t offset = by_index.lower_bound(next)->second.offset;
      std::set<std::string> names;
      auto range = by_index.equal_range(next);

      for (auto it = range.first; it != range.second; ++it)
        names.insert(db.cls(it->second.cls).name);
      for (const std::string &name : names)
        out += "       -- (" + name + ", " + number(offset) +
               ") vtable address --\n";
    }
  }
}  /* print_components */

}  /* anonymous namespace */

std::string print_vtable(a_vtable_context &ctx, a_class_id c,
                         std::string &error)
/*
dumpLayout for the vtable of class c.
*/
{
  const a_class_db &db = ctx.db();
  const a_vtable_info &info = ctx.vtable_info(c, error);
  std::string out;

  if (error.empty()) {
    const a_class_fact &cf = db.cls(c);

    out += "Vtable for '" + cf.name + "' (" +
           number(info.layout.components.size()) + " entries).\n";
    print_components(out, db, info.layout);
    out += "\n";
    if (!cf.vbases.empty()) {
      std::map<std::string, int64_t> names;

      for (const auto &entry : info.vbase_offset_offsets)
        names[db.cls(entry.first).name] = entry.second;
      out += "Virtual base offset offsets for '" + cf.name + "' (" +
             number(names.size()) + (names.size() == 1 ? " entry" : " entries")
             + ").\n";
      for (const auto &entry : names)
        out += "   " + entry.first + " | " + number(entry.second) + "\n";
      out += "\n";
    }
    if (!info.thunks.empty()) {
      std::map<std::string, a_fn_id> methods;

      for (const auto &entry : info.thunks)
        methods[db.fn(entry.first).name] = entry.first;
      for (const auto &m : methods) {
        std::vector<a_thunk_info> list = info.thunks.find(m.second)->second;

        std::sort(list.begin(), list.end(),
                  [](const a_thunk_info &l, const a_thunk_info &r) {
                    if (!(l.this_adj == r.this_adj))
                      return l.this_adj < r.this_adj;
                    return l.return_adj < r.return_adj;
                  });
        out += "Thunks for '" + m.first + "' (" + number(list.size()) +
               (list.size() == 1 ? " entry" : " entries") + ").\n";
        for (size_t i = 0; i < list.size(); ++i) {
          const a_thunk_info &t = list[i];

          out += index_prefix(i);
          if (!t.return_adj.is_empty()) {
            out += "return adjustment: " +
                   adjustment_text(t.return_adj, "vbase offset offset");
            if (!t.this_adj.is_empty())
              out += "\n       ";
          }
          if (!t.this_adj.is_empty())
            out += "this adjustment: " +
                   adjustment_text(t.this_adj, "vcall offset offset");
          out += "\n";
        }
        out += "\n";
      }
    }
    std::map<uint64_t, std::string> indices;

    for (a_fn_id f : cf.fns) {
      auto it = info.method_indices.find(f);

      if (it == info.method_indices.end())
        continue;
      if (db.fn(f).special == fs_destructor) {
        indices[it->second] = db.fn(f).name + " [complete]";
        indices[it->second + 1] = db.fn(f).name + " [deleting]";
      } else {
        indices[it->second] = db.fn(f).name;
      }
    }
    if (!indices.empty()) {
      out += "VTable indices for '" + cf.name + "' (" + number(indices.size())
             + " entries).\n";
      for (const auto &entry : indices)
        out += index_prefix(entry.first) + entry.second + "\n";
    }
    out += "\n";
  }
  return out;
}  /* print_vtable */

std::string print_construction_vtable(const a_class_db &db,
                                      const a_vtable_layout &layout,
                                      a_class_id most_derived,
                                      int64_t most_derived_offset,
                                      a_class_id layout_class)
/*
dumpLayout for a construction vtable.
*/
{
  std::string out = "Construction vtable for ('" + db.cls(most_derived).name +
                    "', " + number(most_derived_offset) + ") in '" +
                    db.cls(layout_class).name + "' (" +
                    number(layout.components.size()) + " entries).\n";

  print_components(out, db, layout);
  out += "\n";
  return out;
}  /* print_construction_vtable */

std::string print_vtt(const a_class_db &db, const a_vtt_layout &vtt,
                      a_class_id c)
/*
The VTT of class c as text.
*/
{
  std::string out = "VTT for '" + db.cls(c).name + "' (" +
                    number(vtt.components.size()) + " entries).\n";

  for (size_t i = 0; i < vtt.components.size(); ++i) {
    const a_vtt_component &comp = vtt.components[i];

    out += index_prefix(i);
    if (comp.present) {
      const a_vtt_vtable &vt = vtt.vtables[comp.vtable_index];

      out += "vtable " + number(comp.vtable_index) + " (" +
             db.cls(vt.base.cls).name + ", " + number(vt.base.offset) +
             (vt.base_is_virtual ? ", virtual" : "") + "): address point (" +
             db.cls(comp.vtable_base.cls).name + ", " +
             number(comp.vtable_base.offset) + ")";
    } else {
      out += "(declaration)";
    }
    out += "\n";
  }
  return out + "\n";
}  /* print_vtt */

}  /* namespace edg_abi */
