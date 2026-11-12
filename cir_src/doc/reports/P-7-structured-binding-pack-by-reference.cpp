/* P-7 reproducer: in --c++26 mode a reference initialized from the name of a
   reference parameter binds to the parameter's own storage, not to its
   referent.  Expected output (clang++ -std=c++2c): every line ends in "ok".
   The pack forms need --c++26; without them the file is correct in
   --c++17, --c++20 and --c++23.  See the report beside this file. */
extern "C" int printf(const char *, ...);
struct T3 { int a; long b; char c; };

/* 1. The smallest form: no template, no auto, no structured binding. */
long typed_cref(const T3 &s) { const T3 &r = s; return (long)r.a + r.b + r.c; }
long auto_ref(T3 &s) { auto &r = s; return (long)r.a + r.b + r.c; }
long scalar_cref(const int &i) { const int &r = i; return r; }

/* 2. Structured bindings by reference (the form M6.1a found first). */
long binding(const T3 &s) { const auto &[a, b, c] = s; return (long)a + b + c; }
template <typename S> long binding_pack(const S &s)
{
  const auto &[...refs] = s;
  return (0L + ... + (long)refs);
}
template <typename S> void write_pack(S &s) { auto &[...r] = s; ((r = 7), ...); }

/* 3. In a template a local reference is affected too. */
template <typename S> long local_ref(const S *p)
{
  const S &s = *p;
  const auto &[a, b, c] = s;
  return (long)a + b + c;
}

/* 4. Controls that are correct in every mode. */
long from_object(T3 s) { const auto &r = s; return (long)r.a + r.b + r.c; }
template <typename S> long by_value_pack(const S &s)
{
  auto [...v] = s;
  return (0L + ... + (long)v);
}
template <typename S> long from_deref(const S *p)
{
  const auto &[a, b, c] = *p;
  return (long)a + b + c;
}

static int failures;
static void check(const char *name, bool ok)
{
  printf("%-14s %s\n", name, ok ? "ok" : "WRONG");
  if (!ok) failures++;
}

int main()
{
  T3 t = {1, 2, 3};
  int i = 6;
  check("typed_cref", typed_cref(t) == 6);
  check("auto_ref", auto_ref(t) == 6);
  check("scalar_cref", scalar_cref(i) == 6);
  check("binding", binding(t) == 6);
  check("binding_pack", binding_pack(t) == 6);
  check("local_ref", local_ref(&t) == 6);
  check("from_object", from_object(t) == 6);
  check("by_value_pack", by_value_pack(t) == 6);
  check("from_deref", from_deref(&t) == 6);
  write_pack(t);
  check("write_pack", t.a == 7 && t.b == 7 && t.c == 7);
  return failures != 0;
}
