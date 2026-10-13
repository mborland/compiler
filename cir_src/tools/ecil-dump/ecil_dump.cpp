/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
ecil-dump -- print an ECIL stream as readable text, one record per line.
The output is stable (suitable for goldens) and ecil-asm turns it back
into the same bytes.

  ecil-dump [--no-check] [--offsets] [--vtable-layouts] [-o out.txt]
            file.ecil|-

--vtable-layouts appends, after the records, the Itanium vtable layouts of the
stream's dynamic classes in the text of clang's -fdump-vtable-layouts
(tools such as edg-cir-abi-diff compare it with clang's).  It decodes the
class facts of the stream, so it needs checking; a stream whose facts are
inconsistent is an error.
*/

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ecil_text.h"

namespace {

bool read_all(const char *path, std::vector<uint8_t> &data)
/*
Read a whole file ("-" for stdin) into data.  Return false on failure.
*/
{
  bool result = true;
  std::FILE *file = std::strcmp(path, "-") == 0 ? stdin
                                                : std::fopen(path, "rb");
  uint8_t buffer[65536];
  size_t n;

  if (file == nullptr) {
    result = false;
  } else {
    while ((n = std::fread(buffer, 1, sizeof buffer, file)) > 0)
      data.insert(data.end(), buffer, buffer + n);
    if (file != stdin)
      std::fclose(file);
  }
  return result;
}  /* read_all */

}  /* anonymous namespace */

void print_help(std::FILE *out)
/*
The usage text (stdout for --help: tools look for the option names in it).
*/
{
  std::fputs("usage: ecil-dump [--no-check] [--offsets] [--vtable-layouts] "
             "[-o out.txt] file.ecil|-\n"
             "  --no-check         do not validate the stream\n"
             "  --offsets          prefix every record with its stream "
             "offset\n"
             "  --vtable-layouts   append the vtable layouts of the dynamic "
             "classes\n"
             "                     (clang's -fdump-vtable-layouts text)\n"
             "  -o out.txt         write to a file\n"
             "  --help             print this text\n", out);
}  /* print_help */

int main(int argc, char **argv)
/*
Parse options, dump the stream, return 0 ok, 1 stream error, 2 usage/IO.
*/
{
  int result = 0;
  const char *input = nullptr;
  const char *output = nullptr;
  ecil::a_dump_options options;
  bool help = false;

  for (int i = 1; i < argc && result == 0; ++i) {
    if (std::strcmp(argv[i], "--help") == 0 ||
        std::strcmp(argv[i], "-h") == 0)
      help = true;
    else if (std::strcmp(argv[i], "--no-check") == 0)
      options.check = false;
    else if (std::strcmp(argv[i], "--offsets") == 0)
      options.show_offsets = true;
    else if (std::strcmp(argv[i], "--vtable-layouts") == 0)
      options.vtable_layouts = true;
    else if (std::strcmp(argv[i], "-o") == 0 && i + 1 < argc)
      output = argv[++i];
    else if (argv[i][0] != '-' || std::strcmp(argv[i], "-") == 0)
      input = argv[i];
    else
      result = 2;
  }
  if (result == 0 && input == nullptr && !help)
    result = 2;
  if (help) {
    print_help(stdout);
  } else if (result == 2) {
    print_help(stderr);
  } else {
    std::vector<uint8_t> data;
    std::string text;
    ecil::an_error error;

    if (!read_all(input, data)) {
      std::fprintf(stderr, "ecil-dump: cannot read %s\n", input);
      result = 2;
    } else {
      bool ok = ecil::dump_stream(data.data(), data.size(), options, text,
                                  error);
      std::FILE *out = output ? std::fopen(output, "wb") : stdout;
      if (out == nullptr) {
        std::fprintf(stderr, "ecil-dump: cannot write %s\n", output);
        result = 2;
      } else {
        std::fwrite(text.data(), 1, text.size(), out);
        if (out != stdout)
          std::fclose(out);
        if (!ok) {
          std::fprintf(stderr, "ecil-dump: %s error at byte %llu: %s\n",
                       error.kind == ecil::an_error_kind::version ? "version"
                                                                  : "format",
                       static_cast<unsigned long long>(error.stream_offset),
                       error.message.c_str());
          result = 1;
        }
      }
    }
  }
  return result;
}  /* main */
