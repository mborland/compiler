//options:--c++17
// M4 slice S1: a polymorphic zoo with multiple inheritance (non-primary bases,
// this adjustments), covariant returns, abstract classes, virtual calls in
// constructors and destructors, arrays of objects, deletes through every base,
// a static object and a local class.  The output is compared with g++'s.
#include <cstdio>
struct Animal {
  int legs;
  explicit Animal(int l) : legs(l) { std::printf("Animal(%d) sees %s\n", l, name()); }
  virtual const char *name() const { return "animal"; }
  virtual int sound() const = 0;
  virtual Animal *clone() const = 0;
  virtual ~Animal() { std::printf("~Animal sees %s\n", name()); }
};
struct Swimmer {
  int depth;
  Swimmer() : depth(7) {}
  virtual int dive(int d) const { return depth + d; }
  virtual Swimmer *self() { return this; }
  virtual ~Swimmer() { std::printf("~Swimmer\n"); }
};
struct Duck : Animal, Swimmer {
  int feathers;
  Duck() : Animal(2), feathers(100) { std::printf("Duck() sees %s\n", name()); }
  const char *name() const override { return "duck"; }
  int sound() const override { return 1; }
  Duck *clone() const override { return new Duck(*this); }
  int dive(int d) const override { return Swimmer::dive(d) * 2; }
  Duck *self() override { return this; }
  ~Duck() override { std::printf("~Duck sees %s\n", name()); }
};
struct Mallard final : Duck {
  const char *name() const override { return "mallard"; }
  int sound() const override { return 3; }
};
struct Dog : Animal {
  Dog() : Animal(4) {}
  int sound() const override { return 2; }
  Animal *clone() const override { return new Dog(*this); }
};
static Dog global_dog;
int total(const Animal *const *zoo, int n) {
  int sum = 0;
  for (int i = 0; i < n; i++) sum += zoo[i]->sound() * 10 + zoo[i]->legs;
  return sum;
}
int main() {
  Duck d;
  Mallard m;
  Dog dog;
  const Animal *zoo[] = { &d, &m, &dog, &global_dog };
  std::printf("total %d\n", total(zoo, 4));
  Swimmer *s = &m;
  std::printf("dive %d %d\n", s->dive(1), static_cast<Swimmer &>(d).dive(2));
  Animal *a = d.clone();
  Animal *b = dog.clone();
  std::printf("clones %s %s\n", a->name(), b->name());
  Swimmer *ds = static_cast<Duck *>(a);
  std::printf("self %d\n", ds->self() == ds);
  delete a;
  delete b;
  Animal *arr[3] = { new Duck, new Dog, new Mallard };
  for (Animal *p : arr) std::printf("%s\n", p->name());
  for (Animal *p : arr) delete p;
  struct Local : Swimmer { int dive(int d) const override { return d - 1; } };
  Local l;
  Swimmer &ls = l;
  std::printf("local %d\n", ls.dive(10));
  return 0;
}
