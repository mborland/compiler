/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
session.cpp -- the session: chunked consume, F_HEADER to MLIR module, sticky
errors, diagnostics delivered on the calling thread, finish.

M1 scope: the empty-module path (F_HEADER, empty F_GLOBALS, F_END).  Any
other ECIL record is accepted by the format layer and answered here with
EDG_CIR_ERROR_UNSUPPORTED until the M2 emitter exists.
*/

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "cir_module.h"
#include "emitter.h"
#include "llvm/Support/ErrorHandling.h"

#include "session.h"

using namespace edg_cir;

namespace {

bool operand_uint(const ecil::an_operand &op, uint64_t &value)
/*
Fetch an unsigned operand (nil gives false).
*/
{
  return ecil::operand_u64(op, value);
}  /* operand_uint */

}  /* anonymous namespace */

/* The record handler: collects the header facts, ignores meta records and
   rejects what M1 cannot emit. */
struct an_edg_cir_session::a_handler : public ecil::a_stream_handler {
  explicit a_handler(an_edg_cir_session &owner) : s(owner) {}

  bool on_preamble(const ecil::a_preamble &) override
    { return true; }
  bool on_frame_begin(const ecil::a_frame_info &frame) override;
  bool on_record(const ecil::a_record &record,
                 const ecil::a_record_context &context) override;
  bool on_frame_end(const ecil::a_frame_info &frame) override;
  bool on_extension_frame(const ecil::a_frame_info &) override
    { return true; }

  bool string_operand(const ecil::an_operand &op, std::string &out,
                      bool allow_nil);

  an_edg_cir_session &s;
  uint32_t            end_status = 0;
};  /* an_edg_cir_session::a_handler */

bool an_edg_cir_session::a_handler::string_operand(
                           const ecil::an_operand &op, std::string &out,
                           bool allow_nil)
/*
Resolve a sid operand to its string.  A nil gives an empty string when
allowed.
*/
{
  bool result = true;
  uint64_t sid = 0;

  out.clear();
  if (op.kind == ecil::an_operand_kind::nil) {
    result = allow_nil;
  } else if (operand_uint(op, sid)) {
    auto found = s.strings.find(sid);
    if (found == s.strings.end())
      result = false;
    else
      out = found->second;
  } else {
    result = false;
  }
  if (!result)
    s.fail(EDG_CIR_ERROR_FORMAT, edc_format,
           "a string id was used before its STR record");
  return result;
}  /* an_edg_cir_session::a_handler::string_operand */

bool an_edg_cir_session::a_handler::on_frame_begin(
                                            const ecil::a_frame_info &frame)
/*
An abort frame ends the session with EDG_CIR_ERROR_ABORTED.
*/
{
  bool result = true;

  if (frame.kind == efk_abort) {
    s.abort_stream("the front end aborted the ECIL stream (F_ABORT)");
    result = false;
  }
  return result;
}  /* an_edg_cir_session::a_handler::on_frame_begin */

bool an_edg_cir_session::a_handler::on_frame_end(
                                            const ecil::a_frame_info &frame)
/*
At the end of F_HEADER create the module; at the end of F_END check the
status the front end reported.
*/
{
  bool result = true;

  if (frame.kind == efk_header) {
    if (!s.header.have_target || !s.header.have_lang) {
      s.fail(EDG_CIR_ERROR_FORMAT, edc_format,
             "F_HEADER lacks HDR_TARGET or HDR_LANG");
      result = false;
    } else {
      an_edg_cir_status status;
      s.module = edg_cir::create_cir_module(s, status);
      result = status == EDG_CIR_OK && s.module != nullptr;
      if (result)
        edg_cir::set_module_type_widths(s);
      result = result && s.status == EDG_CIR_OK;
      if (result) {
        /* Replay the type introductions of the header into the emitter. */
        ecil::a_cursor cursor(frame.payload, frame.payload + frame.length);
        ecil::a_decode_context decode;
        ecil::an_error error;
        ecil::a_record record;
        ecil::a_record_context context;

        decode.stream_minor = s.reader->preamble().minor;
        decode.allow_supp = (frame.flags & ECIL_FRAME_FLAG_HAS_SUPP) != 0;
        context.frame = &frame;
        context.in_body = false;
        context.function_depth = 0;
        context.offset = frame.offset;
        s.emitter.reset(new edg_cir::a_emitter(s));
        while (result && cursor.remaining() > 0 &&
               ecil::decode_record(cursor, decode, record, error)) {
          if ((record.opcode >= eop_ty_void && record.opcode <= eop_ty_opaque)
              || record.opcode == eop_loc)
            result = s.emitter->on_record(record, context);
        }
      }
    }
  } else if (s.emitter && frame.kind != efk_end && frame.kind != efk_abort) {
    result = s.emitter->on_frame_end(frame.kind);
  } else if (frame.kind == efk_end && end_status != 0) {
    char buffer[96];
    std::snprintf(buffer, sizeof buffer,
                  "the ECIL stream ends with status %u (front-end errors)",
                  end_status);
    s.abort_stream(buffer);
    result = false;
  }
  return result;
}  /* an_edg_cir_session::a_handler::on_frame_end */

