/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_text.h -- a line-oriented text form of the class facts, for tests and for
the tools that feed the builder from the front end's IL display
(tools/fe_facts_to_abi.py).  Not part of the stream format.

  class ID "NAME" size N nvsize N dynamic 0|1 primary ID|-1 pvirtual 0|1 nslots N
    base ID VIRTUAL(0|1) OFFSET [public 0|1]   direct base, declaration order
    vbase ID OFFSET [public 0|1]          every virtual base
    fns ID ...                            declared virtual functions
    ovr FN SUBOFF OVRFN OVROFF            final overrider
  fn ID "NAME" owner ID slot N pure 0|1 deleted 0|1 special 0..3 implicit 0|1
     sig N ret ID|-1
Optional trailing attributes: on a class line `mangled "M"` (the <type>
mangling, "1D") and `empty 0|1`; on a fn line `mangled "_Z..."`.
    overridden ID ...
  pointer_size N
Lines of one entity are indented; "#" starts a comment.
*/

#ifndef EDG_ABI_TEXT_H
#define EDG_ABI_TEXT_H 1

#include <string>

#include "abi_facts.h"

namespace edg_abi {

std::string parse_facts(const std::string &text, a_class_db &db);
/* Fill db from text; the first problem, or an empty string. */

std::string write_facts(const a_class_db &db);
/* The text form of db (parse_facts of it gives the same tables). */

}  /* namespace edg_abi */

#endif /* EDG_ABI_TEXT_H */
