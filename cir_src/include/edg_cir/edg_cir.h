/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
edg_cir.h -- the C API of the EDG CIR back-end library (libedg_cir).

This is the only interface between the front end and the CIR library.  It
is plain C, depends only on <stddef.h> and <stdint.h>, and must compile
cleanly as C99, C++14 (the front end: -pedantic -Wconversion
-Wsign-conversion) and C++17.  The library consumes an ECIL stream (see
ecil_format.h) and produces CIR, LLVM IR, assembly or an object file.

Rules for callers and implementers:
- No C++ types, exceptions, longjmp or exit() cross this boundary.
- A session that hits an error is sticky: every later call returns the
  first error status without doing more work.
- Diagnostics are delivered through the callback in the options; the
  library never writes to stdout/stderr on its own unless no callback is
  given.
- Threads: decoding and CIR op construction may run on the calling thread
  (they are non-recursive).  Every MLIR verify, walk, erase, pass, printer
  and the LLVM pipeline run on the library's own worker thread with a large
  stack.  Callbacks are always invoked on the thread that called the API
  function (diagnostics from the worker are queued and delivered after it
  joins).
- The API is not reentrant: a callback must not call any edg_cir_*
  function, must not longjmp or exit, and must return normally.  A front
  end must not route a callback into its own error machinery if that could
  re-enter the session (e.g. an abort path that calls consume).
- The library copies any bytes it keeps (strings, names, partial frames);
  the caller's buffer need only be valid during the call.
- While a session call runs, the library installs process-global state:
  LLVM's fatal-error handler and signal-based crash recovery (SIGSEGV,
  SIGABRT, SIGBUS, ...).  Guarded calls serialize on an internal mutex, so
  use one active session per process, and use a session from only one
  thread at a time.  The embedder must not install its own LLVM fatal-error
  handler or conflicting SIGSEGV/SIGABRT handlers while an edg_cir_* call
  is active.  After a recovered crash the session is poisoned: later calls
  fail and close deliberately leaks its internal state.
- Owned by the orchestrator; changes bump EDG_CIR_API_VERSION.
*/

#ifndef EDG_CIR_H
#define EDG_CIR_H 1

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define EDG_CIR_API_VERSION 1

#if defined(_WIN32)
#if defined(EDG_CIR_BUILDING_LIBRARY)
#define EDG_CIR_API __declspec(dllexport)
#else /* !defined(EDG_CIR_BUILDING_LIBRARY) */
#define EDG_CIR_API
#endif /* defined(EDG_CIR_BUILDING_LIBRARY) */
#else /* !defined(_WIN32) */
#define EDG_CIR_API __attribute__((visibility("default")))
#endif /* defined(_WIN32) */

/* Status codes returned by the API. */
typedef enum an_edg_cir_status {
  EDG_CIR_OK                = 0,
  EDG_CIR_ERROR_USAGE       = 1,  /* Bad arguments or call order. */
  EDG_CIR_ERROR_FORMAT      = 2,  /* Malformed ECIL stream. */
  EDG_CIR_ERROR_VERSION     = 3,  /* Unsupported ECIL major/minor version. */
  EDG_CIR_ERROR_UNSUPPORTED = 4,  /* Construct or target not supported. */
  EDG_CIR_ERROR_ABORTED     = 5,  /* The stream ended with F_ABORT. */
  EDG_CIR_ERROR_VERIFY      = 6,  /* The MLIR verifier rejected the CIR. */
  EDG_CIR_ERROR_PIPELINE    = 7,  /* A CIR or LLVM pass failed. */
  EDG_CIR_ERROR_IO          = 8,  /* Output could not be written. */
  EDG_CIR_ERROR_INTERNAL    = 9   /* Internal error in the library. */
} an_edg_cir_status;

/* What the session produces at edg_cir_session_finish. */
typedef enum an_edg_cir_output_kind {
  EDG_CIR_OUTPUT_NONE        = 0,  /* Decode and verify only. */
  EDG_CIR_OUTPUT_CIR         = 1,  /* Textual CIR as emitted. */
  EDG_CIR_OUTPUT_CIR_LOWERED = 2,  /* Textual CIR after CIR-to-CIR passes. */
  EDG_CIR_OUTPUT_LLVM_IR     = 3,  /* Textual LLVM IR. */
  EDG_CIR_OUTPUT_LLVM_BC     = 4,  /* LLVM bitcode. */
  EDG_CIR_OUTPUT_ASM         = 5,  /* Target assembly. */
  EDG_CIR_OUTPUT_OBJECT      = 6   /* Target object file. */
} an_edg_cir_output_kind;

