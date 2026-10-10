/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
capi_tests -- tests of the C API (edg_cir.h) through libedg_cir.so.

  capi_tests <empty.hex> <abort.hex> <edg-cir-binary>

Streams are built from the text form with ecil::assemble_text (the format
library is linked statically into the test; the shared library exports only
edg_cir_*).
*/

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ecil_text.h"
#include "edg_cir/edg_cir.h"

namespace {

int failures = 0;

#define CHECK(cond, ...)                                                     \
  do {                                                                       \
    if (!(cond)) {                                                           \
      ++failures;                                                            \
      std::fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
      std::fprintf(stderr, __VA_ARGS__);                                     \
      std::fprintf(stderr, "\n");                                            \
    }                                                                        \
  } while (0)

struct a_capture {
  std::string                bytes;
  std::vector<std::string>   messages;
  std::thread::id            caller;
  bool                       wrong_thread = false;
  int                        fail_after = -1;   /* write callback failure */
  int                        writes = 0;
  int                        errors = 0;        /* ERROR-severity diags */
};  /* a_capture */

int write_cb(void *user, const void *bytes, size_t length)
/*
Collect output chunks (optionally fail after N chunks).
*/
{
  a_capture *c = static_cast<a_capture *>(user);

  if (std::this_thread::get_id() != c->caller)
    c->wrong_thread = true;
  c->bytes.append(static_cast<const char *>(bytes), length);
  ++c->writes;
  return c->fail_after >= 0 && c->writes > c->fail_after ? 1 : 0;
}  /* write_cb */

void diag_cb(void *user, an_edg_cir_severity severity, uint32_t,
             const char *message,
             const char *, uint32_t, uint32_t)
/*
Collect diagnostics and check they arrive on the calling thread.
*/
{
  a_capture *c = static_cast<a_capture *>(user);

  if (std::this_thread::get_id() != c->caller)
    c->wrong_thread = true;
  c->messages.push_back(message);
  if (severity == EDG_CIR_SEVERITY_ERROR)
    ++c->errors;
}  /* diag_cb */

std::vector<uint8_t> assemble(const std::string &text)
/*
Assemble text to bytes (abort the test on failure).
*/
{
  std::vector<uint8_t> bytes;
  std::string error;

  if (!ecil::assemble_text(text, bytes, error)) {
    std::fprintf(stderr, "assemble failed: %s\n", error.c_str());
    std::exit(2);
  }
  return bytes;
}  /* assemble */

const char *header_text =
    "ecil 0.6 features=0x00000000\n"
    "frame HEADER\n"
    "  STR 1 \"x86_64-unknown-linux-gnu\"\n"
    "  STR 2 \"\"\n"
    "  STR 3 \"capi_tests\"\n"
    "  TY_INT 1 32 true 4 4\n"
    "  TY_INT 2 64 false 8 8\n"
    "  HDR_TARGET 1 2 nil 64 64 6\n"
    "  HDR_LANG 0 201112 0\n"
    "  HDR_PRODUCER 3 nil\n"
    "  HDR_TYPE_ROLE 3 1\n"
    "  HDR_TYPE_ROLE 1 2\n";

std::vector<uint8_t> empty_stream()
/*
The empty-translation-unit stream the writer produces in M1.
*/
{
  return assemble(std::string(header_text) +
                  "frame GLOBALS\nframe END\n  END 2 0\n");
}  /* empty_stream */

an_edg_cir_status run(const std::vector<uint8_t> &stream,
                      an_edg_cir_output_kind kind, a_capture &capture,
                      int opt_level = 0, uint32_t flags = 0,
                      const char *triple = nullptr, size_t split = 0)
/*
Run a whole session on the stream; output and diagnostics go to capture.
*/
{
  an_edg_cir_options options;
  an_edg_cir_session *session = nullptr;
  an_edg_cir_status status;

  capture.caller = std::this_thread::get_id();
  edg_cir_options_init(&options);
  options.output_kind = kind;
  options.write_callback = write_cb;
  options.write_user_data = &capture;
  options.diagnostic_callback = diag_cb;
  options.diagnostic_user_data = &capture;
  options.opt_level = opt_level;
  options.flags = flags;
  options.target_triple_override = triple;
  status = edg_cir_session_open(&options, &session);
  if (status != EDG_CIR_OK) {
    /* Nothing was opened: there is no session to consume or finish. */
  } else if (split == 0 || split >= stream.size()) {
    status = edg_cir_session_consume(session, stream.data(), stream.size());
  } else {
    status = edg_cir_session_consume(session, stream.data(), split);
    if (status == EDG_CIR_OK)
      status = edg_cir_session_consume(session, stream.data() + split,
                                       stream.size() - split);
  }
  if (status == EDG_CIR_OK)
    status = edg_cir_session_finish(session);
  if (session != nullptr)
    edg_cir_session_close(session);
  return status;
}  /* run */

std::vector<uint8_t> parse_hex_file(const char *path)
/*
Read a whitespace-separated hex byte file.
*/
{
  std::ifstream in(path);
  std::vector<uint8_t> bytes;
  std::string word;

  while (in >> word)
    bytes.push_back(static_cast<uint8_t>(std::strtoul(word.c_str(), nullptr,
                                                      16)));
  return bytes;
}  /* parse_hex_file */

void test_output_kinds(const std::vector<uint8_t> &stream)
/*
Every output kind for the empty module.
*/
{
  a_capture cir, lowered, ll, bc, as, obj, none;

  CHECK(run(stream, EDG_CIR_OUTPUT_CIR, cir) == EDG_CIR_OK, "cir");
  CHECK(cir.bytes.find("cir.triple = \"x86_64-unknown-linux-gnu\"") !=
        std::string::npos, "cir.triple attribute missing:\n%s",
        cir.bytes.c_str());
  CHECK(cir.bytes.find("cir.target_abi = \"\"") != std::string::npos,
        "cir.target_abi missing");
  CHECK(cir.bytes.find("dlti.dl_spec") != std::string::npos,
        "dlti.dl_spec missing");
  CHECK(cir.bytes.find("cir.lang = #cir.lang<c>") != std::string::npos,
        "cir.lang missing");
  CHECK(cir.bytes.find("cir.lowering_lang_options") != std::string::npos,
        "lowering_lang_options missing");
  CHECK(cir.bytes.find("cir.size_type_width = 64") != std::string::npos,
        "size_type_width missing");
  CHECK(cir.bytes.find("cir.int_type_width = 32") != std::string::npos,
        "int_type_width missing");
  CHECK(!cir.wrong_thread, "callback on the wrong thread");
  CHECK(run(stream, EDG_CIR_OUTPUT_CIR_LOWERED, lowered) == EDG_CIR_OK,
        "cir-lowered");
  CHECK(lowered.bytes.find("module") != std::string::npos, "lowered text");
  CHECK(run(stream, EDG_CIR_OUTPUT_LLVM_IR, ll) == EDG_CIR_OK, "llvm");
  CHECK(ll.bytes.find("target triple = \"x86_64-unknown-linux-gnu\"") !=
        std::string::npos, "llvm triple:\n%s", ll.bytes.c_str());
  CHECK(run(stream, EDG_CIR_OUTPUT_LLVM_BC, bc) == EDG_CIR_OK, "bc");
  CHECK(bc.bytes.size() > 4 && bc.bytes.compare(0, 2, "BC") == 0,
        "bitcode magic");
  CHECK(run(stream, EDG_CIR_OUTPUT_ASM, as) == EDG_CIR_OK, "asm");
  CHECK(as.bytes.find(".file") != std::string::npos, "assembly text:\n%s",
        as.bytes.c_str());
  CHECK(run(stream, EDG_CIR_OUTPUT_OBJECT, obj) == EDG_CIR_OK, "obj");
  CHECK(obj.bytes.size() > 64 && obj.bytes.compare(0, 4, "\x7f""ELF") == 0,
        "ELF magic");
  CHECK(run(stream, EDG_CIR_OUTPUT_NONE, none) == EDG_CIR_OK, "none");
  CHECK(none.bytes.empty(), "no output expected");
  for (int level = 1; level <= 3; ++level) {
    a_capture o;
    CHECK(run(stream, EDG_CIR_OUTPUT_OBJECT, o, level) == EDG_CIR_OK,
          "-O%d", level);
  }
  {
    a_capture o;
    CHECK(run(stream, EDG_CIR_OUTPUT_OBJECT, o, 0, EDG_CIR_FLAG_VERIFY_EACH |
             EDG_CIR_FLAG_DISABLE_CIR_PASSES) == EDG_CIR_OK, "flags");
  }
}  /* test_output_kinds */

const char *const linux_triple = "x86_64-unknown-linux-gnu";

void test_chunking(const std::vector<uint8_t> &stream,
                   const char *triple = nullptr)
/*
The result must not depend on how the stream is split into chunks.  The
writer's goldens are produced on a Mac, so they run with a triple override.
*/
{
  a_capture whole;

  CHECK(run(stream, EDG_CIR_OUTPUT_CIR, whole, 0, 0, triple) == EDG_CIR_OK,
        "whole");
  for (size_t split = 1; split < stream.size(); ++split) {
    a_capture part;
    an_edg_cir_status st = run(stream, EDG_CIR_OUTPUT_CIR, part, 0, 0,
                               triple, split);
    CHECK(st == EDG_CIR_OK && part.bytes == whole.bytes,
          "split at %zu: status %d", split, static_cast<int>(st));
  }
  {
    /* Byte by byte through the C API. */
    a_capture c;
    an_edg_cir_options options;
    an_edg_cir_session *session = nullptr;
    an_edg_cir_status st;

    c.caller = std::this_thread::get_id();
    edg_cir_options_init(&options);
    options.output_kind = EDG_CIR_OUTPUT_CIR;
    options.write_callback = write_cb;
    options.write_user_data = &c;
    options.target_triple_override = triple;
    st = edg_cir_session_open(&options, &session);
    for (size_t k = 0; st == EDG_CIR_OK && k < stream.size(); ++k)
      st = edg_cir_session_consume(session, &stream[k], 1);
    if (st == EDG_CIR_OK)
      st = edg_cir_session_finish(session);
    edg_cir_session_close(session);
    CHECK(st == EDG_CIR_OK && c.bytes == whole.bytes, "byte-by-byte");
  }
}  /* test_chunking */

void test_errors(const std::vector<uint8_t> &stream, const char *abort_hex)
/*
Error statuses, sticky errors and call-order rules.
*/
{
  a_capture c;
  std::vector<uint8_t> bad = stream;

  bad[0] = 'X';
  CHECK(run(bad, EDG_CIR_OUTPUT_CIR, c) == EDG_CIR_ERROR_FORMAT, "bad magic");
  CHECK(!c.messages.empty(), "a diagnostic is expected");
  CHECK(run(assemble("ecil 1.0 features=0x0\n"), EDG_CIR_OUTPUT_CIR, c) ==
        EDG_CIR_ERROR_VERSION, "major version");
  CHECK(run(assemble("ecil 0.6 features=0x00010000\n"), EDG_CIR_OUTPUT_CIR,
            c) == EDG_CIR_ERROR_VERSION, "required feature");
  CHECK(run(assemble(std::string(header_text) +
                     "frame DECLS\n  ?0x99 1\nframe END\n  END 2 0\n"),
            EDG_CIR_OUTPUT_CIR, c) == EDG_CIR_ERROR_FORMAT, "unknown opcode");
  {
    std::string aarch64 = header_text;
    size_t at = aarch64.find("x86_64-unknown-linux-gnu");
    aarch64.replace(at, 24, "aarch64-unknown-linux-gnu");
    CHECK(run(assemble(aarch64 + "frame GLOBALS\nframe END\n  END 2 0\n"),
              EDG_CIR_OUTPUT_CIR, c) == EDG_CIR_ERROR_UNSUPPORTED,
          "aarch64 triple");
  }
  CHECK(run(stream, EDG_CIR_OUTPUT_CIR, c, 0, 0,
            "x86_64-pc-windows-msvc") == EDG_CIR_ERROR_UNSUPPORTED,
        "triple override");
  CHECK(run(assemble(std::string(header_text) +
                     "frame REGION\n  FUNC_BEGIN 99 0\n  FUNC_END 0\n"
                     "frame END\n  END 2 0\n"),
            EDG_CIR_OUTPUT_CIR, c) == EDG_CIR_ERROR_FORMAT,
        "FUNC_BEGIN of an undeclared function");
  {
    a_capture quiet;
    CHECK(run(assemble(std::string(header_text) + "frame ABORT\n"),
              EDG_CIR_OUTPUT_CIR, quiet) == EDG_CIR_ERROR_ABORTED,
          "F_ABORT");
    CHECK(quiet.errors == 0, "F_ABORT reports no error diagnostic (%d)",
          quiet.errors);
  }
  {
    std::vector<uint8_t> golden = parse_hex_file(abort_hex);
    CHECK(!golden.empty() &&
          run(golden, EDG_CIR_OUTPUT_CIR, c, 0, 0, linux_triple) ==
              EDG_CIR_ERROR_ABORTED,
          "writer's abort golden");
  }
  CHECK(run(assemble(std::string(header_text) +
                     "frame GLOBALS\nframe END\n  END 2 3\n"),
            EDG_CIR_OUTPUT_CIR, c) == EDG_CIR_ERROR_ABORTED, "END status");
  {
    /* Sticky errors, finish before END, consume after finish. */
    an_edg_cir_options options;
    an_edg_cir_session *session = nullptr;

    edg_cir_options_init(&options);
    options.output_kind = EDG_CIR_OUTPUT_NONE;
    options.diagnostic_callback = diag_cb;
    options.diagnostic_user_data = &c;
    c.caller = std::this_thread::get_id();
    CHECK(edg_cir_session_open(&options, &session) == EDG_CIR_OK, "open");
    CHECK(edg_cir_session_consume(session, bad.data(), bad.size()) ==
          EDG_CIR_ERROR_FORMAT, "first error");
    CHECK(edg_cir_session_consume(session, stream.data(), stream.size()) ==
          EDG_CIR_ERROR_FORMAT, "sticky");
    CHECK(edg_cir_session_finish(session) == EDG_CIR_ERROR_FORMAT,
          "sticky finish");
    edg_cir_session_close(session);

    CHECK(edg_cir_session_open(&options, &session) == EDG_CIR_OK, "open 2");
    CHECK(edg_cir_session_consume(session, stream.data(), 20) == EDG_CIR_OK,
          "partial");
    CHECK(edg_cir_session_finish(session) == EDG_CIR_ERROR_FORMAT,
          "finish before F_END (truncated)");
    edg_cir_session_close(session);

    CHECK(edg_cir_session_open(&options, &session) == EDG_CIR_OK, "open 3");
    CHECK(edg_cir_session_consume(session, stream.data(), stream.size()) ==
          EDG_CIR_OK, "whole");
    CHECK(edg_cir_session_finish(session) == EDG_CIR_OK, "finish");
    CHECK(edg_cir_session_consume(session, stream.data(), 1) ==
          EDG_CIR_ERROR_USAGE, "consume after finish");
    edg_cir_session_close(session);
    edg_cir_session_close(nullptr);
  }
  {
    an_edg_cir_options options;
    an_edg_cir_session *session = nullptr;

    CHECK(edg_cir_session_open(nullptr, &session) == EDG_CIR_ERROR_USAGE,
          "null options");
    edg_cir_options_init(&options);
    options.api_version = 99;
    CHECK(edg_cir_session_open(&options, &session) == EDG_CIR_ERROR_USAGE,
          "api version");
    edg_cir_options_init(&options);
    options.opt_level = 9;
    CHECK(edg_cir_session_open(&options, &session) == EDG_CIR_ERROR_USAGE,
          "opt level");
    CHECK(edg_cir_session_consume(nullptr, "", 0) == EDG_CIR_ERROR_USAGE,
          "null session");
  }
  {
    a_capture failing;
    failing.fail_after = 0;
    CHECK(run(stream, EDG_CIR_OUTPUT_CIR, failing) == EDG_CIR_ERROR_IO,
          "write callback failure");
  }
}  /* test_errors */

#if EDG_CIR_TEST_HOOKS
void test_crash_recovery(const std::vector<uint8_t> &stream)
/*
A fatal LLVM error, a usage-style fatal error, a crash and an abort in the
worker all become EDG_CIR_ERROR_INTERNAL; the session stays usable
afterwards.
*/
{
  static const char *const hooks[] = {"fatal", "usage", "segv", "abort"};

  for (const char *hook : hooks) {
    a_capture c;
    setenv("EDG_CIR_TEST_CRASH", hook, 1);
    CHECK(run(stream, EDG_CIR_OUTPUT_OBJECT, c) == EDG_CIR_ERROR_INTERNAL,
          "crash hook %s", hook);
    CHECK(!c.messages.empty() && !c.wrong_thread, "diagnostic for %s", hook);
    unsetenv("EDG_CIR_TEST_CRASH");
    a_capture again;
    CHECK(run(stream, EDG_CIR_OUTPUT_OBJECT, again) == EDG_CIR_OK,
          "session after crash hook %s", hook);
  }

  /* The same while consuming (on the caller's thread, not the worker). */
  static const char *const consume_hooks[] = {"fatal", "segv", "abort"};
  for (const char *hook : consume_hooks) {
    a_capture c;
    setenv("EDG_CIR_TEST_CONSUME_CRASH", hook, 1);
    CHECK(run(stream, EDG_CIR_OUTPUT_OBJECT, c) == EDG_CIR_ERROR_INTERNAL,
          "consume crash hook %s", hook);
    CHECK(!c.messages.empty() && !c.wrong_thread,
          "diagnostic for consume hook %s", hook);
    unsetenv("EDG_CIR_TEST_CONSUME_CRASH");
    a_capture again;
    CHECK(run(stream, EDG_CIR_OUTPUT_OBJECT, again) == EDG_CIR_OK,
          "session after consume hook %s", hook);
  }
}  /* test_crash_recovery */
#endif

void test_misc(const std::vector<uint8_t> &stream, const char *tool)
/*
Output path, process_file, version and status strings, the edg-cir tool.
*/
{
  std::string ecil_path = "capi_tests.tmp.ecil";
  std::string out_path = "capi_tests.tmp.o";
  an_edg_cir_options options;

  {
    std::ofstream f(ecil_path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(stream.data()),
            static_cast<std::streamsize>(stream.size()));
  }
  edg_cir_options_init(&options);
  options.output_kind = EDG_CIR_OUTPUT_OBJECT;
  options.output_path = out_path.c_str();
  CHECK(edg_cir_process_file(&options, ecil_path.c_str()) == EDG_CIR_OK,
        "process_file");
  {
    std::ifstream f(out_path, std::ios::binary);
    char magic[4] = {0, 0, 0, 0};
    f.read(magic, 4);
    CHECK(std::memcmp(magic, "\x7f""ELF", 4) == 0, "object file written");
  }
  CHECK(edg_cir_process_file(&options, "no/such/file.ecil") ==
        EDG_CIR_ERROR_IO, "missing input");
  CHECK(std::strstr(edg_cir_version_string(), "ECIL 0.6") != nullptr,
        "version string: %s", edg_cir_version_string());
  for (int s = 0; s <= 9; ++s)
    CHECK(std::strcmp(edg_cir_status_string(
                          static_cast<an_edg_cir_status>(s)),
                      "unknown status") != 0, "status string %d", s);
  {
    std::string command = std::string(tool) + " --emit=asm " + ecil_path +
                          " > capi_tests.tmp.s";
    CHECK(std::system(command.c_str()) == 0, "edg-cir --emit=asm");
    command = std::string(tool) + " -O2 --verify-each -o capi_tests.tmp2.o " +
              ecil_path;
    CHECK(std::system(command.c_str()) == 0, "edg-cir -O2 --verify-each");
    command = std::string(tool) + " --emit=asm - < " + ecil_path +
              " > /dev/null";
    CHECK(std::system(command.c_str()) == 0, "edg-cir from stdin");
    command = std::string(tool) + " --triple=aarch64-linux-gnu " + ecil_path +
              " -o /dev/null 2> /dev/null";
    CHECK(std::system(command.c_str()) != 0, "edg-cir refuses aarch64");
  }
  std::remove(ecil_path.c_str());
  std::remove(out_path.c_str());
  std::remove("capi_tests.tmp.s");
  std::remove("capi_tests.tmp2.o");
}  /* test_misc */

}  /* anonymous namespace */

int main(int argc, char **argv)
/*
Run the driver tests; return 0 when all pass.
*/
{
  int status = 0;

  if (argc < 4) {
    std::fprintf(stderr,
                 "usage: capi_tests <empty.hex> <abort.hex> <edg-cir>\n");
    status = 2;
  } else {
    std::vector<uint8_t> stream = empty_stream();
    std::vector<uint8_t> golden = parse_hex_file(argv[1]);

    test_output_kinds(stream);
    if (golden.empty()) {
      CHECK(false, "cannot read %s", argv[1]);
    } else {
      a_capture c;
      CHECK(run(golden, EDG_CIR_OUTPUT_OBJECT, c, 0, 0, linux_triple) ==
                EDG_CIR_OK,
            "the writer's empty.hex golden stream (status via writer format)");
      CHECK(c.bytes.size() > 64, "object for the writer's golden stream");
      test_chunking(golden, linux_triple);
    }
    test_chunking(stream);
    test_errors(stream, argv[2]);
#if EDG_CIR_TEST_HOOKS
    test_crash_recovery(stream);
#endif
    test_misc(stream, argv[3]);
    std::printf("capi_tests: %d failures\n", failures);
    status = failures == 0 ? 0 : 1;
  }
  return status;
}  /* main */
