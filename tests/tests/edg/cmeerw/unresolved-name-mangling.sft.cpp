//type:fp
//require:DO_IL_LOWERING 1
//options:--gnu_version 150200:--clang_version 220000
//options_all:-w --c++17 -tused --il_display
//filter:awk '/^func-scope variable@/{f=1; next}/^$/{f=0}f' | grep -E '^  enclosing_routine:' | sed -e 's/@[0-9a-f]*//' -e 's/^  enclosing_routine: *file-scope routine: //' | grep '^_Z'

// The mangled names of function templates whose return type or parameter
// type contains an expression that depends on a template parameter (the
// usual enable_if), instantiated with int.  In GNU mode they are those of
// g++ 15.2 (g++ 4.9 to 16 give the same names at every -fabi-version): the
// scope of a member of a class template is "sr" followed by the type of the
// class (srNS_4is_dIT_EE5value), and a variable template specialization with
// dependent arguments is the name with the arguments (X2vtIT_EE).  In clang
// mode the scope is still written as a level for each enclosing scope
// (sr1n4is_dIT_EE), which is not what clang writes when the qualifier is not
// spelled the same way in the whole translation unit; the variable templates
// are those of clang 22.

template<bool B, class T = void> struct enable_if {};
template<class T> struct enable_if<true, T> { typedef T type; };

template<class T> struct is_g { static constexpr bool value = true; };
template<class T> constexpr bool vt_g = true;

// Global scope, namespace, nested namespaces.
template<class T> typename enable_if<is_g<T>::value, T>::type
s1(T x) { return x; }
template<class T> typename enable_if<vt_g<T>, T>::type
s7(T x) { return x; }

namespace n {
template<class T> struct is_d { static constexpr bool value = true; };
template<class T> struct is_e { static constexpr bool value = true; };
template<class T> constexpr bool vt = true;
template<class T> typename enable_if<is_d<T>::value, T>::type
s2(T x) { return x; }
// A repeated qualifier, and a different template-id with the same template.
template<class T> typename enable_if<is_d<T>::value && is_d<T*>::value, T>::type
s6a(T x) { return x; }
template<class T> typename enable_if<n::is_d<T>::value && n::is_e<T>::value, T>::type
s6b(T x) { return x; }
template<class T> typename enable_if<is_d<T>::value || is_e<T>::value, T>::type
s6c(T x, T *) { return x; }
template<class T> typename enable_if<vt<T>, T>::type
s8(T x) { return x; }
template<class T> typename enable_if<vt<T*> || vt<T>, T>::type
p4(T x) { return x; }
template<class T> typename enable_if<vt<T> && vt_g<T*>, T>::type
p8(T x) { return x; }
template<class T> typename enable_if<(&vt<int> != nullptr) && is_d<T>::value, T>::type
p3(T x) { return x; }
namespace m {
template<class T> struct is_d { static constexpr bool value = true; };
template<class T> typename enable_if<is_d<T>::value, T>::type
s3(T x) { return x; }
}
}

// A written qualifier, from the global scope and from other namespaces.
template<class T> typename enable_if<n::is_d<T>::value, T>::type
s5a(T x) { return x; }
namespace o {
template<class T> typename enable_if<n::is_d<T>::value, T>::type
s5b(T x) { return x; }
template<class T> typename enable_if<n::m::is_d<T>::value, T>::type
s5c(T x) { return x; }
}

// A member of a class template, and a written qualifier in a member.
template<class U> struct W {
  template<class T> struct is_m { static constexpr bool value = true; };
  template<class T> static typename enable_if<is_m<T>::value, T>::type
  s4(T x) { return x; }
  template<class T> static typename enable_if<n::is_d<T>::value, T>::type
  s4b(T x) { return x; }
};

// The condition in a parameter type.
template<class T> void s11(T x, typename enable_if<n::is_d<T>::value, int>::type) { }

// Classes nested in class templates (the qualifier is a nested class).
template<class V> struct CT { struct in { static constexpr bool value = true; }; };
namespace n {
template<class V> struct NT { struct in { static constexpr bool value = true; }; };
}
template<class V> struct CU {
  template<class U> struct in2 { static constexpr bool value = true; };
};
template<class T> typename enable_if<CT<T>::in::value, T>::type
q1(T x) { return x; }
template<class T> typename enable_if<n::NT<T>::in::value, T>::type
q2(T x) { return x; }
template<class T> typename enable_if<CU<T>::template in2<T>::value, T>::type
q3(T x) { return x; }

// A static member variable template of a class that is not a template (p5)
// and of a class template specialization (p6), and decltype of qualified
// calls, operators and member access (d1-d7).  Not here: a pseudo-destructor
// call (g++ and clang disagree), a member template called with arguments
// (the qualifier is lost), CV6<T>::template cv<T> (a spurious "ad"), and a
// static variable template (its name has the module's name in this build).
struct S5 { template<class T> static constexpr bool sv = true; };
template<class V> struct CV6 {
  template<class T> static constexpr bool cv = true;
};
template<class T> typename enable_if<S5::sv<T>, T>::type
p5(T x) { return x; }
template<class T> typename enable_if<CV6<int>::cv<T>, T>::type
p6(T x) { return x; }
namespace n {
template<class V> struct C {
  static V f(V v) { return v; }
  static V g(V v, V) { return v; }
  V operator+(V v) const { return v; }
  V operator[](V v) const { return v; }
  V operator()(V v) const { return v; }
  V m;
};
}
template<class T> auto d1(T x) -> decltype(n::C<T>::f(x)) { return x; }
template<class T> auto d2(T x) -> decltype(n::C<T>::g(x, x)) { return x; }
template<class T> auto d3(T x) -> decltype(n::C<T>() + x) { return x; }
template<class T> auto d4(T x) -> decltype(n::C<T>().operator+(x)) {
  return x;
}
template<class T> auto d5(T x) -> decltype(n::C<T>()[x]) { return x; }
template<class T> auto d6(T x) -> decltype(n::C<T>()(x)) { return x; }
template<class T> auto d7(T x) -> decltype(n::C<T>().m) { return x; }

void use() {
  s1(1); s7(1); n::s2(1); n::m::s3(1); s5a(1); o::s5b(1); o::s5c(1);
  W<int>::s4(1); W<int>::s4b(1); n::s6a(1); n::s6b(1); n::s6c(1, (int *)0);
  n::s8(1); n::p3(1); n::p4(1); n::p8(1); s11(1, 1); q1(1); q2(1); q3(1);
  p5(1); p6(1);
  d1(1); d2(1); d3(1); d4(1); d5(1); d6(1); d7(1);
}
