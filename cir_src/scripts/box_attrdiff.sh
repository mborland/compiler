# Inside cir-env, tree root (~/edg-cir-lib): attr-diff of the pinned units for
# the library of the given tree (main: build/cir, or a scratch tree under
# the root such as s2: s2/build/cir).   box_attrdiff.sh TAG [TREE]
TAG=$1; TREE=${2:-}
export PATH=$PWD/dev_tools/bin:$PATH PYTHONPATH=$PWD/dev_tools/pylibs
export EDG_GCC_INCL_SCRAPE=$(edg-scrape-compiler gcc --lang c++ includes) EDG_GCC_CINCL_SCRAPE=$(edg-scrape-compiler gcc --lang c includes) EDG_GCC_VER_SCRAPE=$(edg-scrape-compiler gcc version)
[ -e .tmp/boost-inc ] || { mkdir -p .tmp; ln -s ../m5a/.tmp/boost-inc .tmp/boost-inc; }
fe=build/gcc-cir
if [ -n "$TREE" ]; then
  fe=build/gcc-cir-$TAG; mkdir -p $fe
  sed -e "s#/edg/workspace/build/cir/#/edg/workspace/$TREE/build/cir/#" build/gcc-cir/environment.sh > $fe/environment.sh
fi
python3 dev_tools/bin/edg-cir-attr-diff --root . --fe-build $fe --units pinned --system-headers \
  --boost-include /edg/workspace/.tmp/boost-inc --levels 0,2 --json .tmp/ad-$TAG.json > .tmp/ad-$TAG.txt 2>&1
echo "rc=$?" >> .tmp/ad-$TAG.txt
