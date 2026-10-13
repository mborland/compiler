// S2: the shape of libstdc++'s stream classes: ios_base (virtual
// destructor) <- ios <- virtual base of istream and ostream, the diamond
// iostream, and ofstream : ostream; a virtual function called by a
// constructor of a class with a virtual base (the vptr during
// construction).
struct ios_base {
  virtual ~ios_base();
  int flags;
};
struct ios : ios_base {
  ios();
  virtual void init(int mode);
  ~ios() override;
  int state;
};
struct ostream : virtual ios {
  ostream();
  virtual ostream &put(int c);
  ~ostream() override;
  long count;
};
struct istream : virtual ios {
  istream();
  virtual int get();
  ~istream() override;
  long gcount;
};
struct iostream : istream, ostream {
  iostream();
  ~iostream() override;
};
struct ofstream : ostream {
  ofstream();
  ostream &put(int c) override;
  ~ofstream() override;
  int fd;
};
ios_base::~ios_base() {}
ios::ios() : state(0) {}
void ios::init(int mode) { state = mode; }
ios::~ios() {}
ostream::ostream() : count(0) { init(1); }
ostream &ostream::put(int c) { ++count; state += c; return *this; }
ostream::~ostream() {}
istream::istream() : gcount(0) { init(2); }
int istream::get() { ++gcount; return state; }
istream::~istream() {}
iostream::iostream() {}
iostream::~iostream() {}
ofstream::ofstream() : fd(3) {}
ostream &ofstream::put(int c) { return ostream::put(c + fd); }
ofstream::~ofstream() {}
iostream io;
ofstream of;
