//type:fn
//options:--c++20
//require:BACK_END_TYPE 3
/* Initializing a [[no_unique_address]] member of a class that has tail padding
   with a class value is diagnosed by the CIR back end and the constructor is
   dropped (a full-size store would overwrite the next member). */
struct Tag {
  long a;
  char c;
  Tag(int x) : a(x), c(1) {}
};
Tag make();
struct S {
  [[no_unique_address]] Tag t;
  char after;
  S() : t(make()), after(1) {}
};
int use()
{
  S s;
  return s.after;
}
