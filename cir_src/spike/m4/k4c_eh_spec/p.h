#include <exception>
extern "C" int printf(const char *, ...);
extern "C" void _exit(int) noexcept;
extern "C" int fflush(void *) noexcept;
// Dynamic exception specifications (C++14): the eh filter and unexpected paths.
struct T { int id; T(int i) : id(i) { printf("T(%d)\n", id); } ~T() { printf("~T(%d)\n", id); } };
void only_int(int k) throw(int);
void int_or_bad(int k) throw(int, std::bad_exception);
void caller(int k);
int run_all();
