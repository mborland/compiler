//type:fp
//options:--c++20 --gnu_version=999999 -tused
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A function instance whose template argument is a lambda in the default
// argument of another function is named when it is instantiated, before the
// lambda has a parent routine: its name is late (gnu/cpp/cpp2a/lambda-uneval17
// failed an assertion in the mangler).  A static local of such a function
// (h<lambda>) is named from the function's name, so it waits too.

constexpr char f(...) = delete;
constexpr decltype(auto) f_adl(auto a) { return f(a); }

namespace A {
    constexpr char f(auto) { return 'A'; }
    template<class T> int h(T) { static int count = 0; return ++count; }
    template<char TemplateParam = f_adl([]{})> void g(char FunctionParam = f_adl([]{}), int Counter = h([]{})) {
        char Local = f_adl([]{});
    }
}

namespace B {
    constexpr char f(auto) = delete;
    void call() { A::g(); }
}
