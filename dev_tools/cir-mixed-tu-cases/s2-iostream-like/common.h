/* The shape of libstdc++'s stream classes: a base with a virtual
   destructor (ios_base), a class with state (ios), two classes with ios as
   a virtual base (istream, ostream) calling a virtual function of it during
   construction, and the diamond iostream : istream, ostream; plus ofstream :
   ostream with a non-virtual base chain.  a.cpp defines everything but the
   last two classes, which are in b.cpp. */
struct ios_base {
  virtual ~ios_base();
  int flags = 1;
};
struct ios : ios_base {
  ios();
  virtual void init(int mode);
  ~ios() override;
  int fill = 32;
  int state = 0;
};
struct ostream : virtual ios {
  ostream();
  virtual ostream &put(int c);
  virtual ostream &flush();
  ~ostream() override;
  long count = 0;
};
struct istream : virtual ios {
  istream();
  virtual int get();
  ~istream() override;
  long gcount = 0;
};
struct iostream : istream, ostream {
  iostream();
  ~iostream() override;
};
struct ofstream : ostream {
  ofstream();
  ostream &put(int c) override;
  ~ofstream() override;
  int fd = 3;
};
iostream *make_iostream();   // b.cpp
ofstream *make_ofstream();   // b.cpp
void destroy_ios(ios_base *p);  // a.cpp