bool an_edg_cir_session::a_handler::on_record(
                              const ecil::a_record &record,
                              const ecil::a_record_context &context)
/*
Process one record.
*/
{
  bool result = true;
  uint64_t v = 0;

  if (s.flags & EDG_CIR_FLAG_TRACE) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "trace @%llu %s",
                  static_cast<unsigned long long>(context.offset),
                  record.info->name);
    s.report(EDG_CIR_SEVERITY_NOTE, edc_trace, buffer);
  }
  switch (record.opcode) {
  case eop_str: {
    std::string text(reinterpret_cast<const char *>(record.ops[1].data),
                     record.ops[1].length);
    if (operand_uint(record.ops[0], v))
      s.strings[v] = text;
    break;
  }
  case eop_hdr_target: {
    a_header_facts &h = s.header;
    uint64_t n = 0;
    result = string_operand(record.ops[0], h.triple, false) &&
             string_operand(record.ops[1], h.target_abi, true);
    if (result) {
      h.have_datalayout =
                  record.ops[2].kind != ecil::an_operand_kind::nil;
      result = string_operand(record.ops[2], h.datalayout, true);
    }
    if (result) {
      operand_uint(record.ops[3], n);
      h.ptr_bits = static_cast<uint32_t>(n);
      operand_uint(record.ops[4], n);
      h.reg_bits = static_cast<uint32_t>(n);
      operand_uint(record.ops[5], n);
      h.target_flags = static_cast<uint32_t>(n);
      h.have_target = true;
    }
    break;
  }
  case eop_hdr_lang: {
    a_header_facts &h = s.header;
    uint64_t n = 0;
    operand_uint(record.ops[0], n);
    h.language = static_cast<uint32_t>(n);
    operand_uint(record.ops[1], n);
    h.standard = static_cast<uint32_t>(n);
    operand_uint(record.ops[2], n);
    h.lang_flags = static_cast<uint32_t>(n);
    h.have_lang = true;
    break;
  }
  case eop_hdr_producer:
    result = string_operand(record.ops[0], s.header.producer, false) &&
             string_operand(record.ops[1], s.header.source_file, true);
    break;
  case eop_hdr_type_role: {
    uint64_t role = 0;
    uint64_t gid = 0;
    operand_uint(record.ops[0], role);
    operand_uint(record.ops[1], gid);
    s.type_roles[static_cast<uint32_t>(role)] = gid;
    if (s.module)
      edg_cir::set_module_type_widths(s);
    result = s.status == EDG_CIR_OK;
    break;
  }
  case eop_ty_int: {
    uint64_t gid = 0;
    uint64_t bits = 0;
    operand_uint(record.ops[0], gid);
    operand_uint(record.ops[1], bits);
    s.int_widths[gid] = static_cast<uint32_t>(bits);
    if (s.emitter)
      result = s.emitter->on_record(record, context);
    break;
  }
  case eop_end: {
    uint64_t status = 0;
    operand_uint(record.ops[1], status);
    end_status = static_cast<uint32_t>(status);
    break;
  }
  default:
    if (s.emitter) {
      result = s.emitter->on_record(record, context);
    } else if (record.opcode >= eop_ty_void &&
               record.opcode <= eop_ty_opaque) {
      /* Type introductions in F_HEADER are replayed once the module and
         the emitter exist (at the end of the header frame). */
    } else if (record.opcode == eop_loc || record.opcode == eop_ext) {
      /* Ignorable in the header. */
    } else {
      s.fail(EDG_CIR_ERROR_FORMAT, edc_format,
             std::string(record.info->name) + " is not allowed in F_HEADER");
      result = false;
    }
    break;
  }
  return result && s.status == EDG_CIR_OK;
}  /* an_edg_cir_session::a_handler::on_record */

