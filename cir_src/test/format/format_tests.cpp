/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
format_tests -- tests of lib/format (LLVM-free).

  format_tests <fixtures-dir> [<writer-golden-dir>]

* every fixture file (*.txt) whose name does not start with "bad_" must be in
  canonical form: ecil-asm then ecil-dump reproduces the file (comment and
  blank lines aside) and ecil-asm of that dump reproduces the bytes, and the
  dump must not change when the stream is fed to the reader in chunks split
  at every byte offset or in single bytes;
* every "bad_*.txt" starts with "; expect: format|version <substring>" and
  must be rejected with that kind and message;
* the worked example of spec section 10 is checked byte for byte;
* the writer's golden hex streams (if given) are dumped and round-tripped;
* stress: deeply nested brackets and large arrays use no recursion.
*/

#include <dirent.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ecil_text.h"

namespace {

int failures = 0;

void report(const std::string &name, const std::string &message)
/*
Report a failure.
*/
{
  ++failures;
  std::fprintf(stderr, "FAIL %s: %s\n", name.c_str(), message.c_str());
}  /* report */

bool read_file(const std::string &path, std::string &text)
/*
Read a whole file into text.
*/
{
  std::ifstream in(path, std::ios::binary);
  std::stringstream buffer;

  buffer << in.rdbuf();
  text = buffer.str();
  return static_cast<bool>(in) || !text.empty();
}  /* read_file */

std::string normalize(const std::string &text)
/*
Drop comment-only and blank lines (fixtures carry header comments).
*/
{
  std::string result;
  std::istringstream in(text);
  std::string line;

  while (std::getline(in, line)) {
    size_t first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos || line[first] == ';')
      continue;
    result += line;
    result += '\n';
  }
  return result;
}  /* normalize */

std::vector<std::string> list_fixtures(const std::string &dir)
/*
List *.txt files in dir, sorted.
*/
{
  std::vector<std::string> names;
  DIR *d = opendir(dir.c_str());

  if (d != nullptr) {
    while (struct dirent *entry = readdir(d)) {
      std::string name = entry->d_name;
      if (name.size() > 4 && name.compare(name.size() - 4, 4, ".txt") == 0)
        names.push_back(name);
    }
    closedir(d);
  }
  std::sort(names.begin(), names.end());
  return names;
}  /* list_fixtures */

void test_good_fixture(const std::string &dir, const std::string &name)
/*
Canonical round trip plus chunk-splitting invariance for a good fixture.
*/
{
  std::string text;
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> bytes2;
  std::string error;
  std::string dumped;
  ecil::an_error stream_error;
  ecil::a_dump_options options;

  bool ok = true;

  options.check = name.compare(0, 8, "nocheck_") != 0;
  if (!read_file(dir + "/" + name, text)) {
    report(name, "cannot read");
    ok = false;
  }
  if (ok && !ecil::assemble_text(text, bytes, error)) {
    report(name, "assemble: " + error);
    ok = false;
  }
  if (ok && !ecil::dump_stream(bytes.data(), bytes.size(), options, dumped,
                               stream_error)) {
    report(name, "dump rejected a good stream: " + stream_error.message +
                 "\n" + dumped);
    ok = false;
  }
  if (ok && dumped != normalize(text)) {
    report(name, "not canonical; dump is:\n" + dumped);
    ok = false;
  }
  if (ok && (!ecil::assemble_text(dumped, bytes2, error) || bytes2 != bytes)) {
    report(name, "assemble(dump(x)) != x: " + error);
    ok = false;
  }
  /* Chunk splitting at every byte offset, and byte-by-byte. */
  for (size_t split = 1; ok && split < bytes.size(); ++split) {
    std::string again;
    ecil::a_dump_options split_options;
    split_options.check = options.check;
    split_options.split_at = split;
    if (!ecil::dump_stream(bytes.data(), bytes.size(), split_options, again,
                           stream_error) || again != dumped) {
      char buffer[64];
      std::snprintf(buffer, sizeof buffer, "split at %zu differs", split);
      report(name, buffer);
      ok = false;
    }
  }
  for (size_t chunk : {size_t(1), size_t(3), size_t(7), size_t(16)}) {
    std::string again;
    ecil::a_dump_options chunk_options;
    chunk_options.check = options.check;
    chunk_options.chunk_size = chunk;
    if (ok && (!ecil::dump_stream(bytes.data(), bytes.size(), chunk_options,
                                  again, stream_error) || again != dumped))
      report(name, "chunked feed differs");
  }
}  /* test_good_fixture */

