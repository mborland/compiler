int f(int);
int h3(bool b) { static int y = (b && f(1) > 0) ? f(2) : f(3); return y; }
