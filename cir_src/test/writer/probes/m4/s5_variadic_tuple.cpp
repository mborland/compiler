//options: --c++11
#include <string>
#include <cstring>
#include <cstdio>
#include <assert.h>
template<typename T>
struct reference_wrapper
{
  reference_wrapper(T& x) : ptr(&x) { }

  operator T&() const { return *ptr; }

  T* ptr;
};

template<typename T> reference_wrapper<T> ref(T& x) { return x; }
template<typename T> reference_wrapper<const T> cref(const T& x) { return x; }


template<typename T>
struct add_reference
{
  typedef T& type;
};

template<typename T>
struct add_reference<T&>
{
  typedef T& type;
};

template<typename T, typename U>
struct is_same
{
  static const bool value = false;
};

template<typename T>
struct is_same<T, T>
{
  static const bool value = true;
};


template<typename T>
struct add_const_reference
{
  typedef const T& type;
};

template<typename T>
struct add_const_reference<T&>
{
  typedef T& type;
};


template<typename... Values>
class tuple;

template<> class tuple<> { };

template<typename Head, typename... Tail>
class tuple<Head, Tail...>
  : private tuple<Tail...>
{
  typedef tuple<Tail...> inherited;

 public:
  tuple() { }



  tuple(typename add_const_reference<Head>::type v,
        typename add_const_reference<Tail>::type... vtail)
    : m_head(v), inherited(vtail...) { }

  template<typename... VValues>
  tuple(const tuple<VValues...>& other)
    : m_head(other.head()), inherited(other.tail()) { }

  template<typename... VValues>
  tuple& operator=(const tuple<VValues...>& other)
  {
    m_head = other.head();
    tail() = other.tail();
    return *this;
  }

  typename add_reference<Head>::type head() { return m_head; }
  typename add_reference<const Head>::type head() const { return m_head; }
  inherited& tail() { return *this; }
  const inherited& tail() const { return *this; }

 protected:
  Head m_head;
};

template<typename T>
struct make_tuple_result
{
  typedef T type;
};

template<typename T>
struct make_tuple_result<reference_wrapper<T> >
{
  typedef T& type;
};


struct ignore_t {
  template<typename T> ignore_t& operator=(const T&) { return *this; }
} ignore;

template<typename... Values>
tuple<typename make_tuple_result<Values>::type...>
make_tuple(const Values&... values)
{
  return tuple<typename make_tuple_result<Values>::type...>(values...);
}

template<typename... Values>
tuple<Values&...> tie(Values&... values)
{
  return tuple<Values&...>(values...);
}


template<typename Tuple>
struct tuple_size;

template<>
struct tuple_size<tuple<> >
{
  static const std::size_t value = 0;
};

template<typename Head, typename... Tail>
struct tuple_size<tuple<Head, Tail...> >
{
  static const std::size_t value = 1 + tuple_size<tuple<Tail...> >::value;
};

template<int I, typename Tuple>
struct tuple_element;

template<int I, typename Head, typename... Tail>
struct tuple_element<I, tuple<Head, Tail...> >
{
  typedef typename tuple_element<I-1, tuple<Tail...> >::type type;
};

template<typename Head, typename... Tail>
struct tuple_element<0, tuple<Head, Tail...> >
{
  typedef Head type;
};


template<int I, typename Tuple>
class get_impl;

template<int I, typename Head, typename... Values>
class get_impl<I, tuple<Head, Values...> >
{
  typedef typename tuple_element<I-1, tuple<Values...> >::type Element;
  typedef typename add_reference<Element>::type RJ;
  typedef typename add_const_reference<Element>::type PJ;
  typedef get_impl<I-1, tuple<Values...> > Next;

 public:
  static RJ get(tuple<Head, Values...>& t)
  { return Next::get(t.tail()); }

  static PJ get(const tuple<Head, Values...>& t)
  { return Next::get(t.tail()); }
};

template<typename Head, typename... Values>
class get_impl<0, tuple<Head, Values...> >
{
  typedef typename add_reference<Head>::type RJ;
  typedef typename add_const_reference<Head>::type PJ;

 public:
  static RJ get(tuple<Head, Values...>& t) { return t.head(); }
  static PJ get(const tuple<Head, Values...>& t) { return t.head(); }
};

template<int I, typename... Values>
typename add_reference<
           typename tuple_element<I, tuple<Values...> >::type
         >::type
get(tuple<Values...>& t)
{
  return get_impl<I, tuple<Values...> >::get(t);
}

template<int I, typename... Values>
typename add_const_reference<
           typename tuple_element<I, tuple<Values...> >::type
         >::type
get(const tuple<Values...>& t)
{
  return get_impl<I, tuple<Values...> >::get(t);
}


inline bool operator==(const tuple<>&, const tuple<>&) { return true; }

template<typename T, typename... TTail, typename U, typename... UTail>
bool operator==(const tuple<T, TTail...>& t, const tuple<U, UTail...>& u)
{
  return t.head() == u.head() && t.tail() == u.tail();
}

template<typename... TValues, typename... UValues>
bool operator!=(const tuple<TValues...>& t, const tuple<UValues...>& u)
{
  return !(t == u);
}

