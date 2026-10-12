# Upstream ClangIR drift since the LLVM pin (for M4)

Written by cir-config (K8), 2026-10-12.

## Method

- Pin: `cir_src/LLVM_REVISION` = `894bfff5ca9fbe8ffc4171d5f5a4e68d11537f6d`
  (committer date 2026-10-09).
- Upstream HEAD: `2e0d9e12b36e853d9b108c4da09509cd54d1f3f9`, 2026-10-12 13:58
  UTC ("[SPIR-V] Lower llvm.minimum/maximum instead of selecting fmin/fmax").
- 264 commits lie between the two. The history was fetched into a separate
  blobless bare repository, `~/edg-cir-config/llvm-drift` on the box (12 MB, ref
  `refs/remotes/upstream/main`, `git fetch --shallow-since=2026-10-08
  --filter=blob:none`). The `llvm-project` clone that the image build uses was
  not touched.
- Command: `git log 894bfff5..upstream/main -- clang/lib/CIR
  clang/include/clang/CIR clang/test/CIR`, then `git show --stat` and
  `git diff --stat 894bfff5 upstream/main -- clang/lib/CIR clang/include/clang/CIR`.
- Box disk: 129 GB free before, 120 GB after. The
  drift repository is the only thing this item adds (12 MB); the rest of the
  change (about 8 GB) is not from this item.
- Only the paths above were examined. MLIR and LLVM changes between the two
  revisions (151 files under `mlir/` and `llvm/` differ) were not reviewed.

## The ten commits that touch CIR

There are only ten, all from 2026-10-09 to 2026-10-12.

| Commit | Date | Summary | Area |
| --- | --- | --- | --- |
| `0baa8d99` | 10-12 | Matrix-Insert and lowering for lvalue subscript (#230600) | matrix |
| `bb69a27f` | 10-12 | `matrix-subtraction` lowering (#230536) | matrix |
| `533bab2e` | 10-12 | AMDGCN raw buffer store format builtins (#223112) | AMDGPU |
| `71e68dd7` | 10-12 | AMDGCN raw ptr buffer atomic builtins (#223137) | AMDGPU |
| `3261d9d9` | 10-12 | HIP/SPIR-V: constant globals in addrspace(1) (#230813) | HIP |
| `efef068f` | 10-12 | AMDGCN `grid_size` builtins (#230798) | AMDGPU |
| `37c927a7` | 10-11 | Missing coroutine region successors (#230466) | coroutines |
| `fa70420c` | 10-09 | Bytecode encodings for attributes (#229586) | bytecode |
| `6bcb0a8b` | 10-09 | NFC: missing NYI handling for some x86 builtins (#230609) | builtins |
| `765392a6` | 10-09 | Pass x87 long double vectors on x86_64 (#230302) | CallConvLowering |

The remaining commits that mention CIR are SVE intrinsic tests
(`c288f9fa`, `452c9b52`, `b1767ea3`, `6115c6d6`, `85b28b1f`, `d7b282cb`,
`6d14e849`), which touch no CIR library code.

## By M4 area

| Area | Commits after the pin | Notes |
| --- | --- | --- |
| EH: FlattenCFG, EHABILowering, `cir.try`/throw/catch, cleanups, guard abort, catch-copy thunks | none | `FlattenCFG.cpp`, `EHABILowering.cpp`, `CIRGenException.cpp`, `CIRGenCleanup.cpp` are identical at the pin and at HEAD. `37c927a7` only changes the successor lists of the coroutine ops. |
| Vtables, VTT, thunks | none | `CIRGenVTables.cpp`, `CIRGenItaniumCXXABI.cpp`, `CIRGenClass.cpp` unchanged. `fa70420c` adds a `vtable-blockaddr` bytecode test, no vtable code. |
| RTTI, `dyn_cast`, `get_type_info` | none | |
| Member pointers, `#cir.method` | `fa70420c` (indirectly) | adds a builder overload for `DataMemberAttr` that reuses a `member_path`, and bytecode encodings of the attributes. `#cir.method` still has only `symbol` and `vtable_offset`: no this-adjustment. |
| CallConvLowering | `765392a6` | x87 long double vector arguments on x86_64 in `CallConvLoweringPass.cpp`. The only file of `Dialect/Transforms` that differs. Nothing about `cir.try_call`. |
| Other | the rest | matrix, AMDGPU/HIP/SPIR-V, coroutine successors, bytecode, builtins: not used by M4. |

## Would a bump fix a known M4 gap?

No. Each gap below was checked in the pin tree and against the diff to HEAD.

| Known gap | At the pin | At HEAD | A bump fixes it? |
| --- | --- | --- | --- |
| Multi-block catch-copy thunks | `EHABILowering.cpp:816` and `:886`: "multi-block catch-copy thunks are NYI" | file unchanged | no: single-block thunks in the library (the workaround in the plan) |
| `#cir.method` with a non-zero this-adjustment | `CIRAttrs.td`: `MethodAttr` has `type`, `symbol`, `vtable_offset` only; `LowerItaniumCXXABI.cpp` reads `adj` only from dynamic method values | `MethodAttr` parameters unchanged (the diff to `CIRAttrs.td` is a `DataMemberAttr` builder) | no: a workaround in the library is needed (not designed here; cir-lib decides, for example a constant aggregate for non-zero adjustments) |
| Cleanup regions that do not end in `cir.yield` | `FlattenCFG.cpp` (cleanup scope lowering walks yield-terminated exit blocks, lines ~960-1030, ~1360-1550) | file unchanged | no: the library must end such regions with a yield |
| `cir.try_call` in CallConvLowering | `CIRABIRewriteContext.cpp:1723`: "TryCallOp not yet implemented in CallConvLowering" | `CIRABIRewriteContext.cpp` unchanged; `765392a6` changes only `CallConvLoweringPass.cpp` for x87 vectors | no: a workaround in the library is needed (not designed here; cir-lib decides, for example by keeping such calls out of `cir.try_call`) |
| `__cxa_free_exception` (freeing the exception object when the copy in a throw fails) | no occurrence in `lib/CIR` or `include/clang/CIR` | none added | no: the library emits the call itself |

Conclusion: **no pin bump is needed or helpful for M4a-M4.** The drift is
10 commits in 3 days, none in an M4 area. Re-run the check before M4 starts and
again at the gate (`git fetch` in `~/edg-cir-config/llvm-drift`, then the log
command above).

## Re-running

```
ssh mborland@192.168.1.167
cd ~/edg-cir-config/llvm-drift
git fetch -q --shallow-since=2026-10-08 --filter=blob:none upstream \
    main:refs/remotes/upstream/main
git log --format='%h %ad %s' --date=short \
    894bfff5ca9fbe8ffc4171d5f5a4e68d11537f6d..upstream/main -- \
    clang/lib/CIR clang/include/clang/CIR clang/test/CIR
```
