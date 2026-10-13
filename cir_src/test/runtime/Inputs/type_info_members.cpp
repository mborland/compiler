// The members of std::type_info through edg_cir_rt against g++'s inline ones.
#include <cstdio>
#include <cstring>
#include <typeinfo>

extern "C" {
bool rt_eq(const std::type_info *, const std::type_info *)
    __asm__("_ZNKSt9type_infoeqERKS_");
bool rt_ne(const std::type_info *, const std::type_info *)
    __asm__("_ZNKSt9type_infoneERKS_");
bool rt_before(const std::type_info *, const std::type_info *)
    __asm__("_ZNKSt9type_info6beforeERKS_");
const char *rt_name(const std::type_info *)
    __asm__("_ZNKSt9type_info4nameEv");
}

struct fake : std::type_info {
  explicit fake(const char *n) : std::type_info(n) {}
};

int main() {
  static const char star_a[] = "*1A";
  static const char star_a2[] = "*1A";   // another object, same text
  static const char star_b[] = "*1B";
  static const char plain_a[] = "1A";
  static const char plain_a2[] = "1A";   // another object, same text
  static const char plain_b[] = "1B";
  static const char plain_z[] = "1Z";
  const char *names[] = {star_a, star_a2, star_b, plain_a, plain_a2, plain_b,
                         plain_z};
  const int n = sizeof(names) / sizeof(names[0]);
  fake *objects[n];
  int failures = 0;

  for (int i = 0; i < n; ++i)
    objects[i] = new fake(names[i]);
  for (int i = 0; i < n; ++i) {
    if (std::strcmp(rt_name(objects[i]), objects[i]->name()) != 0) {
      std::printf("name %d\n", i);
      ++failures;
    }
    for (int j = 0; j < n; ++j) {
      const std::type_info &a = *objects[i];
      const std::type_info &b = *objects[j];
      if (rt_eq(&a, &b) != (a == b) || rt_ne(&a, &b) != (a != b) ||
          rt_before(&a, &b) != a.before(b)) {
        std::printf("pair %d %d: eq %d/%d before %d/%d\n", i, j,
                    (int)rt_eq(&a, &b), (int)(a == b),
                    (int)rt_before(&a, &b), (int)a.before(b));
        ++failures;
      }
    }
  }
  // before is a strict weak order over the mixture (what std::set needs):
  // irreflexive, asymmetric and transitive.
  for (int i = 0; i < n; ++i) {
    if (rt_before(objects[i], objects[i])) ++failures;
    for (int j = 0; j < n; ++j) {
      if (rt_before(objects[i], objects[j]) && rt_before(objects[j], objects[i]))
        ++failures;
      for (int k = 0; k < n; ++k)
        if (rt_before(objects[i], objects[j]) &&
            rt_before(objects[j], objects[k]) &&
            !rt_before(objects[i], objects[k]))
          ++failures;
    }
  }
  std::printf(failures ? "%d failures\n" : "ok\n", failures);
  return failures != 0;
}
