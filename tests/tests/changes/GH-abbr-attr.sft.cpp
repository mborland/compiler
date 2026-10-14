//type:fp
//options:--c++20 --gnu_version=150000
//remark:[GH #?] Attributes after the parameter list of an abbreviated function template
// 10/14/26  [GH #?]
//
// An attribute after the parameter list of a function declaration with an
// "auto" parameter (an abbreviated function template) was not scanned when the
// declaration was first parsed: the front end took the "[[" for an array
// declarator, and gave an error.  The attributes are now skipped there, and
// scanned when the declaration is parsed again as a template.

void f1(auto x) [[gnu::noinline]];
void f2(auto x) [[gnu::noinline]] { }
auto f3(auto x) [[gnu::noinline]] [[gnu::cold]] -> int { return 1; }
template <class T> void f4(T x, auto y) [[gnu::noinline]];

void use() { f1(1); f2(2); f3(3); f4(4, 5); }
