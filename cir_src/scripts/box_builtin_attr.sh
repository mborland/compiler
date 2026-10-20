# Does clang -fclangir put `builtin` on a new-expression's operator new call?
mkdir -p /tmp/ba && cd /tmp/ba
cat > a.cpp <<'EOT'
#include <new>
extern void sink(int *);
struct S { int a; S(); ~S(); };
int *f() { return new int(3); }
void g() { int *p = new int[4]; sink(p); delete[] p; }
void h() { S *s = new S; delete s; }
void k() { void *v = ::operator new(8); ::operator delete(v); }
EOT
/edg/llvm-cir/bin/clang --version | head -1
for mode in "-fclangir" ""; do
  for lvl in -O0 -O2; do
    echo "== clang++ $mode $lvl -std=c++17 -S -emit-llvm -Xclang -disable-llvm-passes"
    /edg/llvm-cir/bin/clang++ $mode -std=c++17 $lvl -S -emit-llvm -Xclang -disable-llvm-passes a.cpp -o a.ll 2>&1 | head -3
    grep -n "_Znwm\|_Znam\|call.*builtin\|^attributes.*builtin" a.ll | head -12
  done
done
echo "== group definitions with builtin"
/edg/llvm-cir/bin/clang++ -fclangir -std=c++17 -O2 -S -emit-llvm -Xclang -disable-llvm-passes a.cpp -o a.ll
grep -n "attributes #.*builtin" a.ll
grep -n "call .*@_Znwm" a.ll | head
echo "== clangir attribute groups used by the _Znwm calls"
for n in $(grep "call .*@_Znwm" a.ll | grep -o "#[0-9]*" | sort -u); do grep "^attributes $n " a.ll; done
