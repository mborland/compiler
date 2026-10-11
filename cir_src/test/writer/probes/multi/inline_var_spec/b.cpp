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


bool f_ci(const int *p)
{
  return &ci == p;
}

bool f_tci(const int *p)
{
  return &tci<int> == p;
}

bool f_ptci(const int *p)
{
  return &tci<int *> == p;
}

bool f_ici(const int *p)
{
  return &ici == p;
}

bool f_tici(const int *p)
{
  return &tici<int> == p;
}

bool f_ptici(const int *p)
{
  return &tici<int *> == p;
}

bool f_etici(const int *p)
{
  return &tici<void> == p;
}
