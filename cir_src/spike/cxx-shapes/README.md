# C++ CIR shape spikes (M3a)

Sources and helper scripts behind `cir_src/doc/research/cxx_cir_shapes.md`.
Everything runs inside the cir-env image (toolchain in `/edg/llvm-cir/bin`,
`g++` for linking); none of it is part of the library or its tests.

- `clang-cir-shapes.sh f.cpp` - the pin's `clang -fclangir -emit-cir` with
  and without the CIR pipeline (`f.raw.cir` is plain CIRGen, `f.cir` has the
  passes run); `.txt` copies have the locations stripped.
- `cir-to-obj.sh x.cir` - runs a (hand-written) `.cir` through the pin's
  passes in the order of `runCIRToCIRPasses` plus the pre-lowering passes
  (`--cir-canonicalize --cir-target-lowering --cir-cxxabi-lowering
  --cir-lowering-prepare --cir-call-conv-lowering=target=x86_64`, then
  `--cir-hoist-allocas --cir-flatten-cfg --cir-eh-abi-lowering
  --cir-goto-solver`), `cir-translate --cir-to-llvmir`, and `clang -c`
  (the module's data layout string is replaced by the x86-64 one).
- `s1c/strip.sh in.cir out.cir` - drop locations so the CIR can be edited.
- `sN/` - the snippets of spike N (`lib.cpp` supplies the address-logging
  ctor/dtor, `main*.cpp` the drivers). A hand variant is the clang raw CIR
  with one function body replaced; the replaced bodies are quoted in the
  report.
