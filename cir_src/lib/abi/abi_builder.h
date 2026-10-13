/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_builder.h -- the interface the ECIL record decoder fills.

Records name classes and functions by their stream ids (sparse 64-bit
numbers, in any order); the builder gives them dense ids, resolves forward
references when the last record is in, and finishes with the deep validation
of abi_facts.h.  Nothing here knows the wire format: the decoder maps
  TY_RECORD_BODY / REC_FIELD(BASE, VIRTUAL_BASE)  -> add_base / add_vbase
  CLASS_INFO                                      -> set_class
  CLASS_VFUNCS                                    -> add_vfunc
  VFUNC overridden list                           -> add_overridden
  CLASS_OVERRIDES                                 -> add_override
(see README.md for the field-by-field table).
*/

#ifndef EDG_ABI_BUILDER_H
#define EDG_ABI_BUILDER_H 1

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "abi_facts.h"

namespace edg_abi {

typedef uint64_t a_key;      /* A stream id of a class or a function. */

/* What CLASS_INFO states about a class. */
struct a_class_info {
  std::string  name;
  std::string  mangled;
  int64_t      size = 0;
  int64_t      nvsize = 0;
  int64_t      nv_data_end = -1;  /* End of the non-virtual data (-1: not
                                     known, nvsize stands in). */
  bool         is_dynamic = false;
  bool         is_empty = false;
  bool         layout_only = false;
  bool         has_primary = false;
  a_key        primary = 0;
  bool         primary_virtual = false;
  uint32_t     nslots = 0;
};  /* a_class_info */

/* What CLASS_VFUNCS states about one virtual function. */
struct a_vfunc_info {
  std::string  name;
  std::string  mangled;
  int32_t      slot = -1;
  bool         is_pure = false;
  bool         is_deleted = false;
  a_fn_special special = fs_none;
  bool         is_implicit = false;
  bool         ref_result = false;   /* The result is a reference: a
                                        covariant return adjustment has no
                                        null check. */
  uint32_t     sig = 0;
  bool         has_ret_class = false;
  a_key        ret_class = 0;
};  /* a_vfunc_info */

class a_fact_builder {
public:
  void set_pointer_size(uint32_t bytes)
    { db_.pointer_size = bytes; }
  void set_class(a_key cls, const a_class_info &info);
  /* CLASS_INFO.  A second call for one class is a problem. */
  void set_plain_class(a_key cls, const std::string &name, int64_t size,
                       int64_t data_size, bool is_empty);
  /* A class that is not dynamic and has no CLASS_INFO (12.2: those are sent
     at their first need only): the decoder defines it from its record body
     (size, data_size as nvsize) and feeds its BASE fields through add_base
     (a non-dynamic class has no virtual bases, so its BASE fields are all
     its bases; is_public is not known and true is passed).  Such a class is
     reached by the walk of vmi_class_flags and by the layout of the classes
     derived from it. */
  void add_base(a_key cls, a_key base, bool is_virtual, bool is_public,
                int64_t offset);
  /* A direct base, in declaration order.  Offset: in `cls`; a virtual base:
     the offset of that base in `cls`.  `is_public` is the PUBLIC flag of the
     class's CLASS_BASES entry; for a plain class (set_plain_class, no
     CLASS_BASES) it is a PLACEHOLDER: the decoder passes true, and nothing
     reads it (only the RTTI base flags of a class that has CLASS_BASES of
     its own use is_public, and that class's own entries are real). */
  void add_vbase(a_key cls, a_key base, bool is_public, int64_t offset);
  /* An entry of the class's virtual base list (every virtual base, direct
     or not, in vbases() order). */
  void add_vfunc(a_key cls, a_key fn, const a_vfunc_info &info);
  /* A virtual function declared in `cls`, in declaration order. */
  void add_overridden(a_key fn, a_key overridden_fn);
  /* `fn` overrides `overridden_fn` (directly or not). */
  void add_override(a_key cls, a_key fn, int64_t sub_off, a_key overrider,
                    int64_t overrider_off);
  /* CLASS_OVERRIDES: the final overrider of `fn` at the subobject at
     `sub_off` of `cls`. */

  std::string finish(a_class_db &out);
  /* Resolve the ids, derive the overridden-function lists from the final
     overriders and check everything: validate_deep, then the slots of
     CLASS_VFUNCS against the builder's own layout (check_all_slots).  The
     first problem, or an empty string with `out` filled; the builder is
     reset either way. */

  size_t class_count() const
    { return db_.classes.size(); }
  size_t fn_count() const
    { return db_.fns.size(); }

private:
  a_class_id class_of(a_key key);
  a_fn_id fn_of(a_key key);

  a_class_db                              db_;
  std::unordered_map<a_key, a_class_id>   class_ids_;
  std::unordered_map<a_key, a_fn_id>      fn_ids_;
  std::vector<a_key>                      class_keys_;
  std::vector<a_key>                      fn_keys_;
  std::vector<uint8_t>                    class_defined_;
  std::vector<uint8_t>                    fn_defined_;
  std::vector<a_key>                      primary_key_;
  std::vector<a_key>                      ret_key_;
  std::string                             problem_;
};  /* a_fact_builder */

}  /* namespace edg_abi */

#endif /* EDG_ABI_BUILDER_H */
