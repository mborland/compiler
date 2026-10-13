#include "common.h"

R1::~R1() {}
R2::~R2() {}
D::~D() {}
static R2 r2_object;
static R1 r1_object;
R2 *A::get() { return &r2_object; }
R1 &A::ref() { return r1_object; }
R1 *A::same() { return &r1_object; }
A::~A() {}
int through_a(A *p) { return p->get()->r2 + p->ref().r1 + p->same()->r1; }
