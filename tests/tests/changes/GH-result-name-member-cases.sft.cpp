//type:fp
//options_all:--c++20 --instantiate=used
//remark:[GH #?] Member functions defined outside their class with a registered attribute that can name the result
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:41
// 10/15/26  [GH #?]
//
// A registered attribute that can name the result of a function (r_post,
// with and without a name) and one that cannot (r_pre) in the declaration of a
// member function defined outside its class use the members of the class and
// "this" like any other attribute after the parameter list.  The scope that is
// pushed for the name of the result, for a function that has one or not, is
// not the scope of the declarator; it was taken for it when "this" was looked
// up for a member function that is not yet known to be nonstatic, which was an
// internal error for a member function without a qualifier whose contract used
// a member.  Each case is a shape of member function.

#define E(...) [[edg::r_post(__VA_ARGS__)]]
#define Q(...) [[edg::r_pre(__VA_ARGS__)]]
#if TEST_NUMBER == 1
// coordinator repro
struct counter { int val; int bump(); }; int counter::bump() E(val >= 0) { return val; }
#elif TEST_NUMBER == 2
// named result
struct C { int val; int bump(); }; int C::bump() E(r, r == val) { return val; }
#elif TEST_NUMBER == 3
// explicit this
struct C { int val; int bump(); }; int C::bump() E(this->val >= 0) { return val; }
#elif TEST_NUMBER == 4
// requires, same shape (reported OK)
struct C { int val; int bump(); }; int C::bump() Q(val >= 0) { return val; }
#elif TEST_NUMBER == 5
// const member (reported OK)
struct C { int val; int bump() const; }; int C::bump() const E(val >= 0) { return val; }
#elif TEST_NUMBER == 6
// in-class decl has ensures, out-of-class def repeats it
struct C { int val; int bump() E(val >= 0); }; int C::bump() E(val >= 0) { return val; }
#elif TEST_NUMBER == 7
// in-class decl has ensures, out-of-class def has none
struct C { int val; int bump() E(val >= 0); }; int C::bump() { return val; }
#elif TEST_NUMBER == 8
// static member function, uses static data member
struct C { static int s; static int g(); }; int C::g() E(s >= 0) { return s; }
#elif TEST_NUMBER == 9
// conversion function out of class, unnamed ensures
struct C { int val; operator int(); }; C::operator int() E(val >= 0) { return val; }
#elif TEST_NUMBER == 10
// constructor out of class
struct C { int val; C(int x); }; C::C(int x) E(val == x) : val(x) {}
#elif TEST_NUMBER == 11
// destructor out of class
struct C { int val; ~C(); }; C::~C() E(val == 0) {}
#elif TEST_NUMBER == 12
// operator out of class, named
struct C { int val; int operator+(int y); }; int C::operator+(int y) E(r, r == val + y) { return val + y; }
#elif TEST_NUMBER == 13
// friend defined out of class (no this)
struct C { int val; friend int fr(C &c); }; int fr(C &c) E(c.val >= 0) { return c.val; }
#elif TEST_NUMBER == 14
// member template of a non-template class, out of class
struct C { int val; template<class U> int g(U u); }; template<class U> int C::g(U u) E(val >= 0) { return val; }
int use() { C c; return c.g(1); }
#elif TEST_NUMBER == 15
// member of class template, out of class
template<class T> struct C { T val; int bump(); }; template<class T> int C<T>::bump() E(val >= 0) { return 0; }
int use() { C<int> c; return c.bump(); }
#elif TEST_NUMBER == 16
// member template of class template, out of class
template<class T> struct C { T val; template<class U> int g(U u); }; template<class T> template<class U> int C<T>::g(U u) E(r, r >= val) { return 0; }
int use() { C<int> c; return c.g(1L); }
#elif TEST_NUMBER == 17
// nested class member out of class
struct O { struct I { int v; int g(); }; }; int O::I::g() E(v > 0) { return v; }
#elif TEST_NUMBER == 18
// volatile member function
struct C { int val; int bump() volatile; }; int C::bump() volatile E(val >= 0) { return val; }
#elif TEST_NUMBER == 19
// ref-qualified member functions
struct C { int val; int b1() &; int b2() &&; }; int C::b1() & E(val >= 0) { return val; } int C::b2() && E(val >= 0) { return val; }
#elif TEST_NUMBER == 20
// member call through implicit this
struct C { int val; bool ok() const; int bump(); }; int C::bump() E(ok()) { return val; }
#elif TEST_NUMBER == 21
// out-of-class declaration-only of member of class template (explicit specialization), ensures uses member
template<class T> struct C { T val; int bump() E(val >= 0); }; template<> int C<int>::bump() E(val >= 0) { return 0; }
#elif TEST_NUMBER == 22
// explicit spec without contract, inherited ensures with this
template<class T> struct C { T val; int bump() E(val >= 0); }; template<> int C<int>::bump() { return 0; }
#elif TEST_NUMBER == 23
// unnamed ensures, in-class definition (reported OK)
struct C { int val; int bump() E(val >= 0) { return val; } };
#elif TEST_NUMBER == 24
// ensures with this, out-of-class defined member of local class? (not allowed); use a union instead
union U { int a; int g(); }; int U::g() E(a >= 0) { return a; }
#elif TEST_NUMBER == 25
// operator= out of class returning *this, named result address check
struct C { int val; C &operator=(const C &o); }; C &C::operator=(const C &o) E(r, &r == this && val == o.val) { val = o.val; return *this; }
#elif TEST_NUMBER == 26
// declared in class without ensures, defined out of class with ensures in a namespace
namespace N { struct C { int val; int bump(); }; } int N::C::bump() E(val >= 0) { return val; }
#elif TEST_NUMBER == 27
// virtual member out of class
struct C { int val; virtual int bump(); }; int C::bump() E(val >= 0) { return val; }
#elif TEST_NUMBER == 28
// mutable access from const member: ok
struct C { mutable int val; int bump() const; }; int C::bump() const E(r, r == val) { return val; }
#elif TEST_NUMBER == 29
struct counter { int val; void set(int w); }; void counter::set(int w) E(val == w) { val = w; }
#elif TEST_NUMBER == 30
struct counter { int val; void set(int w) E(val == w); }; void counter::set(int w) E(val == w) { val = w; }
#elif TEST_NUMBER == 31
// in-class ensures, out-of-class without: OK?
struct counter { int val; void set(int w) E(val == w); }; void counter::set(int w) { val = w; }
#elif TEST_NUMBER == 32
// ensures not using members at all, out of class, non-const member
struct counter { int val; int get(int w); }; int counter::get(int w) E(w >= 0) { return w; }
#elif TEST_NUMBER == 33
// ensures with named result not using members, out of class
struct counter { int val; int get(int w); }; int counter::get(int w) E(r, r == w) { return w; }
#elif TEST_NUMBER == 34
// ensures with true only, out of class
struct counter { int val; int get(int w); }; int counter::get(int w) E(true) { return w; }
#elif TEST_NUMBER == 35
// ensures referencing a static data member (no this), out of class
struct counter { static int s; int get(int w); }; int counter::get(int w) E(s >= 0) { return w; }
#elif TEST_NUMBER == 36
// ensures referencing a member function name in sizeof/decltype (unevaluated)
struct counter { int val; int get(int w); }; int counter::get(int w) E(sizeof(val) == 4) { return w; }
#elif TEST_NUMBER == 37
// requires + ensures both using this, out of class
struct counter { int val; void set(int w); }; void counter::set(int w) Q(val >= 0) E(val == w) { val = w; }
#elif TEST_NUMBER == 38
// class template, void member, in-class ensures and out-of-class def repeating it, instantiated
template<class T> struct counter { T val; void set(T w) E(val == w); };
template<class T> void counter<T>::set(T w) E(val == w) { val = w; }
void u() { counter<int> c; c.set(1); }
#elif TEST_NUMBER == 39
template<class T> struct P {}; template<class T> struct P<T*> { T *v; int g(); }; template<class T> int P<T*>::g() E(v != nullptr) { return 0; }
int u() { P<int*> p; return p.g(); }
#elif TEST_NUMBER == 40
// in-class definition in a class template, member use: OK expected
template<class T> struct C { T val; int g() E(val >= 0) { return 0; } }; int u() { C<int> c; return c.g(); }
#elif TEST_NUMBER == 41
// out-of-class, noexcept member, unnamed ensures with member
struct C { int val; int g() noexcept; }; int C::g() noexcept E(val >= 0) { return val; }
#endif
