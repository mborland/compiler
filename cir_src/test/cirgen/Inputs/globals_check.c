/* Companion of globals.test: gcc checks the values and layout of globals
   that were defined by the EDG-CIRGen output. */
#include <string.h>
struct S { char c; int i; short s; };
struct B { int a:3; int b:5; unsigned c:20; long long d:40; };
struct __attribute__((packed)) PB { char c; int a:4; int b:20; short s; };
union U { int i; char c[8]; };
struct N { struct S s[2]; char tail; };
extern int g_i;
extern char g_str[6];
extern char *g_p;
extern int g_arr[4];
extern int *g_ap;
extern struct S g_s;
extern struct B g_b;
extern struct PB g_pb;
extern union U g_u;
extern double g_d;
extern float g_f;
extern long double g_ld;
extern int (*g_fp)(int);
extern int g_t;
extern int g_rep[100];
extern struct N g_n;
extern char g_s2[10];
extern int *g_null;
extern const int g_const;
extern int g_weak;
extern int *g_ip;
extern int (*g_farr[2])(int);
extern int twice(int);

#define CHECK(n, c) do { if (!(c)) return n; } while (0)

int main(void)
{
  int k;
  CHECK(1, g_i == 5);
  CHECK(2, memcmp(g_str, "hello", 6) == 0);
  CHECK(3, strcmp(g_p, "world") == 0);
  CHECK(4, g_arr[0] == 1 && g_arr[3] == 4);
  CHECK(5, g_ap == &g_arr[2] && *g_ap == 3);
  CHECK(6, g_s.c == 1 && g_s.i == 2 && g_s.s == 3);
  CHECK(7, g_b.a == -2 && g_b.b == 7 && g_b.c == 12345 &&
           g_b.d == 123456789012LL);
  CHECK(8, g_pb.c == 9 && g_pb.a == -3 && g_pb.b == 54321 && g_pb.s == 77);
  CHECK(9, g_u.c[0] == 1 && g_u.c[7] == 8);
  CHECK(10, g_d == 1.5 && g_f == 2.5f && g_ld == 3.25L);
  CHECK(11, g_fp == twice && g_fp(4) == 8);
  CHECK(12, g_t == 0);
  for (k = 0; k < 100; ++k)
    CHECK(13, g_rep[k] == ((k >= 10 && k <= 19) ? 9 : (k == 99 ? 7 : 0)));
  CHECK(14, g_n.s[0].c == 1 && g_n.s[1].i == 6 && g_n.s[1].s == 7 &&
            g_n.tail == 'z');
  CHECK(15, g_s2[0] == 'a' && g_s2[1] == 'b' && g_s2[2] == 0 &&
            g_s2[9] == 0);
  CHECK(16, g_null == 0);
  CHECK(17, g_const == 42);
  CHECK(18, g_weak == 3);
  CHECK(19, (long)g_ip == 4096);
  CHECK(20, g_farr[0] == twice && g_farr[1] == 0);
  return 0;
}
