//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A used static data member of a class template (key 11, "used") is emitted
// for the instances that are complete, as gcc does (W<int> and W<char>, the
// second by an explicit instantiation), and not for one that is only named
// (W<long> *p), nor for a variable template that is not instantiated.
template<class T> struct W {
  __attribute__((used)) static int sm;
  int x;
};
template<class T> int W<T>::sm = 5;
template<class T> __attribute__((used)) T var_t = T();
W<int> w;
W<long> *p;
template struct W<char>;
int main() { return w.x; }
