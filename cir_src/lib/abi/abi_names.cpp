/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_names.cpp -- special names; see abi_names.h.  Derived in part from
clang/lib/AST/ItaniumMangle.cpp (Apache-2.0 WITH LLVM-exception).
*/

#include "abi_names.h"

#include <vector>

namespace edg_abi {

namespace {

/* The components of a simple <name>: ["St"] (<source-name> [I <builtin
   type>+ E])* ; "IiE" is a component of its own. */
struct a_simple_name {
  bool                      ok = false;
  bool                      nested = false;
  std::vector<std::string>  comps;    /* "St", "3foo", ... */
};  /* a_simple_name */

bool is_digit(char c)
/*
Whether c is a decimal digit.
*/
{
  return c >= '0' && c <= '9';
}  /* is_digit */

a_simple_name parse_simple_name(const std::string &s)
/*
Split a class name of the supported subset into components.
*/
{
  a_simple_name r;
  size_t pos = 0;
  size_t end = s.size();
  bool bad = s.empty();

  if (!bad && s[0] == 'N') {
    r.nested = true;
    pos = 1;
    if (s.back() != 'E')
      bad = true;
    else
      end = s.size() - 1;
  }
  while (!bad && pos < end) {
    if (s.compare(pos, 2, "St") == 0 && r.comps.empty()) {
      r.comps.push_back("St");
      pos += 2;
    } else if (is_digit(s[pos]) && s[pos] != '0') {
      size_t len = 0;
      size_t at = pos;

      while (at < end && is_digit(s[at]))
        len = len * 10 + static_cast<size_t>(s[at++] - '0');
      if (at + len > end) {
        bad = true;
      } else {
        r.comps.push_back(s.substr(pos, at + len - pos));
        pos = at + len;
      }
    } else if (s[pos] == 'I' && !r.comps.empty() &&
               r.comps.back()[0] != 'I' && r.comps.back() != "St") {
      size_t at = pos + 1;

      while (at < end && s[at] >= 'a' && s[at] <= 'z')
        ++at;
      if (at >= end || s[at] != 'E' || at == pos + 1) {
        bad = true;
      } else {
        r.comps.push_back(s.substr(pos, at + 1 - pos));
        pos = at + 1;
      }
    } else {
      bad = true;
    }
  }
  r.ok = !bad && !r.comps.empty();
  if (r.ok && !r.nested) {
    /* Unscoped: [St] source-name [args]. */
    size_t names = 0;

    for (const std::string &c : r.comps) {
      if (c != "St" && c[0] != 'I')
        ++names;
    }
    r.ok = names == 1 && !(r.comps.size() > 1 && r.comps[0] != "St" &&
                           r.comps[1][0] != 'I');
  }
  return r;
}  /* parse_simple_name */

std::string join(const std::vector<std::string> &comps, size_t from,
                 size_t to)
/*
The concatenation of comps[from, to).
*/
{
  std::string out;

  for (size_t k = from; k < to; ++k)
    out += comps[k];
  return out;
}  /* join */

std::string subst_text(size_t index)
/*
The substitution S_, S0_, S1_, ... of candidate number `index`.
*/
{
  std::string out;

  if (index == 0) {
    out = "S_";
  } else {
    std::string digits;
    size_t n = index - 1;

    do {
      size_t d = n % 36;

      digits.insert(digits.begin(),
                    static_cast<char>(d < 10 ? '0' + d : 'A' + (d - 10)));
      n /= 36;
    } while (n != 0);
    out = "S" + digits + "_";
  }
  return out;
}  /* subst_text */

}  /* anonymous namespace */

std::string mangle_number(int64_t value)
/*
<number> ::= [n] <non-negative decimal integer>
*/
{
  std::string out;
  uint64_t magnitude = static_cast<uint64_t>(value);

  if (value < 0) {
    out = "n";
    magnitude = 0 - magnitude;
  }
  return out + std::to_string(static_cast<unsigned long long>(magnitude));
}  /* mangle_number */

std::string vtable_symbol(const std::string &type_mangled)
/*
_ZTV<type>
*/
{
  return "_ZTV" + type_mangled;
}  /* vtable_symbol */

std::string vtt_symbol(const std::string &type_mangled)
/*
_ZTT<type>
*/
{
  return "_ZTT" + type_mangled;
}  /* vtt_symbol */

std::string typeinfo_symbol(const std::string &type_mangled)
/*
_ZTI<type>
*/
{
  return "_ZTI" + type_mangled;
}  /* typeinfo_symbol */

std::string typeinfo_name_symbol(const std::string &type_mangled)
/*
_ZTS<type>
*/
{
  return "_ZTS" + type_mangled;
}  /* typeinfo_name_symbol */

std::string typeinfo_name_string(const std::string &type_mangled,
                                 bool tu_local)
/*
The string of _ZTS: the mangled type, '*' first for a TU-local type.
*/
{
  return tu_local ? "*" + type_mangled : type_mangled;
}  /* typeinfo_name_string */

std::string ctor_vtable_symbol(const std::string &complete_mangled,
                               int64_t offset,
                               const std::string &base_mangled, bool &exact)
/*
_ZTC<complete><offset>_<base>, the base name compressed against the
candidates of the complete name: every proper prefix of the nested name in
order, then the whole name (unless it is only "St" + one component, a single
candidate of its own).
*/
{
  a_simple_name first = parse_simple_name(complete_mangled);
  a_simple_name second = parse_simple_name(base_mangled);
  std::string base_text = base_mangled;

  exact = first.ok && second.ok;
  if (exact) {
    std::vector<std::string> candidates;
    size_t n = first.comps.size();

    /* Candidates of the first name.  "St" alone is not one. */
    for (size_t k = 1; k <= n; ++k) {
      std::string text = join(first.comps, 0, k);

      if (text != "St")
        candidates.push_back(text);
    }
    /* The second name: the longest prefix (possibly all of it) that is a
       candidate. */
    size_t m = second.comps.size();
    size_t best = 0;
    size_t best_index = 0;

    for (size_t k = m; k >= 1 && best == 0; --k) {
      std::string text = join(second.comps, 0, k);

      for (size_t c = 0; c < candidates.size(); ++c) {
        if (candidates[c] == text) {
          best = k;
          best_index = c;
          break;
        }
      }
    }
    if (best != 0) {
      if (best == m) {
        base_text = subst_text(best_index);
      } else {
        base_text = "N" + subst_text(best_index) +
                    join(second.comps, best, m) + "E";
      }
    }
  }
  return "_ZTC" + complete_mangled + mangle_number(offset) + "_" + base_text;
}  /* ctor_vtable_symbol */

std::string thunk_symbol(const a_adjustment &this_adj,
                         const a_adjustment &return_adj,
                         const std::string &target_symbol)
/*
_ZT[c]<this call-offset>[<return call-offset>]<encoding>
*/
{
  std::string out;

  if (target_symbol.compare(0, 2, "_Z") == 0) {
    auto call_offset = [](const a_adjustment &adj) {
      std::string text;

      if (adj.offset_offset == 0) {
        text = "h" + mangle_number(adj.non_virtual) + "_";
      } else {
        text = "v" + mangle_number(adj.non_virtual) + "_" +
               mangle_number(adj.offset_offset) + "_";
      }
      return text;
    };

    out = "_ZT";
    if (!return_adj.is_empty())
      out += "c";
    out += call_offset(this_adj);
    if (!return_adj.is_empty())
      out += call_offset(return_adj);
    out += target_symbol.substr(2);
  }
  return out;
}  /* thunk_symbol */

std::string dtor_variant_symbol(const std::string &dtor_symbol, char variant)
/*
Replace the last D0E/D1E/D2E.
*/
{
  std::string out;
  size_t at = std::string::npos;

  for (size_t k = 0; k + 2 < dtor_symbol.size(); ++k) {
    if (dtor_symbol[k] == 'D' && dtor_symbol[k + 1] >= '0' &&
        dtor_symbol[k + 1] <= '2' && dtor_symbol[k + 2] == 'E')
      at = k;
  }
  if (at != std::string::npos) {
    out = dtor_symbol;
    out[at + 1] = variant;
  }
  return out;
}  /* dtor_variant_symbol */

}  /* namespace edg_abi */
