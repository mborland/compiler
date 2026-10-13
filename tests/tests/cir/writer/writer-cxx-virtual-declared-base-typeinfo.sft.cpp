//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A class typeinfo, defined or declared, needs the typeinfo of each direct
// base declared first (std::bad_alloc's vtable needs std::exception's): the
// base's key function is elsewhere, the derived class's vtable is used by its
// constructor, and the vtable of a declared-only class also declares its
// typeinfo.
struct Exc { Exc(); virtual ~Exc(); virtual const char *what() const; };
struct Bad : Exc { Bad() {} };
struct Bad2 : Bad { Bad2() {} int x; };
struct Mid : virtual Exc { Mid() {} };
Bad b1;
Bad2 *make() { return new Bad2; }
void use() { Mid m; (void)m; }
