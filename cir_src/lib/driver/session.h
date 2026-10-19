/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
session.h -- the internal definition of an_edg_cir_session (the opaque type
of the C API in edg_cir.h) and the helpers shared by the driver sources.
*/

#ifndef EDG_CIR_SESSION_H
#define EDG_CIR_SESSION_H 1

#include <cstdint>
#include <chrono>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <sys/resource.h>

#include "cir_config.h"
#include "edg_cir/edg_cir.h"
#include "ecil_reader.h"

namespace edg_cir {

/* Diagnostic codes passed to the callback. */
enum a_diagnostic_code : uint32_t {
  edc_format       = 1001,
  edc_version      = 1002,
  edc_unsupported  = 1003,
  edc_aborted      = 1004,
  edc_usage        = 1005,
  edc_trace        = 1006,
  edc_mlir         = 2001,
  edc_llvm         = 2002,
  edc_pipeline     = 2003,
  edc_internal     = 2004,
  edc_io           = 2005,
  edc_stats        = 3001
};  /* a_diagnostic_code */

struct a_diagnostic {
  an_edg_cir_severity severity;
  uint32_t            code;
  std::string         message;
  std::string         file;
  uint32_t            line;
  uint32_t            column;
};  /* a_diagnostic */

/* Facts from F_HEADER. */
struct a_header_facts {
  bool         have_target;
  bool         have_lang;
  std::string  triple;
  std::string  target_abi;
  bool         have_datalayout;
  std::string  datalayout;
  uint32_t     ptr_bits;
  uint32_t     reg_bits;
  uint32_t     target_flags;
  uint32_t     language;
  uint32_t     standard;
  uint32_t     lang_flags;
  std::string  producer;
  std::string  source_file;

  a_header_facts()
    : have_target(false), have_lang(false), have_datalayout(false),
      ptr_bits(0), reg_bits(0), target_flags(0), language(0), standard(0),
      lang_flags(0) {}
};  /* a_header_facts */

/* EDG_CIR_FLAG_PRINT_STATS: counters and stage times in nanoseconds.  The
   clocks are read only when the flag is set. */
struct a_session_stats {
  uint64_t frames = 0;
  uint64_t records = 0;
  uint64_t bytes = 0;
  uint64_t consume_ns = 0;   /* Every consume call: decode, checks, emit. */
  uint64_t emit_ns = 0;      /* The emitter's share of consume_ns. */
  uint64_t finish_ns = 0;    /* Emitter finish: names, layouts, policy. */
  uint64_t verify_ns = 0;
  uint64_t passes_ns = 0;    /* CIR-to-CIR passes. */
  uint64_t lower_ns = 0;     /* CIR to LLVM IR, GlobalDCE and the optimizer. */
  uint64_t codegen_ns = 0;   /* Target code generation or printing. */
  /* Peak resident memory (KiB, the process's maximum so far) at the end of
     each stage: the growth between two of them is what the stage added.  The
     module is resident from emit to the end. */
  uint64_t rss_start_kib = 0;      /* When the session was created. */
  uint64_t rss_emitted_kib = 0;
  uint64_t rss_finished_kib = 0;
  uint64_t rss_verified_kib = 0;
  uint64_t rss_passed_kib = 0;
  uint64_t rss_lowered_kib = 0;
};  /* a_session_stats */

inline uint64_t stats_now_ns()
/* A monotonic clock in nanoseconds. */
{
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count());
}  /* stats_now_ns */

inline uint64_t stats_peak_kib()
/* The process's peak resident set size so far, in KiB (0 if unknown). */
{
  struct rusage usage;
  uint64_t result = 0;

  if (getrusage(RUSAGE_SELF, &usage) == 0) {
#ifdef __APPLE__
    result = static_cast<uint64_t>(usage.ru_maxrss) / 1024;
#else
    result = static_cast<uint64_t>(usage.ru_maxrss);
#endif
  }
  return result;
}  /* stats_peak_kib */

