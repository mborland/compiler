#!/bin/bash
# Instruction counts of the CIR front end (cachegrind Ir), inside the cir-env
# container, repository mounted at /edg/workspace.
#   perf_ir.sh BIN OUTDIR corpus [JOBS [LIST]]
#                                         the C writer corpus (LIST, default
#                                         .tmp-sweep/corpus.tsv); one cg.<test>
#                                         file per test
#   perf_ir.sh BIN OUTDIR file FILE.c     one file (e.g. the 6000-function TU)
# BIN is a cpfe-cir built with symbols; its stream goes to /dev/null.  With
# TOOL=callgrind (the default) the files are callgrind outputs (inclusive costs
# per function, cg_prof.py); TOOL=cachegrind gives cachegrind files (cg_sum.py).
# Prints the summary of the files.
bin=$1; out=$2; mode=$3
cd /edg/workspace
rm -rf "$out"; mkdir -p "$out"
cat > "$out/shim.sh" <<EOS
#!/bin/bash
exec valgrind --tool=${TOOL:-callgrind} -q --${TOOL:-callgrind}-out-file=$PWD/$out/cg.\${CG_TAG:-%p} $bin --ecil_output=/dev/null "\$@"
EOS
chmod +x "$out/shim.sh"
export EDG_CIR_RT_LIBDIR=/edg/workspace/build/cir/lib LD_LIBRARY_PATH=/edg/workspace/build/cir/lib EDG_BASE=/edg/workspace/bases/docker/cir-env/gcc CPFE=$PWD/$out/shim.sh ECCP=/edg/workspace/util/eccp.sh
if [ "$mode" = corpus ]; then
  jobs=${4:-12}
  export OUT="$out"
  run1() {
    f=$1; opts=$2
    export CG_TAG=$(echo "$f" | tr '/' '_')
    ./dev_tools/bin/eccp --cpfe_only --c $opts tests/tests/imported/gnu/c/$f > /dev/null 2>&1
  }
  export -f run1
  while IFS=$'\t' read -r f opts; do printf '%s\0%s\0' "$f" "$opts"; done < ${5:-.tmp-sweep/corpus.tsv} \
    | xargs -0 -n 2 -P "$jobs" bash -c 'run1 "$0" "$1"'
else
  ./dev_tools/bin/eccp --cpfe_only --c "$4" > /dev/null 2>&1
fi
if [ "${TOOL:-callgrind}" = callgrind ]; then
  python3 -I cir_src/test/writer/tools/cg_prof.py --fn "${FNS:-}" "$out"/cg.*
else
  python3 -I cir_src/test/writer/tools/cg_sum.py "$out"/cg.*
fi