an_edg_cir_session::an_edg_cir_session(const an_edg_cir_options &options)
/*
Copy the options (strings included) and set up the stream reader.
*/
  : output_kind(options.output_kind),
    have_output_path(options.output_path != nullptr),
    write_callback(options.write_callback),
    write_user_data(options.write_user_data),
    diagnostic_callback(options.diagnostic_callback),
    diagnostic_user_data(options.diagnostic_user_data),
    have_triple_override(options.target_triple_override != nullptr),
    opt_level(options.opt_level), flags(options.flags),
    status(EDG_CIR_OK), finished(false), in_worker(false), tee(nullptr),
    poisoned(false), last_line(0), last_column(0)
{
  if (options.output_path != nullptr)
    output_path = options.output_path;
  if (options.target_triple_override != nullptr)
    triple_override = options.target_triple_override;
  /* Debug aid: EDG_CIR_TEE=<path> copies the incoming stream to a file. */
  if (const char *tee_path = std::getenv("EDG_CIR_TEE"))
    tee = std::fopen(tee_path, "wb");
  handler.reset(new a_handler(*this));
  reader.reset(new ecil::a_stream_reader(*handler));
}  /* an_edg_cir_session::an_edg_cir_session */

an_edg_cir_session::~an_edg_cir_session()
/*
Destroy the module (and its context) before the rest.  After a recovered
crash the MLIR state may be corrupt: it is leaked on purpose instead of being
destroyed.
*/
{
  if (tee != nullptr)
    std::fclose(tee);
  if (poisoned) {
    (void)emitter.release();
    (void)module.release();
  }
  emitter.reset();
  module.reset();
}  /* an_edg_cir_session::~an_edg_cir_session */

void an_edg_cir_session::report(an_edg_cir_severity severity, uint32_t code,
                                const std::string &message)
/*
Deliver a diagnostic now (calling thread) or queue it (worker running).
*/
{
  edg_cir::a_diagnostic d;

  d.severity = severity;
  d.code = code;
  d.message = message;
  d.file = last_file;
  d.line = last_line;
  d.column = last_column;
  if (in_worker) {
    queue.push_back(d);
  } else if (diagnostic_callback != nullptr) {
    diagnostic_callback(diagnostic_user_data, d.severity, d.code,
                        d.message.c_str(),
                        d.file.empty() ? nullptr : d.file.c_str(), d.line,
                        d.column);
  } else {
    std::fprintf(stderr, "edg_cir: %s%s\n",
                 severity == EDG_CIR_SEVERITY_NOTE ? "note: "
                 : severity == EDG_CIR_SEVERITY_WARNING ? "warning: "
                                                        : "error: ",
                 d.message.c_str());
  }
}  /* an_edg_cir_session::report */

void an_edg_cir_session::deliver_queue()
/*
Deliver the diagnostics queued by the worker, on the calling thread.
*/
{
  std::vector<edg_cir::a_diagnostic> pending;

  pending.swap(queue);
  for (const edg_cir::a_diagnostic &d : pending) {
    if (diagnostic_callback != nullptr)
      diagnostic_callback(diagnostic_user_data, d.severity, d.code,
                          d.message.c_str(),
                          d.file.empty() ? nullptr : d.file.c_str(), d.line,
                          d.column);
    else
      std::fprintf(stderr, "edg_cir: %s\n", d.message.c_str());
  }
}  /* an_edg_cir_session::deliver_queue */

an_edg_cir_status an_edg_cir_session::fail(an_edg_cir_status new_status,
                                           uint32_t code,
                                           const std::string &message)
/*
Set the sticky status (the first error wins) and report it.
*/
{
  if (status == EDG_CIR_OK) {
    status = new_status;
    status_message = message;
    report(EDG_CIR_SEVERITY_ERROR, code, message);
  }
  return status;
}  /* an_edg_cir_session::fail */