class a_cir_module;  /* LLVM-dependent; defined in cir_module.h. */
class a_emitter;     /* The ECIL-to-CIR emitter; defined in emitter.h. */

}  /* namespace edg_cir */

struct an_edg_cir_session {
  an_edg_cir_session(const an_edg_cir_options &options);
  ~an_edg_cir_session();

  /* Options (strings copied). */
  an_edg_cir_output_kind                output_kind;
  std::string                           output_path;
  bool                                  have_output_path;
  an_edg_cir_write_callback             write_callback;
  void                                 *write_user_data;
  an_edg_cir_diagnostic_callback        diagnostic_callback;
  void                                 *diagnostic_user_data;
  std::string                           triple_override;
  bool                                  have_triple_override;
  int32_t                               opt_level;
  uint32_t                              flags;

  /* State. */
  an_edg_cir_status                     status;
  std::string                           status_message;
  bool                                  finished;
  bool                                  in_worker;
  std::FILE                            *tee;  /* EDG_CIR_TEE debug copy */
  bool                                  crash_held;  /* hold_crash_recovery */
  bool                                  poisoned;  /* A crash was recovered:
                                                      the MLIR state may be
                                                      corrupt. */
  edg_cir::a_session_stats              stats;
  bool                                  stats_on;  /* PRINT_STATS */
  std::vector<edg_cir::a_diagnostic>    queue;
  std::map<uint32_t, uint64_t>          type_roles;
  edg_cir::a_header_facts               header;
  std::unique_ptr<edg_cir::a_cir_module>
                                        module;
  std::unique_ptr<edg_cir::a_emitter>   emitter;
  std::string                           output;
  std::string                           last_file;
  uint32_t                              last_line;
  uint32_t                              last_column;

  void print_stats();
  an_edg_cir_status consume(const void *bytes, size_t length);
  bool consume_guarded(const void *bytes, size_t length);
  bool consume_timed(const void *bytes, size_t length);
  an_edg_cir_status finish();

  /* Report a diagnostic: delivered now on the calling thread, or queued
     while the worker runs. */
  void report(an_edg_cir_severity severity, uint32_t code,
              const std::string &message);
  void deliver_queue();
  /* Set the sticky status (first error wins) and report it. */
  an_edg_cir_status fail(an_edg_cir_status new_status, uint32_t code,
                         const std::string &message);
  /* The front end sent F_ABORT: set the sticky ABORTED status with a note
     only (the front end has already reported its own error). */
  an_edg_cir_status abort_stream(const std::string &why);

  struct a_handler;
  std::unique_ptr<a_handler>            handler;
  std::unique_ptr<ecil::a_stream_reader>
                                        reader;
};  /* an_edg_cir_session */

namespace edg_cir {

/* pipeline.cpp: run the CIR passes, lowering and code generation on the
   worker thread and fill session.output. */
an_edg_cir_status run_pipeline(an_edg_cir_session &session);

/* A session holds crash recovery enabled from its first consumed frame until
   finish (or destruction): one sigaction round trip per session. */
void hold_crash_recovery();
void release_crash_recovery();

/* Run body under crash recovery and a fatal-error handler (process-global,
   serialized by a mutex).  False means body crashed; message holds the
   fatal-error text (possibly empty).  stack_mb != 0 runs on a new thread. */
bool run_guarded(const std::function<void()> &body, unsigned stack_mb,
                 std::string &message);

/* True on the guard worker thread (inside a guarded section, which the
   thread that waits for it holds guard_mutex for). */
bool on_guard_worker_thread();

#if EDG_CIR_TEST_HOOKS
/* Test hooks (EDG_CIR_TEST_CRASH, EDG_CIR_TEST_CONSUME_CRASH): crash, exit
   or overflow the stack in the calling thread. */
void run_test_crash_hook(const char *hook);
#endif

/* cir_module.cpp */
std::unique_ptr<a_cir_module>
create_cir_module(an_edg_cir_session &session, an_edg_cir_status &status);
void set_module_type_widths(an_edg_cir_session &session);

}  /* namespace edg_cir */

#endif /* EDG_CIR_SESSION_H */
