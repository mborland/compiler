/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
capi.cpp -- the C API of edg_cir.h (the only exported symbols).  No C++
exception, longjmp or exit crosses this boundary.
*/

#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>
#include <mutex>

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "mlir/IR/AsmState.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Pass/PassManager.h"

#include "session.h"

using namespace edg_cir;

namespace {

bool apply_llvm_args(const char *const *args, std::string &error)
/*
Hand the NULL-terminated llvm_args to LLVM's command-line parser, once per
process (later sessions' arguments are ignored).  Return false on a bad
option.
*/
{
  static std::once_flag once;
  static bool ok = true;
  static std::string message;

  std::call_once(once, [args] {
    std::vector<const char *> argv;
    llvm::raw_string_ostream errors(message);

    /* MLIR's pass manager, printer and context command-line options
       (print-ir, timing, ...) exist only when registered. */
    mlir::registerPassManagerCLOptions();
    mlir::registerAsmPrinterCLOptions();
    mlir::registerMLIRContextCLOptions();
    argv.push_back("edg_cir");
    for (const char *const *p = args; p != nullptr && *p != nullptr; ++p)
      argv.push_back(*p);
    if (argv.size() > 1)
      ok = llvm::cl::ParseCommandLineOptions(static_cast<int>(argv.size()),
                                             argv.data(), "", &errors);
    errors.flush();
  });
  error = message;
  return ok;
}  /* apply_llvm_args */

}  /* anonymous namespace */

