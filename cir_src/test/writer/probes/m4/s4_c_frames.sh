# S4 probe: a throw through a C frame. A C function compiled by the CIR back end
# (as C) calls a C++ function that throws; the exception is caught by the C++
# caller. Run in the cir-env container with the tree at /edg/workspace (the
# build/gcc-cir front end and build/cir library); the output must equal the one
# with the C function built by g++ (ok 1, ~R, ~R, caught 2).
mkdir -p /edg/workspace/.tmp/cf; cd /edg/workspace/.tmp/cf
. /edg/workspace/build/gcc-cir/environment.sh
export EDG_BASE=/edg/workspace/bases/docker/cir-env/gcc
export LD_LIBRARY_PATH=/edg/workspace/build/cir/lib EDG_CIR_RT_LIBDIR=/edg/workspace/build/cir/lib ECCP_LIBDIR=/edg/workspace/build/cir/lib
cat > cframe.c <<'EOT'
void call_back(void (*f)(int), int n) { f(n); f(n + 1); }
EOT
cat > main.cpp <<'EOT'
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
extern "C" void call_back(void (*f)(int), int n);
struct R { ~R() { puts("~R"); } };
void thrower(int v) { R r; if (v == 2) throw v; printf("ok %d\n", v); }
int main() {
  try { call_back(thrower, 1); } catch (int i) { printf("caught %d\n", i); }
  return 0;
}
EOT
for f in cframe.c main.cpp; do
b=${f%.*}
cat > w_$b.sh <<EOT
#!/bin/sh
exec $CPFE_CIR --ecil_output=/edg/workspace/.tmp/cf/$b.ecil "\$@"
EOT
chmod +x w_$b.sh; rm -f $b.ecil $b.o
CPFE=/edg/workspace/.tmp/cf/w_$b.sh CPFE_CP=/edg/workspace/.tmp/cf/w_$b.sh /edg/workspace/util/eccp.sh -c $( [ "$f" = cframe.c ] && echo --c99 || echo -tused ) $f > $b.fe.log 2>&1
grep -i "error" $b.fe.log | head -3
/edg/workspace/build/cir/bin/edg-cir --emit=obj -o $b.o $b.ecil 2>&1 | head -3
done
g++ cframe.o main.o -o cf.exe 2>&1 | head -5; ./cf.exe; echo rc=$?
g++ -x c -c cframe.c -o cframe_g.o && g++ cframe_g.o main.o -o cf2.exe 2>&1 | head -2; ./cf2.exe; echo rc2=$?