bool an_edg_cir_session::consume_guarded(const void *bytes, size_t length)
/*
Feed the reader under crash recovery: op construction happens here, on the
caller's thread, and a fatal LLVM/MLIR error or a crash must not exit() or
kill the front end.
*/
{
  bool accepted = false;
  std::string reason;
  bool completed = edg_cir::run_guarded(
      [this, bytes, length, &accepted] {
#if EDG_CIR_TEST_HOOKS
        /* Test hook: a crash or fatal error while consuming (op
           construction runs on the front end's thread). */
        if (const char *hook = std::getenv("EDG_CIR_TEST_CONSUME_CRASH")) {
          if (std::strcmp(hook, "fatal") == 0)
            llvm::report_fatal_error("EDG_CIR_TEST_CONSUME_CRASH=fatal");
          else if (std::strcmp(hook, "segv") == 0)
            std::raise(SIGSEGV);
          else if (std::strcmp(hook, "abort") == 0)
            std::abort();
        }
#endif
        accepted = reader->consume(bytes, length);
      }, 0, reason);

  if (!completed) {
    poisoned = true;
    fail(EDG_CIR_ERROR_INTERNAL, edc_internal,
         reason.empty() ? "internal crash while consuming the ECIL stream"
                        : "internal error while consuming the ECIL stream: " +
                          reason);
    accepted = false;
  }
  return accepted;
}  /* an_edg_cir_session::consume_guarded */

an_edg_cir_status an_edg_cir_session::abort_stream(const std::string &why)
/*
F_ABORT or a failing END status: the front end has already reported its
error, so the sticky status is set and only a note is emitted.
*/
{
  if (status == EDG_CIR_OK) {
    status = EDG_CIR_ERROR_ABORTED;
    status_message = why;
    report(EDG_CIR_SEVERITY_NOTE, edc_aborted, status_message);
  }
  return status;
}  /* an_edg_cir_session::abort_stream */

an_edg_cir_status an_edg_cir_session::consume(const void *bytes,
                                              size_t length)
/*
Feed the next bytes of the stream.
*/
{
  if (tee != nullptr && length != 0) {
    std::fwrite(bytes, 1, length, tee);
    std::fflush(tee);
  }
  if (status == EDG_CIR_OK) {
    if (finished) {
      fail(EDG_CIR_ERROR_USAGE, edc_usage, "consume after finish");
    } else if (!consume_guarded(bytes, length)) {
      if (status == EDG_CIR_OK && !reader->error().ok()) {
        const ecil::an_error &e = reader->error();
        char where[48];
        std::snprintf(where, sizeof where, " (at stream byte %llu)",
                      static_cast<unsigned long long>(e.stream_offset));
        if (e.kind == ecil::an_error_kind::version)
          fail(EDG_CIR_ERROR_VERSION, edc_version, e.message + where);
        else
          fail(EDG_CIR_ERROR_FORMAT, edc_format, e.message + where);
      } else if (status == EDG_CIR_OK) {
        fail(EDG_CIR_ERROR_INTERNAL, edc_internal,
             "the record handler stopped without an error");
      }
    }
  }
  return status;
}  /* an_edg_cir_session::consume */

an_edg_cir_status an_edg_cir_session::finish()
/*
Finish the stream and run the pipeline.
*/
{
  if (status == EDG_CIR_OK && finished) {
    fail(EDG_CIR_ERROR_USAGE, edc_usage, "finish called twice");
  } else if (status == EDG_CIR_OK) {
    finished = true;
    if (!reader->finish()) {
      const ecil::an_error &e = reader->error();
      if (e.kind == ecil::an_error_kind::version)
        fail(EDG_CIR_ERROR_VERSION, edc_version, e.message);
      else
        fail(EDG_CIR_ERROR_FORMAT, edc_format, e.message);
    } else if (reader->aborted()) {
      abort_stream("the front end aborted the ECIL stream (F_ABORT)");
    } else if (!module) {
      fail(EDG_CIR_ERROR_INTERNAL, edc_internal, "no module was created");
    } else {
      edg_cir::run_pipeline(*this);
    }
  }
  return status;
}  /* an_edg_cir_session::finish */
