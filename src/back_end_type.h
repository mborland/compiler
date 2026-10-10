/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*

back_end_type.h -- Selects the back end and derives the BACK_END_IS_*
                   macros from it.

*/

/*
This header is included from basics.h immediately after TRUE and FALSE are
defined, so that it precedes every use of the BACK_END_* macros (including
the test in lang_feat.h).  basics.h includes it inside the "edg" namespace, so
it must contain macros only: no declarations and no system includes.
*/

#ifndef BACK_END_TYPE_H
#define BACK_END_TYPE_H 1

/*
The values of BACK_END_TYPE.  These numbers are permanent: they appear in
--dump_configuration output and in //require: lines of tests.  0 is not a
valid value on purpose: an undefined identifier evaluates to 0 in #if, so a
misspelled BACK_END_TYPE_* name would otherwise silently select a back end.
*/
#define BACK_END_TYPE_EXTERNAL 4  /* A user-supplied back_end(). */
#define BACK_END_TYPE_C_GEN 1     /* The C-generating back end. */
#define BACK_END_TYPE_CP_GEN 2    /* The C++-generating back end. */
#define BACK_END_TYPE_CIR 3       /* The ClangIR (CIR) back end. */

#ifndef BACK_END_TYPE

/*
Legacy configurations select a back end with BACK_END_IS_C_GEN_BE and/or
BACK_END_IS_CP_GEN_BE.  A configuration that sets neither gets the C-
generating back end (this has always been the default).
*/
#ifndef BACK_END_IS_C_GEN_BE
#define BACK_END_IS_C_GEN_BE TRUE  /* You can change this. */
#endif /* ifndef BACK_END_IS_C_GEN_BE */
#ifndef BACK_END_IS_CP_GEN_BE
#define BACK_END_IS_CP_GEN_BE FALSE  /* You can change this. */
#endif /* ifndef BACK_END_IS_CP_GEN_BE */

#if BACK_END_IS_C_GEN_BE && BACK_END_IS_CP_GEN_BE
 #error -- BACK_END_IS_C_GEN_BE and BACK_END_IS_CP_GEN_BE cannot both be TRUE.
#endif /* BACK_END_IS_C_GEN_BE && BACK_END_IS_CP_GEN_BE */

#if BACK_END_IS_C_GEN_BE
#define BACK_END_TYPE BACK_END_TYPE_C_GEN
#elif BACK_END_IS_CP_GEN_BE
#define BACK_END_TYPE BACK_END_TYPE_CP_GEN
#else /* !BACK_END_IS_C_GEN_BE && !BACK_END_IS_CP_GEN_BE */
#define BACK_END_TYPE BACK_END_TYPE_EXTERNAL
#endif /* BACK_END_IS_C_GEN_BE */

#else /* defined(BACK_END_TYPE) */

/*
New-style configurations set only BACK_END_TYPE.  Derive the legacy macros
using the same TRUE/FALSE tokens the old defaults used.  If a configuration
also sets one of them, it must agree.
*/
#if BACK_END_TYPE == 0
 #error -- BACK_END_TYPE is 0; this usually means that it was set to a \
           misspelled or undefined BACK_END_TYPE_* name.
#endif /* BACK_END_TYPE == 0 */
#if BACK_END_TYPE != BACK_END_TYPE_EXTERNAL && \
    BACK_END_TYPE != BACK_END_TYPE_C_GEN && \
    BACK_END_TYPE != BACK_END_TYPE_CP_GEN && \
    BACK_END_TYPE != BACK_END_TYPE_CIR
 #error -- BACK_END_TYPE is not one of the BACK_END_TYPE_* values.
#endif /* BACK_END_TYPE != ... */

#ifndef BACK_END_IS_C_GEN_BE
#if BACK_END_TYPE == BACK_END_TYPE_C_GEN
#define BACK_END_IS_C_GEN_BE TRUE
#else /* BACK_END_TYPE != BACK_END_TYPE_C_GEN */
#define BACK_END_IS_C_GEN_BE FALSE
#endif /* BACK_END_TYPE == BACK_END_TYPE_C_GEN */
#endif /* ifndef BACK_END_IS_C_GEN_BE */
#ifndef BACK_END_IS_CP_GEN_BE
#if BACK_END_TYPE == BACK_END_TYPE_CP_GEN
#define BACK_END_IS_CP_GEN_BE TRUE
#else /* BACK_END_TYPE != BACK_END_TYPE_CP_GEN */
#define BACK_END_IS_CP_GEN_BE FALSE
#endif /* BACK_END_TYPE == BACK_END_TYPE_CP_GEN */
#endif /* ifndef BACK_END_IS_CP_GEN_BE */

#if (BACK_END_IS_C_GEN_BE != 0) != (BACK_END_TYPE == BACK_END_TYPE_C_GEN)
 #error -- BACK_END_IS_C_GEN_BE disagrees with BACK_END_TYPE.
#endif /* (BACK_END_IS_C_GEN_BE != 0) != ... */
#if (BACK_END_IS_CP_GEN_BE != 0) != (BACK_END_TYPE == BACK_END_TYPE_CP_GEN)
 #error -- BACK_END_IS_CP_GEN_BE disagrees with BACK_END_TYPE.
#endif /* (BACK_END_IS_CP_GEN_BE != 0) != ... */

#endif /* ifndef BACK_END_TYPE */

/*
Derived macros for code that is common to several back ends.
BACK_END_GENERATES_SOURCE is TRUE if the back end writes C or C++ source (and
so has the --gen_c_*_version options and the generated-code target dialect).
BACK_END_HAS_OUTPUT_FILE is TRUE if the back end writes an output file whose
name the driver passes with --gen_c_file_name (and that uses the generated
file line directive options): the generating back ends, and CIR.
*/
#if BACK_END_IS_C_GEN_BE || BACK_END_IS_CP_GEN_BE
#define BACK_END_GENERATES_SOURCE TRUE
#else /* !(BACK_END_IS_C_GEN_BE || BACK_END_IS_CP_GEN_BE) */
#define BACK_END_GENERATES_SOURCE FALSE
#endif /* BACK_END_IS_C_GEN_BE || BACK_END_IS_CP_GEN_BE */
#if BACK_END_GENERATES_SOURCE || BACK_END_TYPE == BACK_END_TYPE_CIR
#define BACK_END_HAS_OUTPUT_FILE TRUE
#else /* !(BACK_END_GENERATES_SOURCE || BACK_END_TYPE == BACK_END_TYPE_CIR) */
#define BACK_END_HAS_OUTPUT_FILE FALSE
#endif /* BACK_END_GENERATES_SOURCE || BACK_END_TYPE == BACK_END_TYPE_CIR */

#endif /* ifndef BACK_END_TYPE_H */
