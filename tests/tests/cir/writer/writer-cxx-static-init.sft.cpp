//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
int f(int v);
struct S { int id; S(int i); ~S(); };
struct Z { int v; ~Z(); };
Z z = {1};
int a = f(1);
int b = (f(2) > 0 && f(3) > 0) ? f(4) : f(5);
S s(6);
static S ss(7);
inline int iv = f(8);
template <class T> struct TV { static T v; };
template <class T> T TV<T>::v = f(9);
int use_tv() { return TV<int>::v; }
struct H { static S sm; static int cnt; };
S H::sm(10);
int H::cnt = f(11);
S arr[2] = { S(12), S(13) };
const S &sr = S(14);
Z tconst = { 2 };
int tmp_init = S(15).id + f(16);
thread_local int tl = 5;
thread_local int tl_dyn = f(17);
inline S &inl() { static S inl_s(18); return inl_s; }
int h() { static int x = f(19); static S ls(20); return x + ls.id; }
int h2() { static const S &lr = S(21); return lr.id; }
int h3(int c) { static int y = (c && f(22) > 0) ? f(23) : f(24); return y; }
int h4() { static Z lz = {3}; return lz.v; }
int h5() { static thread_local int k = f(25); return k; }
