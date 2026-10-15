//type:fp
//options:--c++17 --strict:--c++20:--c++20 --instantiate=used:--ms_c++20
//remark:[GH #?] An explicit specialization of a member template declared in its class template
// 10/15/26  [GH #?]
//
// An explicit specialization of a member template that is declared in the
// class (valid since CWG 727, and in Microsoft mode) of a class template is
// accepted, whether the class template is instantiated or not.  When such a
// specialization is declared, its template is not known yet: the checks of
// the registered attributes of explicit specializations must not use the
// symbol of a template then (they used an uninitialized one, and the front end
// could crash).

template <class T> struct Outer {
  template <class U> static int f(U) { return 0; }
  template <> int f<int>(int) { return 1; }
};

template <class T> struct Never_instantiated {
  template <class U> int g(U) { return 0; }
  template <> int g<long>(long) { return 2; }
};

int x = Outer<char>::f(3) + Outer<char>::f(3L);
