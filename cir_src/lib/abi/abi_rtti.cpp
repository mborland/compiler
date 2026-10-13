/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_rtti.cpp -- type_info layouts.  Derived in part from
clang/lib/CodeGenUtils/ItaniumCXXABIUtils.cpp and
clang/lib/CodeGen/ItaniumCXXABI.cpp (Apache-2.0 WITH LLVM-exception).
The recursive walk of computeVMIClassTypeInfoFlags is an explicit stack.
*/

#include "abi_rtti.h"

#include <set>
#include <sstream>

namespace edg_abi {

bool can_use_single_inheritance(const a_class_db &db, a_class_id c)
/*
See abi_rtti.h.
*/
{
  const a_class_fact &cls = db.cls(c);
  bool ok = false;

  if (cls.bases.size() == 1) {
    const a_base_fact &b = cls.bases[0];
    const a_class_fact &base = db.cls(b.cls);

    ok = !b.is_virtual && b.is_public &&
         (base.is_empty || base.is_dynamic == cls.is_dynamic);
  }
  return ok;
}  /* can_use_single_inheritance */

namespace {

bool has_non_virtual_base_below(const a_class_db &db, a_class_id c,
                                std::vector<int8_t> &memo)
/*
Whether class c or any class reachable from it through its bases has a
non-virtual base (memoized; explicit stack).
*/
{
  struct a_frame {
    a_class_id  cls;
    size_t      next;
    bool        found;
  };  /* a_frame */
  std::vector<a_frame> stack;

  if (memo[c] < 0) {
    stack.push_back({c, 0, false});
    while (!stack.empty()) {
      a_frame &top = stack.back();
      const a_class_fact &cls = db.cls(top.cls);

      if (top.next >= cls.bases.size()) {
        bool found = top.found;

        memo[top.cls] = found ? 1 : 0;
        stack.pop_back();
        if (!stack.empty() && found)
          stack.back().found = true;
      } else {
        const a_base_fact &b = cls.bases[top.next++];

        if (!b.is_virtual)
          top.found = true;
        if (memo[b.cls] >= 0)
          top.found = top.found || memo[b.cls] == 1;
        else
          stack.push_back({b.cls, 0, false});
      }
    }
  }
  return memo[c] == 1;
}  /* has_non_virtual_base_below */

}  /* anonymous namespace */

uint32_t vmi_class_flags(const a_class_db &db, a_class_id c)
/*
See abi_rtti.h.  Order matters (a base seen "before" decides which kind of
repeat it is), so the walk is the original's pre-order.  The original walks
every route to a class again; the second visit of a virtual base is not
walked: all it can add is the diamond flag (set by the visit itself) and, when
a non-virtual base lies below it, the repeat flag, which is looked up (a
lattice of virtual bases would cost a walk per route).
*/
{
  struct a_frame {
    a_class_id  cls;
    size_t      next;
  };  /* a_frame */
  std::vector<a_frame> stack;
  std::set<a_class_id> non_virtual_seen;
  std::set<a_class_id> virtual_seen;
  std::vector<int8_t> below(db.classes.size(), -1);
  uint32_t flags = 0;

  stack.push_back({c, 0});
  while (!stack.empty()) {
    a_frame &top = stack.back();
    const a_class_fact &cls = db.cls(top.cls);

    if (top.next >= cls.bases.size()) {
      stack.pop_back();
    } else {
      const a_base_fact &b = cls.bases[top.next++];
      bool walk = true;

      if (b.is_virtual) {
        if (!virtual_seen.insert(b.cls).second) {
          flags |= vmi_diamond_shaped;
          if (has_non_virtual_base_below(db, b.cls, below))
            flags |= vmi_non_diamond_repeat;
          walk = false;
        } else if (non_virtual_seen.count(b.cls) != 0) {
          flags |= vmi_non_diamond_repeat;
        }
      } else {
        if (!non_virtual_seen.insert(b.cls).second)
          flags |= vmi_non_diamond_repeat;
        else if (virtual_seen.count(b.cls) != 0)
          flags |= vmi_non_diamond_repeat;
      }
      if (walk)
        stack.push_back({b.cls, 0});
    }
  }
  return flags;
}  /* vmi_class_flags */

bool rtti_layout(a_vtable_context &ctx, a_class_id c, a_rtti_layout &out,
                 std::string &error)
/*
See abi_rtti.h.
*/
{
  const a_class_db &db = ctx.db();
  const a_class_fact &cls = db.cls(c);

  out = a_rtti_layout();
  if (cls.bases.empty()) {
    out.kind = rtti_class;
    out.vtable_symbol = "_ZTVN10__cxxabiv117__class_type_infoE";
  } else if (can_use_single_inheritance(db, c)) {
    out.kind = rtti_si_class;
    out.vtable_symbol = "_ZTVN10__cxxabiv120__si_class_type_infoE";
  } else {
    out.kind = rtti_vmi_class;
    out.vtable_symbol = "_ZTVN10__cxxabiv121__vmi_class_type_infoE";
  }
  out.fields.push_back({rf_vtable_ptr, no_id, 2});
  out.fields.push_back({rf_name_ptr, no_id, 0});
  if (out.kind == rtti_si_class) {
    out.fields.push_back({rf_base_typeinfo, cls.bases[0].cls, 0});
  } else if (out.kind == rtti_vmi_class) {
    out.fields.push_back({rf_u32, no_id, vmi_class_flags(db, c)});
    out.fields.push_back({rf_u32, no_id,
                          static_cast<int64_t>(cls.bases.size())});
    for (const a_base_fact &b : cls.bases) {
      int64_t offset = b.offset;
      uint64_t bits;

      if (b.is_virtual) {
        offset = ctx.virtual_base_offset_offset(c, b.cls, error);
        if (!error.empty())
          break;
      }
      bits = static_cast<uint64_t>(offset) << bcti_offset_shift;
      if (b.is_virtual)
        bits |= bcti_virtual;
      if (b.is_public)
        bits |= bcti_public;
      out.fields.push_back({rf_base_typeinfo, b.cls, 0});
      out.fields.push_back({rf_i64, no_id, static_cast<int64_t>(bits)});
    }
  }
  return error.empty();
}  /* rtti_layout */

int64_t dynamic_cast_offset_hint(const a_class_db &db, a_class_id src,
                                 a_class_id dst)
/*
Every path from dst down to src through the direct base lists, in
depth-first declaration order (CXXBasePaths of isDerivedFrom), then the walk
of computeOffsetHint over the paths.  Explicit stack.
*/
{
  struct a_frame {
    a_class_id  cls;
    size_t      next;
    bool        is_public;     /* Every step so far is public. */
    bool        has_virtual;
    int64_t     offset;
  };  /* a_frame */
  struct a_path {
    bool     is_public;
    bool     has_virtual;
    int64_t  offset;
  };  /* a_path */
  std::vector<a_frame> stack;
  std::vector<a_path> paths;
  int64_t hint = -2;
  unsigned public_paths = 0;
  int64_t offset = 0;
  bool done = false;

  stack.push_back({dst, 0, true, false, 0});
  while (!stack.empty()) {
    a_frame &top = stack.back();
    const a_class_fact &c = db.cls(top.cls);

    if (top.next >= c.bases.size()) {
      stack.pop_back();
    } else {
      const a_base_fact &b = c.bases[top.next++];
      bool is_public = top.is_public && b.is_public;
      bool has_virtual = top.has_virtual || b.is_virtual;
      int64_t sum = top.offset + b.offset;

      if (b.cls == src)
        paths.push_back({is_public, has_virtual, sum});
      else
        stack.push_back({b.cls, 0, is_public, has_virtual, sum});
    }
  }
  for (size_t k = 0; !done && k < paths.size(); ++k) {
    if (!paths[k].is_public)
      continue;
    ++public_paths;
    if (paths[k].has_virtual) {
      hint = -1;
      done = true;
    } else if (public_paths == 1) {
      offset = paths[k].offset;
    }
  }
  if (!done) {
    if (public_paths == 0)
      hint = -2;
    else if (public_paths > 1)
      hint = -3;
    else
      hint = offset;
  }
  return hint;
}  /* dynamic_cast_offset_hint */

std::string print_rtti(const a_class_db &db, const a_rtti_layout &layout)
/*
One line per field.
*/
{
  std::ostringstream out;

  for (const a_rtti_field &f : layout.fields) {
    switch (f.kind) {
    case rf_vtable_ptr:
      out << "vtable " << layout.vtable_symbol << "+" << f.value << "\n";
      break;
    case rf_name_ptr:
      out << "name\n";
      break;
    case rf_base_typeinfo:
      out << "base " << db.cls(f.cls).name << "\n";
      break;
    case rf_u32:
      out << "u32 " << f.value << "\n";
      break;
    case rf_i64:
      out << "i64 " << f.value << "\n";
      break;
    }
  }
  return out.str();
}  /* print_rtti */

}  /* namespace edg_abi */
