/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
cir_module.h -- the MLIR context, module and target machine of a session.
Only the driver sources include this header; it pulls in LLVM and MLIR.
*/

#ifndef EDG_CIR_MODULE_H
#define EDG_CIR_MODULE_H 1

#include <memory>
#include <string>

#include "llvm/IR/DataLayout.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"

#include "session.h"

namespace edg_cir {

class a_cir_module {
public:
  a_cir_module()
    : context(mlir::MLIRContext::Threading::DISABLED), layout("") {}

  /* The context is declared first so it is destroyed last. */
  mlir::MLIRContext                    context;
  mlir::OwningOpRef<mlir::ModuleOp>    module;
  std::unique_ptr<llvm::TargetMachine> machine;
  llvm::Triple                         triple;
  llvm::DataLayout                     layout;
};  /* a_cir_module */

}  /* namespace edg_cir */

#endif /* EDG_CIR_MODULE_H */
