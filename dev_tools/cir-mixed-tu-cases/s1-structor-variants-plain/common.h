/* The same without virtual functions: a non-polymorphic base whose
   constructor and destructor are external (a.cpp) and a derived class
   (b.cpp) that calls their base-object variants C2 and D2. */
struct A {
  A();
  ~A();
  int a;
};
struct B : A {
  B();
  ~B();
  int b;
};
B *make_b();          // b.cpp
void destroy(B *p);   // b.cpp: delete p
int sum(A *p);        // a.cpp
