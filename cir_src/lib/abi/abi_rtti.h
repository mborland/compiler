/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_rtti.h -- the layout of a class's std::type_info object (Itanium C++ ABI
2.9.5): which of __class_type_info, __si_class_type_info and
__vmi_class_type_info describes it, the flag word and the base descriptions.
The selection is clang::CodeGenUtils::canUseSingleInheritance and
computeVMIClassTypeInfoFlags; the fields are those of
ItaniumRTTIBuilder::BuildSIClassTypeInfo / BuildVMIClassTypeInfo.

Derived in part from clang/lib/CodeGenUtils/ItaniumCXXABIUtils.cpp and
clang/lib/CodeGen/ItaniumCXXABI.cpp (Apache-2.0 WITH LLVM-exception).
*/

#ifndef EDG_ABI_RTTI_H
#define EDG_ABI_RTTI_H 1

#include <cstdint>
#include <string>
#include <vector>

#include "abi_vtable.h"

namespace edg_abi {

enum a_rtti_kind : uint8_t {
  rtti_class = 0,      /* no bases: abi::__class_type_info */
  rtti_si_class,       /* one public non-virtual base: __si_class_type_info */
  rtti_vmi_class       /* everything else: __vmi_class_type_info */
};  /* a_rtti_kind */

/* The __flags_masks of __vmi_class_type_info. */
const uint32_t vmi_non_diamond_repeat = 1;
const uint32_t vmi_diamond_shaped = 2;
/* The __offset_flags_masks of __base_class_type_info. */
const uint32_t bcti_virtual = 1;
const uint32_t bcti_public = 2;
const uint32_t bcti_offset_shift = 8;

enum a_rtti_field_kind : uint8_t {
  rf_vtable_ptr,       /* &<vtable symbol> + 2 pointers (the address point) */
  rf_name_ptr,         /* &_ZTS<type> (never the non-unique bit: on x86-64
                          clang's shouldRTTIBeUnique holds and libstdc++
                          dereferences the pointer) */
  rf_base_typeinfo,    /* &_ZTI of class `cls` */
  rf_u32,              /* an unsigned int (flags, base count) */
  rf_i64               /* a long: __offset_flags */
};  /* a_rtti_field_kind */

struct a_rtti_field {
  a_rtti_field_kind  kind;
  a_class_id         cls = no_id;
  int64_t            value = 0;
};  /* a_rtti_field */

struct a_rtti_layout {
  a_rtti_kind               kind = rtti_class;
  std::string               vtable_symbol;   /* of the abi:: class */
  std::vector<a_rtti_field> fields;          /* in memory order */
};  /* a_rtti_layout */

bool can_use_single_inheritance(const a_class_db &db, a_class_id c);
/* canUseSingleInheritance: exactly one base, non-virtual, public, and the
   base empty or dynamic exactly when the class is. */

uint32_t vmi_class_flags(const a_class_db &db, a_class_id c);
/* computeVMIClassTypeInfoFlags: a depth-first walk of every base path;
   repeated non-virtual bases (or a base both virtual and non-virtual) set
   vmi_non_diamond_repeat, a repeated virtual base vmi_diamond_shaped. */

bool rtti_layout(a_vtable_context &ctx, a_class_id c, a_rtti_layout &out,
                 std::string &error);
/* The layout of the type_info object of class c.  A virtual base's
   __offset_flags needs its vtable's virtual base offset offset, hence the
   context; false (and error) if that layout fails. */

std::string print_rtti(const a_class_db &db, const a_rtti_layout &layout);
/* A line per field, for tests ("vtable _ZTV...+2", "name", "base D",
   "u32 N", "i64 N"). */

}  /* namespace edg_abi */

#endif /* EDG_ABI_RTTI_H */
