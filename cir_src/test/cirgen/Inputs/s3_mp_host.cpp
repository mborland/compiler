// The host side of cxx-s3-member-pointers.test: defines the member functions,
// reads the member pointer globals of the CIR unit (their representation is
// the Itanium ABI's: -1 for a null data member pointer, {ptr or vtable
// offset + 1, this adjustment} for a member function pointer), passes member
// pointers by value in both directions and checks the results.
struct B1 { int a; int f(int x); };
struct B2 { int b; int g(int x); };
struct D : B1, B2 { int d; int h(int x); };
struct Holder { int x; int D::*pm; };
struct Der : Holder { int z; };

int B1::f(int x) { return a + x; }
int B2::g(int x) { return b * x; }
int D::h(int x) { return d - x; }

extern int B1::*g_pa;
extern int B2::*g_pb;
extern int D::*g_pd;
extern int D::*g_pdb;
extern int (B1::*g_pf)(int);
extern int (D::*g_ph)(int);
extern int (D::*g_pg)(int);
extern int (D::*g_arr[3])(int);
extern int D::*g_null;
extern Holder g_holder;
extern Holder g_holder2;
extern int D::*g_dmarr[3];
extern Holder g_harr[2];

extern "C" {
int get_dm(D *p, int D::*m);
int call_m(D *p, int (D::*m)(int), int x);
bool is_set(int D::*m);
bool same(int D::*a, int D::*b);
int D::*to_derived(int B2::*m);
int B2::*to_base(int D::*m);
int (D::*mk_method())(int);
Holder zero_holder();
Der make_der();
void zero_big(int D::*(*p)[64]);
}

int main()
{
  D d;
  d.a = 1; d.b = 2; d.d = 3;
  B1 &r1 = d;
  B2 &r2 = d;

  /* The globals the CIR unit initialized. */
  if (d.*g_pa != 0 && r1.*g_pa != 1) return 1;
  if (r1.*g_pa != 1 || r2.*g_pb != 2 || d.*g_pd != 3) return 2;
  if (d.*g_pdb != 2) return 3;
  if (g_pdb != static_cast<int D::*>(&B2::b)) return 4;
  if ((r1.*g_pf)(10) != 11) return 5;
  if ((d.*g_ph)(1) != 2) return 6;
  if ((d.*g_pg)(5) != 10) return 7;
  if (g_pg != static_cast<int (D::*)(int)>(&B2::g)) return 8;
  if ((d.*g_arr[0])(1) != 2 || (d.*g_arr[1])(5) != 10) return 9;
  if (g_arr[2] != nullptr || g_arr[0] != &D::h) return 10;
  if (g_null != nullptr) return 11;
  if (g_holder.x != 0 || g_holder.pm != nullptr) return 12;
  if (g_holder2.x != 7 || g_holder2.pm != nullptr) return 13;
  for (int i = 0; i < 3; ++i)
    if (g_dmarr[i] != nullptr) return 14;
  if (g_harr[0].x != 1 || g_harr[0].pm != nullptr || g_harr[1].x != 0 ||
      g_harr[1].pm != nullptr)
    return 15;

  /* Member pointers made here, used there. */
  if (get_dm(&d, &D::d) != 3 || get_dm(&d, static_cast<int D::*>(&B2::b)) != 2)
    return 20;
  if (call_m(&d, &D::h, 4) != -1) return 21;
  if (call_m(&d, static_cast<int (D::*)(int)>(&B2::g), 5) != 10) return 22;
  if (call_m(&d, static_cast<int (D::*)(int)>(&B1::f), 5) != 6) return 23;
  if (is_set(nullptr) || !is_set(&D::d)) return 24;
  if (!same(&D::d, &D::d) || same(&D::d, nullptr) || !same(nullptr, nullptr))
    return 25;
  if (to_derived(&B2::b) != static_cast<int D::*>(&B2::b)) return 26;
  if (to_base(static_cast<int D::*>(&B2::b)) != &B2::b) return 27;
  if (mk_method() != static_cast<int (D::*)(int)>(&B2::g)) return 28;
  Holder h = zero_holder();
  if (h.x != 0 || h.pm != nullptr) return 29;
  Der z = make_der();
  if (z.x != 0 || z.pm != nullptr || z.z != 9) return 30;
  /* An array of 64 (512 bytes: copied from a private constant of nulls). */
  int D::*big[64];
  for (int i = 0; i < 64; ++i) big[i] = &D::d;
  zero_big(&big);
  for (int i = 0; i < 64; ++i)
    if (big[i] != nullptr) return 31;
  return 0;
}
