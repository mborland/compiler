struct R1 { virtual ~R1(); int a; };
struct R2 { virtual ~R2(); int b; };
struct D2 : R1, R2 { D2(); ~D2(); int d; };
struct P { virtual int p(); virtual ~P(); int pp; };
struct Q { virtual R2 *get(); virtual ~Q(); int qq; };
struct X : P, Q { D2 obj; ~X() override; int p() override; D2 *get() override; X(); };
R1::~R1() {}
R2::~R2() {}
D2::D2() { a = 1; b = 2; d = 3; }
D2::~D2() {}
int P::p() { return 1; }
P::~P() {}
R2 *Q::get() { return 0; }
Q::~Q() {}
X::X() {}
X::~X() {}
int X::p() { return 5; }
int main() {
  X x;
  Q *q = &x;
  R2 *r = q->get();
  if (r == nullptr) return 1;
  if (r != static_cast<R2 *>(x.get())) return 2;
  if (r->b != 2) return 3;
  if (dynamic_cast<D2 *>(r) != &x.obj) return 4;
  Q q0;
  if (q0.get() != 0) return 5;
  if (x.p() != 5) return 6;
  return 0;
}
