# Inside cir-env, tree root: the S1 instruction counts on the pinned real-world
# units, the M5a library (the m5a scratch tree) against this tree's.
#   box_s1_perf.sh base|after
mkdir -p .tmp && [ -e .tmp/boost-inc ] || ln -s ../m5a/.tmp/boost-inc .tmp/boost-inc
export PATH=$PWD/dev_tools/bin:$PATH PYTHONPATH=$PWD/dev_tools/pylibs
export EDG_GCC_INCL_SCRAPE=$(edg-scrape-compiler gcc --lang c++ includes) EDG_GCC_CINCL_SCRAPE=$(edg-scrape-compiler gcc --lang c includes) EDG_GCC_VER_SCRAPE=$(edg-scrape-compiler gcc version)
fe=build/gcc-release-cir
if [ "$1" = base ]; then
  fe=build/gcc-release-cir-base
  mkdir -p $fe
  sed -e 's#/edg/workspace/build/cir/#/edg/workspace/m5a/build/cir/#' build/gcc-release-cir/environment.sh > $fe/environment.sh
fi
python3 dev_tools/bin/edg-cir-perf --root . --fe-build $fe --only realworld --units pinned --levels 0,2 \
  --boost-include /edg/workspace/.tmp/boost-inc --no-clang --repeat 1 -j 3 --json .tmp/s1-$1.json > .tmp/s1-$1.log 2>&1
echo "done $1 rc=$?" >> .tmp/s1-$1.log
