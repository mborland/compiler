int f(int);
struct S { int id; S(int i); ~S(); };
int a = f(1);
int b = (f(2) > 0 && f(3) > 0) ? f(4) : f(5);
S s(6);
inline int iv = f(7);
template <class T> struct TV { static T v; };
template <class T> T TV<T>::v = f(8);
int use_tv() { return TV<int>::v; }
int h() { static int x = f(9); static S ls(10); return x + ls.id; }
