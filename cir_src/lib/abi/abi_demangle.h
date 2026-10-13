/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_demangle.h -- display names for the layout printers.

The stream carries only manglings ("1B", "_ZN1B1fEi"); the printers of the
vtable layouts spell classes and functions the way clang's
-fdump-vtable-layouts does ("B", "B::f(int)").  This is a small non-recursive
demangler for the subset that vtable tests and ordinary hierarchies use:
source-names, "St", nested names (no template arguments, no substitutions),
constructors, destructors, operators, cv-qualified member functions, and the
parameter types made of builtin types, class names and the P, R, O, K, V
qualifiers.  Anything else gives an empty result and the caller prints the
mangled name.

Not a mangler and not a validator: the names only decorate output.
*/

#ifndef EDG_ABI_DEMANGLE_H
#define EDG_ABI_DEMANGLE_H 1

#include <string>

namespace edg_abi {

std::string demangle_class(const std::string &type_mangled);
/* "N1N1CE" -> "N::C"; empty when out of the subset. */

std::string demangle_function(const std::string &symbol);
/* "_ZN1B1fEi" -> "B::f(int)", "_ZN1BD1Ev" -> "B::~B()"; empty when out of
   the subset. */

}  /* namespace edg_abi */

#endif /* EDG_ABI_DEMANGLE_H */
