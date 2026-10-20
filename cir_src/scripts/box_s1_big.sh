# Instructions (cachegrind Ir) of edg-cir on a generated stream: the M5a
# library (m5a scratch tree) against this tree's.   box_s1_big.sh STREAM [OPT]
S=${1:-m5a/build/big/cxx6000.ecil}; O=${2:--O0}
for tag in m5a build; do
  b=$tag/cir; [ $tag = m5a ] && b=m5a/build/cir
  LD_LIBRARY_PATH=$b/lib valgrind --tool=cachegrind --cache-sim=no --cachegrind-out-file=/tmp/cg.$tag $b/bin/edg-cir $O --emit=asm -o /tmp/o.$tag.s $S >/dev/null 2>&1; echo "$tag Ir: $(grep summary /tmp/cg.$tag | awk "{print \$2}")"
  echo "$tag: $(cmp -s /tmp/o.m5a.s /tmp/o.build.s && echo same-asm || echo asm-differs-or-pending)"
done
