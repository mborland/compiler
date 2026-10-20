/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
edg-cir -- the secondary-process mode of the CIR back end: read an ECIL
stream from a file (or stdin) and produce CIR, LLVM IR, assembly or an
object.  A thin client of the C API (it never sees LLVM).

  edg-cir [-o out] [--emit=cir|cir-lowered|llvm|bc|asm|obj] [-O0..-O3]
          [--verify-each] [--keep-going] [--no-llvm-passes] [--eager-bodies]
          [--lazy-symbols] [--llvm-arg=ARG] [--chunk-functions=N]
          [--chunk-threshold=N] [--triple=T] [--stats] [--trace]
          [--version] file.ecil|-
*/

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "edg_cir/edg_cir.h"

namespace {

void diagnostic(void *, an_edg_cir_severity severity, uint32_t code,
                const char *message, const char *file, uint32_t line,
                uint32_t column)
/*
Print a diagnostic in the usual file:line:col form.
*/
{
  static const char *const names[] = {"note", "warning", "error", "fatal"};

  if (file != nullptr && line != 0)
    std::fprintf(stderr, "%s:%u:%u: ", file, line, column);
  std::fprintf(stderr, "edg-cir: %s [%u]: %s\n",
               names[severity < 4 ? severity : 3], code, message);
}  /* diagnostic */

int usage()
/*
Print the usage and return the usage exit code.
*/
{
  std::fprintf(stderr,
      "usage: edg-cir [-o out] [--emit=cir|cir-lowered|llvm|bc|asm|obj] "
      "[-O0..-O3] [--verify-each] [--keep-going] [--no-llvm-passes] "
      "[--eager-bodies] [--lazy-symbols] [--llvm-arg=ARG] "
      "[--chunk-functions=N] [--chunk-threshold=N] "
      "[--triple=T] [--stats] [--trace] [--version] file.ecil|-\n");
  return 64;
}  /* usage */

}  /* anonymous namespace */

int main(int argc, char **argv)
/*
Parse the options, run edg_cir_process_file, return 0 on success; otherwise
the API status code (1..9), or 64 for a usage error.
*/
{
  an_edg_cir_options options;
  const char *input = nullptr;
  const char *output = nullptr;
  std::string triple;
  std::vector<const char *> llvm_args;
  an_edg_cir_status status;
  bool bad = false;
  bool show_version = false;
  int result = 0;

  edg_cir_options_init(&options);
  options.diagnostic_callback = diagnostic;
  for (int i = 1; i < argc && !bad; ++i) {
    const char *arg = argv[i];
    if (std::strcmp(arg, "-o") == 0 && i + 1 < argc) {
      output = argv[++i];
    } else if (std::strncmp(arg, "--emit=", 7) == 0) {
      const char *kind = arg + 7;
      if (!std::strcmp(kind, "cir")) options.output_kind = EDG_CIR_OUTPUT_CIR;
      else if (!std::strcmp(kind, "cir-lowered"))
        options.output_kind = EDG_CIR_OUTPUT_CIR_LOWERED;
      else if (!std::strcmp(kind, "llvm"))
        options.output_kind = EDG_CIR_OUTPUT_LLVM_IR;
      else if (!std::strcmp(kind, "bc"))
        options.output_kind = EDG_CIR_OUTPUT_LLVM_BC;
      else if (!std::strcmp(kind, "asm"))
        options.output_kind = EDG_CIR_OUTPUT_ASM;
      else if (!std::strcmp(kind, "obj"))
        options.output_kind = EDG_CIR_OUTPUT_OBJECT;
      else if (!std::strcmp(kind, "none"))
        options.output_kind = EDG_CIR_OUTPUT_NONE;
      else bad = true;
    } else if (arg[0] == '-' && arg[1] == 'O' && arg[2] >= '0' &&
               arg[2] <= '3' && arg[3] == '\0') {
      options.opt_level = arg[2] - '0';
    } else if (std::strcmp(arg, "--verify-each") == 0) {
      options.flags |= EDG_CIR_FLAG_VERIFY_EACH;
    } else if (std::strcmp(arg, "--keep-going") == 0) {
      options.flags |= EDG_CIR_FLAG_KEEP_GOING;
    } else if (std::strcmp(arg, "--stats") == 0) {
      options.flags |= EDG_CIR_FLAG_PRINT_STATS;
    } else if (std::strcmp(arg, "--trace") == 0) {
      options.flags |= EDG_CIR_FLAG_TRACE;
    } else if (std::strcmp(arg, "--no-llvm-passes") == 0) {
      options.flags |= EDG_CIR_FLAG_NO_LLVM_PASSES;
    } else if (std::strncmp(arg, "--chunk-functions=", 18) == 0) {
      options.chunk_functions =
          static_cast<uint32_t>(std::strtoul(arg + 18, nullptr, 10));
    } else if (std::strncmp(arg, "--chunk-threshold=", 18) == 0) {
      options.chunk_threshold =
          static_cast<uint32_t>(std::strtoul(arg + 18, nullptr, 10));
    } else if (std::strcmp(arg, "--lazy-symbols") == 0) {
      /* With --stats: a note for each deferred body (lazy-identity). */
      setenv("EDG_CIR_LAZY_SYMBOLS", "1", 1);
    } else if (std::strncmp(arg, "--llvm-arg=", 11) == 0) {
      llvm_args.push_back(arg + 11);
    } else if (std::strcmp(arg, "--eager-bodies") == 0) {
      options.flags |= EDG_CIR_FLAG_EAGER_BODIES;
    } else if (std::strcmp(arg, "--disable-cir-passes") == 0) {
      options.flags |= EDG_CIR_FLAG_DISABLE_CIR_PASSES;
    } else if (std::strncmp(arg, "--triple=", 9) == 0) {
      triple = arg + 9;
      options.target_triple_override = triple.c_str();
    } else if (std::strcmp(arg, "--version") == 0) {
      std::printf("%s\n", edg_cir_version_string());
      show_version = true;
    } else if (arg[0] != '-' || std::strcmp(arg, "-") == 0) {
      input = arg;
    } else {
      bad = true;
    }
  }
  if (show_version) {
    result = 0;
  } else if (bad || input == nullptr) {
    result = usage();
  } else if (output == nullptr &&
             (options.output_kind == EDG_CIR_OUTPUT_OBJECT ||
              options.output_kind == EDG_CIR_OUTPUT_LLVM_BC)) {
    std::fprintf(stderr, "edg-cir: binary output needs -o\n");
    result = 64;
  } else {
    options.output_path = output != nullptr ? output : "-";
    if (!llvm_args.empty()) {
      llvm_args.push_back(nullptr);
      options.llvm_args = llvm_args.data();
    }
    status = edg_cir_process_file(&options, input);
    if (status != EDG_CIR_OK)
      std::fprintf(stderr, "edg-cir: %s\n", edg_cir_status_string(status));
    result = static_cast<int>(status);
  }
  return result;
}  /* main */
