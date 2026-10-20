//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// cold and hot exclude each other (LLVM's verifier rejects a function with
// both) and the front end does not diagnose the pair, so the writer sends
// only the one written first, as gcc does (it warns and ignores the second):
// in one attribute list in either order, in the two spellings, and on
// separate declarations in either order (the key sent first stays).
__attribute__((cold, hot)) int ch(int x) { return x; }
__attribute__((hot, cold)) int hc(int x) { return x; }
[[gnu::cold, gnu::hot]] int std_ch(int x) { return x; }
[[gnu::hot]] [[gnu::cold]] int std_hc(int x) { return x; }
int sep_ch(int x) __attribute__((cold));
int use_sep_ch(int x) { return sep_ch(x); }
int sep_ch(int x) __attribute__((hot));
int sep_ch(int x) { return x; }
int sep_hc(int x) __attribute__((hot));
int use_sep_hc(int x) { return sep_hc(x); }
int sep_hc(int x) __attribute__((cold));
int sep_hc(int x) { return x; }
int use(int x) { return ch(x) + hc(x) + std_ch(x) + std_hc(x); }
