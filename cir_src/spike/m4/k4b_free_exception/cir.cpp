#include "p.h"
int mk(int k) { if (k == 1) throw 11; return k; }
int f1(int k) { try { throw Ex(mk(k)); } catch (Ex &e) { return e.code; } catch (int i) { return i; } }
int f2(int k) { try { throw Big(k); } catch (Big &b) { return b.a[0]; } catch (int i) { return i; } }
int f3(int k) { try { int x = 1; if (k) throw Ex(mk(k) + mk(1)); return x; } catch (int i) { return i + 100; } catch (Ex &) { return -1; } }
