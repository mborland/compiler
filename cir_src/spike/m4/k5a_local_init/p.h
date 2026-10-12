extern "C" int printf(const char *, ...);
// Function-local statics: a throwing initializer must abort the guard
// (retry on the next call); a static local in a structor with two variants
// (C1 and C2) is one object guarded once.
struct T { int id; T(int i) : id(i) { printf("T(%d)\n", id); } ~T() { printf("~T(%d)\n", id); } };
int mk(int k);
int once(int k);
int twice(int k);
struct V { int v; V(); virtual ~V(); };
struct D : virtual V { int d; D(int k); ~D() override; };
struct E : D { E(int k); ~E() override; };
int ctor_static(int k);
int inline_static(int k);
