//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// C++17 built-in assignment of a class: the right operand is evaluated first,
// but only its address is kept (an object, or the temporary of a prvalue with
// a destructor); the value is read after the target is designated, as g++ and
// clang do.  A trivially copyable prvalue (the result of a call) is a fixed
// value and is kept as it is.
extern "C" int printf(const char *, ...);
struct T { int v; };
T obj{1}, dst{0};
T *getp() { obj.v = 2; return &dst; }
struct N { int v; ~N() { printf("~N(%d)\n", v); } };
N nobj{1}, ndst{0};
N *ngetp() { nobj.v = 2; return &ndst; }
N nmake(int v) { printf("nmake %d\n", v); return N{v}; }
struct B { T a; T b; };
B arr[3];
int idx;
int nxt() { arr[1].a.v = 40; return idx++; }
T *tp;
T &getr() { obj.v = 3; return dst; }
T mk(int v) { return T{v}; }
int main_unused() {
  *getp() = obj;
  printf("%d %d\n", dst.v, obj.v);
  obj.v = 1; dst.v = 0;
  *ngetp() = nobj;
  printf("%d %d\n", ndst.v, nobj.v);
  nobj.v = 5;
  *ngetp() = nmake(9);
  printf("%d\n", ndst.v);
  arr[1].b.v = 0;
  arr[nxt()].a = arr[1].a;
  printf("%d %d\n", arr[0].a.v, arr[1].a.v);
  obj.v = 1; dst.v = 0;
  getr() = obj;
  printf("%d %d\n", dst.v, obj.v);
  obj.v = 1; dst.v = 0;
  *getp() = static_cast<T &&>(obj);
  printf("%d\n", dst.v);
  *getp() = mk(7);
  printf("%d\n", dst.v);
  return 0;
}
