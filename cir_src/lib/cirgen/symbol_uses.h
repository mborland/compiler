/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
symbol_uses.h -- the symbols an operation names.

mlir::SymbolTable::getSymbolUses does not see every use in a CIR module: it
does not enter every nested operation the way a walk does, and it reports
only the symbols it can attribute to the operation's own attributes.  This
helper visits every symbol reference (FlatSymbolRefAttr and SymbolRefAttr, at
any depth inside any attribute) of an operation and of every operation nested
in it.  Lazy emission, the finish-time liveness closure and the chunked
lowering all use it.

A symbol that CIR keeps as a plain string attribute (the function of a
method-pointer constant, `#cir.method<@f>`) is not a SymbolRefAttr: its users
record an explicit edge (a_emitter::address_uses_).
*/

#ifndef EDG_CIR_SYMBOL_USES_H
#define EDG_CIR_SYMBOL_USES_H

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Operation.h"

namespace edg_cir {

template <typename a_visitor>
void for_each_symbol_use(mlir::Operation *root, a_visitor visit)
/*
Call visit(user, root_reference) for every symbol reference in the attributes
of root and of every operation nested in it; user is the operation whose
attributes hold the reference, root_reference its mlir::StringAttr.
*/
{
  root->walk([&](mlir::Operation *op) {
    op->getAttrDictionary().walk([&](mlir::SymbolRefAttr reference) {
      visit(op, reference.getRootReference());
    });
  });
}  /* for_each_symbol_use */

}  /* namespace edg_cir */

#endif /* EDG_CIR_SYMBOL_USES_H */
