# CIR toolchain spike

Proves, on the x86-64 box in the `cir-env` image, that a program using only the
public ClangIR C++ API can: build a CIR module by hand (non-recursively), set
the module attributes, run `cir::runCIRToCIRPasses`,
`cir::direct::lowerDirectlyFromCIRToLLVMIR` and the LLVM code generator on a
big-stack worker thread, and produce a linkable object.  It validates the CMake
link set, the mandatory module attributes and the worker-thread setup that
`cir_src` will depend on (see `../doc/research/cir_api_inventory.md`).

The program it builds:

```c
int g = 5;
int f(int a, int b) { return a < b ? a + b : 0; }
int main(void) {
  int s = 0;
  for (int i = 0; i < g; ++i) s += f(i, 3);
  return s == 12 ? 0 : 1;
}
```

`s = f(0,3)+f(1,3)+f(2,3)+f(3,3)+f(4,3) = 3+4+5+0+0 = 12`, so the linked program
must exit with status 0 (a wrong constant would give status 1).

Status: **built and run on the box, passes** (2026-10-09, image
`edgcpp/cir-env:latest`, toolchain LLVM 24.0.0git at the pinned SHA).  Results
and the fixes needed are in "Spike results" below.

## Build and run (on the box)

Scripts used (run inside the container, cwd `/edg/workspace`): `box_run.sh` (configure +
build), `box_test.sh` (run, link, run, -S, -O2, ulimit, clang comparison).  The pruned
`/edg/llvm-cir` prefix fails `find_package(LLVM)` (LLVMExports.cmake references files
that were pruned), so configure against the full install mounted at `/full`
(`SPIKE_PREFIX=/full`).

Prerequisite: the toolchain prefix installed by `dev_tools/bin/edg-build-llvm-cir`
(in the `cir-env` image: `/edg/llvm-cir`, exported as `EDG_LLVM_CIR_PREFIX`).

```sh
cd ~/edg-cir-lib                      # box tree, rsynced from the Mac tree
edg-exec --img cir-env -- bash -c '
  set -e
  test -f "$EDG_LLVM_CIR_PREFIX/include/clang/CIR/Dialect/IR/CIROps.h.inc"
  cmake -S cir_src/spike -B build/cir-spike -G Ninja \
        -DEDG_LLVM_CIR_PREFIX="$EDG_LLVM_CIR_PREFIX"
  ninja -C build/cir-spike spike
  # CIR text before the passes, then the object
  build/cir-spike/spike -print-cir -o build/cir-spike/spike.o
  gcc build/cir-spike/spike.o -o build/cir-spike/spike_bin
  build/cir-spike/spike_bin; echo "exit status: $?"   # expect 0
'
```

Equivalent with docker directly (as in `cir-lib.md`):
`sg docker -c "docker run --rm -v \$PWD:/w -w /w <cir-env image> bash -c '...'"`.

Options of `spike`:

| Option | Effect |
| --- | --- |
| `-print-cir` | print the module before the passes (generic CIR text) |
| `-print-cir-post` | print the module after `runCIRToCIRPasses` |
| `-print-llvm` | print the LLVM IR after lowering |
| `-S` | emit assembly instead of an object |
| `-O0/-O1/-O2` | LLVM `PassBuilder` pipeline (and `enableCIRSimplify` for `-O1+`) |
| `-o FILE` | output (default `spike.o`) |
| `-stack-mb N` | worker stack size (default 256) |
| `-triple T` | default `x86_64-unknown-linux-gnu` (CallConvLowering only handles x86_64 non-Windows) |

Exit status: 0 ok, 1 usage/target error, 2 verify failed, 3 CIR passes failed,
4 lowering returned null, 5 code generation failed.

## Checks to record (spike gate)

1. Build log: the resolved link line
   (`ninja -C build/cir-spike -t commands spike | tail -1`) and
   `ldd build/cir-spike/spike` (expect only libc/libstdc++/libm/libgcc, no LLVM
   shared libs unless cir-config built dylibs).
2. `-print-cir` output; compare with clang's raw CIRGen for the same C source to
   check shapes:
   `$EDG_LLVM_CIR_PREFIX/bin/clang -cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -clangir-disable-passes spike.c -o -`.
3. Run result: `spike_bin` exits 0.
4. Stack test: `ulimit -s 256; build/cir-spike/spike -stack-mb 64 -o /tmp/x.o` still
   works (the worker, not the main thread, carries the recursion).
5. Variants: `-O2`, `-S`, and `-triple aarch64-unknown-linux-gnu` (expect the CIR
   passes to leave signatures unchanged and the LLVM target lookup to succeed only
   if AArch64 was enabled in the toolchain; the front-end must refuse that
   triple itself).
6. Delete the `cir.target_abi` or `dlti.dl_spec` attribute in
   `set_module_attributes` and confirm the diagnostics (`missing 'cir.target_abi'
   attribute`, `CallConvLowering requires a DataLayout`) -- proves which
   attributes are mandatory.

## What the code demonstrates

* `set_module_attributes`: the full CIRGen attribute set, including
  `cir::setMLIRDataLayout` from the `TargetMachine` data layout and
  `clang_abi_compat = ClangABI::Latest`.
* `create_function` / `create_global_g`: external definitions with the visibility
  CIRGen uses (public for external linkage), `dso_local`, `nothrow`, alignment.
* `build_f`: `cir.ternary` through `OperationState` (builder callbacks would infer a
  void result), with explicit per-region blocks and yields.
