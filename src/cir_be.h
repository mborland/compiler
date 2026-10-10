/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*

cir_be.h -- Declarations for the CIR back end shim (cir_be.c): the
            back_end() entry point, the options for the in-process CIR
            library session, and the queue that delivers library
            diagnostics through the EDG error machinery.

*/

/* Avoid including these declarations more than once: */
#ifndef CIR_BE_H
#define CIR_BE_H 1

#include "ecil_write.h"

#if ECIL_WRITER_ENABLED

/* Conditionally open the "edg" namespace. */
BEGIN_EDG_NAMESPACE

#ifdef EDG_CIR_LIBRARY_LINKED
/*
Fill in the options of the in-process session: output kind and path, the
diagnostic callback and the optimization level.  The caller has already
run edg_cir_options_init.
*/
extern void cir_be_fill_session_options(an_edg_cir_options *options);
#endif /* EDG_CIR_LIBRARY_LINKED */

/*
Report, through str_error/str_warning, the diagnostics the library queued
during earlier API calls.  Call only when no API call is in progress.
*/
extern void cir_be_report_queued_diagnostics(void);

extern void cir_be_discard_queued_diagnostics(void);

/*
Report a failed API call.  If the library queued no error message for it,
issue one naming the status.
*/
extern void cir_be_report_status(const char *what, int status);

/* Conditionally close the "edg" namespace. */
END_EDG_NAMESPACE

#endif /* ECIL_WRITER_ENABLED */

#endif /* ifndef CIR_BE_H */