inline bool operator<(const tuple<>&, const tuple<>&) { return false; }

template<typename T, typename... TTail, typename U, typename... UTail>
bool operator<(const tuple<T, TTail...>& t, const tuple<U, UTail...>& u)
{
  return (t.head() < u.head() ||
          (!(t.head() < u.head()) && t.tail() < u.tail()));
}

template<typename... TValues, typename... UValues>
bool operator>(const tuple<TValues...>& t, const tuple<UValues...>& u)
{
  return u < t;
}

template<typename... TValues, typename... UValues>
bool operator<=(const tuple<TValues...>& t, const tuple<UValues...>& u)
{
  return !(u < t);
}

template<typename... TValues, typename... UValues>
bool operator>=(const tuple<TValues...>& t, const tuple<UValues...>& u)
{
  return !(t < u);
}

int a0[tuple_size<tuple<> >::value == 0? 1 : -1];
int a1[tuple_size<tuple<int, float, double> >::value == 3? 1 : -1];
int a2a[is_same<tuple_element<0, tuple<int, float, double> >::type, int>
         ::value? 1 : -1];
int a2b[is_same<tuple_element<1, tuple<int, float, double> >::type, float>
         ::value? 1 : -1];
int a2c[is_same<tuple_element<2, tuple<int, float, double> >::type, double>
         ::value? 1 : -1];

int main()
{
  tuple<> t0;
  tuple<int> t1(1);
  tuple<int, float> t2(1, 3.14159f);
  tuple<int, float, const char*> t3a(1, 3.14159f, "Hello, world!");
  tuple<long, double, std::string> t3b(t3a);
  t3b = t3a;


  tuple<int, float, std::string> t3c =
    ::make_tuple(17, 2.718281828, std::string("Fun"));

  int seventeen = 17;
  double pi = 3.14159;
  std::fprintf(stderr,"a %d %g %s | b %ld %g %s\n", get<0>(t3a), (double)get<1>(t3a), get<2>(t3a), get<0>(t3b), get<1>(t3b), get<2>(t3b).c_str());
  std::fprintf(stderr,"eq %d %d %d  whole %d\n", (int)(t3a.head() == t3b.head()), (int)(t3a.tail().head() == t3b.tail().head()), (int)(t3a.tail().tail().head() == t3b.tail().tail().head()), (int)(t3a == t3b));
  tuple<int&, double&> seventeen_pi = make_tuple(ref(seventeen), ref(pi));
  tuple<int&, const double&> seventeen_pi2 =
    make_tuple(ref(seventeen), cref(pi));
  tuple<int&, double&> seventeen_pi_tied = tie(seventeen, pi);
  
 ((
 get<0>(t3a) == 1
 ) ? static_cast<void> (0) : __assert_fail (
 "get<0>(t3a) == 1"
 , "./cpp0x/variadic-tuple.C", 285, __PRETTY_FUNCTION__))
                         ;
  
 ((
 get<1>(t3a) == 3.14159f
 ) ? static_cast<void> (0) : __assert_fail (
 "get<1>(t3a) == 3.14159f"
 , "./cpp0x/variadic-tuple.C", 286, __PRETTY_FUNCTION__))
                                ;
  
 ((
 std::strcmp(get<2>(t3a), "Hello, world!") == 0
 ) ? static_cast<void> (0) : __assert_fail (
 "std::strcmp(get<2>(t3a), \"Hello, world!\") == 0"
 , "./cpp0x/variadic-tuple.C", 287, __PRETTY_FUNCTION__))
                                                       ;

  
 ((
 t3a == t3b
 ) ? static_cast<void> (0) : __assert_fail (
 "t3a == t3b"
 , "./cpp0x/variadic-tuple.C", 289, __PRETTY_FUNCTION__))
                   ;
  
 ((
 !(t3a != t3b)
 ) ? static_cast<void> (0) : __assert_fail (
 "!(t3a != t3b)"
 , "./cpp0x/variadic-tuple.C", 290, __PRETTY_FUNCTION__))
                      ;
  
 ((
 !(t3a < t3b)
 ) ? static_cast<void> (0) : __assert_fail (
 "!(t3a < t3b)"
 , "./cpp0x/variadic-tuple.C", 291, __PRETTY_FUNCTION__))
                     ;
  
 ((
 !(t3a > t3b)
 ) ? static_cast<void> (0) : __assert_fail (
 "!(t3a > t3b)"
 , "./cpp0x/variadic-tuple.C", 292, __PRETTY_FUNCTION__))
                     ;
  
 ((
 t3a <= t3b && t3b <= t3a
 ) ? static_cast<void> (0) : __assert_fail (
 "t3a <= t3b && t3b <= t3a"
 , "./cpp0x/variadic-tuple.C", 293, __PRETTY_FUNCTION__))
                                 ;
  
 ((
 t3a >= t3b && t3b >= t3a
 ) ? static_cast<void> (0) : __assert_fail (
 "t3a >= t3b && t3b >= t3a"
 , "./cpp0x/variadic-tuple.C", 294, __PRETTY_FUNCTION__))
                                 ;
}
