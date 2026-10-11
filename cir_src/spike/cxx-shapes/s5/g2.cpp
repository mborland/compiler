int f(int);
struct S { int id; S(int i); ~S(); };
const S &sr = S(11);
int h2() { static const S &lr = S(12); return lr.id; }
