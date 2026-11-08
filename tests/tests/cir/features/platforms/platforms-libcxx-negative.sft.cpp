//type:fn
//options:--g++ --c++17 -stdlib=libc++
//require:BACK_END_TYPE 3
// eccp rejects -stdlib= as an unknown option: libstdc++ is the only C++
// library that cir-env tests.
int main() { return 0; }
