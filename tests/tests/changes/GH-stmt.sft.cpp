//type:fp
//options_all:--c++20
//remark:[GH #?] Attribute target constraints for null and iteration statements
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
// 10/14/26  [GH #?]
//
// A registered attribute can require (or prohibit) a null statement ("s:+n!",
// "s:-n!") or an iteration statement ("s:+i!", "s:-i!").  An iteration
// statement is a while, do, for or range-based for statement.  Statement
// attributes do not appear in the IL display, so this test checks only that
// the attributes are accepted where they are allowed; GH-stmt-err checks the
// diagnostics.  In edg:: r_loop requires an iteration statement, r_null a
// null statement, r_noloop and r_nonull prohibit them, and r_stmt takes any
// statement.

void f(int n, int (&arr)[3]) {
  [[edg::r_loop(n > 0)]] while (n) { --n; }
  [[edg::r_loop(n > 0)]] do { --n; } while (n);
  [[edg::r_loop(n > 0)]] for (;;) { break; }
  [[edg::r_loop(n > 0)]] for (int i = 0; i < 3; ++i) { }
  [[edg::r_loop(n > 0)]] for (int v : arr) { (void)v; }
  [[edg::r_null(n > 0)]];
  L: [[edg::r_null(n > 0)]];
  goto L;
  [[edg::r_nonull(n > 0)]] n = 1;
  [[edg::r_nonull(n > 0)]] { }
  [[edg::r_noloop(n > 0)]] if (n) { }
  [[edg::r_noloop(n > 0)]] n = 2;
  [[edg::r_noloop(n > 0)]];
  [[edg::r_stmt(n > 0)]] while (n) { --n; }
  [[edg::r_stmt(n > 0)]];
}

// The names in a loop attribute are looked up before the init-statement of a
// for statement (or the declaration of a range-based for): the "i" and "v" in
// the arguments are the parameters, not the variables of the loop.
void bind(int i, int (&arr)[3]) {
  [[edg::r_loop(i > 3)]] for (int i = 0; i < 3; ++i) { }
  [[edg::r_loop(i > 3)]] for (int i : arr) { }
}

template <class T> void tf(T v) {
  [[edg::r_loop(v > 0)]] while (v) { --v; }
  [[edg::r_loop(v > 0)]] for (T i = 0; i < 3; ++i) { }
  [[edg::r_null(v > 0)]];
}
void use() { tf(1); }
