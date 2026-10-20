//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// C++17 evaluates the right operand of an assignment before the left one.
// The order is kept for volatile accesses (memory-mapped registers): a
// right operand that is a volatile read is not "pure" and is evaluated into a
// temporary first, for a member of a volatile object as for a volatile
// variable. Operands without volatile accesses are not spilled.
struct Dev {
  volatile unsigned ctrl;
  volatile unsigned status;
  unsigned a;
  unsigned b;
};
extern volatile unsigned vctrl, vstatus;
void member_compound(Dev *dev) { dev->ctrl |= dev->status; }
void plain_compound() { vctrl |= vstatus; }
void member_assign(Dev *dev) { dev->ctrl = dev->status; }
void mixed(Dev *dev) { dev->a |= dev->status; }
void not_volatile(Dev *dev) { dev->a |= dev->b; }
