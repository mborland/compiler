# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# lit configuration of the cir_src tests (the tools come from the build tree).
import os
import lit.formats

config.name = "edg-cir"
config.test_format = lit.formats.ShTest(execute_external=False)
config.suffixes = [".test"]
config.test_source_root = os.path.join(config.edg_cir_source_dir, "test")
config.excludes = ["format", "writer", "driver", "Inputs"]
config.test_exec_root = os.path.join(config.edg_cir_binary_dir, "test", "lit")

bin_dir = os.path.join(config.edg_cir_binary_dir, "bin")
tool_dir = os.path.join(config.edg_cir_binary_dir, "tools")
llvm_bin = os.path.join(config.edg_llvm_cir_prefix, "bin")

config.substitutions.append(("%ecil-asm", os.path.join(
    config.edg_cir_binary_dir, "tools", "ecil-asm", "ecil-asm")))
config.substitutions.append(("%ecil-dump", os.path.join(
    config.edg_cir_binary_dir, "tools", "ecil-dump", "ecil-dump")))
config.substitutions.append(("%edg-cir", os.path.join(bin_dir, "edg-cir")))
config.substitutions.append(("%FileCheck", os.path.join(llvm_bin, "FileCheck")))
config.substitutions.append(("%not", os.path.join(llvm_bin, "not")))
config.environment["LD_LIBRARY_PATH"] = os.path.join(
    config.edg_cir_binary_dir, "lib")
config.substitutions.append(("%clang", os.path.join(llvm_bin, "clang")))
config.substitutions.append(("%rtlibdir", os.path.join(
    config.edg_cir_binary_dir, "lib")))
config.substitutions.append(("%python", "python3"))
config.substitutions.append(("%scripts", os.path.join(
    config.edg_cir_source_dir, "scripts")))
