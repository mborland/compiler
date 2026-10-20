# Inside cir-env, from a tree root with build/gcc-cir (front end) and
# build/cir (edg-cir): cir-config's chunk-identity on this tree.
#   box_chunk_identity.sh TAG [chunk-identity options...]
TAG=$1; shift
export PATH=$PWD/dev_tools/bin:$PATH PYTHONPATH=$PWD/dev_tools/pylibs
export EDG_GCC_INCL_SCRAPE=$(edg-scrape-compiler gcc --lang c++ includes) EDG_GCC_CINCL_SCRAPE=$(edg-scrape-compiler gcc --lang c includes) EDG_GCC_VER_SCRAPE=$(edg-scrape-compiler gcc version)
[ -e build/gcc-cir ] || ln -s gcc-release-cir build/gcc-cir
python3 dev_tools/bin/edg-cir-chunk-identity --root . --boost-include /edg/workspace/.tmp/boost-inc --json .tmp/ci-$TAG.json "$@" > .tmp/ci-$TAG.txt 2>&1
echo "rc=$?" >> .tmp/ci-$TAG.txt
tail -${TAIL:-25} .tmp/ci-$TAG.txt
