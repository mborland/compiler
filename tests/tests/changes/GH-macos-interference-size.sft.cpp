//type:fp
//options_all:--clang --c++17
//remark:[GH #?] The macOS clang bases define __GCC_CONSTRUCTIVE_SIZE and __GCC_DESTRUCTIVE_SIZE
// 10/15/26  [GH #?]
//
// Clang 19 and later predefine __GCC_CONSTRUCTIVE_SIZE and
// __GCC_DESTRUCTIVE_SIZE, and libc++ (<__new/interference_size.h>, reached by
// <new>, <string>, <vector>, <algorithm>, ...) uses them unconditionally from
// C++17 on to define std::hardware_constructive_interference_size and
// std::hardware_destructive_interference_size.  The macOS clang bases
// (bases/cmake-native/macos-*/clang) did not define them, so every C++17 or
// later translation unit that reached <new> failed with "identifier
// "__GCC_DESTRUCTIVE_SIZE" is undefined".
//
// The values depend on the target (Apple clang 21 gives 64 and 256 for arm64,
// 64 and 64 for x86_64), so the test checks that the macros are defined and
// usable the way libc++ uses them, not their values.
//
// This is a regression check for the macOS bases, run with a native build
// (see "MacOS" in HACKING.md).  The Linux bases only define the macros for
// GNU mode (the GCC base) or for clang mode (the clang base), not for
// --clang with the GCC base used by the Docker configurations, so the checks
// are made only on a macOS host in clang mode (__APPLE__ is predefined by the
// front end itself when it is built on macOS, whatever the base is, and
// __clang__ by --clang), which is where the SDK's libc++ needs the macros;
// elsewhere the test is empty and passes.

#if defined(__APPLE__) && defined(__clang__)

#ifndef __GCC_DESTRUCTIVE_SIZE
#error "__GCC_DESTRUCTIVE_SIZE is not defined"
#endif
#ifndef __GCC_CONSTRUCTIVE_SIZE
#error "__GCC_CONSTRUCTIVE_SIZE is not defined"
#endif

typedef decltype(sizeof(0)) size_t;

// As in libc++'s <__new/interference_size.h>.
inline constexpr size_t hardware_destructive_interference_size =
    __GCC_DESTRUCTIVE_SIZE;
inline constexpr size_t hardware_constructive_interference_size =
    __GCC_CONSTRUCTIVE_SIZE;

// Both are positive powers of two, and the destructive size (the one that
// avoids false sharing) is not smaller than the constructive one.
static_assert(hardware_constructive_interference_size > 0 &&
              (hardware_constructive_interference_size &
               (hardware_constructive_interference_size - 1)) == 0,
              "constructive size is a power of two");
static_assert(hardware_destructive_interference_size > 0 &&
              (hardware_destructive_interference_size &
               (hardware_destructive_interference_size - 1)) == 0,
              "destructive size is a power of two");
static_assert(hardware_destructive_interference_size >=
              hardware_constructive_interference_size,
              "destructive size is not smaller than constructive size");

#endif

int main() { return 0; }
