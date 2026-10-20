# The gate step lazy-identity (cir-config's tool) on the given sets.
export PATH=$PWD/dev_tools/bin:$PATH PYTHONPATH=$PWD/dev_tools/pylibs
export EDG_GCC_INCL_SCRAPE=$(edg-scrape-compiler gcc --lang c++ includes) EDG_GCC_CINCL_SCRAPE=$(edg-scrape-compiler gcc --lang c includes) EDG_GCC_VER_SCRAPE=$(edg-scrape-compiler gcc version)
[ -e .tmp/boost-inc ] || { mkdir -p .tmp; ln -s ../m5a/.tmp/boost-inc .tmp/boost-inc; }
timeout 1500 python3 dev_tools/bin/edg-cir-lazy-identity "$@" > .tmp/lazy-tool.log 2>&1
echo "rc=$?" >> .tmp/lazy-tool.log
