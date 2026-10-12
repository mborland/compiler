//options: --c++20
// M4a probe: key function corner cases: constexpr virtual (implicitly inline),
// a defaulted destructor defined out of class (not user-provided), a deleted
// virtual function, a consteval virtual function, and an inline virtual
// function defined after the class.
struct C1 { constexpr virtual int a() const { return 1; } virtual void b(); };           // key b
struct C2 { virtual ~C2(); virtual void b(); };
C2::~C2() = default;                                                                    // not user-provided
struct C3 { virtual void a() = delete; virtual void b(); };                              // key b?
struct C4 { virtual consteval int a() const { return 1; } virtual void b(); };           // key b
struct C5 { constexpr virtual int a() const; virtual void b(); };                        // a is constexpr: skipped
constexpr int C5::a() const { return 2; }
struct C6 { virtual void a(); virtual void b(); };
inline void C6::a() {}                                                                   // inline later: no key
void C1::b() {} void C2::b() {} void C3::b() {} void C4::b() {} void C5::b() {} void C6::b() {}
void use() { C1 c1; C2 c2; C3 c3; C4 c4; C5 c5; C6 c6; }
int main() { return 0; }
