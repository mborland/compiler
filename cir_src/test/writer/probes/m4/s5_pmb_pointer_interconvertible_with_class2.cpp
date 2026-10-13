//options: --c++20 --gnu_version=140000 -O2
extern "C" int printf(const char *, ...);
// P0466R5
// (M::e and M::f, no_unique_address members at offset 0 of M, cannot be told from the first member by their value: left out.)
// { dg-do run { target c++20 } }

namespace std
{
template <class S, class M>
constexpr bool
is_pointer_interconvertible_with_class (M S::*m) noexcept
{
  return __builtin_is_pointer_interconvertible_with_class (m);
}
}

struct A;
struct B { int b; double b2; };
struct C : virtual B { int c; };
struct D {};
struct E {};
struct F : public B, D, E {};
struct G : public D, E { int g; };
struct H {};
struct I : public G, H {};
struct J { int j1; private: int j2; public: int j3; };
struct K : public J {};
struct L : public B, D, E {};
struct M { D d [[no_unique_address]]; E e [[no_unique_address]]; int f; };
union U { int a; double b; long long c; };
struct V { union { int a; long b; }; int c; };
union X { int a; union { short b; long c; }; long long d; };
struct Y { void foo () {} };
union Z { int a; private: int b; protected: int c; public: int d; };

int
main ()
{ printf("start\n");
  auto t1 = &B::b;
  if (!std::is_pointer_interconvertible_with_class (t1))
    printf("fail 1\n", 1);
  auto t2 = &B::b2;
  if (std::is_pointer_interconvertible_with_class (t2))
    printf("fail 2\n", 2);
  auto t3 = &C::b;
  if (!std::is_pointer_interconvertible_with_class (t3))
    printf("fail 3\n", 3);
  auto t4 = &F::b;
  if (!std::is_pointer_interconvertible_with_class (t4))
    printf("fail 4\n", 4);
  int F::*t5 = &F::b;
  if (!std::is_pointer_interconvertible_with_class (t5))
    printf("fail 5\n", 5);
  auto t6 = &G::g;
  if (!std::is_pointer_interconvertible_with_class (t6))
    printf("fail 6\n", 6);
  int G::*t7 = &G::g;
  if (!std::is_pointer_interconvertible_with_class (t7))
    printf("fail 7\n", 7);
  auto t8 = &I::g;
  if (!std::is_pointer_interconvertible_with_class (t8))
    printf("fail 8\n", 8);
  int I::*t9 = &I::g;
  if (!std::is_pointer_interconvertible_with_class (t9))
    printf("fail 9\n", 9);
  auto t10 = &J::j1;
  if (std::is_pointer_interconvertible_with_class (t10))
    printf("fail 10\n", 10);
  auto t11 = &J::j3;
  if (std::is_pointer_interconvertible_with_class (t11))
    printf("fail 11\n", 11);
  auto t12 = &K::j1;
  if (std::is_pointer_interconvertible_with_class (t12))
    printf("fail 12\n", 12);
  auto t13 = &K::j3;
  if (std::is_pointer_interconvertible_with_class (t13))
    printf("fail 13\n", 13);
  auto t14 = &L::b;
  if (!std::is_pointer_interconvertible_with_class (t14))
    printf("fail 14\n", 14);
  int L::*t15 = &L::b;
  if (!std::is_pointer_interconvertible_with_class (t15))
    printf("fail 15\n", 15);
  auto t16 = &L::b;
  if (!std::is_pointer_interconvertible_with_class (t16))
    printf("fail 16\n", 16);
  auto t17 = &M::d;
  if (!std::is_pointer_interconvertible_with_class (t17))
    printf("fail 17\n", 17);
  
    auto t20 = &U::a;
  if (!std::is_pointer_interconvertible_with_class (t20))
    printf("fail 20\n", 20);
  auto t21 = &U::b;
  if (!std::is_pointer_interconvertible_with_class (t21))
    printf("fail 21\n", 21);
  auto t22 = &U::c;
  if (!std::is_pointer_interconvertible_with_class (t22))
    printf("fail 22\n", 22);
  auto t23 = &V::a;
  if (!std::is_pointer_interconvertible_with_class (t23))
    printf("fail 23\n", 23);
  auto t24 = &V::b;
  if (!std::is_pointer_interconvertible_with_class (t24))
    printf("fail 24\n", 24);
  auto t25 = &V::c;
  if (std::is_pointer_interconvertible_with_class (t25))
    printf("fail 25\n", 25);
  auto t26 = &X::a;
  if (!std::is_pointer_interconvertible_with_class (t26))
    printf("fail 26\n", 26);
  auto t27 = &X::b;
  if (!std::is_pointer_interconvertible_with_class (t27))
    printf("fail 27\n", 27);
  auto t28 = &X::c;
  if (!std::is_pointer_interconvertible_with_class (t28))
    printf("fail 28\n", 28);
  auto t29 = &X::d;
  if (!std::is_pointer_interconvertible_with_class (t29))
    printf("fail 29\n", 29);
  auto t30 = (int B::*) nullptr;
  if (std::is_pointer_interconvertible_with_class (t30))
    printf("fail 30\n", 30);
  auto t31 = &Y::foo;
  if (std::is_pointer_interconvertible_with_class (t31))
    printf("fail 31\n", 31);
  auto t32 = &Z::a;
  if (!std::is_pointer_interconvertible_with_class (t32))
    printf("fail 32\n", 32);
  auto t33 = &Z::d;
  if (!std::is_pointer_interconvertible_with_class (t33))
    printf("fail 33\n", 33);
}