extern "C" {

EDG_CIR_API void edg_cir_options_init(an_edg_cir_options *options)
/*
Zero the options and set struct_size, api_version and the defaults.
*/
{
  if (options != nullptr) {
    std::memset(options, 0, sizeof *options);
    options->struct_size = sizeof *options;
    options->api_version = EDG_CIR_API_VERSION;
    options->output_kind = EDG_CIR_OUTPUT_OBJECT;
    options->opt_level = 0;
  }
}  /* edg_cir_options_init */

EDG_CIR_API an_edg_cir_status edg_cir_session_open(
                                     const an_edg_cir_options  *options,
                                     an_edg_cir_session       **session)
/*
Create a session.  The options struct may be older (smaller) than ours.
*/
{
  an_edg_cir_status result = EDG_CIR_OK;
  an_edg_cir_options local;

  if (session != nullptr)
    *session = nullptr;
  if (options == nullptr || session == nullptr) {
    result = EDG_CIR_ERROR_USAGE;
  } else {
    edg_cir_options_init(&local);
    /* struct_size covers at least struct_size, api_version, output_kind.  A
       caller of an older API (smaller struct) is accepted: the fields it
       does not have keep their defaults.  A newer one is not. */
    if (options->struct_size < 3 * sizeof(uint32_t) ||
        options->api_version < 1 ||
        options->api_version > EDG_CIR_API_VERSION) {
      result = EDG_CIR_ERROR_USAGE;
    } else {
      size_t n = options->struct_size < sizeof local ? options->struct_size
                                                     : sizeof local;
      std::memcpy(&local, options, n);
      local.struct_size = sizeof local;
      if (local.output_kind < EDG_CIR_OUTPUT_NONE ||
          local.output_kind > EDG_CIR_OUTPUT_OBJECT || local.opt_level < 0 ||
          local.opt_level > 3)
        result = EDG_CIR_ERROR_USAGE;
    }
  }
  if (result == EDG_CIR_OK) {
    std::string error;
    if (local.llvm_args != nullptr && !apply_llvm_args(local.llvm_args,
                                                       error)) {
      std::string text = "bad llvm_args: " + error;
      if (local.diagnostic_callback != nullptr)
        local.diagnostic_callback(local.diagnostic_user_data,
                                  EDG_CIR_SEVERITY_ERROR, edc_usage,
                                  text.c_str(), nullptr, 0, 0);
      else
        std::fprintf(stderr, "edg_cir: %s\n", text.c_str());
      result = EDG_CIR_ERROR_USAGE;
    } else {
      an_edg_cir_session *created = new (std::nothrow)
                                              an_edg_cir_session(local);
      if (created == nullptr)
        result = EDG_CIR_ERROR_INTERNAL;
      else
        *session = created;
    }
  }
  return result;
}  /* edg_cir_session_open */

EDG_CIR_API an_edg_cir_status edg_cir_session_consume(
                                     an_edg_cir_session *session,
                                     const void         *bytes,
                                     size_t              length)
/*
Feed the next bytes of the ECIL stream.
*/
{
  an_edg_cir_status result = EDG_CIR_ERROR_USAGE;

  if (session != nullptr && (bytes != nullptr || length == 0))
    result = session->consume(bytes, length);
  return result;
}  /* edg_cir_session_consume */

EDG_CIR_API an_edg_cir_status edg_cir_session_finish(
                                     an_edg_cir_session *session)
/*
Finish the stream, run the pipeline and write the output.
*/
{
  an_edg_cir_status result = EDG_CIR_ERROR_USAGE;

  if (session != nullptr) {
    result = session->finish();
    if (result == EDG_CIR_OK && !session->output.empty()) {
      if (session->write_callback != nullptr) {
        const char *p = session->output.data();
        size_t left = session->output.size();
        while (left > 0 && result == EDG_CIR_OK) {
          size_t n = left < 65536 ? left : 65536;
          if (session->write_callback(session->write_user_data, p, n) != 0)
            result = session->fail(EDG_CIR_ERROR_IO, edc_io,
                                   "the write callback reported a failure");
          p += n;
          left -= n;
        }
      } else if (session->have_output_path) {
        bool to_stdout = session->output_path == "-";
        std::FILE *file = to_stdout ? stdout
                                    : std::fopen(session->output_path.c_str(),
                                                 "wb");
        if (file == nullptr ||
            std::fwrite(session->output.data(), 1, session->output.size(),
                        file) != session->output.size())
          result = session->fail(EDG_CIR_ERROR_IO, edc_io,
                                 "cannot write " + session->output_path);
        if (file != nullptr && !to_stdout && std::fclose(file) != 0 &&
            result == EDG_CIR_OK)
          result = session->fail(EDG_CIR_ERROR_IO, edc_io,
                                 "error closing " + session->output_path);
      }
    }
  }
  return result;
}  /* edg_cir_session_finish */

EDG_CIR_API void edg_cir_session_close(an_edg_cir_session *session)
/*
Destroy a session (NULL is accepted).
*/
{
  delete session;
}  /* edg_cir_session_close */

EDG_CIR_API an_edg_cir_status edg_cir_process_file(
                                     const an_edg_cir_options *options,
                                     const char               *path)
/*
Open a session, consume the ECIL file ("-" is stdin), finish and close.
*/
{
  an_edg_cir_status result = EDG_CIR_ERROR_USAGE;
  an_edg_cir_session *session = nullptr;

  if (path != nullptr)
    result = edg_cir_session_open(options, &session);
  if (result == EDG_CIR_OK) {
    std::FILE *file = std::strcmp(path, "-") == 0 ? stdin
                                                  : std::fopen(path, "rb");
    if (file == nullptr) {
      result = session->fail(EDG_CIR_ERROR_IO, edc_io,
                             std::string("cannot read ") + path);
    } else {
      /* Read whole units: the preamble, then each frame by its header, so
         the reader decodes every frame in place (no 64 KiB chunking and no
         re-copy of a frame split across chunks). */
      std::vector<unsigned char> buffer(ECIL_PREAMBLE_SIZE);
      size_t n = std::fread(buffer.data(), 1, ECIL_PREAMBLE_SIZE, file);

      if (n > 0)
        result = edg_cir_session_consume(session, buffer.data(), n);
      while (result == EDG_CIR_OK && n == ECIL_PREAMBLE_SIZE) {
        unsigned char header[ECIL_FRAME_HEADER_SIZE];
        size_t got = std::fread(header, 1, sizeof header, file);

        if (got == 0)
          break;
        if (got < sizeof header) {
          /* Truncated header: the reader reports it at finish. */
          result = edg_cir_session_consume(session, header, got);
          break;
        }
        {
          uint32_t length = static_cast<uint32_t>(header[0]) |
                            static_cast<uint32_t>(header[1]) << 8 |
                            static_cast<uint32_t>(header[2]) << 16 |
                            static_cast<uint32_t>(header[3]) << 24;
          size_t have = sizeof header;

          /* An absurd length is left for the reader to reject. */
          if (length <= (1u << 30)) {
            /* Read the payload in bounded steps: the buffer never grows
               beyond the bytes the file really holds, whatever the header
               claims. */
            static const size_t step = size_t(1) << 20;
            size_t payload = 0;

            buffer.assign(header, header + sizeof header);
            while (payload < length) {
              size_t want = length - payload < step ? length - payload : step;
              size_t n_read;

              buffer.resize(sizeof header + payload + want);
              n_read = std::fread(buffer.data() + sizeof header + payload,
                                  1, want, file);
              payload += n_read;
              if (n_read < want)
                break;
            }
            have = sizeof header + payload;
            result = edg_cir_session_consume(session, buffer.data(), have);
            if (payload < length)
              break;
          } else {
            result = edg_cir_session_consume(session, header, sizeof header);
            break;
          }
        }
      }
      if (file != stdin)
        std::fclose(file);
      if (result == EDG_CIR_OK)
        result = edg_cir_session_finish(session);
    }
  }
  /* A run that failed before the finish still reports its statistics (the
     finish of a failed session only prints the chunked-lowering note and
     releases the crash recovery hold). */
  if (session != nullptr && result != EDG_CIR_OK)
    (void)session->finish();
  edg_cir_session_close(session);
  return result;
}  /* edg_cir_process_file */

EDG_CIR_API const char *edg_cir_version_string(void)
/*
A static string naming the library, ECIL and LLVM versions.
*/
{
  static const std::string text = std::string("edg_cir ") +
      std::to_string(EDG_CIR_LIBRARY_VERSION_MAJOR) + "." +
      std::to_string(EDG_CIR_LIBRARY_VERSION_MINOR) + " (API " +
      std::to_string(EDG_CIR_API_VERSION) + "), ECIL " +
      std::to_string(ECIL_VERSION_MAJOR) + "." +
      std::to_string(ECIL_VERSION_MINOR) + ", LLVM " LLVM_VERSION_STRING " " +
      EDG_CIR_LLVM_REVISION;

  return text.c_str();
}  /* edg_cir_version_string */

EDG_CIR_API const char *edg_cir_status_string(an_edg_cir_status status)
/*
A static description of a status code.
*/
{
  const char *result = "unknown status";

  switch (status) {
  case EDG_CIR_OK: result = "ok"; break;
  case EDG_CIR_ERROR_USAGE: result = "bad arguments or call order"; break;
  case EDG_CIR_ERROR_FORMAT: result = "malformed ECIL stream"; break;
  case EDG_CIR_ERROR_VERSION:
    result = "unsupported ECIL version or feature";
    break;
  case EDG_CIR_ERROR_UNSUPPORTED:
    result = "unsupported construct or target";
    break;
  case EDG_CIR_ERROR_ABORTED: result = "the stream was aborted"; break;
  case EDG_CIR_ERROR_VERIFY: result = "the IR verifier rejected the module";
    break;
  case EDG_CIR_ERROR_PIPELINE: result = "a CIR or LLVM pass failed"; break;
  case EDG_CIR_ERROR_IO: result = "output could not be written"; break;
  case EDG_CIR_ERROR_INTERNAL: result = "internal error"; break;
  }
  return result;
}  /* edg_cir_status_string */

}  /* extern "C" */