* `build_main`: `cir.scope` and `cir.for` opened with empty callbacks and filled by
  moving the insertion point; `cir.get_global`, `cir.call`, `cir.cmp`, `cir.select`,
  `cir.inc`, `cir.add nsw`.
* Allocas are placed at the best alloca point of the owning scope region
  (`getBestAllocaInsertPoint`), as `CIRGenFunction::emitAlloca`; `HoistAllocas`
  moves them to the entry block during lowering.
* `MLIRContext::Threading::DISABLED` plus `llvm::thread` with an explicit
  stack size (`std::optional<unsigned>` is required).

## Spike results (2026-10-09)

* Build: clean on the first compile of `spike.cpp` -- **no API spelling changes were
  needed** (every `create`/builder signature in the inventory was right, including
  `ForOp` via `createFor`, `ScopeOp` with no-op callback, `TernaryOp` via
  `OperationState`, `setDSOLocal` through `CIRGlobalValueInterface`,
  `llvm::thread(std::optional<unsigned>, ...)`).
* Two spike fixes, both **link/setup findings** (not spelling):
  1. `mlir::LLVM::LLVMDialect` must be **loaded** (not just registered) before
     `cir::setMLIRDataLayout`; otherwise `LLVM ERROR: can't create type
     'mlir::LLVM::LLVMPointerType' because storage uniquer isn't initialized`.
  2. `InitializeAllTargets()` references every target the toolchain was built with
     (X86;AArch64), so `aarch64{codegen,asmparser,desc,info}` must be linked too (or
     use `InitializeX86*` explicitly).
* `-print-cir` verifies; `runCIRToCIRPasses` + lowering + codegen succeed; `spike.o`
  links with gcc and **the program exits 0**; `-S`, `-O2 -S`, `-print-llvm`,
  `-print-cir-post` all succeed.  `ulimit -s 256` with `-stack-mb 64` succeeds.
* Binary: 448 MB unstripped (RelWithDebInfo + `-gline-tables-only` archives), 127 MB
  stripped; `ldd`: only libstdc++, libm, libgcc_s, libc (all LLVM/MLIR/clang
  statically linked).  The link line pulled in clangCIR, clangCodeGenUtils,
  clangAST/Lex/Basic, MLIRCIRTargetLowering, MLIRABI, LLVMABI, MLIROpenMP/OpenACC,
  GPU/Math/etc. dialect libs, LLVMPasses/ipo/Vectorize/GlobalISel...
* Output quality: LLVM IR from `-print-llvm` is plain -O0 shape (alloca/store/load, a
  `br` to a fresh block per scope/region, phi for the ternary) with **no function
  attributes** (no `nounwind`, `noinline`, `uwtable`, `noundef`, TBAA, frame-pointer or
  target-cpu), `target datalayout/triple` set, and a `Debug Info Version` module flag.
  `HoistAllocas` put all allocas in the entry block.  `-S` (CodeGenOptLevel::Default)
  gives correct x86-64.  The missing attributes are things clang's CIRGen adds
  (`nothrow` is mapped to nothing here; clang also sets `no_inline` at -O0, `uwtable`,
  `cir.target-cpu/features/tune-cpu`, arg `llvm.noundef`) -- optional for correctness.
* Comparison with `clang -fclangir -emit-cir -Xclang -clangir-disable-passes` on
  `spike.c`: same structure (global, alloca/store of params, `__retval`, `cir.scope` ->
  `cir.for` with cond/body/step, `cir.call`, `cir.ternary` in `f`, `cir.select` in
  `main`).  Differences: (a) clang emits `alloca ... init` and `llvm.noundef` arg attrs,
  `no_inline`, `uwtable = #cir.uwtable<async>`, `cir.target-cpu/target-features/
  tune-cpu`, source locations; (b) clang computes `a<b` as `cmp; bool_to_int; int_to_bool`
  (redundant round trip, folded later) where we use the `cmp` bool directly; (c) clang
  emits the first `__retval` store of 0 for main's implicit path and the `cir.alloca "i"`
  with `init`; (d) operand evaluation order: clang loads `a` then `b`, our hand code
  loaded `b` first in `f` (harmless); (e) clang's module symbol name is the absolute
  path and has `cir.sdk_version` only on Darwin.  The module attribute set matches ours
  exactly (`cir.default_tls_model`, `cir.lang`, `cir.lowering_lang_options` with
  `clang_abi_compat = 16`, `cir.module_asm = []`, `cir.size_type_width`, `cir.target_abi
  = ""`, `dlti.dl_spec`) -- **confirms the section 7 table**.  The data layout `dl_spec`
  printed by clang equals ours.

## Re-run against the shipping image (gate R condition)

Re-run in `edgcpp/cir-env:latest` against `/edg/llvm-cir` (no `SPIKE_PREFIX`), from a
clean `build/cir-spike`: configure, build and `box_test.sh` all pass; output
(CIR, LLVM IR, asm) is byte-identical to the `/full` run; binary 447853232 bytes,
same `ldd`.  Check 5: `-triple aarch64-unknown-linux-gnu` (`box_aarch64.sh`) exits 0
and emits an object: `CallConvLowering` leaves signatures unchanged (no AArch64 ABI
lowering yet), the LLVM AArch64 backend happily codegens the unlowered x86-style
signatures.  So nothing refuses the triple downstream; the library must reject
non-x86-64 triples itself.
