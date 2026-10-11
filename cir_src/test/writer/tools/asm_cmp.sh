#!/bin/bash
# Assembly of the writer corpus through the two-process path, for comparing
# two front ends (commit against commit), inside the cir-env container.
#   asm_cmp.sh BIN OUTDIR [JOBS [LIST]]
# For every test of LIST (default .tmp-sweep/corpus.tsv) it writes
# OUTDIR/<test>.ecil (the stream, deleted unless KEEP_ECIL=1), OUTDIR/<test>.s
# (edg-cir --emit=asm, empty if the compilation failed) and OUTDIR/<test>.rc
# (front-end status, then edg-cir status).  Compare two runs with
#   diff -rq A B --exclude='*.ecil'
bin=$1; out=$2; jobs=${3:-12}; list=${4:-.tmp-sweep/corpus.tsv}
cd /edg/workspace
rm -rf "$out"; mkdir -p "$out"
cat > "$out/shim.sh" <<EOS
#!/bin/bash
exec $bin --ecil_output=$PWD/$out/\$ASM_TAG.ecil "\$@"
EOS
chmod +x "$out/shim.sh"
export EDG_CIR_RT_LIBDIR=/edg/workspace/build/cir/lib LD_LIBRARY_PATH=/edg/workspace/build/cir/lib EDG_BASE=/edg/workspace/bases/docker/cir-env/gcc CPFE=$PWD/$out/shim.sh ECCP=/edg/workspace/util/eccp.sh
export OUT=$PWD/$out
run1() {
  f=$1; opts=$2
  export ASM_TAG=$(echo "$f" | tr '/' '_')
  ./dev_tools/bin/eccp --cpfe_only --c $opts tests/tests/imported/gnu/c/$f > $OUT/$ASM_TAG.err 2>&1
  rc1=$?
  rc2=-
  if [ -s $OUT/$ASM_TAG.ecil ] && [ $rc1 = 0 ]; then
    timeout 120 build/cir/bin/edg-cir --emit=asm -o $OUT/$ASM_TAG.s $OUT/$ASM_TAG.ecil > $OUT/$ASM_TAG.err2 2>&1
    rc2=$?
  fi
  echo "$rc1 $rc2" > $OUT/$ASM_TAG.rc
  [ "$KEEP_ECIL" = 1 ] || rm -f $OUT/$ASM_TAG.ecil
}
export -f run1
while IFS=$'\t' read -r f opts; do printf '%s\0%s\0' "$f" "$opts"; done < $list \
  | xargs -0 -n 2 -P "$jobs" bash -c 'run1 "$0" "$1"'
echo "done: $(ls $out/*.rc | wc -l) tests, $(ls $out/*.s 2>/dev/null | wc -l) assembled"
