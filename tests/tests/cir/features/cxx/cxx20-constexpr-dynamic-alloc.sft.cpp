//type:rp
//options:--c++20:--c++20 -O2
//require:BACK_END_TYPE 3
/* cxx20.constexpr_dynamic_alloc: new and delete in constant evaluation; the same code at run time. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
constexpr int sum_alloc(int n)
{
  int *p = new int[n];
  for (int i = 0; i < n; ++i) p[i] = i + 1;
  int s = 0;
  for (int i = 0; i < n; ++i) s += p[i];
  delete[] p;
  return s;
}
static_assert(sum_alloc(10) == 55, "array");

struct Node {
  int v;
  Node *next;
  constexpr Node(int x, Node *n) : v(x), next(n) {}
};
constexpr int list_sum(int n)
{
  Node *head = nullptr;
  for (int i = 1; i <= n; ++i) head = new Node(i, head);
  int s = 0;
  while (head) {
    s += head->v;
    Node *dead = head;
    head = head->next;
    delete dead;
  }
  return s;
}
static_assert(list_sum(6) == 21, "list");

constexpr int single()
{
  int *p = new int(41);
  ++*p;
  int r = *p;
  delete p;
  return r;
}
static_assert(single() == 42, "single");

struct Owner {
  int *p;
  constexpr Owner(int v) : p(new int(v)) {}
  constexpr ~Owner() { delete p; }
  constexpr int get() const { return *p; }
};
constexpr int owner_test() { Owner o(7); return o.get() * 2; }
static_assert(owner_test() == 14, "owner");

static void run()
{
  int n = 12;
  CHECK(sum_alloc(n) == 78);
  CHECK(list_sum(n) == 78);
  CHECK(single() == 42);
  CHECK(owner_test() == 14);
  constexpr int c = sum_alloc(4);
  CHECK(c == 10);
}

int main()
{
  run();
  if (failures == 0) puts("cxx20.constexpr_dynamic_alloc ok");
  return failures != 0;
}
