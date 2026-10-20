# Inside cir-env, tree root with the front end in build/gcc-cir (a link to
# build/gcc-release-cir) and the library in build/cir: the two attr-diff runs
# of the gate (the probe set of the smoke lists, and the pinned units), as the
# checkpoint runs them.   box_attrdiff2.sh TAG   -> .tmp/ad2-TAG-{probe,pinned}.{txt,json}
TAG=$1
export LD_LIBRARY_PATH=$PWD/build/cir/lib
export PATH=$PWD/dev_tools/bin:$PATH PYTHONPATH=$PWD/dev_tools/pylibs
export EDG_GCC_INCL_SCRAPE=$(edg-scrape-compiler gcc --lang c++ includes) EDG_GCC_CINCL_SCRAPE=$(edg-scrape-compiler gcc --lang c includes) EDG_GCC_VER_SCRAPE=$(edg-scrape-compiler gcc version)
[ -e build/gcc-cir ] || ln -s gcc-release-cir build/gcc-cir
mkdir -p .tmp
python3 dev_tools/bin/edg-cir-attr-diff --levels 0,2 --known tests/.edgy/filters/cir-attr-known.txt --require-no-llvm-passes --filter @tests/.edgy/filters/cir-c-smoke.txt --filter @tests/.edgy/filters/cir-cpp-smoke.txt --json .tmp/ad2-$TAG-probe.json > .tmp/ad2-$TAG-probe.txt 2>&1
echo "probe exit $?"
python3 dev_tools/bin/edg-cir-attr-diff --levels 0,2 --known tests/.edgy/filters/cir-attr-known.txt --require-no-llvm-passes --units pinned --system-headers --boost-include .tmp/boost-inc --require-compared --nounwind-target --json .tmp/ad2-$TAG-pinned.json > .tmp/ad2-$TAG-pinned.txt 2>&1
echo "pinned exit $?"
for k in probe pinned; do head -4 .tmp/ad2-$TAG-$k.txt | cut -c1-200; done