/* Diagnostic severities passed to the callback. */
typedef enum an_edg_cir_severity {
  EDG_CIR_SEVERITY_NOTE    = 0,
  EDG_CIR_SEVERITY_WARNING = 1,
  EDG_CIR_SEVERITY_ERROR   = 2,
  EDG_CIR_SEVERITY_FATAL   = 3
} an_edg_cir_severity;

/* Option flags (an_edg_cir_options::flags). */
#define EDG_CIR_FLAG_VERIFY_EACH         0x0001u
                        /* Run the MLIR verifier after every pass. */
#define EDG_CIR_FLAG_DISABLE_CIR_PASSES  0x0002u
                        /* Skip the CIR-to-CIR pipeline (debugging). */
#define EDG_CIR_FLAG_PRINT_STATS         0x0004u
                        /* Report timings and peak memory at finish. */
#define EDG_CIR_FLAG_TRACE               0x0008u
                        /* Trace decoded records (checking builds). */
#define EDG_CIR_FLAG_KEEP_GOING          0x0010u
                        /* After an unsupported construct, keep going. */

/*
Diagnostic callback.  message is NUL-terminated and valid only during the
call.  file/line/column come from the most recent ECIL LOC record (file may
be NULL and line 0 when unknown).  The callback must return normally.
*/
typedef void (*an_edg_cir_diagnostic_callback)(
                                       void                *user_data,
                                       an_edg_cir_severity  severity,
                                       uint32_t             code,
                                       const char          *message,
                                       const char          *file,
                                       uint32_t             line,
                                       uint32_t             column);

/*
Output callback, used instead of output_path when set.  It is called one or
more times with consecutive chunks of the output and returns 0 on success.
*/
typedef int (*an_edg_cir_write_callback)(void       *user_data,
                                         const void *bytes,
                                         size_t      length);

/*
Session options.  Always initialize with edg_cir_options_init, then set
fields; struct_size and api_version let the library accept older callers.
*/
typedef struct an_edg_cir_options {
  uint32_t      struct_size;     /* sizeof(an_edg_cir_options). */
  uint32_t      api_version;     /* EDG_CIR_API_VERSION of the caller. */
  an_edg_cir_output_kind
                output_kind;
  const char   *output_path;     /* NULL with no write callback: none. */
  an_edg_cir_write_callback
                write_callback;  /* Takes precedence over output_path. */
  void         *write_user_data;
  an_edg_cir_diagnostic_callback
                diagnostic_callback;
  void         *diagnostic_user_data;
  const char   *target_triple_override;
                                 /* NULL: use the stream's HDR_TARGET. */
  int32_t       opt_level;       /* 0-3. */
  uint32_t      flags;           /* EDG_CIR_FLAG_*. */
  const char   *const *llvm_args;
                                 /* NULL-terminated, passed to LLVM's
                                    command-line parser once per process. */
} an_edg_cir_options;

/* An opaque session that consumes one ECIL stream. */
typedef struct an_edg_cir_session an_edg_cir_session;

EDG_CIR_API void edg_cir_options_init(an_edg_cir_options *options);

/*
Create a session.  On success *session is set and EDG_CIR_OK returned.
*/
EDG_CIR_API an_edg_cir_status edg_cir_session_open(
                                     const an_edg_cir_options  *options,
                                     an_edg_cir_session       **session);

/*
Feed the next bytes of the ECIL stream.  Chunks may be split anywhere; whole
frames contained in a chunk are decoded in place without copying (anything
retained is copied), and a partial trailing frame is buffered until the
rest arrives.  The bytes need only remain valid for the duration of the
call.
*/
EDG_CIR_API an_edg_cir_status edg_cir_session_consume(
                                     an_edg_cir_session *session,
                                     const void         *bytes,
                                     size_t              length);

/*
Finish the stream (F_END must have been consumed), run the pipeline and
write the requested output.
*/
EDG_CIR_API an_edg_cir_status edg_cir_session_finish(
                                     an_edg_cir_session *session);

/* Destroy a session.  Accepts NULL. */
EDG_CIR_API void edg_cir_session_close(an_edg_cir_session *session);

/*
Convenience: open a session, consume the ECIL file at path ("-" for stdin),
finish and close.
*/
EDG_CIR_API an_edg_cir_status edg_cir_process_file(
                                     const an_edg_cir_options *options,
                                     const char               *path);

/* A static string naming the library, ECIL and LLVM versions. */
EDG_CIR_API const char *edg_cir_version_string(void);

/* A static description of a status code. */
EDG_CIR_API const char *edg_cir_status_string(an_edg_cir_status status);

#ifdef __cplusplus
}  /* extern "C" */
#endif /* __cplusplus */

#endif /* EDG_CIR_H */
