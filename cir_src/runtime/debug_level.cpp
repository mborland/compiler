/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
debug_level -- the variable __debug_level of EDG's own C++ runtime
(lib_src/runtime.h, defined in lib_src/vars.c as `int __debug_level`, 0 by
default).  Some of the front end's tests set it to turn on the runtime's
exception tracing (extern int __debug_level; __debug_level = 5;).  The CIR
flow links libstdc++ instead of that runtime, which has no such tracing, so
the variable only has to exist and be an int.

It is alone in its source file so that it sits in its own member of the
edg_cir_rt archive: a program that never mentions __debug_level does not
link it, and a program that defines its own does not clash with it.
*/

int __debug_level = 0;
