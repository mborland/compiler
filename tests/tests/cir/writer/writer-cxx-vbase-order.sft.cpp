//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// M4 slice S2: the order of the virtual bases.  The front end constructs them
// depth first, left to right (here Q, P, R: its postorder base list, the order
// of the VIRTUAL_BASE fields and of the MEMINIT(vbase) groups of C1); D1 is
// synthesized with their cleanups begun in the same order, so that they run in
// reverse (R, P, Q); virtual bases are not destroyed by D2.  g++ prints
// "Q P R L1 L2 Top ~Top ~L2 ~L1 ~R ~P ~Q".
extern "C" int printf(const char *, ...);
struct P { P() { printf("P\n"); } virtual ~P() { printf("~P\n"); } };
struct Q { Q() { printf("Q\n"); } ~Q() { printf("~Q\n"); } };
struct R { R() { printf("R\n"); } ~R() { printf("~R\n"); } };
struct L1 : virtual Q, virtual P { L1() { printf("L1\n"); } ~L1() { printf("~L1\n"); } };
struct L2 : virtual R { L2() { printf("L2\n"); } ~L2() { printf("~L2\n"); } };
struct Top : L1, virtual R, L2 { Top() { printf("Top\n"); } ~Top() { printf("~Top\n"); } };
int main() { Top t; return 0; }
