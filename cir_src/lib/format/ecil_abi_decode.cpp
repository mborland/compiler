/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
ecil_abi_decode.cpp -- see ecil_abi_decode.h.  No recursion: the classes to
define are a worklist.
*/

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "abi_builder.h"
#include "abi_demangle.h"
#include "abi_print.h"
#include "abi_vtable.h"
#include "ecil_abi_decode.h"
#include "edg_cir/ecil_format.h"

namespace ecil {

namespace {

std::string pretty_class(const std::string &mangled)
/*
clang's spelling of the class when the mangling is in the demangler's
subset, else the mangling itself.
*/
{
  std::string text = edg_abi::demangle_class(mangled);

  return text.empty() ? mangled : text;
}  /* pretty_class */

std::string pretty_function(const std::string &symbol)
/*
The same for a function symbol.
*/
{
  std::string text = edg_abi::demangle_function(symbol);

  return text.empty() ? symbol : text;
}  /* pretty_function */

std::string type_text(const a_id_tables &ids, uint32_t g)
/*
clang's spelling of the result type of a virtual function as far as ECIL
carries it: builtin types by width and signedness (char, short, int, long,
__int128: the ABI-identical alternatives cannot be told apart), a pointer
(a reference too) as `T *`, a class by its name, anything else as "?".
*/
{
  std::string stars;
  std::string base;
  uint32_t current = g;
  const a_gid_entry *e = ids.find_gid(current);

  while (e != nullptr && e->kind == gk_ptr) {
    stars += stars.empty() ? " *" : "*";
    current = e->element;
    e = ids.find_gid(current);
  }
  if (e == nullptr) {
    base = "?";
  } else if (e->kind == gk_void) {
    base = "void";
  } else if (e->kind == gk_bool) {
    base = "bool";
  } else if (e->kind == gk_int) {
    bool is_signed = e->role != 0;

    switch (e->extent) {
    case 8: base = is_signed ? "char" : "unsigned char"; break;
    case 16: base = is_signed ? "short" : "unsigned short"; break;
    case 32: base = is_signed ? "int" : "unsigned int"; break;
    case 64: base = is_signed ? "long" : "unsigned long"; break;
    default: base = is_signed ? "__int128" : "unsigned __int128"; break;
    }
  } else if (e->kind == gk_float) {
    base = e->size == 4 ? "float" : e->size == 8 ? "double" : "long double";
  } else if (e->kind == gk_record) {
    const a_class_facts *f = ids.class_facts(current);

    base = f != nullptr && f->mangle_sid != 0
               ? pretty_class(std::string(ids.string(f->mangle_sid))) : "?";
  } else {
    base = "?";
  }
  return base + stars;
}  /* type_text */

edg_abi::a_fn_special special_of(const a_gid_entry &fn,
                                 const std::string &symbol, bool implicit)
/*
The special member kind that orders an implicit virtual function: a
destructor, else (only assignment operators can be implicit and virtual) the
copy or the move assignment, told apart by the parameter's mangling.
*/
{
  edg_abi::a_fn_special special = edg_abi::fs_none;

  if (fn.role & sr_dtor) {
    special = edg_abi::fs_destructor;
  } else if (implicit && symbol.find("aSE") != std::string::npos) {
    special = symbol.find("aSEO") != std::string::npos
                  ? edg_abi::fs_move_assign : edg_abi::fs_copy_assign;
  }
  return special;
}  /* special_of */

}  /* anonymous namespace */

bool build_abi_facts(const a_id_tables &ids, edg_abi::a_class_db &db,
                     std::string &problem)
/*
See ecil_abi_decode.h.
*/
{
  edg_abi::a_fact_builder fb;
  std::vector<uint32_t> work;
  std::set<uint32_t> seen;
  std::set<uint32_t> classes;

  /* The classes with a CLASS_INFO start the worklist, in G-id order; a
     dynamic class whose vtable facts were never sent (its layout facts
     came right away, 12.2) has no virtual functions to check its slots
     against, so it stays out unless a class that has them needs it as a
     base (bases-first: then it has them too). */
  for (const auto &entry : ids.all_class_facts()) {
    if (entry.second.complete && (entry.second.stage & cs_info) &&
        (!entry.second.dynamic || (entry.second.stage & cs_vfuncs)))
      classes.insert(entry.first);
  }
  for (uint32_t g : classes) {
    work.push_back(g);
    seen.insert(g);
  }
  auto note = [&](uint32_t g) {
    const a_class_facts *f = ids.class_facts(g);

    if (f != nullptr && f->complete && seen.insert(g).second)
      work.push_back(g);
  };
  auto text = [&](uint32_t sid) {
    return std::string(ids.string(sid));
  };

  fb.set_pointer_size(8);
  for (size_t next = 0; next < work.size(); ++next) {
    uint32_t g = work[next];
    const a_class_facts &f = *ids.class_facts(g);
    const a_gid_entry *e = ids.find_gid(g);
    std::string mangled = text(f.mangle_sid);
    std::string name = pretty_class(mangled);

    if (name.empty())
      name = "class#" + std::to_string(g);
    if (e == nullptr)
      continue;
    if (f.stage & cs_info) {
      edg_abi::a_class_info info;

      info.name = name;
      info.mangled = mangled;
      info.size = static_cast<int64_t>(f.size);
      info.nvsize = static_cast<int64_t>(f.nvsize);
      info.nv_data_end = static_cast<int64_t>(f.nv_data_end);
      info.is_dynamic = f.dynamic;
      info.is_empty = f.empty;
      /* A dynamic class without vtable facts: its layout is all that a
         covariant function's result class needs. */
      info.layout_only = f.dynamic && !(f.stage & cs_vfuncs);
      if (f.primary_idx >= 0 &&
          static_cast<size_t>(f.primary_idx) < f.fields.size()) {
        const a_class_field &field =
            f.fields[static_cast<size_t>(f.primary_idx)];

        info.has_primary = true;
        info.primary = field.type;
        info.primary_virtual = (field.flags & ECIL_FIELD_VIRTUAL_BASE) != 0;
      }
      info.nslots = static_cast<uint32_t>(f.nslots);
      fb.set_class(g, info);
      for (const a_class_base_entry &base : f.base_entries) {
        const a_class_field &field = f.fields[base.field];

        fb.add_base(g, field.type,
                    (field.flags & ECIL_FIELD_VIRTUAL_BASE) != 0,
                    (base.flags & ECIL_CLASS_BASE_PUBLIC) != 0,
                    static_cast<int64_t>(field.offset));
        note(field.type);
      }
      for (const a_class_field &field : f.fields) {
        if (field.flags & ECIL_FIELD_VIRTUAL_BASE) {
          fb.add_vbase(g, field.type, true,
                       static_cast<int64_t>(field.offset));
          note(field.type);
        }
      }
      for (const a_class_vfunc_entry &entry : f.vfuncs) {
        const a_gid_entry *fn = ids.find_gid(entry.fn);
        const a_gid_entry *ft = fn != nullptr ? ids.find_gid(fn->element)
                                              : nullptr;
        const a_gid_entry *rt = ft != nullptr ? ids.find_gid(ft->element)
                                              : nullptr;
        edg_abi::a_vfunc_info info_fn;
        std::string symbol = fn != nullptr ? text(fn->name_sid) : "";

        info_fn.mangled = symbol;
        info_fn.name = pretty_function(symbol);
        if (info_fn.name.empty())
          info_fn.name = "fn#" + std::to_string(entry.fn);
        /* clang prints the result type in front, except for a destructor. */
        if (fn != nullptr && !(fn->role & sr_dtor) && ft != nullptr) {
          std::string result_type = type_text(ids, ft->element);

          /* "Z *Z::me()", "int Z::f()". */
          info_fn.name = result_type +
                         (!result_type.empty() && result_type.back() == '*'
                              ? "" : " ") + info_fn.name;
        }
        info_fn.slot = static_cast<int32_t>(entry.slot);
        info_fn.is_pure = (entry.flags & ECIL_VFUNC_PURE) != 0;
        info_fn.ref_result = (entry.flags & ECIL_VFUNC_REF_RESULT) != 0;
        info_fn.is_deleted = (entry.flags & ECIL_VFUNC_DELETED) != 0;
        info_fn.is_implicit = (entry.flags & ECIL_VFUNC_IMPLICIT) != 0;
        info_fn.sig = entry.sig;
        if (fn != nullptr)
          info_fn.special = special_of(*fn, symbol, info_fn.is_implicit);
        if (rt != nullptr && rt->kind == gk_ptr) {
          const a_gid_entry *pointee = ids.find_gid(rt->element);
          const a_class_facts *pf = pointee != nullptr
                                        ? ids.class_facts(rt->element)
                                        : nullptr;

          /* Any class may be the result class of a covariant function: the
             layout facts of a dynamic one (the writer sends them), the
             bases of a plain one (12.2). */
          if (pointee != nullptr && pointee->kind == gk_record &&
              pf != nullptr && pf->complete &&
              (!pf->dynamic || (pf->stage & cs_info))) {
            info_fn.has_ret_class = true;
            info_fn.ret_class = rt->element;
            note(rt->element);
          }
        }
        fb.add_vfunc(g, entry.fn, info_fn);
      }
      for (const a_class_override_entry &o : f.overrides)
        fb.add_override(g, o.fn, o.sub_off, o.overrider, o.overrider_off);
    } else {
      /* A class without CLASS_INFO: not dynamic (12.2), so a BASE field is
         a base, and the class has no virtual bases. */
      fb.set_plain_class(g, name, static_cast<int64_t>(f.size),
                         static_cast<int64_t>(f.data_size), f.empty);
      for (const a_class_field &field : f.fields) {
        if (field.flags & ECIL_FIELD_BASE) {
          fb.add_base(g, field.type, false, true,
                      static_cast<int64_t>(field.offset));
          note(field.type);
        }
      }
    }
  }
  problem = fb.finish(db);
  return problem.empty();
}  /* build_abi_facts */

bool print_abi_layouts(const a_id_tables &ids, std::string &out,
                       std::string &problem)
/*
See ecil_abi_decode.h.
*/
{
  edg_abi::a_class_db db;
  bool ok = build_abi_facts(ids, db, problem);

  if (ok) {
    edg_abi::a_vtable_context ctx(db);

    for (edg_abi::a_class_id c = 0; ok && c < db.classes.size(); ++c) {
      const edg_abi::a_class_fact &cf = db.classes[c];
      const a_class_facts *f = ids.class_facts(static_cast<uint32_t>(cf.key));
      std::string error;

      if (!cf.is_dynamic || f == nullptr || !(f->stage & cs_vfuncs))
        continue;
      out += edg_abi::print_vtable(ctx, c, error);
      if (!error.empty()) {
        problem = cf.name + ": " + error;
        ok = false;
        break;
      }
      if (!cf.vbases.empty()) {
        std::unique_ptr<edg_abi::a_vtt_layout> vtt = ctx.vtt_layout(c, true,
                                                                    error);

        if (!vtt) {
          problem = cf.name + ": VTT: " + error;
          ok = false;
          break;
        }
        for (size_t k = 1; ok && k < vtt->vtables.size(); ++k) {
          const edg_abi::a_vtt_vtable &vt = vtt->vtables[k];
          std::unique_ptr<edg_abi::a_vtable_layout> cv =
              ctx.construction_vtable_layout(vt.base.cls, vt.base.offset,
                                             vt.base_is_virtual, c, error);

          if (!cv) {
            problem = cf.name + ": construction vtable: " + error;
            ok = false;
          } else {
            out += edg_abi::print_construction_vtable(db, *cv, vt.base.cls,
                                                      vt.base.offset, c);
          }
        }
      }
    }
  }
  return ok;
}  /* print_abi_layouts */

}  /* namespace ecil */
