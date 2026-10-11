//type:rp
//options:--c++17
//require:BACK_END_TYPE 3
// new[] and delete[] (slice S4): array cookies, constructor and destructor
// order, an over-aligned element type, class-specific operator new/delete with
// size, delete of null, a replaced global operator new[] that records the sizes
// it is asked for.
extern "C" int printf(const char *, ...);
extern "C" void *malloc(unsigned long);
extern "C" void free(void *);
unsigned long g_new_size, g_del_size;
void *g_new_ptr, *g_del_ptr;
int log_[64];
int n_;
void *operator new[](unsigned long sz) { g_new_size = sz; g_new_ptr = malloc(sz); return g_new_ptr; }
void operator delete[](void *p) noexcept { g_del_ptr = p; free(p); }
void operator delete[](void *p, unsigned long sz) noexcept { g_del_ptr = p; g_del_size = sz; free(p); }
struct T {
  int id;
  T() { id = n_; log_[n_++] = id; }
  ~T() { log_[n_++] = id + 100; }
};
struct alignas(32) O {
  int x;
  O() { x = 7; }
  ~O() { x = -1; }
};
struct C {
  int v;
  C() : v(3) {}
  ~C() {}
  static void *operator new(unsigned long sz) { g_new_size = sz; return malloc(sz); }
  static void operator delete(void *p, unsigned long sz) { g_del_size = sz; free(p); }
};
int bad;
int main() {
  T *p = new T[3];
  if (g_new_size != 3 * sizeof(T) + 8) bad |= 1;
  if (((unsigned long)p & 3) != 0 || (char *)p - (char *)g_new_ptr != 8) bad |= 2;
  delete[] p;
  if (g_del_size != g_new_size || g_del_ptr != g_new_ptr) bad |= 4;
  const int want[] = {0, 1, 2, 102, 101, 100};
  if (n_ != 6) bad |= 8;
  for (int i = 0; i < 6 && i < n_; ++i) if (log_[i] != want[i]) bad |= 16;
  O *o = new O[2];
  if (((unsigned long)o & 31) != 0 || o[1].x != 7) bad |= 64;
  delete[] o;
  C *c = new C;
  if (g_new_size != sizeof(C) || c->v != 3) bad |= 128;
  delete c;
  if (g_del_size != sizeof(C)) bad |= 256;
  T *np = nullptr;
  delete[] np;
  int *ip = new int[5];
  ip[4] = 9;
  delete[] ip;
  printf("bad=%d\n", bad);
  return bad != 0;
}
