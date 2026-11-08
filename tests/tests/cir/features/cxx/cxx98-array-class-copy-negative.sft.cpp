//type:fn
//options:--c++17
//require:BACK_END_TYPE 3
/* The copy of an array of class objects (a structured binding of an array by
   value) is diagnosed by the CIR back end and the function is dropped. */
struct E {
  int v;
  E() : v(0) {}
  E(const E &o) : v(o.v + 1) {}
};
int copy_it()
{
  E arr[2];
  auto [x, y] = arr;
  return x.v + y.v;
}
