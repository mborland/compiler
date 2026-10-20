/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
session.cpp -- the session: frame-wise consume under crash recovery, F_HEADER
to MLIR module (the header's type records are replayed into the emitter once
the module exists), records handed to the emitter, sticky errors, diagnostics
delivered on the calling thread, statistics, finish.
*/

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/resource.h>

#include "chunk.h"
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
  bool emit_record(const ecil::a_record &record,
                   const ecil::a_record_context &context);

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
  } else if (operand_uint(op, sid) && s.reader->ids().has_string(sid)) {
    std::string_view text = s.reader->ids().string(sid);
    out.assign(text.data(), text.size());
  } else {
    result = false;
  }
  if (!result)
    s.fail(EDG_CIR_ERROR_FORMAT, edc_format,
           "a string id was used before its STR record");
  return result;
}  /* an_edg_cir_session::a_handler::string_operand */

bool an_edg_cir_session::a_handler::emit_record(
                              const ecil::a_record &record,
                              const ecil::a_record_context &context)
/*
Pass a record to the emitter, timing it when statistics are requested.
*/
{
  bool result;

  if (s.stats_on) {
    uint64_t start = edg_cir::stats_now_ns();
    result = s.emitter->on_record(record, context);
    s.stats.emit_ns += edg_cir::stats_now_ns() - start;
  } else {
    result = s.emitter->on_record(record, context);
  }
  return result;
}  /* an_edg_cir_session::a_handler::emit_record */

bool an_edg_cir_session::a_handler::on_frame_begin(
                                            const ecil::a_frame_info &frame)
