/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_vtable.h -- the Itanium vtable layout builder and the VTT builder of the
CIR library's ABI port.

A port of clang's ItaniumVTableBuilder, ItaniumVTableContext, VCallOffsetMap,
VCallAndVBaseOffsetBuilder (clang/lib/AST/VTableBuilder.cpp) and VTTBuilder
(clang/lib/AST/VTTBuilder.cpp), without the Microsoft ABI and without
FinalOverriders (the final overriders are front-end facts).  The work runs
from the dense tables of abi_facts.h.  Every recursion of the original is an
explicit heap stack here: the depth of an inheritance chain is an input
property (recursive tuple types), so the stack of the machine must not grow
with it.

Derived in part from clang/lib/AST/VTableBuilder.cpp,
clang/lib/AST/VTTBuilder.cpp, clang/include/clang/AST/VTableBuilder.h,
clang/include/clang/AST/VTTBuilder.h and clang/include/clang/Basic/Thunk.h
(Apache-2.0 WITH LLVM-exception).
*/

#ifndef EDG_ABI_VTABLE_H
#define EDG_ABI_VTABLE_H 1

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "abi_facts.h"

namespace edg_abi {

/* A base subobject: a class at an offset (clang's BaseSubobject). */
struct a_subobject {
  a_class_id  cls;
  int64_t     offset;

  bool operator<(const a_subobject &o) const
    { return cls != o.cls ? cls < o.cls : offset < o.offset; }
  bool operator==(const a_subobject &o) const
    { return cls == o.cls && offset == o.offset; }
};  /* a_subobject */

/* The adjustment of a this pointer or of a result (clang's ThisAdjustment and
   ReturnAdjustment of the Itanium ABI). */
struct a_adjustment {
  int64_t  non_virtual = 0;
  int64_t  offset_offset = 0;  /* this: vcall offset offset; result: vbase
                                  offset offset. */

  bool is_empty() const
    { return non_virtual == 0 && offset_offset == 0; }
  bool operator==(const a_adjustment &o) const
    { return non_virtual == o.non_virtual && offset_offset == o.offset_offset; }
  bool operator<(const a_adjustment &o) const
    {
      /* clang orders the virtual part with memcmp of its bytes (the vtable
         dump of thunks depends on it): little-endian byte order here. */
      bool less = false;

      if (non_virtual != o.non_virtual) {
        less = non_virtual < o.non_virtual;
      } else {
        for (int k = 0; k < 8; ++k) {
          unsigned a = static_cast<unsigned>(
              (static_cast<uint64_t>(offset_offset) >> (8 * k)) & 0xFF);
          unsigned b = static_cast<unsigned>(
              (static_cast<uint64_t>(o.offset_offset) >> (8 * k)) & 0xFF);

          if (a != b) {
            less = a < b;
            break;
          }
        }
      }
      return less;
    }
};  /* a_adjustment */

/* A thunk: its adjustments and the function that created the vtable entry
   (clang's ThunkInfo; the this type is the class of that function). */
struct a_thunk_info {
  a_adjustment  this_adj;
  a_adjustment  return_adj;
  a_fn_id       method = no_id;
  a_class_id    this_class = no_id;

  bool is_empty() const
    { return this_adj.is_empty() && return_adj.is_empty() && method == no_id; }
  bool operator==(const a_thunk_info &o) const
    {
      return this_adj == o.this_adj && return_adj == o.return_adj &&
             method == o.method && this_class == o.this_class;
    }
};  /* a_thunk_info */

enum a_component_kind : uint8_t {
  ck_vcall_offset = 0,
  ck_vbase_offset,
  ck_offset_to_top,
  ck_rtti,
  ck_function,
  ck_complete_dtor,
  ck_deleting_dtor,
  ck_unused_function
};  /* a_component_kind */

/* One entry of a vtable (clang's VTableComponent). */
struct a_component {
  a_component_kind  kind;
  int64_t           offset;  /* The offset kinds. */
  uint32_t          id;      /* ck_rtti: the class; the function kinds: the
                                function. */
};  /* a_component */

struct a_address_point {
  unsigned  vtable_index;
  unsigned  address_point_index;
};  /* a_address_point */

/* A vtable group (clang's VTableLayout). */
struct a_vtable_layout {
  std::vector<size_t>                     vtable_indices;
  std::vector<a_component>                components;
  std::vector<std::pair<uint64_t, a_thunk_info>>  thunks;  /* By index. */
  std::map<a_subobject, a_address_point>  address_points;

