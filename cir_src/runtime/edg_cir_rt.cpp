/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
edg_cir_rt -- the small runtime library that programs compiled through the CIR
back end link next to libstdc++/libgcc_s.  It is empty for ECIL v0 (C);
C++ gaps (e.g. type_info::name, __dynamic_cast shims) arrive with M3/M4.
*/

extern "C" int edg_cir_rt_version(void)
/*
Return the runtime library interface version (0: placeholder, no services).
*/
{
  return 0;
}  /* edg_cir_rt_version */
