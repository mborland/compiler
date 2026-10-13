/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
ecil_abi_decode.h -- the decoder from the facts the validator kept of the
CLASS_*, REC_FIELD and DECL_FUNC records (ecil_ids.h) to the class-fact tables
of lib/abi (abi_facts.h), through its fact builder.  LLVM-free; used by the
code generator at finish and by `ecil-dump --vtable-layouts`.

It runs once, after the last record: the validator has the final picture
(CLASS_INFO and CLASS_BASES of a plain class may arrive long after the
class was used as a base, and CLASS_MANGLE may be re-issued), so nothing is
fed to the builder earlier.

The classes handed to the builder are those with a CLASS_INFO and, reached
from them through base fields and covariant result types, the plain classes
without one: those go in through set_plain_class and their BASE fields (12.2:
a class that is not dynamic gets CLASS_INFO and CLASS_BASES only at need).
The `is_public` of such a base is not known (there is no CLASS_BASES), so the
builder gets `true`; it matters only for the RTTI base flags of a class that
has CLASS_BASES of its own.
*/

#ifndef ECIL_ABI_DECODE_H
#define ECIL_ABI_DECODE_H 1

#include <string>

#include "abi_facts.h"
#include "ecil_ids.h"

namespace ecil {

bool build_abi_facts(const a_id_tables &ids, edg_abi::a_class_db &db,
                     std::string &problem);
/* Decode every class of the stream read so far.  False with the first
   problem when the facts are inconsistent (the builder's finish: the deep
   validation and the slot checks). */

bool print_abi_layouts(const a_id_tables &ids, std::string &out,
                       std::string &problem);
/*
`ecil-dump --vtable-layouts`: the facts of build_abi_facts printed in the
format of clang's -fdump-vtable-layouts, for every dynamic class whose vtable
facts the stream carries (CLASS_VFUNCS sent): its vtable ("Vtable for",
"Virtual base offset offsets", "Thunks for", "VTable indices") and, for a class
with virtual bases, its construction vtables.  Function and class names are
the demangled ones (abi_demangle.h) where the mangling is in its subset, else
the stream's mangled names.
*/

}  /* namespace ecil */

#endif /* ECIL_ABI_DECODE_H */