  size_t num_vtables() const
    { return vtable_indices.size(); }
  size_t vtable_size(size_t i) const
    {
      size_t next = i + 1 == vtable_indices.size() ? components.size()
                                                   : vtable_indices[i + 1];
      return next - vtable_indices[i];
    }
};  /* a_vtable_layout */

/* What the builder knows about a class after laying out its vtable. */
struct a_vtable_info {
  a_vtable_layout                        layout;
  std::map<a_fn_id, int64_t>             method_indices;     /* From the
                                                                address point;
                                                                a destructor:
                                                                the complete
                                                                one. */
  std::map<a_class_id, int64_t>          vbase_offset_offsets;
  std::map<a_fn_id, std::vector<a_thunk_info>>  thunks;
};  /* a_vtable_info */

/* The VTT of a class (clang's VTTBuilder results). */
struct a_vtt_component {
  uint64_t     vtable_index;   /* Into vtables. */
  a_subobject  vtable_base;    /* The address point's subobject. */
  bool         present;        /* False for a declaration-only VTT. */
};  /* a_vtt_component */

struct a_vtt_vtable {
  a_subobject  base;
  bool         base_is_virtual;
};  /* a_vtt_vtable */

struct a_vtt_layout {
  std::vector<a_vtt_component>        components;
  std::vector<a_vtt_vtable>           vtables;   /* [0] is the class's own. */
  std::map<a_subobject, uint64_t>     sub_vtt_indices;
  std::map<a_subobject, uint64_t>     secondary_virtual_pointer_indices;
};  /* a_vtt_layout */

/* The builder and its cache (clang's ItaniumVTableContext). */
class a_vtable_context {
public:
  explicit a_vtable_context(const a_class_db &db);
  ~a_vtable_context();

  const a_vtable_info &vtable_info(a_class_id c, std::string &error);
  /* The vtable group of class c (computed once).  On failure the returned
     object is empty and error says why. */

  std::unique_ptr<a_vtable_layout> construction_vtable_layout(
      a_class_id most_derived, int64_t most_derived_offset,
      bool most_derived_is_virtual, a_class_id layout_class,
      std::string &error);
  /* The vtable group of `most_derived` as a base at that offset of
     `layout_class` (a construction vtable, _ZTC). */

  int64_t virtual_base_offset_offset(a_class_id c, a_class_id vbase,
                                     std::string &error);
  /* Where, relative to the address point, the offset of virtual base `vbase`
     is stored in the vtable of c. */

  a_fn_id find_original_method(a_fn_id f) const;
  /* The function that created the slot f uses (clang: findOriginalMethod). */

  const std::vector<a_fn_id> &all_overridden(a_fn_id f);
  /* Every function f overrides, directly or not (the facts' lists, which are
     the whole set). */

  const a_class_db &db() const
    { return db_; }

  void set_original_method(a_fn_id key, a_fn_id value)
    { original_[key] = value; }
  /* Used by the builder. */

  std::unique_ptr<a_vtt_layout> vtt_layout(a_class_id c,
                                           bool generate_definition,
                                           std::string &error);
  /* The VTT of class c: the order of the vtable pointers and the indices the
     structors use (clang's VTTBuilder). */

private:
  const a_class_db                                 &db_;
  std::map<a_class_id, a_vtable_info>              info_;
  std::map<a_fn_id, a_fn_id>                       original_;
  std::map<std::pair<a_class_id, a_class_id>, int64_t>  vbase_oo_;
  a_vtable_info                                    empty_info_;
  std::map<a_fn_id, std::vector<a_fn_id>>          overridden_closure_;
};  /* a_vtable_context */

}  /* namespace edg_abi */

#endif /* EDG_ABI_VTABLE_H */
