//type:rp
//options:--c++17
//require:BACK_END_TYPE 3
// Function-local thread_local with dynamic initialization and a destructor,
// from three threads and the main thread: every thread constructs its own
// instance once (the TLS guard), the instance is stable across calls, each
// worker's instance is destroyed when the worker exits (__cxa_thread_atexit),
// and the main thread's is destroyed at exit before the static destructors.
extern "C" int printf(const char *, ...);
extern "C" int pthread_create(unsigned long *, const void *, void *(*)(void *), void *);
extern "C" int pthread_join(unsigned long, void **);
extern "C" int pthread_mutex_lock(void *);
extern "C" int pthread_mutex_unlock(void *);
extern "C" int usleep(unsigned);
alignas(8) char mtx[64];
int ctors, dtors, next_id;
struct T {
  int id;
  T(int i) : id(i) {
    pthread_mutex_lock(mtx); ++ctors; pthread_mutex_unlock(mtx);
    usleep(20000);
  }
  ~T() { pthread_mutex_lock(mtx); ++dtors; pthread_mutex_unlock(mtx); }
};
struct Report { ~Report() { printf("at exit: ctors=%d dtors=%d\n", ctors, dtors); } } report;
T &get() {
  thread_local T t([] { pthread_mutex_lock(mtx); int v = ++next_id; pthread_mutex_unlock(mtx); return v; }());
  return t;
}
void *worker(void *) {
  T *p = &get();
  for (int i = 0; i < 5; ++i) if (&get() != p) printf("instance moved\n");
  return 0;
}
int main() {
  unsigned long th[3];
  for (int i = 0; i < 3; ++i) pthread_create(&th[i], 0, worker, 0);
  for (int i = 0; i < 3; ++i) pthread_join(th[i], 0);
  printf("after join: ctors=%d dtors=%d\n", ctors, dtors);
  get(); get();
  printf("main: ctors=%d dtors=%d id=%d\n", ctors, dtors, get().id);
  return 0;
}
