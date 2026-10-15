//type:fp
//options:--clang
//remark:[GH #?] The macOS clang bases no longer predefine __BLOCKS__ and __block
// 10/15/26  [GH #?]
//
// The front end does not support Apple's blocks extension (there is no block
// pointer type, and "^" cannot start a declarator), so __BLOCKS__ must not be
// predefined: the macOS SDK headers declare their block-taking functions
// (atexit_b, bsearch_b, qsort_b, ...) under "#ifdef __BLOCKS__", and every
// translation unit that included <stdlib.h> got errors.  The macOS clang bases
// (bases/cmake-native/macos-*/clang) used to define the macro because Apple
// clang does.  Clang defines __BLOCKS__ and __block together (and only when
// the extension is enabled), so __block must not be predefined either.  This
// agrees with the front end's own __has_feature(blocks) and
// __has_extension(blocks), which are 0 (libc++ tests them).
//
// The Linux bases never defined these macros, so this test passes with them
// with or without the change.  It is only a regression check for the macOS
// bases, run with a native build (see "MacOS" in HACKING.md); there it failed
// before the macros were removed from the bases.  If the front end gains
// support for blocks, the macros can be predefined again and the checks below
// updated.

// The declaration that failed to parse.
#if __BLOCKS__
int atexit_b(void (^)(void));
#endif

#ifdef __BLOCKS__
#error "__BLOCKS__ is defined"
#endif

#ifdef __block
#error "__block is defined"
#endif

#if __has_feature(blocks) || __has_extension(blocks)
#error "the front end does not support blocks"
#endif

int main() { return 0; }
