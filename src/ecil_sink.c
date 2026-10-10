/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*

ecil_sink.c -- Output sinks for the ECIL stream writer.

A sink is a forward-only consumer of stream bytes with four operations:
acquire a buffer, submit it (the sink owns it from then on and frees it when
it is done), finish (the stream ended normally) and abort (the stream was
invalidated).  Two implementations exist:

  - a file or stdout sink that writes with write(2) and never seeks;
  - an in-process sink that feeds the CIR library session (compiled only
    when the library is linked, EDG_CIR_LIBRARY_LINKED).

Buffer pools, threads or sockets can later replace the implementations
without changing the writer.

*/

/* Header files common to all files. */
#include "fe_common.h"

#ifdef PCH_PRAGMA_GUARD
/* Mark the end of the sequence of headers subject to precompiled header
   processing. */
#pragma hdrstop
#endif /* ifdef PCH_PRAGMA_GUARD */

/* Additional header files. */
#include "ecil_write.h"
#include "cir_be.h"

#if ECIL_WRITER_ENABLED

#include <errno.h>
#include <fcntl.h>
#if defined(_WIN32)
#include <io.h>
#else /* !defined(_WIN32) */
#include <unistd.h>
#endif /* defined(_WIN32) */

/* Conditionally open the "edg" namespace. */
BEGIN_EDG_NAMESPACE

#if defined(_WIN32)
#define ECIL_OPEN_FLAGS (_O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY)
#define ecil_write_fd(fd, buf, len) _write((fd), (buf), (unsigned int)(len))
#define ecil_close_fd _close
#define ecil_open_fd(name, flags) _open((name), (flags), _S_IREAD | _S_IWRITE)
#else /* !defined(_WIN32) */
#define ECIL_OPEN_FLAGS (O_WRONLY | O_CREAT | O_TRUNC)
#define ecil_write_fd(fd, buf, len) write((fd), (buf), (len))
#define ecil_close_fd close
#define ecil_open_fd(name, flags) open((name), (flags), 0666)
#endif /* defined(_WIN32) */

/* The kinds of sink. */
enum an_ecil_sink_kind {
  esnk_file,
  esnk_in_process
};

struct an_ecil_sink {
  enum an_ecil_sink_kind
                kind;
  int           fd;             /* File sinks: the descriptor, or -1. */
  a_boolean     owns_fd;        /* FALSE for stdout. */
  a_boolean     failed;         /* A write or API call failed. */
  a_boolean     aborted;        /* The stream was invalidated. */
  a_boolean     aborting;       /* The F_ABORT frame is being submitted. */
  char          *name;          /* File sinks: for messages. */
#ifdef EDG_CIR_LIBRARY_LINKED
  an_edg_cir_session
                *session;
#endif /* EDG_CIR_LIBRARY_LINKED */
};


static an_ecil_sink *ecil_sink_alloc(enum an_ecil_sink_kind kind)
/*
Allocate a sink of the given kind, or return NULL.
*/
{
  an_ecil_sink  *sink = (an_ecil_sink *)calloc(1, sizeof(an_ecil_sink));

  if (sink != NULL) {
    sink->kind = kind;
    sink->fd = -1;
  }  /* if */
  return sink;
}  /* ecil_sink_alloc */


an_ecil_sink *ecil_sink_open_file(const char *file_name)
/*
Open a file sink.  "-" means standard output.  On failure an error is
reported and NULL returned.
*/
{
  an_ecil_sink  *sink = ecil_sink_alloc(esnk_file);

  if (sink == NULL) {
    cir_be_report_status("out of memory opening the ECIL output", -1);
  } else if (strcmp(file_name, "-") == 0) {
    sink->fd = 1;
    sink->owns_fd = FALSE;
    sink->name = strdup("<stdout>");
  } else {
    sink->fd = ecil_open_fd(file_name, ECIL_OPEN_FLAGS);
    sink->owns_fd = TRUE;
    sink->name = strdup(file_name);
    if (sink->fd < 0) {
      char  message[600];

      (void)snprintf(message, sizeof(message),
                     "cannot open ECIL output file \"%.400s\": %.100s",
                     file_name, strerror(errno));
      cir_be_report_status(message, -1);
      free(sink->name);
      free(sink);
      sink = NULL;
    }  /* if */
  }  /* if */
  return sink;
}  /* ecil_sink_open_file */


#ifdef EDG_CIR_LIBRARY_LINKED

an_ecil_sink *ecil_sink_open_in_process(void)
/*
Open a session of the linked CIR library as the sink.  On failure an error
is reported and NULL returned.
*/
{
  an_ecil_sink        *sink = ecil_sink_alloc(esnk_in_process);
  an_edg_cir_options  options;
  an_edg_cir_status   status;

  if (sink == NULL) {
    cir_be_report_status("out of memory opening the CIR session", -1);
  } else {
    edg_cir_options_init(&options);
    cir_be_fill_session_options(&options);
    status = edg_cir_session_open(&options, &sink->session);
    cir_be_report_queued_diagnostics();
    if (status != EDG_CIR_OK) {
      cir_be_report_status("cannot open a CIR session", (int)status);
      free(sink);
      sink = NULL;
    }  /* if */
  }  /* if */
  return sink;
}  /* ecil_sink_open_in_process */

#endif /* EDG_CIR_LIBRARY_LINKED */


