//type:fp
//options:--cfront_2.1
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// Under --cfront_2.1 the front end accepts a jump past the declaration of an
// object with a destructor, and the writer diagnoses a case or goto label
// that follows one.  The initialization of a static local between the
// declaration and the label (STATIC_INIT_BEGIN) is a region inside the body:
// it must not forget the cleanup that is open.
struct D { ~D(); };
void f(int x) {
  switch (x) {
  case 1:
    D d;
    static int s = x;
  case 2:
    break;
  }
}
