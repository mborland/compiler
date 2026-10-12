extern "C" int printf(const char *, ...);
extern "C" int fflush(void *) noexcept;
struct G { int v; G(int x) : v(x) { printf("G(%d)\n", x); } ~G() { printf("~G(%d)\n", v); } };
int mk(int k);
extern G g1;
extern G g2;
void reg();
