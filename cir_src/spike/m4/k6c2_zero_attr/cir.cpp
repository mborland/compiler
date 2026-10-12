#include "p.h"
extern "C" void *memcpy(void *, const void *, unsigned long);
inline void *operator new(unsigned long, void *p) noexcept { return p; }
void V::f() {}

S g_s{};
S g_s2 = {};
S g_s3;
S g_arr[3];
S g_arr2[3] = {};
S g_part[3] = {{1}};
D g_d{};
N g_n{};
N g_n2;
M g_m{};
M g_m2;
long S::*g_pm;
long S::*g_pm2{};
long S::*g_pms[3];
F g_f{};
F g_f2;
V g_v{};

#define SNAP(nm, obj) static void nm(unsigned char *o) { memcpy(o, (const void *)&(obj), sizeof(obj)); }
SNAP(c_g_s, g_s) SNAP(c_g_s2, g_s2) SNAP(c_g_s3, g_s3) SNAP(c_g_arr, g_arr)
SNAP(c_g_arr2, g_arr2) SNAP(c_g_part, g_part) SNAP(c_g_d, g_d) SNAP(c_g_n, g_n)
SNAP(c_g_n2, g_n2) SNAP(c_g_m, g_m) SNAP(c_g_m2, g_m2) SNAP(c_g_pm, g_pm)
SNAP(c_g_pm2, g_pm2) SNAP(c_g_pms, g_pms) SNAP(c_g_f, g_f) SNAP(c_g_f2, g_f2)

static void c_l_s1(unsigned char *o) { S s{}; memcpy(o, &s, sizeof s); }
static void c_l_s2(unsigned char *o) { S s = {}; memcpy(o, &s, sizeof s); }
static void c_l_s3(unsigned char *o) { S s{1}; memcpy(o, &s, sizeof s); }
static void c_l_s4(unsigned char *o) { S s = S(); memcpy(o, &s, sizeof s); }
static void c_l_a1(unsigned char *o) { S a[4] = {}; memcpy(o, a, sizeof a); }
static void c_l_a2(unsigned char *o) { S a[4] = {{1}}; memcpy(o, a, sizeof a); }
static void c_l_a3(unsigned char *o) { S a[4]{}; memcpy(o, a, sizeof a); }
static void c_l_n1(unsigned char *o) { N n{}; memcpy(o, &n, sizeof n); }
static void c_l_n2(unsigned char *o) { N n = {}; memcpy(o, &n, sizeof n); }
static void c_l_n3(unsigned char *o) { N n{{1}, 2}; memcpy(o, &n, sizeof n); }
static void c_l_d1(unsigned char *o) { D d{}; memcpy(o, &d, sizeof d); }
static void c_l_d2(unsigned char *o) { D d = D(); memcpy(o, &d, sizeof d); }
static void c_l_p1(unsigned char *o) { long S::*p{}; memcpy(o, &p, sizeof p); }
static void c_l_p2(unsigned char *o) { long S::*p = nullptr; memcpy(o, &p, sizeof p); }
static void c_l_p3(unsigned char *o) { long S::*p[3] = {}; memcpy(o, p, sizeof p); }
static void c_l_p4(unsigned char *o) { long S::*p = &S::b; p = {}; memcpy(o, &p, sizeof p); }
static void c_l_m1(unsigned char *o) { M m{}; memcpy(o, &m, sizeof m); }
static void c_l_f1(unsigned char *o) { F f{}; memcpy(o, &f, sizeof f); }
static void c_l_f2(unsigned char *o) { F f; memcpy(o, &f, sizeof f); }
static void c_l_v1(unsigned char *o) { V v{}; memcpy(o + 8, &v.vpm, 16); }

static void c_n_s1(unsigned char *o) { S *p = new S(); memcpy(o, p, sizeof *p); }
static void c_n_s2(unsigned char *o) { S *p = new S{}; memcpy(o, p, sizeof *p); }
static void c_n_s3(unsigned char *o) { S *p = new S[3](); memcpy(o, p, 3 * sizeof *p); }
static void c_n_d1(unsigned char *o) { D *p = new D(); memcpy(o, p, sizeof *p); }
static void c_n_n1(unsigned char *o) { N *p = new N(); memcpy(o, p, sizeof *p); }
static void c_n_n2(unsigned char *o) { N *p = new N[2](); memcpy(o, p, 2 * sizeof *p); }
static void c_n_pl(unsigned char *o) {
  alignas(8) unsigned char buf[sizeof(S)];
  for (unsigned i = 0; i < sizeof buf; ++i) buf[i] = 0x5a;
  S *p = new (buf) S();
  memcpy(o, p, sizeof *p);
}
static void c_n_f1(unsigned char *o) { F *p = new F(); memcpy(o, p, sizeof *p); }

static S ret_s() { return {}; }
static N ret_n() { return N(); }
static void c_r_s(unsigned char *o) { S s = ret_s(); memcpy(o, &s, sizeof s); }
static void c_r_n(unsigned char *o) { N n = ret_n(); memcpy(o, &n, sizeof n); }

static S &sl_s() { static S s; return s; }
static S &sl_s2() { static S s{}; return s; }
static N &sl_n() { static N n; return n; }
static void c_s_s(unsigned char *o) { memcpy(o, &sl_s(), sizeof(S)); }
static void c_s_s2(unsigned char *o) { memcpy(o, &sl_s2(), sizeof(S)); }
static void c_s_n(unsigned char *o) { memcpy(o, &sl_n(), sizeof(N)); }

// assignment from a value-initialized temporary, and a member init list
struct W { long S::*pm; long w; W() : w(1) {} };
struct W2 { S s; long S::*pm2; W2() : s() {} };
static void c_w_1(unsigned char *o) { W w; memcpy(o + 8, &w.w, 8); }
static void c_w_2(unsigned char *o) { W2 w; memcpy(o, &w.s, sizeof w.s); }
static void c_a_1(unsigned char *o) { S s; s = S(); memcpy(o, &s, sizeof s); }
static void c_a_2(unsigned char *o) { N n; n.s = {}; memcpy(o, &n.s, sizeof n.s); }

#define C(nm, sz) {#nm, c_##nm, sz}
const Case cases[] = {
  C(g_s, 24), C(g_s2, 24), C(g_s3, 24), C(g_arr, 72), C(g_arr2, 72),
  C(g_part, 72), C(g_d, 32), C(g_n, 88), C(g_n2, 88), C(g_m, 24),
  C(g_m2, 24), C(g_pm, 8), C(g_pm2, 8), C(g_pms, 24), C(g_f, 16), C(g_f2, 16),
  C(l_s1, 24), C(l_s2, 24), C(l_s3, 24), C(l_s4, 24), C(l_a1, 96),
  C(l_a2, 96), C(l_a3, 96), C(l_n1, 88), C(l_n2, 88), C(l_n3, 88),
  C(l_d1, 32), C(l_d2, 32), C(l_p1, 8), C(l_p2, 8), C(l_p3, 24), C(l_p4, 8),
  C(l_m1, 24), C(l_f1, 16), C(l_f2, 16),
  C(n_s1, 24), C(n_s2, 24), C(n_s3, 72), C(n_d1, 32),
  C(n_n1, 88), C(n_n2, 176), C(n_pl, 24), C(n_f1, 16),
  C(r_s, 24), C(r_n, 88), C(s_s, 24), C(s_s2, 24), C(s_n, 88),
  C(w_2, 24), C(a_1, 24), C(a_2, 24),
};
const unsigned ncases = sizeof cases / sizeof cases[0];
