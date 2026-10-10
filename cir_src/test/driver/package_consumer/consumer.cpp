/* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
/* Compiled as C++14 with the front end's flags against the package only. */
#include <cstdio>
#include "edg_cir/edg_cir.h"
#include "edg_cir/ecil_format.h"

int main()
{
  an_edg_cir_options options;
  edg_cir_options_init(&options);
  std::printf("%s\n", edg_cir_version_string());
  return options.api_version == EDG_CIR_API_VERSION ? 0 : 1;
}
