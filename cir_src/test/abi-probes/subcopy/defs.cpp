#include "p.h"
void set_base(D &d, const B &b) { static_cast<B &>(d) = b; }
void set_base_val(D &d, B b) { static_cast<B &>(d) = b; }
void set_nua(N &n, const B &b) { n.b = b; }
B get_base(const D &d) { return static_cast<const B &>(d); }
B get_nua(const N &n) { return n.b; }
void set_base_l(DL &d, const L &b) { static_cast<L &>(d) = b; }
void set_nua_l(NL &n, const L &b) { n.l = b; }
B &base_ref(D &d) { return d; }
void assign_through_ref(B &dst, const B &src) { dst = src; }