/*
An abort frame ends the session with EDG_CIR_ERROR_ABORTED.
*/
{
  bool result = true;

  ++s.stats.frames;
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
          if ((record.info->section & ecil::eos_type) ||
              record.opcode == eop_loc)
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

  ++s.stats.records;
  if (s.flags & EDG_CIR_FLAG_TRACE) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "trace @%llu %s",
                  static_cast<unsigned long long>(context.offset),
                  record.info->name);
    s.report(EDG_CIR_SEVERITY_NOTE, edc_trace, buffer);
  }
  switch (record.opcode) {
  case eop_str:
    /* The reader already copied the string into the id tables. */
    break;
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
  case eop_end: {
    uint64_t status = 0;
    operand_uint(record.ops[1], status);
    end_status = static_cast<uint32_t>(status);
    break;
  }
  default:
    if (s.emitter) {
      result = emit_record(record, context);
    } else if (record.info->section & ecil::eos_type) {
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
    chunk_threshold(options.chunk_threshold),
    status(EDG_CIR_OK), finished(false), in_worker(false), tee(nullptr),
    crash_held(false), poisoned(false),
    stats_on((options.flags & EDG_CIR_FLAG_PRINT_STATS) != 0),
    last_line(0), last_column(0)
{
  stats.chunk_size = options.chunk_functions != 0
                         ? options.chunk_functions
                         : edg_cir::k_default_chunk_functions;
  if (options.output_path != nullptr)
    output_path = options.output_path;
  if (options.target_triple_override != nullptr)
    triple_override = options.target_triple_override;
  /* Debug aid: EDG_CIR_TEE=<path> copies the incoming stream to a file. */
  if (const char *tee_path = std::getenv("EDG_CIR_TEE"))
    tee = std::fopen(tee_path, "wb");
  handler.reset(new a_handler(*this));
  reader.reset(new ecil::a_stream_reader(*handler));
  if (stats_on)
    stats.rss_start_kib = edg_cir::stats_peak_kib();
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
  if (emitter || module) {
    if (edg_cir::on_guard_worker_thread()) {
      /* Closed from the guard worker (an exit() inside a guarded section
         runs the atexit handlers and static destructors there): this is
         already a big-stack thread under crash recovery, and a guarded
         section would only wait for itself. */
      emitter.reset();
      module.reset();
    } else {
      /* Destroying nested regions recurses once per nesting level, so it
         runs on the big-stack worker like everything else that walks the
         IR.  If even that fails the state is leaked. */
      std::string reason;
      bool completed = edg_cir::run_guarded(
          [this] {
            emitter.reset();
            module.reset();
          },
          static_cast<unsigned>(EDG_CIR_WORKER_STACK_MB), reason);

      if (!completed) {
        (void)emitter.release();
        (void)module.release();
      }
    }
  }
  if (crash_held)
    edg_cir::release_crash_recovery();
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
Feed the reader under crash recovery: op construction happens here, on a
big-stack worker thread (joined before returning), and a fatal LLVM/MLIR error
or a crash must not exit() or kill the front end.
*/
{
  bool accepted = false;
  std::string reason;
#if EDG_CIR_TEST_HOOKS
  /* Benchmark hook: EDG_CIR_NO_GUARD=1 skips crash recovery (cost
     measurement only). */
  if (std::getenv("EDG_CIR_NO_GUARD") != nullptr)
    return reader->consume(bytes, length);
#endif
  if (!crash_held) {
    edg_cir::hold_crash_recovery();
    crash_held = true;
  }
  /* The MLIR type verifiers and walkers recurse once per level of nesting of
     a record in a record (a base class chain), so the work runs on the
     big-stack worker; diagnostics are queued and delivered after it. */
  in_worker = true;
  bool completed = edg_cir::run_guarded(
      [this, bytes, length, &accepted] {
#if EDG_CIR_TEST_HOOKS
        /* Test hook: a crash, exit or stack overflow while consuming: this
           runs on the guard worker, like the pipeline. */
        if (const char *hook = std::getenv("EDG_CIR_TEST_CONSUME_CRASH"))
          edg_cir::run_test_crash_hook(hook);
#endif
        accepted = reader->consume(bytes, length);
      }, static_cast<unsigned>(EDG_CIR_WORKER_STACK_MB), reason);
  in_worker = false;
  deliver_queue();

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

bool an_edg_cir_session::consume_timed(const void *bytes, size_t length)
/*
consume_guarded, with the byte count and time recorded when statistics are
requested.
*/
{
  bool accepted;

  if (stats_on) {
    uint64_t start = edg_cir::stats_now_ns();
    accepted = consume_guarded(bytes, length);
    stats.consume_ns += edg_cir::stats_now_ns() - start;
    stats.bytes += length;
  } else {
    accepted = consume_guarded(bytes, length);
  }
  return accepted;
}  /* an_edg_cir_session::consume_timed */

void an_edg_cir_session::print_chunk_line()
/*
The chunked-lowering note: how many chunks the module was lowered in (the
globals chunk included, 0 when it was lowered as one module) and the chunk
size.  It is part of every --stats run, also of one that fails early.
*/
{
  char line[128];

  std::snprintf(line, sizeof line,
                "stats: chunked lowering: %llu chunks of at most %llu "
                "functions",
                static_cast<unsigned long long>(stats.chunks),
                static_cast<unsigned long long>(stats.chunk_size));
  report(EDG_CIR_SEVERITY_NOTE, edc_stats, line);
}  /* an_edg_cir_session::print_chunk_line */

void an_edg_cir_session::print_stats()
/*
EDG_CIR_FLAG_PRINT_STATS: report the counters, stage times and peak memory as
notes.
*/
{
  char line[256];
  unsigned long long peak_kib = edg_cir::stats_peak_kib();

  stats_printed = true;
  const a_session_stats &t = stats;
  double decode_ns = t.consume_ns > t.emit_ns
                         ? static_cast<double>(t.consume_ns - t.emit_ns) : 0;

  std::snprintf(line, sizeof line,
                "stats: %llu frames, %llu records, %llu bytes, peak RSS "
                "%llu KiB",
                static_cast<unsigned long long>(t.frames),
                static_cast<unsigned long long>(t.records),
                static_cast<unsigned long long>(t.bytes), peak_kib);
  report(EDG_CIR_SEVERITY_NOTE, edc_stats, line);
  {
    const ecil::a_id_tables &ids = reader->ids();
    std::snprintf(line, sizeof line,
                  "stats: %llu G-ids (%llu holes), %llu string ids (%llu "
                  "holes), %llu symbol renames, %llu substitution walks",
                  static_cast<unsigned long long>(ids.gids_introduced()),
                  static_cast<unsigned long long>(ids.gid_holes()),
                  static_cast<unsigned long long>(ids.sids_introduced()),
                  static_cast<unsigned long long>(ids.sid_holes()),
                  static_cast<unsigned long long>(
                      emitter ? emitter->rename_count() : 0),
                  static_cast<unsigned long long>(
                      emitter ? emitter->walk_count() : 0));
    report(EDG_CIR_SEVERITY_NOTE, edc_stats, line);
  }
  std::snprintf(line, sizeof line,
                "stats: decode+validate %.2f ms, emit %.2f ms, finish %.2f "
                "ms, verify %.2f ms, cir passes %.2f ms, lower %.2f ms, "
                "codegen %.2f ms",
                decode_ns / 1e6, static_cast<double>(t.emit_ns) / 1e6,
                static_cast<double>(t.finish_ns) / 1e6,
                static_cast<double>(t.verify_ns) / 1e6,
                static_cast<double>(t.passes_ns) / 1e6,
                static_cast<double>(t.lower_ns) / 1e6,
                static_cast<double>(t.codegen_ns) / 1e6);
  report(EDG_CIR_SEVERITY_NOTE, edc_stats, line);
  print_chunk_line();
  std::snprintf(line, sizeof line,
                "stats: lazy bodies: %llu deferred, %llu replayed, %llu "
                "dropped",
                static_cast<unsigned long long>(t.lazy_deferred),
                static_cast<unsigned long long>(t.lazy_replayed),
                static_cast<unsigned long long>(t.lazy_dropped));
  report(EDG_CIR_SEVERITY_NOTE, edc_stats, line);
  if (t.lazy_deferred != 0) {
    std::snprintf(line, sizeof line,
                  "stats: lazy detail: %llu bytes held, %llu variables erased "
                  "with them, %llu dropped bodies named by %llu dead "
                  "entities (erased with them); reachability %.2f ms, "
                  "replay %.2f ms",
                  static_cast<unsigned long long>(t.lazy_bytes),
                  static_cast<unsigned long long>(t.lazy_vars),
                  static_cast<unsigned long long>(t.lazy_kept),
                  static_cast<unsigned long long>(t.lazy_namers),
                  static_cast<double>(t.lazy_resolve_ns) / 1e6,
                  static_cast<double>(t.lazy_replay_ns) / 1e6);
    report(EDG_CIR_SEVERITY_NOTE, edc_stats, line);
  }
  /* EDG_CIR_LAZY_SYMBOLS (edg-cir --lazy-symbols): one note per deferred
     body with the symbol it has in the output. */
  if (emitter && std::getenv("EDG_CIR_LAZY_SYMBOLS") != nullptr) {
    for (const auto &item : emitter->lazy_symbols())
      report(EDG_CIR_SEVERITY_NOTE, edc_stats,
             std::string("stats: lazy ") +
                 (item.second ? "replayed " : "dropped ") + item.first);
    for (const std::string &name : emitter->lazy_kept())
      report(EDG_CIR_SEVERITY_NOTE, edc_stats,
             "stats: lazy dead-named " + name);
  }
  std::snprintf(line, sizeof line,
                "stats: peak RSS KiB at start %llu, after emit %llu, finish "
                "%llu, verify %llu, cir passes %llu, lower %llu",
                static_cast<unsigned long long>(t.rss_start_kib),
                static_cast<unsigned long long>(t.rss_emitted_kib),
                static_cast<unsigned long long>(t.rss_finished_kib),
                static_cast<unsigned long long>(t.rss_verified_kib),
                static_cast<unsigned long long>(t.rss_passed_kib),
                static_cast<unsigned long long>(t.rss_lowered_kib));
  report(EDG_CIR_SEVERITY_NOTE, edc_stats, line);
}  /* an_edg_cir_session::print_stats */

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
    } else if (!consume_timed(bytes, length)) {
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
      if (stats_on)
        print_stats();
    }
    if (crash_held) {
      edg_cir::release_crash_recovery();
      crash_held = false;
    }
  }
  if (stats_on && !stats_printed) {
    stats_printed = true;
    print_chunk_line();
  }
  return status;
}  /* an_edg_cir_session::finish */
