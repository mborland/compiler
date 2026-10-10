/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*

cir_be.c -- The CIR back end shim: back_end(), the options of the
            in-process CIR library session, and the delivery of library
            diagnostics through the EDG error machinery.

The library may not be re-entered from its callbacks (edg_cir.h), so the
diagnostic callback only queues messages.  They are reported with
str_error/str_warning after the API call has returned.  Reporting an error
can reach ecil_abort(), which submits F_ABORT to the library; that is safe
here because no API call is in progress.

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

/* Conditionally open the "edg" namespace. */
BEGIN_EDG_NAMESPACE

/* A diagnostic queued by the library callback. */
typedef struct a_queued_diagnostic {
  a_boolean     is_error;       /* FALSE: a warning (notes are dropped). */
  char          *text;
} a_queued_diagnostic;

STATIC_THREAD a_queued_diagnostic
                *queued_diagnostics;
STATIC_THREAD size_t
                num_queued_diagnostics;
#ifdef EDG_CIR_LIBRARY_LINKED
STATIC_THREAD size_t
                queued_diagnostics_capacity;
#endif /* EDG_CIR_LIBRARY_LINKED */
STATIC_THREAD a_boolean
                draining_diagnostics;
STATIC_THREAD a_boolean
                library_error_explained;
                        /* TRUE if an error from the library has been
                           reported since the last failed API call was
                           described. */

#ifdef EDG_CIR_LIBRARY_LINKED

static void queue_diagnostic(void                *user_data,
                             an_edg_cir_severity severity,
                             uint32_t            code,
                             const char          *message,
                             const char          *file,
                             uint32_t            line,
                             uint32_t            column)
/*
The library's diagnostic callback.  It runs inside an API call, so it only
copies the message to the queue and returns.
*/
{
  (void)user_data;
  (void)code;
  (void)column;
  if (severity != EDG_CIR_SEVERITY_NOTE) {
    char    *text;
    size_t  length = strlen(message) + 1;

    if (file != NULL) length += strlen(file) + 16;
    text = (char *)malloc(length);
    if (text != NULL) {
      if (file != NULL) {
        (void)snprintf(text, length, "%s:%u: %s", file, (unsigned int)line,
                       message);
      } else {
        (void)snprintf(text, length, "%s", message);
      }  /* if */
      if (num_queued_diagnostics == queued_diagnostics_capacity) {
        size_t              new_capacity = (queued_diagnostics_capacity == 0)
                                  ? 16 : queued_diagnostics_capacity * 2;
        a_queued_diagnostic *new_queue = (a_queued_diagnostic *)realloc(
                              queued_diagnostics,
                              new_capacity * sizeof(a_queued_diagnostic));

        if (new_queue != NULL) {
          queued_diagnostics = new_queue;
          queued_diagnostics_capacity = new_capacity;
        }  /* if */
      }  /* if */
      if (num_queued_diagnostics == queued_diagnostics_capacity) {
        free(text);
      } else {
        queued_diagnostics[num_queued_diagnostics].is_error =
                                    (severity >= EDG_CIR_SEVERITY_ERROR);
        queued_diagnostics[num_queued_diagnostics].text = text;
        num_queued_diagnostics++;
      }  /* if */
    }  /* if */
  }  /* if */
}  /* queue_diagnostic */


void cir_be_fill_session_options(an_edg_cir_options *options)
/*
Set the options of the in-process session: assembly output to the generated
file name (the --gen_c_file_name value, or <source>.int.s), unoptimized, with
the queueing diagnostic callback.
*/
{
  a_const_char  *output_name = gen_c_file_name;

  if (output_name == NULL) {
    output_name = derived_name(primary_source_file_name, ".int.s");
  }  /* if */
  options->output_kind = EDG_CIR_OUTPUT_ASM;
  options->output_path = output_name;
  options->diagnostic_callback = queue_diagnostic;
  options->diagnostic_user_data = NULL;
  options->opt_level = 0;
}  /* cir_be_fill_session_options */

#endif /* EDG_CIR_LIBRARY_LINKED */


