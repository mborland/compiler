# Writer test tools

- `ecil_dump.py` — minimal ECIL decoder (the real tool is `ecil-dump`).
- `stackcheck.py` — value-stack discipline checker for a decoded stream:
  `ecil_dump.py f.ecil | stackcheck.py name`.
- `sweep_corpus.py` — lists the M2 corpus (rp/lp GCC tests) with options.
- `sweep.sh OUTDIR [CORPUS]` — runs the corpus through `eccp --cpfe_only --c`
  with cpfe-cir (twice, for determinism), validates each stream with
  `ecil-dump`, runs `stackcheck.py`; writes `OUTDIR/results.tsv` (path, rc1,
  rc2, same, invalid, validator message, stackcheck) plus `*.err`. Run it
  in the cir-env container with the repository mounted at /edg/workspace.
- `sweep_analyze.py OUTDIR` — buckets the results (crash, invalid,
  nondeterministic, FE error, unsupported by construct, clean).
- `gen_enums.sh BUILD OUT.json` — enumerator names for "the operator N"
  style reasons.
- `box.sh` — sync/run/wait/fetch helpers for a remote x86-64 box; set
  `BOX_HOST`, `BOX_SOCKET`, `BOX_TREE`.

Typical loop: `box.sh sync`, `box.sh run sweep tools-script` where the script
builds (`cmake --build build/gcc-cir --target cpfe-cir`) and calls
`cir_src/test/writer/tools/sweep.sh .tmp-sweep/run .tmp-sweep/corpus.tsv`,
`box.sh wait sweep`, `box.sh fetch .tmp-sweep/run DIR`,
`sweep_analyze.py DIR`.
- `cmp_run.sh file.c...` (with `cmp_box.sh`) — builds each small program
  with GCC and with the CIR pipeline on the box, runs both and diffs the
  output (SAME or DIFF). `CMP_OPTIONS` adds eccp options. See
  `../probes/nested_struct_return.c` (a struct returned from two blocks that
  declare the same name; found a library bug).
- `cmp_cxx_box.sh` — the C++ counterpart of `cmp_box.sh` (container side):
  builds each `.cpp` of `.tmp-cmp/files` with g++ and with cpfe-cir +
  `edg-cir --emit=asm`, runs both and diffs the output.  Programs are in
  `../probes/cxx_*.cpp` (the argument order of a call is unspecified: print
  one value per statement).
- `cmp_cxx_multi_box.sh` — like `cmp_cxx_box.sh` for programs of several
  translation units (`.tmp-cmp/multi/<name>/*.cpp`, one assembly per file):
  `../probes/multi/tu_local` checks that equally named entities that are
  local to a translation unit (lambdas, local classes, anonymous-namespace
  and unnamed types as template arguments) stay separate.  `CMP_DIR` names
  the directory `cmp_cxx_box.sh` takes the programs from.
- `rt_run.sh 'path.sft.c:options'...` (with `rt_box.sh`) — builds and runs
  imported GCC tests through the CIR pipeline on the box; prints the compile
  diagnostics and the exit status.
- `callgraph_check.py [BUILD...]` — (default `build/gcc-cir`, the debug
  build; an optimized build hides cycles by inlining) compiles the six writer
  files with `-fcallgraph-info` (flags from `ninja -t commands`) and reports the call
  cycles among their functions (must be 0: no recursion on input depth).
  `CG_SRC_DIR=<dir>` checks other sources (an older commit).  Run in cir-env.
- `perf_ir.sh BIN OUT corpus|file ...` — callgrind (or `TOOL=cachegrind`)
  instruction counts of the front end over the corpus (or a list, e.g. every
  8th test) or one file, in cir-env; `cg_prof.py` / `cg_sum.py` aggregate the
  outputs (total Ir, Ir on writer lines, self/inclusive Ir of named
  functions, callees).  `gen_perf_tus.py` writes micro-benchmark TUs for the
  quadratic patterns (unique names, big struct initializers, pending
  records, builtins).
- `asm_cmp.sh BIN OUT [JOBS [LIST]]` — assembly of the corpus through the
  two-process path, for comparing two front ends (`diff -rq A B`).
- The writer fails an assertion in CHECKING builds if a G-id or sid is not
  introduced where it is assigned, or an L-id of a function is not
  introduced by FUNC_END; `ECIL_GID_STRESS=1` moves the G-id table at every
  new G-id (the debug writer does so for the first 160 anyway), so a stale
  `an_ecil_gid_info` pointer is a use-after-free under ASan
  (tests `writer-gid-growth*`).
