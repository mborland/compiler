/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
cir_config.h -- the configuration header of the CIR library.  It holds
build-time constants only; every target and language fact comes from the
ECIL stream.  Nothing from the EDG front end may be visible here.
*/

#ifndef CIR_CONFIG_H
#define CIR_CONFIG_H 1

/*
Tripwire: the front end's basics.h and back_end_type.h define these guards.
A cir_src file that (directly or not) included front-end headers would
pick up front-end macros and break the isolation of the library.
*/
#if defined(BASICS_H) || defined(BACK_END_TYPE_H)
#error "cir_src must not include EDG front-end headers (src/)"
#endif

#include "cir_config_generated.h"

/* Library version. */
#define EDG_CIR_LIBRARY_VERSION_MAJOR 0
#define EDG_CIR_LIBRARY_VERSION_MINOR 1

/* Size of the worker thread stack for MLIR/LLVM work, in MiB. */
#define EDG_CIR_WORKER_STACK_MB 256

/* The only triple the library supports (CallConvLowering is x86-64 SysV). */
#define EDG_CIR_DEFAULT_TRIPLE "x86_64-unknown-linux-gnu"

/* Checking build: extra validation of decoded streams. */
#ifndef EDG_CIR_CHECKING
#define EDG_CIR_CHECKING 0
#endif

#endif /* CIR_CONFIG_H */
