/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
edg_cir_rt -- the small runtime library that programs compiled through the CIR
back end link next to libstdc++/libgcc_s.

The CIR flow scrapes no system headers: <typeinfo> is the front end's built-in
class std::type_info, whose name(), before(), operator== and operator!= are
declared out of line.  Recent libstdc++ releases define some of those inline
in <typeinfo> and export no symbol for them, so the library defines them here
under their Itanium names (_ZNKSt9type_info...) with libstdc++ semantics.  The
object layout is the Itanium one: a vptr followed by the mangled name.  The
functions are defined through asm labels because a header declaring them
inline cannot be included alongside a definition.
*/

namespace {

struct a_type_info_layout {
  const void *vptr;
  const char *name;
};

int compare_strings(const char *a, const char *b)
/*
strcmp: negative, zero or positive.
*/
{
  while (*a != '\0' && *a == *b) {
    ++a;
    ++b;
  }
  return static_cast<int>(static_cast<unsigned char>(*a)) -
         static_cast<int>(static_cast<unsigned char>(*b));
}  /* compare_strings */

bool type_info_equal(const a_type_info_layout *self,
                     const a_type_info_layout *other)
/*
libstdc++'s type_info::operator== (merged names off): the same object or the
same name pointer, else the names compare as strings unless this one's starts
with '*' (internal linkage: unique per object).  The other name is compared
without its '*', as name() returns it.
*/
{
  bool equal;

  if (self == other || self->name == other->name) {
    equal = true;
  } else if (self->name[0] == '*') {
    equal = false;
  } else {
    const char *other_name = other->name + (other->name[0] == '*' ? 1 : 0);

    equal = compare_strings(self->name, other_name) == 0;
  }
  return equal;
}  /* type_info_equal */

bool type_info_before(const a_type_info_layout *self,
                      const a_type_info_layout *other)
/*
libstdc++'s type_info::before: the names compare as strings unless both
start with '*', then by address.
*/
{
  bool result;

  if (self->name[0] != '*' || other->name[0] != '*')
    result = compare_strings(self->name, other->name) < 0;
  else
    result = self->name < other->name;
  return result;
}  /* type_info_before */

}  /* anonymous namespace */

extern "C" {

bool edg_cir_rt_type_info_eq(const a_type_info_layout *self,
                             const a_type_info_layout *other)
    __asm__("_ZNKSt9type_infoeqERKS_");
bool edg_cir_rt_type_info_ne(const a_type_info_layout *self,
                             const a_type_info_layout *other)
    __asm__("_ZNKSt9type_infoneERKS_");
bool edg_cir_rt_type_info_before(const a_type_info_layout *self,
                                 const a_type_info_layout *other)
    __asm__("_ZNKSt9type_info6beforeERKS_");
const char *edg_cir_rt_type_info_name(const a_type_info_layout *self)
    __asm__("_ZNKSt9type_info4nameEv");

bool edg_cir_rt_type_info_eq(const a_type_info_layout *self,
                             const a_type_info_layout *other)
/*
std::type_info::operator==.
*/
{
  return type_info_equal(self, other);
}  /* edg_cir_rt_type_info_eq */

bool edg_cir_rt_type_info_ne(const a_type_info_layout *self,
                             const a_type_info_layout *other)
/*
std::type_info::operator!=.
*/
{
  return !type_info_equal(self, other);
}  /* edg_cir_rt_type_info_ne */

bool edg_cir_rt_type_info_before(const a_type_info_layout *self,
                                 const a_type_info_layout *other)
/*
std::type_info::before.
*/
{
  return type_info_before(self, other);
}  /* edg_cir_rt_type_info_before */

const char *edg_cir_rt_type_info_name(const a_type_info_layout *self)
/*
std::type_info::name (the mangled name without a leading '*').
*/
{
  return self->name + (self->name[0] == '*' ? 1 : 0);
}  /* edg_cir_rt_type_info_name */

int edg_cir_rt_version(void)
/*
Return the runtime library interface version (1: std::type_info members).
*/
{
  return 1;
}  /* edg_cir_rt_version */

}  /* extern "C" */
