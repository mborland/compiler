struct A { int x; A(); A(int); A(const A&); ~A(); };
A make();
void take(A);
