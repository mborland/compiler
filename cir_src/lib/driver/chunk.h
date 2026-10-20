/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
chunk.h -- chunked lowering of the CIR module (slice S3, design (a)).

The CIR-to-CIR passes run on the whole module.  Lowering it to LLVM IR costs
1.7 to 2.7 times the memory of the module itself, so a translation unit with
many function bodies is lowered in pieces: the bodies are cut into chunks of
chunk_functions, every chunk becomes a CIR module of its own (the bodies plus
declarations of the symbols they use), is lowered to an llvm::Module, linked
into the result with llvm::Linker, and its CIR is erased.  Everything that
is not a function body is one more chunk, lowered and linked first.  The
merged module then goes through GlobalDCE, the optimizer and the code
generator unchanged, so the output is the whole-module output (the
functions in the same order, the same code).
*/

#ifndef EDG_CIR_CHUNK_H
#define EDG_CIR_CHUNK_H 1

#include <cstdint>
#include <functional>
#include <memory>

namespace llvm {
class LLVMContext;
class Module;
}  /* namespace llvm */

namespace mlir {
class ModuleOp;
}  /* namespace mlir */

struct an_edg_cir_session;

namespace edg_cir {

/* The defaults of an_edg_cir_options::chunk_functions and chunk_threshold
   (0 selects them). */
constexpr uint32_t k_default_chunk_functions = 500;
constexpr uint32_t k_default_chunk_threshold = 4000;
constexpr uint32_t k_chunk_never = UINT32_MAX;

/* Lowers one CIR module (the whole module, or one chunk) to LLVM IR. */
typedef std::function<std::unique_ptr<llvm::Module>(mlir::ModuleOp)>
    a_lower_function;

bool chunking_wanted(const an_edg_cir_session &session, uint64_t bodies);

std::unique_ptr<llvm::Module> lower_in_chunks(
    an_edg_cir_session &session, mlir::ModuleOp module,
    llvm::LLVMContext &llvm_context, const a_lower_function &lower);

}  /* namespace edg_cir */

#endif /* EDG_CIR_CHUNK_H */
