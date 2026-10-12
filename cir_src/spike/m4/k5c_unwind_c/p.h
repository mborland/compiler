#ifdef __cplusplus
extern "C" {
#endif
// C frames compiled through ClangIR between a C++ throw and its handler.
typedef void (*cb_t)(int);
void c_call(cb_t cb, int v);          /* calls cb(v) */
int c_deep(cb_t cb, int n);           /* recursion of C frames around the call */
int c_array_cb(int (*cmp)(const void *, const void *), int *a, int n);
#ifdef __cplusplus
}
#endif