void test_bad_fixture(const std::string &dir, const std::string &name)
/*
A bad fixture must assemble, then be rejected as its first line expects.
*/
{
  std::string text;
  std::vector<uint8_t> bytes;
  std::string error;
  std::string dumped;
  ecil::an_error stream_error;
  ecil::a_dump_options options;
  std::string expect_kind;
  std::string expect_text;

  bool ok = true;

  if (!read_file(dir + "/" + name, text)) {
    report(name, "cannot read");
    ok = false;
  }
  if (ok) {
    std::istringstream in(text);
    std::string first;
    std::getline(in, first);
    const std::string prefix = "; expect: ";
    if (first.compare(0, prefix.size(), prefix) != 0) {
      report(name, "missing '; expect:' line");
      ok = false;
    } else {
      std::istringstream words(first.substr(prefix.size()));
      words >> expect_kind;
      std::getline(words, expect_text);
      if (!expect_text.empty() && expect_text[0] == ' ')
        expect_text.erase(0, 1);
    }
  }
  if (ok && !ecil::assemble_text(text, bytes, error)) {
    report(name, "assemble: " + error);
    ok = false;
  }
  if (ok && ecil::dump_stream(bytes.data(), bytes.size(), options, dumped,
                              stream_error)) {
    report(name, "bad stream was accepted");
    ok = false;
  }
  if (ok) {
    const char *kind = stream_error.kind == ecil::an_error_kind::version
                         ? "version" : "format";
    if (expect_kind != kind)
      report(name, "expected " + expect_kind + " error, got " + kind + ": " +
                   stream_error.message);
    else if (stream_error.message.find(expect_text) == std::string::npos)
      report(name, "message '" + stream_error.message + "' lacks '" +
                   expect_text + "'");
    /* The same error must appear when the stream is chunked byte by byte. */
    ecil::a_dump_options chunked;
    std::string again;
    ecil::an_error chunk_error;
    chunked.chunk_size = 1;
    if (ecil::dump_stream(bytes.data(), bytes.size(), chunked, again,
                          chunk_error) || chunk_error.message !=
                                            stream_error.message)
      report(name, "chunked feed gives a different result: " +
                   chunk_error.message);
  }
}  /* test_bad_fixture */

std::vector<uint8_t> parse_hex(const std::string &text)
/*
Parse whitespace-separated hex bytes.
*/
{
  std::vector<uint8_t> bytes;
  std::istringstream in(text);
  std::string word;

  while (in >> word)
    bytes.push_back(static_cast<uint8_t>(std::strtoul(word.c_str(), nullptr,
                                                      16)));
  return bytes;
}  /* parse_hex */

void test_spec_example(const std::string &dir)
/*
The example of spec section 10 must assemble to exactly the documented bytes.
*/
{
  static const char expected[] =
      "45 00 00 00 03 00 00 00 "
      "01 01 81 66 "
      "12 01 20 C3 04 04 "
      "16 02 01 00 A2 01 01 "
      "30 03 02 00 01 01 "
      "40 03 00 "
      "42 01 01 01 C0 00 42 02 01 01 C0 00 "
      "70 01 72 00 70 02 72 00 8A 00 D0 01 "
      "70 01 72 00 70 02 72 00 80 01 D1 7B 01 00 D2 5C C3 41 00";
  std::string text;
  std::vector<uint8_t> bytes;
  std::string error;
  std::vector<uint8_t> want = parse_hex(expected);

  if (!read_file(dir + "/example_f.txt", text) ||
      !ecil::assemble_text(text, bytes, error)) {
    report("spec_example", "cannot assemble example_f.txt: " + error);
    return;
  }
  auto at = std::search(bytes.begin(), bytes.end(), want.begin(), want.end());
  if (at == bytes.end())
    report("spec_example", "the region frame differs from spec section 10");
}  /* test_spec_example */

