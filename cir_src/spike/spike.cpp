/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
spike.cpp -- ClangIR toolchain spike for the EDG CIR back end.

Builds, through the public C++ API only (no clang AST, no CIRGen), the CIR
module for

    int g = 5;
    int f(int a, int b) { return a < b ? a + b : 0; }
    int main(void) {
      int s = 0;
      for (int i = 0; i < g; ++i) s += f(i, 3);
      return s == 12 ? 0 : 1;
    }

(s = f(0,3) + f(1,3) + f(2,3) + f(3,3) + f(4,3) = 3 + 4 + 5 + 0 + 0 = 12, so
the program exits 0.)  It then sets the module attributes, runs
cir::runCIRToCIRPasses, cir::direct::lowerDirectlyFromCIRToLLVMIR and the LLVM
code generator, and writes an x86-64 object file.  Everything after option
parsing runs on one worker thread with a large stack; the MLIR context has
multithreading disabled so no pass runs on a small-stack pool thread.

The region-carrying ops are opened without callbacks that recurse, the way the
real emitter will do it: cir.scope / cir.for with empty callbacks and then
explicit insertion-point moves, cir.ternary through OperationState (its
builder infers the result type from yields that do not exist yet).

Derived in part from llvm-project clang/lib/CIR (Apache-2.0 WITH
LLVM-exception).
*/

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

#include "clang/Basic/LangOptions.h"
#include "clang/CIR/CIRDataLayoutSpec.h"
#include "clang/CIR/CIRToCIRPasses.h"
#include "clang/CIR/Dialect/Builder/CIRBaseBuilder.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/InitAllDialects.h"
#include "clang/CIR/LowerToLLVM.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/thread.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"
#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Verifier.h"

