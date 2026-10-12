/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_facts.h -- the class facts the Itanium vtable and VTT builders work from.

LLVM-free.  The tables are dense (a class and a virtual function are small
integers chosen by the caller) and mirror what the writer streams (ECIL 1.2:
TY_RECORD_BODY and REC_FIELD with BASE / VIRTUAL_BASE, CLASS_INFO,
CLASS_BASES, CLASS_VFUNCS, CLASS_OVERRIDES; the covariant adjustments are
derived from the result classes).  Offsets are the
front end's layout: they are what clang's ASTRecordLayout holds.

Derived in part from clang/lib/AST/VTableBuilder.cpp and VTTBuilder.cpp
(Apache-2.0 WITH LLVM-exception); see abi_vtable.cpp.
*/

#ifndef EDG_ABI_FACTS_H
#define EDG_ABI_FACTS_H 1

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace edg_abi {

typedef uint32_t a_class_id;
typedef uint32_t a_fn_id;

const uint32_t no_id = 0xFFFFFFFFu;

/* The special member kinds that order the implicit virtual functions of a
   class (clang sorts them: copy assignment, move assignment, destructor, the
   rest). */
enum a_fn_special : uint8_t {
  fs_none = 0,
  fs_destructor,
  fs_copy_assign,
  fs_move_assign
};  /* a_fn_special */

/* A direct base class, in declaration order. */
struct a_base_fact {
  a_class_id  cls;
  bool        is_virtual;
  int64_t     offset;    /* In the declaring class; for a virtual base, the
                            complete-object offset in that class. */
  bool        is_public = true;   /* Public inheritance (RTTI flags). */
};  /* a_base_fact */

/* A virtual function that has a vtable slot. */
struct a_fn_fact {
  a_class_id  owner;
  int32_t     slot;          /* The front end's number (clang's index from the
                                primary address point); a destructor: the
                                complete destructor's, the deleting one is
                                slot + 1; -1: none. */
  bool        is_pure;
  bool        is_deleted;
  a_fn_special special;
  bool        is_implicit;   /* Compiler-generated. */
  uint32_t    sig;           /* Same vcall-offset signature class: 0 for a
                                destructor, else equal iff same name, same
                                canonical parameter types and method
                                qualifiers. */
  a_class_id  ret_class;     /* The class a pointer or reference result points
                                to; no_id if the result is not one. */
  std::vector<a_fn_id> overridden;  /* Every function this one overrides,
                                       directly or not (the whole set:
                                       nothing computes a closure). */
  std::string name;          /* For diagnostics and the printer: "A::f(int)"
                                (no result type). */
  std::string mangled;       /* The mangled name (a destructor: the complete
                                variant's, "..D1Ev"); empty if unknown. */
};  /* a_fn_fact */

/* The final overrider of a function at a subobject of a class. */
struct a_override_fact {
  a_fn_id  fn;        /* The overridden function. */
  int64_t  sub_off;   /* Its subobject: offset of the declaring class's
                         subobject in the class that holds this list. */
  a_fn_id  ovr;       /* The final overrider. */
  int64_t  ovr_off;   /* Offset of the overrider's class subobject in the
                         class that holds this list (a virtual base: the
                         class's own virtual base offset). */
};  /* a_override_fact */

struct a_class_fact {
  std::string               name;
  std::string               mangled;         /* The <type> mangling of the
                                                class (the part after _ZTV),
                                                e.g. "1D"; empty if unknown. */
  bool                      is_empty = false;  /* No data and no vptr (clang
                                                  CXXRecordDecl::isEmpty). */
  int64_t                   size;
  int64_t                   nvsize;
  bool                      is_dynamic;      /* Has a vptr (virtual
                                                functions or virtual bases,
                                                its own or inherited). */
  a_class_id                primary;         /* no_id if none. */
  bool                      primary_virtual;
  uint32_t                  nslots;          /* Slots of the primary vtable
                                                (a destructor counts 2). */
  std::vector<a_base_fact>  bases;           /* Direct bases, declaration
                                                order. */
  std::vector<a_base_fact>  vbases;          /* Every virtual base, direct or
                                                not, in clang's vbases()
                                                order; offsets in this
                                                class. */
  std::vector<a_fn_id>      fns;             /* Virtual functions declared
                                                here, declaration order,
                                                implicit ones included. */
  std::vector<a_override_fact> overrides;    /* Final overriders, one per
                                                (function, subobject). */
};  /* a_class_fact */

/* The tables of a translation unit (or of a test) and the indexes built
   from them. */
class a_class_db {
public:
  std::vector<a_class_fact>  classes;
  std::vector<a_fn_fact>     fns;
  uint32_t                   pointer_size = 8;   /* Bytes. */

  std::string validate();
  /* Check ids and offsets and build the lookup tables; an empty string when
     the tables are usable, else the first problem. */
  std::string validate_deep();
  /* validate() (if not done), then the consistency of the class facts as the
     stream's CLASS_* records will state them: base and virtual base offsets
     inside the class, the primary base at offset 0, slots, destructor
     pairs, overrider subobjects that exist in the class, overridden
     functions that are in a base of the owner. */

  const a_class_fact &cls(a_class_id c) const
    { return classes[c]; }
  const a_fn_fact &fn(a_fn_id f) const
    { return fns[f]; }

  bool find_base_offset(a_class_id c, a_class_id base, int64_t &offset) const;
  /* The offset of the direct non-virtual base `base` in class c. */
  bool find_vbase_offset(a_class_id c, a_class_id vbase,
                         int64_t &offset) const;
  /* The offset of the virtual base `vbase` (direct or not) in class c. */
  bool find_override(a_class_id c, a_fn_id f, int64_t sub_off, a_fn_id &ovr,
                     int64_t &ovr_off) const;
  /* The final overrider of f at the subobject at sub_off of class c. */

private:
  struct a_pair_hash {
    size_t operator()(const std::pair<uint64_t, uint64_t> &p) const
      { return static_cast<size_t>(p.first * 0x9E3779B97F4A7C15ull ^
                                   p.second); }
  };  /* a_pair_hash */
  typedef std::pair<uint64_t, uint64_t> a_pair;

  std::unordered_map<a_pair, int64_t, a_pair_hash>  base_offsets_;
  std::unordered_map<a_pair, int64_t, a_pair_hash>  vbase_offsets_;
  struct a_override_value {
    a_fn_id  ovr;
    int64_t  ovr_off;
  };  /* a_override_value */
  struct a_override_key_hash {
    size_t operator()(const std::pair<a_pair, uint64_t> &k) const
      { return a_pair_hash()(k.first) ^ static_cast<size_t>(k.second); }
  };  /* a_override_key_hash */
  std::unordered_map<std::pair<a_pair, uint64_t>, a_override_value,
                     a_override_key_hash>           overrides_;
};  /* a_class_db */

}  /* namespace edg_abi */

#endif /* EDG_ABI_FACTS_H */
