//type:fn
//options_all:--clang --clang_version=210000 --c++20
//remark:[GH #?] The substitution into the arguments of the attributes that the front end defines is as before
//match_regex:line 19: error: parameter pack "vals" was referenced but not expanded
//match_regex:\A(?![\s\S]*bad attribute argument substitution)
// 10/17/26  [GH #?]
//
// The error "bad attribute argument substitution" for an argument of an
// attribute whose substituted expression has an error in it is for the
// registered attributes only (GH-postfunc-subst-error).  The diagnose_if
// attribute of Clang, in a function template and in a templated lambda in one,
// has an argument that names a parameter pack that is not expanded (an error in
// the template itself, as in imported/clang/cpp/SemaCXX/attribute-pack-expansion):
// the instances do not have another error for the substitution of the
// argument, as they did not before the registered attributes.  A generic lambda
// in the argument of such an attribute is substituted as before, too
// (GH-generic-lambda-substitution).
template <bool... vals>
void f() __attribute((diagnose_if(vals, "message", "error"))) {}
template <bool... vals>
void g() {
  [] <bool ...inner> () __attribute((diagnose_if(inner, "message", "error"))) {}();
}
void use() {
  f<>();
  f<false>();
  f<true, true>();
  g<true>();
}