void cir_be_report_queued_diagnostics(void)
/*
Report the queued library diagnostics, in order, through the EDG error
machinery and empty the queue.  Reporting an error can call ecil_abort and
through it the library, whose callback may queue further messages; the
loop therefore re-reads the count, and a nested call returns at once.
*/
{
  size_t  i;

  if (!draining_diagnostics) {
    draining_diagnostics = TRUE;
    for (i = 0; i < num_queued_diagnostics; i++) {
      /* Take the text: the queue may be reallocated while reporting. */
      char      *text = queued_diagnostics[i].text;
      a_boolean is_error = queued_diagnostics[i].is_error;

      queued_diagnostics[i].text = NULL;
      /* The library's messages carry their own file:line; the file-scope
         IL (the source-file entries) may already have been freed, so no
         source position is looked up. */
      if (is_error) {
        library_error_explained = TRUE;
        pos_st_error(ec_cir_error, &null_source_position, text);
      } else {
        pos_st_warning(ec_cir_warning, &null_source_position, text);
      }  /* if */
      free(text);
    }  /* for */
    num_queued_diagnostics = 0;
    draining_diagnostics = FALSE;
  }  /* if */
}  /* cir_be_report_queued_diagnostics */


void cir_be_discard_queued_diagnostics(void)
/*
Throw away the queued library diagnostics.
*/
{
  size_t  i;

  for (i = 0; i < num_queued_diagnostics; i++) {
    free(queued_diagnostics[i].text);
    queued_diagnostics[i].text = NULL;
  }  /* for */
  num_queued_diagnostics = 0;
}  /* cir_be_discard_queued_diagnostics */


void cir_be_warning(const char *message)
/*
Issue a warning that something was not translated (for example a function
body that uses a construct the CIR back end does not support).
*/
{
  str_warning(ec_cir_warning, message);
}  /* cir_be_warning */


void cir_be_report_status(const char *what, int status)
/*
Report a failure of the CIR back end.  what describes the failing step.  A
status of -1 means "no library status"; otherwise it is an
an_edg_cir_status.  If the library has already queued an error for this
failure, nothing more is said.
*/
{
  char  message[700];

  if (status >= 0 && library_error_explained) {
    library_error_explained = FALSE;
  } else {
#ifdef EDG_CIR_LIBRARY_LINKED
    if (status >= 0) {
      (void)snprintf(message, sizeof(message), "%.400s (%.200s)", what,
                     edg_cir_status_string((an_edg_cir_status)status));
    } else {
      (void)snprintf(message, sizeof(message), "%.600s", what);
    }  /* if */
#else /* !defined(EDG_CIR_LIBRARY_LINKED) */
    (void)snprintf(message, sizeof(message), "%.600s", what);
#endif /* defined(EDG_CIR_LIBRARY_LINKED) */
    library_error_explained = FALSE;
    pos_st_error(ec_cir_error, &null_source_position, message);
  }  /* if */
}  /* cir_be_report_status */


/* Conditionally close the "edg" namespace. */
END_EDG_NAMESPACE

/*
Leak checking: the CIR library (LLVM) keeps process-lifetime caches that
LeakSanitizer reports.  Suppress them by library name.
*/
#if defined(EDG_CIR_LIBRARY_LINKED)
#if defined(__SANITIZE_ADDRESS__)
#define ECIL_LSAN_SUPPRESSIONS TRUE
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ECIL_LSAN_SUPPRESSIONS TRUE
#endif /* __has_feature(address_sanitizer) */
#endif /* defined(__SANITIZE_ADDRESS__) */
#endif /* defined(EDG_CIR_LIBRARY_LINKED) */

#ifdef ECIL_LSAN_SUPPRESSIONS
extern "C" const char *__lsan_default_suppressions(void)
/*
Called by LeakSanitizer at startup.
*/
{
  return "leak:libedg_cir\nleak:edg_cir_\n";
}  /* __lsan_default_suppressions */
#endif /* ECIL_LSAN_SUPPRESSIONS */

/*
Note that the EDG namespace is not opened for back_end().  Instead we do a
using-directive if the EDG code is in the edg namespace.
*/
USING_NAMESPACE_EDG

void back_end(void)
/*
The back end entry point (called by cfe.c after fe_wrapup when there were no
errors).  By now the whole stream has been written and F_END submitted:
finish the in-process session (run the pipeline, write the output) or close
the --ecil_output file.  Failures are reported through the EDG error
machinery, which gives the compilation a failing exit status.
*/
{
  ecil_stream_finish_back_end();
  cir_be_report_queued_diagnostics();
}  /* back_end */

#endif /* ECIL_WRITER_ENABLED */
