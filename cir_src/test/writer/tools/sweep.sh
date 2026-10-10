#!/bin/bash
# usage: sweep.sh OUTDIR [CORPUS]   (inside the cir-env container; the repository
# is at $EDG_WORKSPACE, default /edg/workspace)
W=${EDG_WORKSPACE:-/edg/workspace}
cd $W
OUT=${1:-.tmp-sweep/run}; CORPUS=${2:-.tmp-sweep/corpus.tsv}
mkdir -p $OUT; : > $OUT/results.tsv
cat > $OUT/shim.sh <<'EOS'
#!/bin/bash
exec timeout 120 ${EDG_WORKSPACE:-/edg/workspace}/build/gcc-cir/bin/cpfe-cir --ecil_output=$ECIL_OUT "$@"
EOS
chmod +x $OUT/shim.sh
export EDG_CIR_RT_LIBDIR=$W/build/cir/lib ECCP=$W/util/eccp.sh EDG_BASE=$W/bases/docker/cir-env/gcc CPFE=$PWD/$OUT/shim.sh
D=build/cir/tools/ecil-dump/ecil-dump
i=0
while IFS=$'\t' read -r f opts; do
  i=$((i+1)); n=$(echo "$f" | tr '/' '_')
  src=tests/tests/imported/gnu/c/$f
  for pass in 1 2; do
    ECIL_OUT=$OUT/$n.$pass.ecil ./dev_tools/bin/eccp --cpfe_only --c $opts $src > $OUT/$n.$pass.err 2>&1; echo $? > $OUT/$n.$pass.rc
  done
  rc1=$(cat $OUT/$n.1.rc); rc2=$(cat $OUT/$n.2.rc)
  same=0; cmp -s $OUT/$n.1.ecil $OUT/$n.2.ecil && same=1
  v=""; dr=0
  if [ -s $OUT/$n.1.ecil ]; then v=$($D $OUT/$n.1.ecil 2>&1 >/dev/null | head -1); [ -n "$v" ] && dr=1; fi
  sc=""
  if [ -s $OUT/$n.1.ecil ] && command -v python3 >/dev/null; then
    sc=$(python3 -I $W/cir_src/test/writer/tools/ecil_dump.py $OUT/$n.1.ecil 2>/dev/null | python3 -I $W/cir_src/test/writer/tools/stackcheck.py "$f" 2>&1 | head -3 | tr '\n' '|')
  fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$f" "$rc1" "$rc2" "$same" "$dr" "$v" "$sc" >> $OUT/results.tsv
  rm -f $OUT/$n.2.ecil
  [ "$dr" = 0 ] && [ "$rc1" = 0 ] && rm -f $OUT/$n.1.ecil
done < $CORPUS
echo done $i > $OUT/DONE
