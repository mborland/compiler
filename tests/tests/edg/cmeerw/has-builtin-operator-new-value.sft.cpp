//type:fp
//options:--gnu_version 140000:--gnu_version 150000:--clang_version 220100
//options_all:-w --c++17

// __has_builtin(__builtin_operator_new) and __has_builtin(__builtin_operator_delete)
// have the value of the C++ feature test macro for sized and aligned
// allocation functions where the compiler has the builtins: 201802 in g++ 15,
// 201802L in clang, and 0 in g++ 14 and earlier.  libstdc++ tests for
// >= 201802L before it uses the builtins.

#define STR2(x) #x
#define STR(x) STR2(x)

#if defined(__clang__)
static_assert(sizeof(STR(__has_builtin(__builtin_operator_new))) == 8, "201802L");
static_assert(sizeof(STR(__has_builtin(__builtin_operator_delete))) == 8, "201802L");
#elif __GNUC__ >= 15
static_assert(sizeof(STR(__has_builtin(__builtin_operator_new))) == 7, "201802");
static_assert(sizeof(STR(__has_builtin(__builtin_operator_delete))) == 7, "201802");
#else
static_assert(sizeof(STR(__has_builtin(__builtin_operator_new))) == 2, "0");
static_assert(sizeof(STR(__has_builtin(__builtin_operator_delete))) == 2, "0");
#endif

#if defined(__clang__) || __GNUC__ >= 15
#if __has_builtin(__builtin_operator_new) != 201802L || \
    __has_builtin(__builtin_operator_delete) != 201802L
#error the builtins must report 201802
#endif
#else
#if __has_builtin(__builtin_operator_new) || __has_builtin(__builtin_operator_delete)
#error the builtins are not available
#endif
#endif
