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
- `rt_run.sh 'path.sft.c:options'...` (with `rt_box.sh`) — builds and runs
  imported GCC tests through the CIR pipeline on the box; prints the compile
  diagnostics and the exit status.