void test_writer_golden(const std::string &dir)
/*
Round-trip the writer-owned golden streams (hex text files).
*/
{
  DIR *d = opendir(dir.c_str());

  if (d == nullptr)
    return;
  std::vector<std::string> names;
  while (struct dirent *entry = readdir(d)) {
    std::string name = entry->d_name;
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".hex") == 0)
      names.push_back(name);
  }
  closedir(d);
  std::sort(names.begin(), names.end());
  for (const std::string &name : names) {
    std::string text;
    std::string dumped;
    std::string error;
    std::vector<uint8_t> again;
    ecil::an_error stream_error;
    ecil::a_dump_options options;

    if (!read_file(dir + "/" + name, text)) {
      report(name, "cannot read");
      continue;
    }
    std::vector<uint8_t> bytes = parse_hex(text);
    bool ok = ecil::dump_stream(bytes.data(), bytes.size(), options, dumped,
                                stream_error);
    bool is_abort = name.find("abort") != std::string::npos;
    if (!ok) {
      report("golden " + name, stream_error.message + "\n" + dumped);
    } else if (!ecil::assemble_text(dumped, again, error) || again != bytes) {
      report("golden " + name, "dump/asm round trip differs: " + error);
    } else if (is_abort && dumped.find("frame ABORT") == std::string::npos) {
      report("golden " + name, "abort frame missing from the dump");
    }
  }
}  /* test_writer_golden */

void test_schema()
/*
The opcode table and the operand rules agree.
*/
{
  std::string problem;

  if (!ecil::schema_is_consistent(problem))
    report("schema", problem);
  if (ecil::find_opcode_info(0x00) != nullptr ||
      ecil::find_opcode_info(0xC0) != nullptr ||
      ecil::find_opcode_info(0xC1) != nullptr ||
      ecil::find_opcode_info(0xC7) != nullptr)
    report("schema", "a reserved opcode value is assigned");
}  /* test_schema */

void test_stress()
/*
Deep nesting and large arrays must not use the call stack.
*/
{
  const int depth = 300000;
  std::string text = "ecil 1.0 features=0x00000000\nframe HEADER\n"
                     "  STR 1 \"x\"\nframe REGION\n  TY_INT 1 32 true 4 4\n"
                     "  TY_FUNC 2 1 0 []\n  DECL_FUNC 3 2 0 1 1\n"
                     "  FUNC_BEGIN 3 0\n";
  std::vector<uint8_t> bytes;
  std::string error;
  std::string dumped;
  ecil::an_error stream_error;
  ecil::a_dump_options options;

  for (int k = 0; k < depth; ++k)
    text += "  BLOCK_BEGIN\n";
  for (int k = 0; k < depth; ++k)
    text += "  BLOCK_END\n";
  text += "  FUNC_END 0\nframe END\n  END 2 0\n";
  if (!ecil::assemble_text(text, bytes, error))
    report("stress", "assemble: " + error);
  else if (!ecil::dump_stream(bytes.data(), bytes.size(), options, dumped,
                              stream_error))
    report("stress", "deep nesting rejected: " + stream_error.message);

  std::string wide = "ecil 1.0 features=0x00000000\nframe HEADER\n"
                     "  STR 1 \"x\"\nframe DECLS\n  TY_INT 1 32 true 4 4\n"
                     "  TY_FUNC 2 1 0 [";
  for (int k = 0; k < 100000; ++k)
    wide += k ? ", 1" : "1";
  wide += "]\nframe END\n  END 2 0\n";
  dumped.clear();
  if (!ecil::assemble_text(wide, bytes, error))
    report("stress", "assemble wide: " + error);
  else if (!ecil::dump_stream(bytes.data(), bytes.size(), options, dumped,
                              stream_error))
    report("stress", "wide array rejected: " + stream_error.message);
}  /* test_stress */

}  /* anonymous namespace */

int main(int argc, char **argv)
/*
Run all format tests; return the number of failures (capped at 1).
*/
{
  int status = 0;
  std::string dir = argc >= 2 ? argv[1] : "";
  std::vector<std::string> names;
  int good = 0;
  int bad = 0;

  if (argc < 2) {
    std::fprintf(stderr,
                 "usage: format_tests <fixtures-dir> [<golden-dir>]\n");
    status = 2;
  } else {
    names = list_fixtures(dir);
    test_schema();
  }
  for (const std::string &name : names) {
    if (name.compare(0, 4, "bad_") == 0) {
      test_bad_fixture(dir, name);
      ++bad;
    } else {
      test_good_fixture(dir, name);
      ++good;
    }
  }
  if (status == 0) {
    test_spec_example(dir);
    if (argc > 2)
      test_writer_golden(argv[2]);
    test_stress();
    std::printf("format_tests: %d good fixtures, %d bad fixtures, "
                "%d failures\n", good, bad, failures);
    status = failures == 0 ? 0 : 1;
  }
  return status;
}  /* main */
