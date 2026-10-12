/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_print.h -- the text form of vtable layouts (the format of
clang -cc1 -fdump-vtable-layouts, which edg-cir-abi-diff normalizes) and the
structural check of the front end's slot numbers.
*/

#ifndef EDG_ABI_PRINT_H
#define EDG_ABI_PRINT_H 1

#include <string>

#include "abi_vtable.h"

namespace edg_abi {

std::string print_vtable(a_vtable_context &ctx, a_class_id c,
                         std::string &error);
/* "Vtable for 'X' (N entries)." with its address points and thunk notes, the
   virtual base offset offsets, the thunks and the vtable indices of class c,
   in clang's dump format. */

std::string print_construction_vtable(const a_class_db &db,
                                      const a_vtable_layout &layout,
                                      a_class_id most_derived,
                                      int64_t most_derived_offset,
                                      a_class_id layout_class);
/* "Construction vtable for ('D', off) in 'E' (N entries)." */

std::string print_vtt(const a_class_db &db, const a_vtt_layout &vtt,
                      a_class_id c);
/* One line per VTT entry: index, vtable, address point. */

std::string check_slots(a_vtable_context &ctx, a_class_id c);
/* The structural check of the front end's slot numbers of class c (prefix
   chain, contiguity, destructor pairs, full coverage of [0, nslots)); an
   empty string when they are consistent. */

std::string check_slots_against_builder(a_vtable_context &ctx, a_class_id c);

std::string check_all_slots(const a_class_db &db);
/* Both checks for every dynamic class; the first problem, or empty. */
/* The front end's slot of every method against the index the builder's own
   AddMethods decisions gave it (covariant returns that need a new slot
   included); an empty string when they agree. */

}  /* namespace edg_abi */

#endif /* EDG_ABI_PRINT_H */
