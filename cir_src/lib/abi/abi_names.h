/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_names.h -- the Itanium special names of the vtable machinery, built from
the mangled names the stream carries.  Nothing here mangles a C++ entity: a
class arrives as its <type> mangling ("1D", "N1N1CE") and a function as its
symbol ("_ZN1B1fEv"); this file only adds the <special-name> forms of
clang/lib/AST/ItaniumMangle.cpp (mangleCXXVTable, mangleCXXVTT,
mangleCXXRTTI, mangleCXXRTTIName, mangleCXXCtorVTable, mangleThunk,
mangleCXXDtorThunk) around them.

Derived in part from clang/lib/AST/ItaniumMangle.cpp (Apache-2.0 WITH
LLVM-exception).
*/

#ifndef EDG_ABI_NAMES_H
#define EDG_ABI_NAMES_H 1

#include <cstdint>
#include <string>

#include "abi_vtable.h"

namespace edg_abi {

std::string mangle_number(int64_t value);
/* <number>: decimal, a negative value as 'n' and its magnitude. */

std::string vtable_symbol(const std::string &type_mangled);
std::string vtt_symbol(const std::string &type_mangled);
std::string typeinfo_symbol(const std::string &type_mangled);
std::string typeinfo_name_symbol(const std::string &type_mangled);
/* _ZTV, _ZTT, _ZTI and _ZTS of a class whose <type> mangling is given. */

std::string typeinfo_name_string(const std::string &type_mangled,
                                 bool tu_local);
/* The contents of the _ZTS string: the mangling, with a leading '*' for a
   TU-local type, as g++ writes it (user decision; clang does not).  libstdc++
   then compares such names by address and type_info::name() skips the '*'.
   `tu_local` is the DECL_ABI_DATA TU_LOCAL flag (ECIL_ABI_DATA_TU_LOCAL) and
   nothing else: internal linkage does not imply it (the typeinfo of `I*`
   with `I` incomplete is internal, its name is "P1I" in every TU). */

std::string ctor_vtable_symbol(const std::string &complete_mangled,
                               int64_t offset,
                               const std::string &base_mangled,
                               bool &exact);
/* _ZTC<complete type><offset>_<base type>: the construction vtable of the
   base subobject at `offset` of the complete class.  The base name is
   compressed against the substitutions the complete name introduced, as the
   Itanium mangler does (the nested-name subset: source-names and "St"; a
   name with template arguments or local scopes is not recompressed).  exact
   is false when the names were out of that subset: the symbol is then the
   plain concatenation, still a unique name of the same construction vtable
   but not byte-identical to clang's. */

std::string thunk_symbol(const a_adjustment &this_adj,
                         const a_adjustment &return_adj,
                         const std::string &target_symbol);
/* _ZT[c]<call-offset>[<call-offset>]<encoding>: the thunk that adjusts `this`
   (and the result when return_adj is not empty) and tail-calls the function
   whose symbol is target_symbol ("_Z..."); empty if target_symbol is not a
   _Z name. */

std::string dtor_variant_symbol(const std::string &dtor_symbol, char variant);
/* The destructor symbol with its D1/D0/D2 variant replaced (variant is '0',
   '1' or '2'); the last "D<digit>E" is the destructor's own.  Empty if there
   is none. */

}  /* namespace edg_abi */

#endif /* EDG_ABI_NAMES_H */
