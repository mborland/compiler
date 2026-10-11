//type:rp
//options:--c++17:--c++17 --gn 130100:--c++17 --clang_version 160000:--ms_c++17 --microsoft_version 1927:--ms_c++17 --microsoft_version 1936
//options_all:-tused tu2.cpp
//source_files:tu2.cpp

extern "C" int printf(const char  *, ...);

const int ci = 1;

template<typename T>
const int tci = 1;

template<typename T>
const int tci<T *> = 1;


inline const int ici = 1;

template<typename T>
inline const int tici = 1;

template<typename T>
inline const int tici<T *> = 1;

template<>
inline const int tici<void> = 1;


extern bool f_ci(const int *);
extern bool f_tci(const int *);
extern bool f_ptci(const int *);

extern bool f_ici(const int *);
extern bool f_tici(const int *);
extern bool f_ptici(const int *);
extern bool f_etici(const int *);

int main()
{
  printf("non-template %d\n", f_ci(&ci));
  printf("template %d\n", f_tci(&tci<int>));
  printf("partial specialization %d\n", f_ptci(&tci<int *>));

  printf("inline non-template %d\n", f_ici(&ici));
  printf("inline template %d\n", f_tici(&tici<int>));
  printf("inline partial specialization %d\n", f_ptici(&tici<int *>));
  printf("inline explicit specialization %d\n", f_etici(&tici<void>));
}
