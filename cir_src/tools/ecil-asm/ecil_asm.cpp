/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
ecil-asm -- assemble the text form of an ECIL stream (see ecil_text.h) to
binary.  The inverse of ecil-dump.

  ecil-asm [-o out.ecil] file.txt|-
*/

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ecil_text.h"

int main(int argc, char **argv)
/*
Parse options, assemble the text, return 0 ok, 1 assembly error, 2 usage/IO.
*/
{
  int result = 0;
  const char *input = nullptr;
  const char *output = nullptr;

  for (int i = 1; i < argc && result == 0; ++i) {
    if (std::strcmp(argv[i], "-o") == 0 && i + 1 < argc)
      output = argv[++i];
    else if (argv[i][0] != '-' || std::strcmp(argv[i], "-") == 0)
      input = argv[i];
    else
      result = 2;
  }
  if (result == 0 && input == nullptr)
    result = 2;
  if (result == 2) {
    std::fprintf(stderr, "usage: ecil-asm [-o out.ecil] file.txt|-\n");
  } else {
    std::FILE *file = std::strcmp(input, "-") == 0 ? stdin
                                                   : std::fopen(input, "rb");
    std::string text;
    char buffer[65536];
    size_t n;

    if (file == nullptr) {
      std::fprintf(stderr, "ecil-asm: cannot read %s\n", input);
      result = 2;
    } else {
      while ((n = std::fread(buffer, 1, sizeof buffer, file)) > 0)
        text.append(buffer, n);
      if (file != stdin)
        std::fclose(file);
      std::vector<uint8_t> bytes;
      std::string error;
      if (!ecil::assemble_text(text, bytes, error)) {
        std::fprintf(stderr, "ecil-asm: %s\n", error.c_str());
        result = 1;
      } else {
        std::FILE *out = output ? std::fopen(output, "wb") : stdout;
        if (out == nullptr) {
          std::fprintf(stderr, "ecil-asm: cannot write %s\n", output);
          result = 2;
        } else {
          std::fwrite(bytes.data(), 1, bytes.size(), out);
          if (out != stdout)
            std::fclose(out);
        }
      }
    }
  }
  return result;
}  /* main */