namespace {

/*
Command-line options.
*/
struct a_spike_options {
  bool         print_cir;
  bool         print_cir_post;
  bool         print_llvm;
  bool         emit_asm;
  int          opt_level;
  unsigned     stack_mb;
  std::string  triple;
  std::string  output;
};  /* a_spike_options */

/*
State shared by the module construction helpers.
*/
struct a_spike {
  mlir::MLIRContext         *context;
  cir::CIRBaseBuilderTy     *builder;
  mlir::ModuleOp            module;
  cir::IntType              s32;
};  /* a_spike */

/*
One unit of work for the worker thread; status is 0 on success.
*/
struct a_spike_job {
  const a_spike_options *options;
  int                    status;
  std::string            message;
};  /* a_spike_job */

static mlir::Location unknown_loc(a_spike &sp)
/*
Return the (only) location this spike uses.
*/
{
  return sp.builder->getUnknownLoc();
}  /* unknown_loc */

static void set_dso_local(mlir::Operation *op)
/*
Mark the function or global op as dso_local through its interface.
*/
{
  mlir::cast<cir::CIRGlobalValueInterface>(op).setDSOLocal(true);
}  /* set_dso_local */

static void set_module_attributes(a_spike &sp, const llvm::Triple &triple,
                                  const llvm::DataLayout &layout)
/*
Set the module attributes CIRGenModule sets for a C translation unit.  Only
cir.triple, cir.target_abi and dlti.dl_spec are mandatory for the pipeline.
*/
{
  mlir::MLIRContext *ctx = sp.context;
  cir::CIRBaseBuilderTy &b = *sp.builder;
  mlir::ModuleOp module = sp.module;
  auto latest_abi = static_cast<int32_t>(clang::LangOptions::ClangABI::Latest);

  module.setSymName("spike.c");
  module->setAttr(cir::CIRDialect::getTripleAttrName(),
                  b.getStringAttr(triple.str()));
  module->setAttr(cir::CIRDialect::getTargetABIAttrName(),
                  b.getStringAttr(""));
  module->setAttr(cir::CIRDialect::getSourceLanguageAttrName(),
                  cir::SourceLanguageAttr::get(ctx, cir::SourceLanguage::C));
  module->setAttr(cir::CIRDialect::getSizeTypeWidthAttrName(),
                  b.getI32IntegerAttr(64));
  module->setAttr(cir::CIRDialect::getIntTypeWidthAttrName(),
                  b.getI32IntegerAttr(32));
  module->setAttr(cir::CIRDialect::getLoweringLangOptionsAttrName(),
                  cir::LoweringLangOptionsAttr::get(
                      ctx, /*exceptions=*/false, /*threadsafe_statics=*/true,
                      /*cuda=*/false, /*cuda_is_device=*/false,
                      /*hip=*/false, /*gpu_rdc=*/false, /*openmp=*/false,
                      /*openmp_is_target_device=*/false, latest_abi));
  module->setAttr(cir::CIRDialect::getDefaultTlsModelAttrName(),
                  cir::TLSModelAttr::get(ctx, cir::TLSModel::GeneralDynamic));
  module->setAttr(cir::CIRDialect::getModuleLevelAsmAttrName(),
                  b.getArrayAttr({}));
  cir::setMLIRDataLayout(module, layout);
}  /* set_module_attributes */

static mlir::Value make_local(a_spike &sp, llvm::StringRef name,
                              mlir::Type type, int64_t align,
                              mlir::Block *scope_block)
/*
Create an alloca named "name" at the best alloca point of scope_block (the
entry block of the innermost scope region, as CIRGenFunction::emitAlloca) and
return its address.  The builder's own insertion point is preserved.
*/
{
  mlir::Value result;
  mlir::OpBuilder::InsertionGuard guard(*sp.builder);

  sp.builder->restoreInsertionPoint(
      cir::CIRBaseBuilderTy::getBestAllocaInsertPoint(scope_block));
  result = sp.builder->createAlloca(unknown_loc(sp),
                                    sp.builder->getPointerTo(type), name,
                                    sp.builder->getAlignmentAttr(align));
  return result;
}  /* make_local */

static mlir::Value load_int(a_spike &sp, mlir::Value addr)
/*
Load a 4-byte-aligned int from addr.
*/
{
  return sp.builder->createLoad(unknown_loc(sp), addr, /*isVolatile=*/false,
                                /*alignment=*/4);
}  /* load_int */

static void store_int(a_spike &sp, mlir::Value value, mlir::Value addr)
/*
Store value into addr with 4-byte alignment.
*/
{
  sp.builder->createStore(unknown_loc(sp), value, addr, /*isVolatile=*/false,
                          /*isNontemporal=*/false,
                          sp.builder->getAlignmentAttr(4));
}  /* store_int */

static mlir::Value int_const(a_spike &sp, int64_t value)
/*
Create a cir.const of type !s32i.
*/
{
  return sp.builder->getConstantInt(unknown_loc(sp), sp.s32, value);
}  /* int_const */

static cir::FuncOp create_function(a_spike &sp, llvm::StringRef name,
                                   cir::FuncType type)
/*
Create a public, dso_local, nothrow definition "name" with its entry block, at
the end of the module.  (CIRGen creates a private declaration first and makes
it public when the body arrives; the end state is the same.)
*/
{
  cir::FuncOp result;
  mlir::OpBuilder::InsertionGuard guard(*sp.builder);

  sp.builder->setInsertionPointToEnd(sp.module.getBody());
  result = cir::FuncOp::create(*sp.builder, unknown_loc(sp), name, type);
  result.setVisibility(mlir::SymbolTable::Visibility::Public);
  result->setAttr(cir::CIRDialect::getNoThrowAttrName(),
                  sp.builder->getUnitAttr());
  set_dso_local(result);
  result.addEntryBlock();
  return result;
}  /* create_function */

static cir::GlobalOp create_global_g(a_spike &sp)
/*
Create "int g = 5;": an external definition with a #cir.int initial value.
*/
{
  cir::GlobalOp result;

  result = sp.builder->createGlobal(
      sp.module, unknown_loc(sp), "g", sp.s32, /*isConstant=*/false,
      cir::GlobalLinkageKind::ExternalLinkage,
      mlir::ptr::MemorySpaceAttrInterface());
  result.setInitialValueAttr(cir::IntAttr::get(sp.s32, 5));
  result.setAlignmentAttr(sp.builder->getI64IntegerAttr(4));
  result.setVisibility(mlir::SymbolTable::Visibility::Public);
  set_dso_local(result);
  return result;
}  /* create_global_g */

static void build_f(a_spike &sp, cir::FuncOp fn)
/*
Fill fn with "return a < b ? a + b : 0;".  The ternary is opened and closed
by hand (OperationState), as the non-recursive emitter will do for TERN_BEGIN
/ TERN_ELSE / TERN_END.
*/
{
  cir::CIRBaseBuilderTy &b = *sp.builder;
  mlir::Location loc = unknown_loc(sp);
  mlir::Block *entry = &fn.getBody().front();
  mlir::Value a_addr;
  mlir::Value b_addr;
  mlir::Value ret_addr;
  mlir::Value cond;
  mlir::Value sum;
  mlir::Operation *ternary;
  mlir::OperationState state(loc, cir::TernaryOp::getOperationName());

  b.setInsertionPointToStart(entry);
  a_addr = make_local(sp, "a", sp.s32, 4, entry);
  b_addr = make_local(sp, "b", sp.s32, 4, entry);
  ret_addr = make_local(sp, "__retval", sp.s32, 4, entry);
  store_int(sp, fn.getArgument(0), a_addr);
  store_int(sp, fn.getArgument(1), b_addr);

  cond = b.createCompare(loc, cir::CmpOpKind::lt, load_int(sp, a_addr),
                         load_int(sp, b_addr));
  state.addOperands(cond);
  state.addTypes(sp.s32);
  state.addRegion();
  state.addRegion();
  ternary = b.create(state);

  b.createBlock(&ternary->getRegion(0));
  sum = b.createNSWAdd(loc, load_int(sp, a_addr), load_int(sp, b_addr));
  b.createYield(loc, mlir::ValueRange{sum});
  b.createBlock(&ternary->getRegion(1));
  b.createYield(loc, mlir::ValueRange{int_const(sp, 0)});
  b.setInsertionPointAfter(ternary);

  store_int(sp, ternary->getResult(0), ret_addr);
  cir::ReturnOp::create(b, loc, mlir::ValueRange{load_int(sp, ret_addr)});
}  /* build_f */

static void build_main(a_spike &sp, cir::FuncOp fn, cir::FuncOp f,
                       cir::GlobalOp g)
/*
Fill fn with the loop of main.  The cir.scope and cir.for are created with
empty callbacks; the regions are then filled by moving the insertion point.
*/
{
  cir::CIRBaseBuilderTy &b = *sp.builder;
  mlir::Location loc = unknown_loc(sp);
  mlir::Block *entry = &fn.getBody().front();
  auto no_op = [](mlir::OpBuilder &, mlir::Location) {};
  mlir::Value ret_addr;
  mlir::Value s_addr;
  mlir::Value i_addr;
  mlir::Value flag;
  mlir::Value call;
  mlir::Value result;
  cir::ScopeOp scope;
  cir::ForOp loop;
  mlir::Block *scope_block;

  b.setInsertionPointToStart(entry);
  ret_addr = make_local(sp, "__retval", sp.s32, 4, entry);
  s_addr = make_local(sp, "s", sp.s32, 4, entry);
  store_int(sp, int_const(sp, 0), s_addr);

  /* for (int i = 0; i < g; ++i) s += f(i, 3); */
  scope = cir::ScopeOp::create(b, loc, no_op);
  scope_block = &scope.getScopeRegion().front();
  b.setInsertionPointToStart(scope_block);
  i_addr = make_local(sp, "i", sp.s32, 4, scope_block);
  store_int(sp, int_const(sp, 0), i_addr);
  loop = b.createFor(loc, no_op, no_op, no_op);

  b.setInsertionPointToStart(&loop.getCond().front());
  flag = b.createCompare(loc, cir::CmpOpKind::lt, load_int(sp, i_addr),
                         load_int(sp, b.createGetGlobal(g)));
  b.createCondition(flag);

  b.setInsertionPointToStart(&loop.getBody().front());
  call = b.createCallOp(loc, f, mlir::ValueRange{load_int(sp, i_addr),
                                                 int_const(sp, 3)})
             .getResult();
  store_int(sp, b.createNSWAdd(loc, load_int(sp, s_addr), call), s_addr);
  b.createYield(loc);

  b.setInsertionPointToStart(&loop.getStep().front());
  store_int(sp, b.createInc(loc, load_int(sp, i_addr), /*nsw=*/true), i_addr);
  b.createYield(loc);

  b.setInsertionPointAfter(loop);
  b.createYield(loc);           /* terminate the cir.scope */
  b.setInsertionPointAfter(scope);

  /* return s == 12 ? 0 : 1;  (a cheap ternary is a cir.select) */
  flag = b.createCompare(loc, cir::CmpOpKind::eq, load_int(sp, s_addr),
                         int_const(sp, 12));
  result = b.createSelect(loc, flag, int_const(sp, 0), int_const(sp, 1));
  store_int(sp, result, ret_addr);
  cir::ReturnOp::create(b, loc, mlir::ValueRange{load_int(sp, ret_addr)});
}  /* build_main */

static void build_module(a_spike &sp, const llvm::Triple &triple,
                         const llvm::DataLayout &layout)
/*
Build the whole module for the spike program.
*/
{
  cir::GlobalOp g;
  cir::FuncOp   f;
  cir::FuncOp   main_fn;
  cir::FuncType f_type = cir::FuncType::get({sp.s32, sp.s32}, sp.s32);
  cir::FuncType main_type = cir::FuncType::get({}, sp.s32);

  set_module_attributes(sp, triple, layout);
  g = create_global_g(sp);
  f = create_function(sp, "f", f_type);
  build_f(sp, f);
  main_fn = create_function(sp, "main", main_type);
  build_main(sp, main_fn, f, g);
}  /* build_module */

static bool emit_object(llvm::Module &module, llvm::TargetMachine &machine,
                        const a_spike_options &options, std::string &message)
/*
Run the LLVM optimization pipeline (if -O) and the code generator, writing an
object (or assembly) file.  Return TRUE on success; otherwise set message.
*/
{
  bool                  result = true;
  std::error_code       error;
  llvm::raw_fd_ostream  out(options.output, error, llvm::sys::fs::OF_None);

  if (error) {
    message = "cannot open " + options.output + ": " + error.message();
    result = false;
  } else {
    if (options.opt_level > 0) {
      llvm::LoopAnalysisManager     lam;
      llvm::FunctionAnalysisManager fam;
      llvm::CGSCCAnalysisManager    cgam;
      llvm::ModuleAnalysisManager   mam;
      llvm::PassBuilder             builder(&machine);
      llvm::OptimizationLevel       level = options.opt_level == 1
                                                ? llvm::OptimizationLevel::O1
                                                : llvm::OptimizationLevel::O2;

      builder.registerModuleAnalyses(mam);
      builder.registerCGSCCAnalyses(cgam);
      builder.registerFunctionAnalyses(fam);
      builder.registerLoopAnalyses(lam);
      builder.crossRegisterProxies(lam, fam, cgam, mam);
      builder.buildPerModuleDefaultPipeline(level).run(module, mam);
    }
    llvm::legacy::PassManager codegen;
    llvm::CodeGenFileType kind = options.emit_asm
                                     ? llvm::CodeGenFileType::AssemblyFile
                                     : llvm::CodeGenFileType::ObjectFile;
    if (machine.addPassesToEmitFile(codegen, out, nullptr, kind)) {
      message = "target cannot emit this file type";
      result = false;
    } else {
      codegen.run(module);
    }
  }
  return result;
}  /* emit_object */

static void run_spike(a_spike_job *job)
/*
Worker thread body: build the module, run the CIR passes, lower to LLVM IR
and emit the object file.  Sets job->status (0 on success) and job->message.
*/
{
  const a_spike_options &options = *job->options;
  llvm::Triple triple(options.triple);
  std::string error;
  const llvm::Target *target = nullptr;
  std::unique_ptr<llvm::TargetMachine> machine;

  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();
  target = llvm::TargetRegistry::lookupTarget(triple, error);
  if (!target) {
    job->message = "no target for " + options.triple + ": " + error;
    job->status = 1;
  } else {
    machine.reset(target->createTargetMachine(
        triple, "generic", "", llvm::TargetOptions(), llvm::Reloc::PIC_,
        std::nullopt, llvm::CodeGenOptLevel::Default));
  }

  if (machine) {
    mlir::MLIRContext context(mlir::MLIRContext::Threading::DISABLED);
    cir::registerAllDialects(context);
    /* The LLVM dialect must be loaded too: setMLIRDataLayout builds
       !llvm.ptr keys. */
    context.loadDialect<mlir::DLTIDialect, cir::CIRDialect,
                        mlir::LLVM::LLVMDialect>();
    cir::CIRBaseBuilderTy builder(context);
    a_spike sp = {&context, &builder, nullptr,
                  cir::IntType::get(&context, 32, /*isSigned=*/true)};
    sp.module = mlir::ModuleOp::create(builder.getUnknownLoc());
    mlir::OwningOpRef<mlir::ModuleOp> owned(sp.module);
    llvm::LLVMContext llvm_context;
    std::unique_ptr<llvm::Module> lowered;

    build_module(sp, triple, machine->createDataLayout());
    if (options.print_cir)
      sp.module->print(llvm::outs());
    if (mlir::failed(mlir::verify(sp.module))) {
      job->message = "CIR verification failed before the passes";
      job->status = 2;
    } else if (mlir::failed(cir::runCIRToCIRPasses(
                   sp.module, context, /*enableVerifier=*/true,
                   /*enableIdiomRecognizer=*/false,
                   /*enableCIRSimplify=*/options.opt_level > 0,
                   /*enableLibOpt=*/false, /*libOptOptions=*/"",
                   /*enableCallConvLowering=*/true))) {
      job->message = "cir::runCIRToCIRPasses failed";
      job->status = 3;
    } else {
      if (options.print_cir_post)
        sp.module->print(llvm::outs());
      lowered = cir::direct::lowerDirectlyFromCIRToLLVMIR(
          sp.module, llvm_context, /*enableOpenMP=*/false);
      if (!lowered) {
        job->message = "lowerDirectlyFromCIRToLLVMIR returned null";
        job->status = 4;
      } else {
        lowered->setTargetTriple(triple);
        lowered->setDataLayout(machine->createDataLayout());
        if (options.print_llvm)
          lowered->print(llvm::outs(), nullptr);
        if (!emit_object(*lowered, *machine, options, job->message))
          job->status = 5;
      }
    }
  } else if (job->status == 0) {
    job->message = "cannot create a target machine for " + options.triple;
    job->status = 1;
  }
}  /* run_spike */

static bool parse_options(int argc, char **argv, a_spike_options &options)
/*
Parse argv into options; return FALSE (after a message) on a bad option.
*/
{
  bool result = true;

  options.print_cir = false;
  options.print_cir_post = false;
  options.print_llvm = false;
  options.emit_asm = false;
  options.opt_level = 0;
  options.stack_mb = 256;
  options.triple = "x86_64-unknown-linux-gnu";
  options.output = "spike.o";
  for (int i = 1; i < argc && result; ++i) {
    const char *arg = argv[i];
    if (!std::strcmp(arg, "-print-cir")) {
      options.print_cir = true;
    } else if (!std::strcmp(arg, "-print-cir-post")) {
      options.print_cir_post = true;
    } else if (!std::strcmp(arg, "-print-llvm")) {
      options.print_llvm = true;
    } else if (!std::strcmp(arg, "-S")) {
      options.emit_asm = true;
    } else if (!std::strncmp(arg, "-O", 2) && arg[2] >= '0' && arg[2] <= '2') {
      options.opt_level = arg[2] - '0';
    } else if (!std::strcmp(arg, "-o") && i + 1 < argc) {
      options.output = argv[++i];
    } else if (!std::strcmp(arg, "-stack-mb") && i + 1 < argc) {
      options.stack_mb = static_cast<unsigned>(std::atoi(argv[++i]));
    } else if (!std::strcmp(arg, "-triple") && i + 1 < argc) {
      options.triple = argv[++i];
    } else {
      llvm::errs() << "usage: spike [-print-cir] [-print-cir-post] "
                      "[-print-llvm] [-S] [-O0|-O1|-O2] [-o file] "
                      "[-stack-mb N] [-triple T]\n";
      result = false;
    }
  }
  return result;
}  /* parse_options */

}  /* anonymous namespace */

int main(int argc, char **argv)
/*
Parse the options, run the spike on a big-stack worker thread, and return 0 on
success, 1 for a usage error, or the job status otherwise.
*/
{
  int            result = 1;
  a_spike_options options;

  if (parse_options(argc, argv, options)) {
    a_spike_job job = {&options, 0, std::string()};
    std::optional<unsigned> stack_bytes(options.stack_mb * 1024u * 1024u);
    llvm::thread worker(stack_bytes, run_spike, &job);

    worker.join();
    if (job.status != 0)
      llvm::errs() << "spike: " << job.message << "\n";
    result = job.status;
  }
  return result;
}  /* main */
