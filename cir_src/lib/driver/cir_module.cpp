/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
cir_module.cpp -- build the MLIR module and its mandatory attributes from
F_HEADER (HDR_TARGET, HDR_LANG, HDR_TYPE_ROLE).  Runs on the calling thread;
it only creates attributes and an empty module (no recursion, no verifier).
*/

#include <mutex>

#include "clang/Basic/LangOptions.h"
#include "clang/CIR/CIRDataLayoutSpec.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/InitAllDialects.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Target/TargetOptions.h"
#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

#include "CIRCompat.h"
#include "cir_module.h"

extern "C" {
void LLVMInitializeX86TargetInfo();
void LLVMInitializeX86Target();
void LLVMInitializeX86TargetMC();
void LLVMInitializeX86AsmPrinter();
}

namespace edg_cir {

namespace {

void initialize_targets()
/*
Initialize the x86 back end once per process.
*/
{
  static std::once_flag once;

  std::call_once(once, [] {
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();
  });
}  /* initialize_targets */

}  /* anonymous namespace */

std::unique_ptr<a_cir_module>
create_cir_module(an_edg_cir_session &session, an_edg_cir_status &status)
/*
Create the module for the facts in session.header.  On failure set status,
report the problem through session.fail and return nullptr.
*/
{
  const a_header_facts &facts = session.header;
  std::unique_ptr<a_cir_module> result;
  std::string triple_text = session.have_triple_override
                              ? session.triple_override : facts.triple;
  llvm::Triple triple(triple_text);

  status = EDG_CIR_OK;
  if (triple.getArch() != llvm::Triple::x86_64 ||
      triple.getOS() != llvm::Triple::Linux) {
    status = session.fail(EDG_CIR_ERROR_UNSUPPORTED, edc_unsupported,
                          "target '" + triple_text + "' is not supported: "
                          "the CIR back end handles x86_64 Linux only");
  } else if (facts.ptr_bits != 64 ||
             (facts.target_flags & ECIL_TARGET_BIG_ENDIAN)) {
    status = session.fail(EDG_CIR_ERROR_UNSUPPORTED, edc_unsupported,
                          "only 64-bit little-endian targets are supported");
  }
  if (status == EDG_CIR_OK) {
    std::string error;
    const llvm::Target *target;

    initialize_targets();
    target = llvm::TargetRegistry::lookupTarget(triple, error);
    if (target == nullptr) {
      status = session.fail(EDG_CIR_ERROR_INTERNAL, edc_internal,
                            "no LLVM target for " + triple_text + ": " +
                            error);
    } else {
      llvm::CodeGenOptLevel level = session.opt_level <= 0
          ? llvm::CodeGenOptLevel::None
          : session.opt_level == 1 ? llvm::CodeGenOptLevel::Less
          : session.opt_level == 2 ? llvm::CodeGenOptLevel::Default
                                   : llvm::CodeGenOptLevel::Aggressive;
      llvm::TargetOptions target_options;
      /* Assembly text must be GNU-as compatible: LLVM 24 emits .prefalign
         only when it assumes the integrated assembler will read the text,
         so for --emit=asm pretend it will not (plain .p2align results). */
      if (session.output_kind == EDG_CIR_OUTPUT_ASM)
        target_options.MCOptions.DisableIntegratedAS = true;
      result.reset(new a_cir_module());
      result->triple = triple;
      result->machine.reset(target->createTargetMachine(
          triple, "x86-64", "", target_options, llvm::Reloc::PIC_,
          std::nullopt, level));
      if (!result->machine) {
        status = session.fail(EDG_CIR_ERROR_INTERNAL, edc_internal,
                              "cannot create a target machine for " +
                              triple_text);
        result.reset();
      }
    }
  }
  if (result) {
    if (facts.have_datalayout) {
      llvm::Expected<llvm::DataLayout> parsed =
                                  llvm::DataLayout::parse(facts.datalayout);
      if (!parsed) {
        std::string message = llvm::toString(parsed.takeError());
        status = session.fail(EDG_CIR_ERROR_FORMAT, edc_format,
                              "bad data layout string: " + message);
        result.reset();
      } else {
        result->layout = *parsed;
      }
    } else {
      result->layout = result->machine->createDataLayout();
    }
  }
  if (result) {
    mlir::MLIRContext *ctx = &result->context;
    mlir::Builder b(ctx);
    bool cxx = facts.language == elang_cxx;
    auto abi_compat = static_cast<int32_t>(
                                  clang::LangOptions::ClangABI::Latest);

    cir::registerAllDialects(*ctx);
    ctx->loadDialect<mlir::DLTIDialect, cir::CIRDialect,
                     mlir::LLVM::LLVMDialect>();
    result->module = mlir::ModuleOp::create(mlir::UnknownLoc::get(ctx));
    mlir::ModuleOp module = result->module.get();
    if (!facts.source_file.empty())
      module.setSymName(facts.source_file);
    module->setAttr(cir::CIRDialect::getTripleAttrName(),
                    b.getStringAttr(result->triple.str()));
    module->setAttr(cir::CIRDialect::getTargetABIAttrName(),
                    b.getStringAttr(facts.target_abi));
    module->setAttr(cir::CIRDialect::getSourceLanguageAttrName(),
                    cir::SourceLanguageAttr::get(
                        ctx, cxx ? cir::SourceLanguage::CXX
                                 : cir::SourceLanguage::C));
    module->setAttr(
        cir::CIRDialect::getLoweringLangOptionsAttrName(),
        edg_cir::compat::lowering_lang_options(
            ctx, (facts.lang_flags & ECIL_LANG_EXCEPTIONS) != 0,
            (facts.lang_flags & ECIL_LANG_THREADSAFE_STATICS) != 0,
            abi_compat));
    module->setAttr(cir::CIRDialect::getDefaultTlsModelAttrName(),
                    cir::TLSModelAttr::get(ctx,
                                           cir::TLSModel::GeneralDynamic));
    module->setAttr(cir::CIRDialect::getModuleLevelAsmAttrName(),
                    b.getArrayAttr({}));
    if (session.opt_level > 0)
      module->setAttr(cir::CIRDialect::getOptInfoAttrName(),
                      cir::OptInfoAttr::get(ctx, session.opt_level, 0));
    cir::setMLIRDataLayout(module, result->layout);
  }
  return result;
}  /* create_cir_module */

void set_module_type_widths(an_edg_cir_session &session)
/*
Apply the HDR_TYPE_ROLE size_t and int roles to cir.size_type_width and
cir.int_type_width (when the role and its integer type are known).
*/
{
  if (session.module) {
    mlir::ModuleOp module = session.module->module.get();
    mlir::Builder b(&session.module->context);

    for (const auto &entry : session.type_roles) {
      const ecil::a_gid_entry *type =
          session.reader->ids().find_gid(entry.second);
      bool is_int = type != nullptr && type->kind == ecil::gk_int;
      if (entry.first == etr_va_list) {
        /* The va_list role names an array type, not an integer type. */
      } else if (!is_int) {
        session.fail(EDG_CIR_ERROR_FORMAT, edc_format,
                     "HDR_TYPE_ROLE names a type that is not an integer "
                     "type introduced earlier");
      } else if (entry.first == etr_size_t) {
        module->setAttr(cir::CIRDialect::getSizeTypeWidthAttrName(),
                        b.getI32IntegerAttr(static_cast<int32_t>(
                                                         type->extent)));
      } else if (entry.first == etr_int) {
        module->setAttr(cir::CIRDialect::getIntTypeWidthAttrName(),
                        b.getI32IntegerAttr(static_cast<int32_t>(
                                                         type->extent)));
      }
    }
  }
}  /* set_module_type_widths */

}  /* namespace edg_cir */
