//type:fp
//options_all:--c23
//remark:[GH #?] Attribute target constraints for null and iteration statements in C
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
// 10/14/26  [GH #?]
//
// The statement constraints of GH-stmt also apply in C.

void f(int n) {
  [[edg::r_loop(n > 0)]] while (n) { --n; }
  [[edg::r_loop(n > 0)]] do { --n; } while (n);
  [[edg::r_loop(n > 0)]] for (int i = 0; i < 3; ++i) { }
  [[edg::r_null(n > 0)]];
  [[edg::r_nonull(n > 0)]] n = 1;
  [[edg::r_noloop(n > 0)]] if (n) { }
}
