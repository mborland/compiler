//type:fp
//options:--c11
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* A static local with no declaration statement (__func__, a compound literal
   in a static initializer) is introduced (DECL_VAR) where it is first used
   and its initializer follows later in the frame, so that building one
   constant never builds another: the chain below nests three deep. */
struct S { int *p; const char *n; };
struct T { struct S *s; };
const char *chain(void) {
  static const char *a = __func__;
  static struct S s = { &(int){5}, __func__ };
  static struct T t = { &(struct S){ &(int){7}, "x" } };
  return a;
}
const char *use_func(void) {
  return __func__;
}