char *ecil_sink_acquire(an_ecil_sink *sink, size_t capacity)
/*
Return a writable buffer of at least capacity bytes, or NULL.  Ownership
passes to the sink with ecil_sink_submit.
*/
{
  (void)sink;
  return (char *)malloc(capacity == 0 ? 1 : capacity);
}  /* ecil_sink_acquire */


static a_boolean ecil_sink_write_all(an_ecil_sink *sink,
                                     const char   *bytes,
                                     size_t       length)
/*
Write all of the bytes to a file sink, retrying on interruption and short
writes.  The first failure is reported and makes the sink failed.
*/
{
  while (length > 0 && !sink->failed) {
    long  written = (long)ecil_write_fd(sink->fd, bytes, length);

    if (written < 0) {
      if (errno != EINTR) {
        char  message[600];

        (void)snprintf(message, sizeof(message),
                       "cannot write ECIL output \"%.400s\": %.100s",
                       sink->name != NULL ? sink->name : "?",
                       strerror(errno));
        sink->failed = TRUE;
        cir_be_report_status(message, -1);
      }  /* if */
    } else {
      bytes += written;
      length -= (size_t)written;
    }  /* if */
  }  /* while */
  return !sink->failed;
}  /* ecil_sink_write_all */


a_boolean ecil_sink_submit(an_ecil_sink *sink, char *buffer, size_t length)
/*
Hand a filled buffer to the sink, which takes ownership of it.  Return FALSE
if the sink has failed or been aborted (the buffer is freed either way).
For the in-process sink this feeds the library; its diagnostics are
reported only after the call has returned.
*/
{
  a_boolean ok = FALSE;

  if (sink->failed) {
    /* Nothing: the failure was reported already. */
  } else if (sink->kind == esnk_file) {
    ok = ecil_sink_write_all(sink, buffer, length);
#ifdef EDG_CIR_LIBRARY_LINKED
  } else if (sink->kind == esnk_in_process && !sink->aborted) {
    an_edg_cir_status status = edg_cir_session_consume(sink->session,
                                                       buffer, length);

    ok = (status == EDG_CIR_OK);
    if (sink->aborting) {
      /* F_ABORT is the consequence of an error that was already reported,
         and this runs inside that error's end-of-diagnostic processing:
         discard the library's messages instead of reporting them. */
      cir_be_discard_queued_diagnostics();
    } else {
      if (!ok) sink->failed = TRUE;
      /* The call has returned: now it is safe to use the EDG error
         machinery (which may re-enter ecil_abort and this function). */
      cir_be_report_queued_diagnostics();
    }  /* if */
    if (!ok && !sink->aborting && status != EDG_CIR_ERROR_ABORTED) {
      cir_be_report_status("the CIR library rejected the ECIL stream",
                           (int)status);
    }  /* if */
#endif /* EDG_CIR_LIBRARY_LINKED */
  }  /* if */
  free(buffer);
  return ok;
}  /* ecil_sink_submit */


a_boolean ecil_sink_finish(an_ecil_sink *sink)
/*
The stream ended normally (F_END was submitted).  A file sink closes its
file; the in-process sink runs the pipeline and writes the output.  Return
TRUE on success.
*/
{
  a_boolean ok = !sink->failed && !sink->aborted;

  if (ok && sink->kind == esnk_file) {
    if (sink->owns_fd && sink->fd >= 0) {
      if (ecil_close_fd(sink->fd) != 0) {
        ok = FALSE;
        cir_be_report_status("cannot close the ECIL output file", -1);
      }  /* if */
      sink->fd = -1;
    }  /* if */
#ifdef EDG_CIR_LIBRARY_LINKED
  } else if (ok && sink->kind == esnk_in_process) {
    an_edg_cir_status status = edg_cir_session_finish(sink->session);

    ok = (status == EDG_CIR_OK);
    if (!ok) sink->failed = TRUE;
    cir_be_report_queued_diagnostics();
    if (!ok) {
      cir_be_report_status("the CIR back end failed", (int)status);
    }  /* if */
#endif /* EDG_CIR_LIBRARY_LINKED */
  }  /* if */
  return ok;
}  /* ecil_sink_finish */


void ecil_sink_begin_abort(an_ecil_sink *sink)
/*
The F_ABORT frame is about to be submitted: its submission must not report
library diagnostics.
*/
{
  sink->aborting = TRUE;
}  /* ecil_sink_begin_abort */


void ecil_sink_abort(an_ecil_sink *sink)
/*
The stream was invalidated (F_ABORT submitted, or the writer failed).  No
more bytes are consumed; resources are released by ecil_sink_close.
*/
{
  sink->aborted = TRUE;
}  /* ecil_sink_abort */


void ecil_sink_close(an_ecil_sink *sink)
/*
Release the sink.  Closes the file or the library session.
*/
{
  if (sink->kind == esnk_file) {
    if (sink->owns_fd && sink->fd >= 0) (void)ecil_close_fd(sink->fd);
#ifdef EDG_CIR_LIBRARY_LINKED
  } else if (sink->kind == esnk_in_process) {
    edg_cir_session_close(sink->session);
#endif /* EDG_CIR_LIBRARY_LINKED */
  }  /* if */
  free(sink->name);
  free(sink);
}  /* ecil_sink_close */


/* Conditionally close the "edg" namespace. */
END_EDG_NAMESPACE

#endif /* ECIL_WRITER_ENABLED */
