extern "C" int printf(const char *, ...);
extern "C" void _exit(int) noexcept;
extern "C" int fflush(void *) noexcept;
static inline void fflush_all() { fflush(nullptr); }
// Event log: every constructor, destructor and handler prints one line.
struct T {
  int id;
  T(int i) : id(i) { printf("T(%d)\n", id); }
  T(const T &o) : id(o.id + 100) { printf("T(copy %d)\n", o.id); }
  ~T() { printf("~T(%d)\n", id); }
};
struct Thrower { Thrower(int fail) { printf("Thrower(%d)\n", fail); if (fail) throw fail; } ~Thrower() { printf("~Thrower\n"); } };
struct Ex { int code; Ex(int c) : code(c) {} Ex(const Ex &o) : code(o.code + 1000) { printf("Ex copy\n"); } virtual ~Ex() { printf("~Ex(%d)\n", code); } };
struct ExD : Ex { ExD(int c) : Ex(c) {} };
struct Triv { int a, b; };
int run_all();
int nested_try(int k);
int catch_all_rethrow(int k);
int jumps(int k);
int ret_in_try(int k);
int cleanups_in_try(int k);
int new_throws(int k);
int throw_temps(int k);
int ternary_throw(int k);
int ctor_try(int k);
int nothrow_term(int k);
int catch_kinds(int k);
int nrvo(int k);
int throw_ctor_fail(int k);
int loops_with_try(int k);
