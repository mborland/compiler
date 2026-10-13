//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A local whose type is a cv-qualified typedef with an alignment attribute
// has that alignment in its LOCAL record.
typedef int t_align __attribute__((aligned(64)));
void g(int *);
void bar() {
  volatile t_align a = 1;
  t_align b = 2;
  const t_align c = 3;
  g((int *)&a);
  g(&b);
  g((int *)&c);
}
