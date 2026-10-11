extern "C" int printf(const char *, ...);
extern "C" void *malloc(unsigned long);
extern "C" void free(void *);
void *operator new(unsigned long, void *p) noexcept { return p; }
int trace;
void note(int x) { trace = trace * 10 + x; }
struct T { int id; T() : id(1) { note(1); } T(int i) : id(i) { note(i); } ~T() { note(9); } };
struct P { int a; int b; };
struct Cs { int v; Cs(int x) : v(x) {} static void *operator new(unsigned long sz) { note(7); return malloc(sz); } static void operator delete(void *p) { note(8); free(p); } };
int scalars() {
  int *a = new int; *a = 3;
  int *b = new int(5);
  int *c = new int[4]; for (int i = 0; i < 4; i++) c[i] = i * i;
  P *p = new P();
  P *q = new P{1, 2};
  int r = *a + *b + c[3] + p->a + q->b;
  delete a; delete b; delete[] c; delete p; delete q;
  return r;
}
int objects(int n) {
  T *t1 = new T;
  T *t2 = new T(4);
  T *arr = new T[n];
  T *arr2 = new T[2]{ T(5), T(6) };
  int r = t1->id + t2->id + arr[n - 1].id + arr2[1].id;
  delete t1; delete t2; delete[] arr; delete[] arr2;
  return r;
}
int placement() {
  alignas(T) char buf[sizeof(T)];
  T *t = new (buf) T(2);
  int r = t->id;
  t->~T();
  Cs *c = new Cs(3);
  r += c->v;
  delete c;
  return r;
}
int main() {
  printf("%d ", scalars()); printf("%d\n", trace); trace = 0;
  printf("%d ", objects(3)); printf("%d\n", trace); trace = 0;
  printf("%d ", placement()); printf("%d\n", trace); trace = 0;
  return 0;
}
