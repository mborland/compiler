//type:fn
//options:--c++11
//require:BACK_END_TYPE 3
/* A member initializer for a member of an anonymous union whose type has a
   destructor is diagnosed by the CIR back end and the constructor is dropped.
   A member of the anonymous union without a destructor is fine. */
struct N {
  int x;
  N(int v) : x(v) {}
  ~N() {}
};
struct Variant {
  enum Tag { INT, NODE } tag;
  union {
    int i;
    N n;
  };
  Variant() : tag(INT), i(5) {}
  explicit Variant(int v) : tag(NODE), n(v) {}
  ~Variant() { if (tag == NODE) n.~N(); }
};
Variant make(int v) { return Variant(v); }
