// M4a probe: order of several virtual bases in the constructor list (C1's
// MEMINIT(vbase) order) and in the destruction list (reverse).
#include <cstdio>
struct P { P() { std::puts("P"); } virtual ~P() { std::puts("~P"); } };
struct Q { Q() { std::puts("Q"); } ~Q() { std::puts("~Q"); } };
struct R { R() { std::puts("R"); } ~R() { std::puts("~R"); } };
struct L1 : virtual Q, virtual P { L1() { std::puts("L1"); } ~L1() { std::puts("~L1"); } };
struct L2 : virtual R { L2() { std::puts("L2"); } ~L2() { std::puts("~L2"); } };
struct Top : L1, virtual R, L2 { Top() { std::puts("Top"); } ~Top() { std::puts("~Top"); } };
int main() { Top t; return 0; }
