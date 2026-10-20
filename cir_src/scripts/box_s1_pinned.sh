# Instructions (cachegrind Ir) of edg-cir on the pinned real-world units: the
# M5a library (the m5a scratch tree) against this tree's, on the same streams
# (made by the debug front end build/gcc-cir).   box_s1_pinned.sh [UNIT...]
export PATH=$PWD/dev_tools/bin:$PATH PYTHONPATH=$PWD/dev_tools/pylibs
[ -e .tmp/boost-inc ] || { mkdir -p .tmp; ln -s ../m5a/.tmp/boost-inc .tmp/boost-inc; }
VER=$(edg-scrape-compiler gcc version); INCL=$(edg-scrape-compiler gcc --lang c++ includes)
SI=""; IFS=:; for d in $INCL; do SI="$SI --sys_include=$d"; done; unset IFS
UNITS=${@:-libstdcxx/chrono libstdcxx/functional libstdcxx/iostream boost/mp11 boost/circular_buffer boost/lexical_cast}
ir() { LD_LIBRARY_PATH=$1/lib valgrind --tool=cachegrind --cache-sim=no --cachegrind-out-file=/tmp/cg.$3 $1/bin/edg-cir $2 --emit=asm -o /tmp/o.$3.s $4 >/dev/null 2>&1; grep summary /tmp/cg.$3 | awk '{print $2}'; }
for u in $UNITS; do
  for lvl in 0 2; do
    opt=; [ $lvl = 2 ] && opt=--cir_opt_level=2
    LD_LIBRARY_PATH=build/cir/lib build/gcc-cir/bin/cpfe-cir --edg_base $PWD/bases/docker/cir-env/gcc --g++ --gnu_version=$VER --c++17 -e 200 --no_wrap --no_wrap_diagnostics $SI -I$PWD/.tmp/boost-inc $opt --ecil_output=/tmp/u.ecil dev_tools/cir-realworld/$u.cpp > /tmp/u.fe 2>&1
    note=
    if [ ! -s /tmp/u.ecil ]; then
      # (an old front end build without --cir_opt_level: the -O0 stream)
      LD_LIBRARY_PATH=build/cir/lib build/gcc-cir/bin/cpfe-cir --edg_base $PWD/bases/docker/cir-env/gcc --g++ --gnu_version=$VER --c++17 -e 200 --no_wrap --no_wrap_diagnostics $SI -I$PWD/.tmp/boost-inc --ecil_output=/tmp/u.ecil dev_tools/cir-realworld/$u.cpp > /tmp/u.fe 2>&1
      note=" (-O0 stream)"
    fi
    [ -s /tmp/u.ecil ] || { echo "$u -O$lvl: front end failed"; continue; }
    b=$(ir m5a/build/cir -O$lvl base /tmp/u.ecil); n=$(ir build/cir -O$lvl new /tmp/u.ecil)
    echo "$u -O$lvl: M5a $((b/1000000)) M, S1 $((n/1000000)) M, $((n*1000/b/10)).$((n*1000/b%10))% asm $(cmp -s /tmp/o.base.s /tmp/o.new.s && echo same || echo DIFFERS)$note"
    rm -f /tmp/u.ecil
  done
done
