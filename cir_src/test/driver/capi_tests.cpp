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

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

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
    "ecil 1.0 features=0x00000000\n"
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
  CHECK(run(assemble("ecil 2.0 features=0x0\n"), EDG_CIR_OUTPUT_CIR, c) ==
        EDG_CIR_ERROR_VERSION, "major version");
  CHECK(run(assemble("ecil 0.9 features=0x0\n"), EDG_CIR_OUTPUT_CIR, c) ==
        EDG_CIR_ERROR_VERSION, "0.x streams are rejected");
  CHECK(run(assemble("ecil 1.0 features=0x00010000\n"), EDG_CIR_OUTPUT_CIR,
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

void test_api_versions(const std::vector<uint8_t> &stream)
/*
API 2 added two fields and two flags.  A caller built against API 1 (a
struct that ends before chunk_functions, api_version 1) is accepted and
gets the defaults of the new fields; the new flags and the chunk fields are
accepted from an API 2 caller and take effect (the chunked-lowering note of
the statistics says so: threshold 1 forces chunking, UINT32_MAX never chunks,
0 selects the defaults); a caller from the future is refused; a struct that is too short to hold the header is refused.
*/
{
  an_edg_cir_options options;
  an_edg_cir_session *session = nullptr;
  a_capture c;
  const uint32_t api1_size =
      static_cast<uint32_t>(offsetof(an_edg_cir_options, chunk_functions));

  CHECK(EDG_CIR_API_VERSION == 2, "this test is for API 2");
  CHECK(api1_size > 3 * sizeof(uint32_t) && api1_size < sizeof options,
        "API 1 struct size %u", api1_size);
  edg_cir_options_init(&options);
  CHECK(options.struct_size == sizeof options &&
            options.chunk_functions == 0 && options.chunk_threshold == 0,
        "defaults of the API 2 fields");
  /* An API 1 caller: the bytes past its struct are garbage (here: ones). */
  {
    unsigned char raw[sizeof options];

    std::memset(raw, 0xff, sizeof raw);
    std::memcpy(raw, &options, api1_size);
    an_edg_cir_options *old = reinterpret_cast<an_edg_cir_options *>(raw);

    old->struct_size = api1_size;
    old->api_version = 1;
    old->output_kind = EDG_CIR_OUTPUT_LLVM_IR;
    old->write_callback = write_cb;
    old->write_user_data = &c;
    old->diagnostic_callback = diag_cb;
    old->diagnostic_user_data = &c;
    c.caller = std::this_thread::get_id();
    CHECK(edg_cir_session_open(old, &session) == EDG_CIR_OK,
          "API 1 sized caller");
    CHECK(session != nullptr &&
              edg_cir_session_consume(session, stream.data(), stream.size()) ==
                  EDG_CIR_OK &&
              edg_cir_session_finish(session) == EDG_CIR_OK,
          "API 1 sized caller runs");
    edg_cir_session_close(session);
    CHECK(c.bytes.find("target triple") != std::string::npos,
          "API 1 sized caller output");
  }
  {
    a_capture on;

    CHECK(run(stream, EDG_CIR_OUTPUT_LLVM_IR, on, 2,
              EDG_CIR_FLAG_EAGER_BODIES | EDG_CIR_FLAG_NO_LLVM_PASSES) ==
              EDG_CIR_OK,
          "API 2 flags");
    CHECK(on.bytes.find("target triple") != std::string::npos,
          "API 2 flags output");
  }
  {
    /* threshold: UINT32_MAX never chunks, 1 forces it (a module with one
       function body or more), 0 is the default (this module is far below
       it).  The stream has two function bodies. */
    static const uint32_t thresholds[3] = {0xffffffffu, 1, 0};
    std::string outputs[3];
    const std::vector<uint8_t> bodies = assemble(
        std::string(header_text) +
        "frame DECLS\n  STR 10 \"f\"\n  STR 11 \"main\"\n"
        "  TY_FUNC 20 1 0 []\n  DECL_FUNC 30 20 0 1 10\n"
        "  DECL_FUNC 31 20 0 9 11\n"
        "frame REGION\n  FUNC_BEGIN 30 0\n  CONST_INT 1 0\n"
        "  RETURN_STMT true\n  FUNC_END 0\n  FUNC_BEGIN 31 0\n"
        "  CALL 30 20 0 0\n  RETURN_STMT true\n  FUNC_END 0\n"
        "frame END\n  END auto 0\n");

    for (int k = 0; k < 3; ++k) {
      a_capture on;
      size_t at;

      edg_cir_options_init(&options);
      options.output_kind = EDG_CIR_OUTPUT_LLVM_IR;
      options.write_callback = write_cb;
      options.write_user_data = &on;
      options.diagnostic_callback = diag_cb;
      options.diagnostic_user_data = &on;
      options.flags = EDG_CIR_FLAG_PRINT_STATS;
      options.chunk_functions = 3;
      options.chunk_threshold = thresholds[k];
      on.caller = std::this_thread::get_id();
      CHECK(edg_cir_session_open(&options, &session) == EDG_CIR_OK &&
                edg_cir_session_consume(session, bodies.data(),
                                        bodies.size()) == EDG_CIR_OK &&
                edg_cir_session_finish(session) == EDG_CIR_OK,
            "chunk fields accepted");
      edg_cir_session_close(session);
      outputs[k] = on.bytes;
      for (const std::string &message : on.messages) {
        const char *key = "stats: chunked lowering: ";

        if ((at = message.find(key)) != std::string::npos) {
          unsigned long chunks = 0, size = 0;

          std::sscanf(message.c_str() + at + std::strlen(key),
                      "%lu chunks of at most %lu", &chunks, &size);
          CHECK(size == 3, "chunk size %lu", size);
          CHECK(k == 1 ? chunks >= 2 : chunks == 0,
                "threshold %u: %lu chunks", thresholds[k], chunks);
          break;
        }
        CHECK(&message != &on.messages.back(),
              "no chunked-lowering note (threshold %u)", thresholds[k]);
      }
    }
    CHECK(outputs[0] == outputs[1] && outputs[0] == outputs[2],
          "chunked and whole-module output are the same");
  }
  edg_cir_options_init(&options);
  options.api_version = EDG_CIR_API_VERSION + 1;
  CHECK(edg_cir_session_open(&options, &session) == EDG_CIR_ERROR_USAGE,
        "a newer API is refused");
  edg_cir_options_init(&options);
  options.api_version = 0;
  CHECK(edg_cir_session_open(&options, &session) == EDG_CIR_ERROR_USAGE,
        "API 0 is refused");
  edg_cir_options_init(&options);
  options.struct_size = 8;
  CHECK(edg_cir_session_open(&options, &session) == EDG_CIR_ERROR_USAGE,
        "a struct too short for the header is refused");
}  /* test_api_versions */

#if EDG_CIR_TEST_HOOKS
void test_crash_recovery(const std::vector<uint8_t> &stream)
/*
A fatal LLVM error, a usage-style fatal error, a crash, an abort and a stack
overflow in the worker all become EDG_CIR_ERROR_INTERNAL; the session stays
usable afterwards.  (The overflow needs the worker's alternate signal stack:
the crash handler has no stack to run on otherwise, and the process dies.)
*/
{
  static const char *const hooks[] = {"fatal", "usage", "segv", "abort",
                                      "overflow"};

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

  /* The same while consuming.  consume_guarded runs the reader and the
     emitter on the guard worker too (the caller's thread only waits for it),
     which is why the "overflow" hook is recovered here as well: the worker's
     signal stack is what the crash handler runs on. */
  static const char *const consume_hooks[] = {"fatal", "usage", "segv",
                                              "abort", "overflow"};
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

/* Exit paths.  The front end (cpfe-cir) calls exit() after errors, after
   F_ABORT, or without ever finishing the session; the library's worker thread
   must not keep the process alive or block it (a handoff deadlock of the
   persistent worker once made cpfe-cir sleep forever after "Compilation
   terminated.", ignoring SIGTERM).  Each case runs in a child process with a
   deadline; the parent kills it when it is late. */

enum a_exit_mode { exit_after_full, exit_after_bytes, exit_after_abort,
                   exit_after_fatal, exit_in_worker, exit_in_worker_close,
                   exit_in_pipeline, overflow_pipeline, overflow_consume,
                   wait_for_signal };

#if EDG_CIR_TEST_HOOKS
an_edg_cir_session *exit_session = nullptr;   /* For the atexit handler. */

void close_session_at_exit()
/*
What a front end's atexit handler or static destructor does: close the
session.  When the exit() came from the guard worker this runs on the worker
while the thread that waits for the section holds the guard mutex.
*/
{
  edg_cir_session_close(exit_session);
}  /* close_session_at_exit */
#endif

int child_body(a_exit_mode mode, const std::vector<uint8_t> &stream,
               const std::vector<uint8_t> &abort_stream)
/*
Open a session, feed it as the mode says and leave the process through exit()
(or wait to be killed): never finish or close the session.
*/
{
  a_capture capture;
  an_edg_cir_options options;
  an_edg_cir_session *session = nullptr;

  capture.caller = std::this_thread::get_id();
  edg_cir_options_init(&options);
  options.output_kind = EDG_CIR_OUTPUT_OBJECT;
  options.write_callback = write_cb;
  options.write_user_data = &capture;
  options.diagnostic_callback = diag_cb;
  options.diagnostic_user_data = &capture;
  options.target_triple_override = "x86_64-unknown-linux-gnu";
  if (edg_cir_session_open(&options, &session) != EDG_CIR_OK)
    return 99;
  switch (mode) {
  case exit_after_full:
    edg_cir_session_consume(session, stream.data(), stream.size());
    break;
  case exit_after_bytes:
    for (size_t k = 0; k < stream.size(); ++k)
      edg_cir_session_consume(session, &stream[k], 1);
    break;
  case exit_after_abort:
    edg_cir_session_consume(session, abort_stream.data(),
                            abort_stream.size());
    break;
  case exit_after_fatal:
#if EDG_CIR_TEST_HOOKS
    setenv("EDG_CIR_TEST_CONSUME_CRASH", "fatal", 1);
    edg_cir_session_consume(session, stream.data(), stream.size());
#endif
    break;
  case exit_in_worker:
  case exit_in_worker_close:
#if EDG_CIR_TEST_HOOKS
    /* The whole stream first, so that the session holds a module; then a
       consume call whose guarded section calls exit() on the worker. */
    edg_cir_session_consume(session, stream.data(), stream.size());
    if (mode == exit_in_worker_close) {
      exit_session = session;
      std::atexit(close_session_at_exit);
    }
    setenv("EDG_CIR_TEST_CONSUME_CRASH", "exit", 1);
    edg_cir_session_consume(session, stream.data(), 1);
#endif
    break;
  case exit_in_pipeline:
#if EDG_CIR_TEST_HOOKS
    exit_session = session;
    std::atexit(close_session_at_exit);
    edg_cir_session_consume(session, stream.data(), stream.size());
    setenv("EDG_CIR_TEST_CRASH", "exit", 1);
    edg_cir_session_finish(session);
#endif
    break;
  case overflow_pipeline:
  case overflow_consume:
#if EDG_CIR_TEST_HOOKS
    /* A stack overflow in a section is reported as a crash of the stage;
       the process must survive it (the exit status says whether it did). */
    {
      an_edg_cir_status result;

      if (mode == overflow_consume) {
        setenv("EDG_CIR_TEST_CONSUME_CRASH", "overflow", 1);
        result = edg_cir_session_consume(session, stream.data(),
                                         stream.size());
      } else {
        setenv("EDG_CIR_TEST_CRASH", "overflow", 1);
        result = edg_cir_session_consume(session, stream.data(),
                                         stream.size());
        if (result == EDG_CIR_OK)
          result = edg_cir_session_finish(session);
      }
      std::_Exit(result == EDG_CIR_ERROR_INTERNAL ? 0 : 1);
    }
#endif
    break;
  case wait_for_signal:
    edg_cir_session_consume(session, stream.data(), stream.size() / 2);
    for (;;)
      pause();
  }
  std::exit(7);
}  /* child_body */

int run_child(a_exit_mode mode, const std::vector<uint8_t> &stream,
              const std::vector<uint8_t> &abort_stream, int signal_number,
              double seconds)
/*
Fork a child for the mode; after a short wait send it signal_number (when
nonzero).  Returns its exit status, 128 + the signal that killed it, or -1 when
it was still alive after the deadline (then it is killed with SIGKILL).
*/
{
  pid_t pid = fork();
  int result = -1;

  if (pid == 0)
    std::_Exit(child_body(mode, stream, abort_stream));
  if (pid > 0) {
    auto start = std::chrono::steady_clock::now();
    bool signalled = signal_number == 0;
    int status = 0;

    for (;;) {
      double elapsed = std::chrono::duration<double>(
          std::chrono::steady_clock::now() - start).count();

      if (waitpid(pid, &status, WNOHANG) == pid) {
        result = WIFEXITED(status) ? WEXITSTATUS(status)
                                   : 128 + WTERMSIG(status);
        break;
      }
      if (!signalled && elapsed > 0.5) {
        kill(pid, signal_number);
        signalled = true;
      }
      if (elapsed > seconds) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        break;
      }
      usleep(20000);
    }
  }
  return result;
}  /* run_child */

void test_exit_paths(const std::vector<uint8_t> &stream,
                     const char *abort_hex)
{
  std::vector<uint8_t> abort_stream = parse_hex_file(abort_hex);
#if EDG_CIR_TEST_HOOKS
  int status;
#endif

  CHECK(run_child(exit_after_full, stream, abort_stream, 0, 10) == 7,
        "exit() after a consumed stream, no finish");
  CHECK(run_child(exit_after_bytes, stream, abort_stream, 0, 10) == 7,
        "exit() after one consume call per byte, no finish");
  CHECK(run_child(exit_after_abort, stream, abort_stream, 0, 10) == 7,
        "exit() after F_ABORT");
#if EDG_CIR_TEST_HOOKS
  CHECK(run_child(exit_after_fatal, stream, abort_stream, 0, 10) == 7,
        "exit() after a fatal error in a guarded section");
  /* exit() on the guard worker, inside a guarded section: the hook exits
     with status 66, and the process must leave with it (not 7, the child's
     own exit, and not the -1 of a deadlock on the guard mutex). */
  status = run_child(exit_in_worker, stream, abort_stream, 0, 10);
  CHECK(status == 66, "exit() on the guard worker while consuming (status "
        "%d)", status);
  status = run_child(exit_in_worker_close, stream, abort_stream, 0, 10);
  CHECK(status == 66, "exit() on the guard worker while consuming, atexit "
        "closes the session (status %d)", status);
  status = run_child(exit_in_pipeline, stream, abort_stream, 0, 10);
  CHECK(status == 66, "exit() on the guard worker in the pipeline, atexit "
        "closes the session (status %d)", status);
  /* The same on the thread of its own that a section gets when no worker
     slot is left.  A stack overflow there needs the signal stack of that
     thread; it runs in a child process that has done nothing else (earlier
     crashes in this process leave the signal state different), and the exit
     status 0 says the process survived and the status was INTERNAL. */
  setenv("EDG_CIR_TEST_NO_WORKER", "1", 1);
  status = run_child(overflow_consume, stream, abort_stream, 0, 30);
  CHECK(status == 0, "stack overflow on a section's own thread while "
        "consuming (status %d)", status);
  status = run_child(overflow_pipeline, stream, abort_stream, 0, 30);
  CHECK(status == 0, "stack overflow on a section's own thread in the "
        "pipeline (status %d)", status);
  status = run_child(exit_in_worker_close, stream, abort_stream, 0, 10);
  CHECK(status == 66, "exit() on a section's own thread, atexit closes the "
        "session (status %d)", status);
  status = run_child(exit_in_pipeline, stream, abort_stream, 0, 10);
  CHECK(status == 66, "exit() on a section's own thread in the pipeline "
        "(status %d)", status);
  unsetenv("EDG_CIR_TEST_NO_WORKER");
#endif
  CHECK(run_child(wait_for_signal, stream, abort_stream, SIGTERM, 10) ==
            128 + SIGTERM,
        "SIGTERM stops a process whose session is open");
}  /* test_exit_paths */

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
  {
    std::string want = "ECIL " + std::to_string(ECIL_VERSION_MAJOR) + "." +
                       std::to_string(ECIL_VERSION_MINOR);
    CHECK(std::strstr(edg_cir_version_string(), want.c_str()) != nullptr,
          "version string: %s", edg_cir_version_string());
  }
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

    /* First, before any session ran in this process: a hang here would
       otherwise stall every later test. */
    test_exit_paths(stream, argv[2]);
    if (failures != 0) {
      std::printf("capi_tests: %d failures (exit paths)\n", failures);
      return 1;
    }
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
    test_api_versions(stream);
#if EDG_CIR_TEST_HOOKS
    test_crash_recovery(stream);
#endif
    test_misc(stream, argv[3]);
    std::printf("capi_tests: %d failures\n", failures);
    status = failures == 0 ? 0 : 1;
  }
  return status;
}  /* main */
